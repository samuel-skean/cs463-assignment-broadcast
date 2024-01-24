#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h> // TODO: What's the difference between this and sys/socket.h,
                        // and why should I care?
#include <sys/epoll.h>

// Info on Design Badness:
// Right now I record the sockets of each client in an array (client_sockets) indexed with another number for each client, 
// so I can loop through that array and send messages only to clients that I have an actual connection with. Empty spaces
// in this array of clients are represented by 0 (which is a valid file descriptor, but not a vaild file descriptor for a client).
// This is how Eriksson did it in his multithreaded server.
// I think I could alternately do this by simply trying to send on the whole range of possible file descriptors, but that would
// mean more error-handling in C (yikes) and would probably be worse performance wise. Look before you leap is generally preferable to me anyway.
//
// However, I store the *states* of the connection with each client in an array of structs indexed directly by the socket file descriptor, not that client's index in the other array.
// This is so I can directly access it in the handle_client function, which takes the socket file descriptor. This is easier because the call to epoll_wait in main gives a list of
// file descriptors that are ready for reading, and not anything else.

// Realization?: All I want to do is store whether the file_descriptor is *good* or not for each possible file descriptor. I can do this with an array of booleans, indexed into with the file descriptor.

#define MAX_CLIENTS 10 // STRETCH TODO: Try increasing this. What happens if it gets exceeded?

#define MAX_FD 20 // STRETCH TODO: Try unifying this with MAX_CLIENTS.

int client_sockets[MAX_CLIENTS] = {}; // Holds the file descriptors for any clients currently connected. If there is no client connected with that number, holds 0.

void unrecoverable_error(char* error_string) {
    const char* fatal = "FATAL: ";
    char output[100]; // I happen to know that all my error messages are smaller than this.
    strcpy(output, fatal);
    strcat(output, error_string);
    perror(output);
    exit(EXIT_FAILURE);
}

// This code is copied verbatim from the epoll_server_example code,
// and I don't really care to understand it at the moment. Why do we have
// to check the flags first?
void set_socket_non_blocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        exit(EXIT_FAILURE);
    }
}

// This code is copied verbatim from the example server.c code.
void broadcast_message(char *message, int sender_sd) {

    for (int i = 0; i < MAX_CLIENTS; i++) {
        int sd = client_sockets[i];
        if (sd > 0 && sd != sender_sd) {
            send(sd, message, strlen(message), 0);
        }
    }
}

struct {
    char* buffer;
    size_t buffer_size;
    size_t used_bytes;
    int messages_sent;
} client_socket_states[MAX_CLIENTS];

