use std::error::Error;
use std::str;

use mio::net::{TcpListener, TcpStream};
use mio::{Events, Interest, Poll, Token};
use std::net::{SocketAddr, IpAddr, Ipv4Addr};
use std::os::fd::AsRawFd;
use std::io::{self, Write, Read};

use slab::Slab;

use clap::Parser;

struct ClientState {
    socket: TcpStream,
    num_messages: u32,
    incoming_buffer: [u8; 255],
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

fn main() -> Result<(), Box<dyn Error>> {
    let args = Args::parse();

    let mut poll = Poll::new()?;

    let mut events = Events::with_capacity(128);

    let address = SocketAddr::new(
        IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), args.port); // This is ugly.

    let mut listener = TcpListener::bind(address)?;

    poll.registry()
        .register(&mut listener, LISTENER, Interest::READABLE)?;

    let mut client_states: Slab<ClientState> = Slab::with_capacity(MAX_CLIENTS);

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
                            println!("Accepted client on socket {} with token {:?}.", client_socket.as_raw_fd(), token);
                            poll.registry().register(&mut client_socket, token, Interest::READABLE)?;

                            client_entry.insert(ClientState { socket: client_socket, num_messages: 0, incoming_buffer: [0; 255] });
                        }
                        Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                            println!("Received error {e:?} from listener.accept().");
                        }
                        e => panic!("Unexpected error {e:?}")
                    };


                }
                Token(client_id) => {
                    let curr_client_state = &mut client_states[client_id];
                    if curr_client_state.socket.read(&mut curr_client_state.incoming_buffer)? != 0 { // TODO: Handle blocking (and other errors) gracefully!
                        println!("Received a message from client with token {} on socket {}", client_id, curr_client_state.socket.as_raw_fd());
                        println!("Message was {}", str::from_utf8(&curr_client_state.incoming_buffer).unwrap());
                    } else {
                        println!("Closing socket {} for client {}", curr_client_state.socket.as_raw_fd(), client_id);
                        client_states.remove(client_id);
                    }
                }
            }
        }
    }
}
