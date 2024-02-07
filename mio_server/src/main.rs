use std::cell::RefCell;
use std::error::Error;
use std::str; // Sigh... Try reimplementing with vec and split_at_mut, should be freaking awesome.

use mio::net::{TcpListener, TcpStream};
use mio::{Events, Interest, Poll, Token};
use std::io::{self, Read, Write};
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::os::fd::AsRawFd;

use slab::Slab;

use clap::Parser;

struct ClientState {
    socket: TcpStream,
    num_messages: u32,
    incoming_buffer: [u8; 255],
    used_bytes: usize,
}

const MAX_CLIENTS: usize = 5;

// Some tokens to allow us to identify which event is for which socket.
const LISTENER: Token = Token(MAX_CLIENTS);

#[derive(Parser, Debug)]
struct Args {
    /// Port value
    #[arg(short, long, default_value_t = 2138)]
    port: u16,
}

fn handle_client(
    sender_id: usize,
    curr_state: &mut ClientState,
    all_states: &Slab<RefCell<ClientState>>,
) -> bool {
    let Ok(num_bytes_read) = curr_state
        .socket
        .read(&mut curr_state.incoming_buffer[curr_state.used_bytes..])
    else {
        panic!("Uh oh, reading from client would've blocked (or caused another error)");
        // TODO: Handle blocking (and other errors) gracefully!
    };
    if num_bytes_read != 0 {
        for (receiver_id, state) in all_states.iter() {
            // I think it's odd that just iter also gives me the indices.
            if receiver_id == sender_id {
                continue;
            }
            state.borrow_mut().socket.write(&curr_state.incoming_buffer);
        }
        return true;
    } else {
        return false;
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let args = Args::parse();

    let mut poll = Poll::new()?;

    let mut events = Events::with_capacity(128);

    let address = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), args.port); // This is ugly.

    let mut listener = TcpListener::bind(address)?;

    poll.registry()
        .register(&mut listener, LISTENER, Interest::READABLE)?;

    let mut client_states: Slab<RefCell<ClientState>> = Slab::with_capacity(MAX_CLIENTS);

    println!("Listening on port {}.", args.port);

    loop {
        poll.poll(&mut events, None)?; // Timeout: None.

        for event in events.iter() {
            match event.token() {
                LISTENER => {
                    match listener.accept() {
                        Ok((mut client_socket, _)) => {
                            if client_states.len() == client_states.capacity() {
                                let msg = b"Error: Maximum number of clients already connected.";
                                println!("{}", str::from_utf8(msg).unwrap()); // What the actual hell.
                                client_socket.write(msg); // TODO: Check if this blocked!

                                continue;
                            }
                            let client_entry = client_states.vacant_entry();
                            let token = Token(client_entry.key());
                            println!(
                                "Accepted client on socket {} with token {:?}.",
                                client_socket.as_raw_fd(),
                                token
                            );
                            poll.registry().register(
                                &mut client_socket,
                                token,
                                Interest::READABLE,
                            )?;

                            client_entry.insert(RefCell::new(ClientState {
                                socket: client_socket,
                                num_messages: 0,
                                incoming_buffer: [0; 255],
                                used_bytes: 0,
                            }));
                        }
                        Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                            println!("Received error {e:?} from listener.accept().");
                        }
                        e => panic!("Unexpected error {e:?}"),
                    };
                }
                Token(client_id) if client_id < MAX_CLIENTS => {
                    let curr_client_state = &client_states[client_id];
                    if !handle_client(
                        client_id,
                        &mut curr_client_state.borrow_mut(),
                        &client_states,
                    ) {
                        println!(
                            "Closing socket {} for client {}",
                            curr_client_state.borrow_mut().socket.as_raw_fd(),
                            client_id
                        );
                        client_states.remove(client_id);
                    }
                }
                _ => panic!("Somehow accepted too many clients!"),
            }
        }
    }
}