// Returns 1 when the client socket should stay open, 0 when the client socket should be closed.
int handle_client(int socket) {
    while (1) {
        // TODO: Make sure there's enough room to at least read *some* more. I'm curious how his original code handles the fact that a client's message could be arbitrarily long, but I'll just use realloc, I think.
        int num_bytes_read = read(socket, client_socket_states[socket].buffer +
                client_socket_states[socket].used_bytes, client_socket_states[socket].buffer_size - client_socket_states[socket].used_bytes);
        if (num_bytes_read == -1) { // This return value means some "error" occurred.
            switch(errno) {
                case EAGAIN: break; // There may be more to read later.
                default:
                    printf("Error on socket %d\n", socket); // An actual error occurred.
            }
            break;
        }
        if (num_bytes_read == 0) { // This connection is finished.
            return 0;
        }





int main(int argc, char* argv[]) {
    int c; // Used for parsing options
    char* port_str = NULL;


    while ((c = getopt(argc, argv, "p:")) != -1) {
        // check for an option 'p' with an argument (':')
        switch (c) {
            case 'p': // getopt found the option 'p'
                port_str = optarg; // optarg an external global variable from unistd that is updated by getopt(), stores the value of the option as a string
                break;
            case '?':
                return 1; // Error messages are handled by getopt. Thanks, getopt!
                           // Did Eriksson forget about this?
            default: // Can we ever even reach here, or was Eriksson just
                     // practicing defensive coding?
                abort();
        }
    }

    if (port_str == NULL) {
        fprintf(stderr, "Usage: %s -p port\n", argv[0]);
        return 1;
    }

    int port = atoi(port_str); // WARN: Not checking for errors in parsing the int.

    //
    // Done parsing options. Real server logic begins here:
    //

    int listen_fd, conn_fd;
        // listen_fd: socket that listens and accepts
        // conn_fd: created for each client, maintains connection
    // int* new_sock_ptr;
    struct sockaddr_in server; // This "_in" has nothing at all to do with the address being an "in" address (whatever that would mean), it just means that the address is an INternet address. I really dislike APIs that can't be bothered to speak English.

    // Create socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0); 
        // domain = AF_INET: IPv4
        // type = SOCK_STREAM: two-way stream
        // protocol = 0: use the default protocol associated with this domain 
    if (listen_fd == -1) {
        unrecoverable_error("Could not create socket");
    }

    int reuseaddr_opt = 1; // we want to set the reuseaddr option to true
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt))) {
        // SOL_SOCKET - SOcketLevel_SOCKET: set options at the socket API level 
        //      (presumably the highest level allowed)
        // SO_REUSEADDR: the actual option we're setting, allow addresses to be reused quickly when the program quits. https://www.baeldung.com/linux/socket-options-difference - tries to explain it, but explicitly refers to BSD over Linux and is confusing (and sort of conflicting with some other info online, that says that at most one socket can every have a given address-port pair - this program might conflict with that though, with multiple clients ending up connected on the same port).
        unrecoverable_error("setsockopt failed");
    }

    // TODO: Replace with the 'easier' method on page 607 of CS 361 Notebook.pdf
    // Prepare the sockaddr_in structure:
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY; // TODO: What's this mean exactly?
    server.sin_port = htons(port); // HostTONetwork byte order (I assume the s stands for socket)

    // Bind the port that listens:
    if (bind(listen_fd, (struct sockaddr*) &server, sizeof(server)) < 0) {
        unrecoverable_error("bind failed");
    }


    if (listen(listen_fd, 3) < 0) { // backlog = 3: buffer of 3 connections held by
                                    // the kernel for us
        unrecoverable_error("listen failed");
    }

    // Accept incoming connections
    puts("Waiting for incoming connections...");

    const int MAXEVENTS = 1000;
    struct epoll_event event; // event to use to add more file descriptors to wait on?
    struct epoll_event* events; // Pointer to array of events representing client events to wait on
    events = calloc(MAXEVENTS, sizeof(event));

    int epoll_file_descriptor = epoll_create1(0); // Used purely with calls to the epoll interface
    event.data.fd = listen_fd;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_file_descriptor, EPOLL_CTL_ADD, listen_fd, &event)) {
        unrecoverable_error("epoll_ctl failed on adding listen_fd");
    }

    while (1) { // We can no longer simply block on accept in the loop, since if we block, the server does no work
        int num_ready_fds = epoll_wait(epoll_file_descriptor, events, MAXEVENTS, -1); // last argument is timeout, which we choose not to specify by passing -1. epoll_wait will block indefinitely (until there are ready events)
        for (int i = 0; i < num_ready_fds; i++) {
            if (events[i].data.fd == listen_fd) {
                conn_fd = accept(listen_fd, NULL, NULL); // We don't care about the address of the client, so we set the address and addrlen to NULL
                    // *now* we can accept, we know it won't delay us since we know there's a connection to accept
                if (conn_fd < 0) {
                    perror("Failed to accept incoming connection");
                    continue;
                }

                event.data.fd = conn_fd;
                event.events = EPOLLIN | EPOLLET; // The client connections are edge-triggered.
                if (epoll_ctl(epoll_file_descriptor, EPOLL_CTL_ADD, conn_fd, &event) != 0) {
                    perror("epoll_ctl failed on adding client socket");
                }
                set_socket_non_blocking(conn_fd);
            } else { // We are dealing with a client connection.
                puts("Received something from a client, ready to read it.\n");
            }
        }
    }
}
