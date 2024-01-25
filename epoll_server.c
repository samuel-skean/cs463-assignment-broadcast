#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h> // TODO: What's the difference between this and sys/socket.h,
                        // and why should I care?
#include <sys/epoll.h>

#define MAX_FDS 20 // STRETCH TODO: Try increasing this.

// Assumption: The server does not have to deal with clients sending the null byte.

struct { 
    char* buffer;
    size_t buffer_size;
    size_t used_bytes;
    int messages_sent;
} client_socket_states[MAX_FDS] = {/* Default initialize it, just in case */}; // This array is indexed into with file descriptors. Thank god they start numbering pretty low!


void panic(char* error_string) {
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

// This code is slightly modified from the example server.c code.
void broadcast_message(char *message, int sender_sd) {

    for (int sd = 0; sd < MAX_FDS; sd++) {
        if ((client_socket_states[sd].buffer != NULL) && sd != sender_sd) {
            send(sd, message, strlen(message), 0);
        }
    }
}


// Returns 1 when the client socket should stay open, 0 when the client socket should be closed.
int handle_client(int socket) {
    while (1) {
        // TODO: Make sure there's enough room to at least read *some* more. I'm curious how his original code handles the fact that a client's message could be arbitrarily long, but I'll just use realloc, I think.
        if (client_socket_states[socket].buffer_size == client_socket_states[socket].used_bytes) {
            // No more room in the buffer, make more:
            client_socket_states[socket].buffer_size *= 2;
            client_socket_states[socket].buffer =
                realloc(client_socket_states[socket].buffer,
                        client_socket_states[socket].buffer_size); // grow the buffer by a factor of 2
        }

        int num_bytes_read = read(socket, client_socket_states[socket].buffer +
                client_socket_states[socket].used_bytes,
                (client_socket_states[socket].buffer_size -
                    client_socket_states[socket].used_bytes) - 1); // We put a null byte at the end of this in the next step, so we can't read into the last byte of the buffer.
        if (num_bytes_read == -1) { // This return value means some "error" occurred.
            switch(errno) {
                case EAGAIN: break; // There may be more to read later.
                default:
                    printf("Error on socket %d\n", socket); // An actual error occurred.
            }
            break;
        }

        if (num_bytes_read == 0) {
            return 0;
        }
        // The user might send multiple messages at once, or take a long time to send one message.
        // Here, we read and deal with all the messages we got at once and copy any leftover
        // bytes (the start of a next message, presumably) into the start of the buffer again.

        char* const end_of_meaningful_buffer = client_socket_states[socket].buffer + client_socket_states[socket].used_bytes + num_bytes_read;
        *end_of_meaningful_buffer = '\0'; // put a null byte at the end of the string we just read in so sscanf is happy.

        char* next_message_ptr = client_socket_states[socket].buffer;
        // TODO: I think I can get rid of the message buffer and batch any messages gotten in one go into one big message to the clients, with lots of newlines.
        char* message_buffer = malloc(client_socket_states[socket].buffer_size); // The buffer to put the newline-terminated messages into, as we read them.
        int message_len;
        while (sscanf(next_message_ptr, "%[^\n]%n\n", message_buffer, &message_len) != EOF) {
            broadcast_message(message_buffer, socket);
            next_message_ptr += message_len + 1;
        }
        if (next_message_ptr != client_socket_states[socket].buffer) {
            memmove(client_socket_states[socket].buffer, next_message_ptr,
                    end_of_meaningful_buffer
                    - next_message_ptr /* the start of this last message */);
        }
        client_socket_states[socket].used_bytes = end_of_meaningful_buffer - next_message_ptr;
    }
    return 1;
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
                panic("Unexpected error parsing options.");
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
        panic("Could not create socket");
    }

    int reuseaddr_opt = 1; // we want to set the reuseaddr option to true
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt))) {
        // SOL_SOCKET - SOcketLevel_SOCKET: set options at the socket API level 
        //      (presumably the highest level allowed)
        // SO_REUSEADDR: the actual option we're setting, allow addresses to be reused quickly when the program quits. https://www.baeldung.com/linux/socket-options-difference - tries to explain it, but explicitly refers to BSD over Linux and is confusing (and sort of conflicting with some other info online, that says that at most one socket can every have a given address-port pair - this program might conflict with that though, with multiple clients ending up connected on the same port).
        panic("setsockopt failed");
    }

    // TODO: Replace with the 'easier' method on page 607 of CS 361 Notebook.pdf
    // Prepare the sockaddr_in structure:
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY; // TODO: What's this mean exactly?
    server.sin_port = htons(port); // HostTONetwork byte order (I assume the s stands for socket)

    // Bind the port that listens:
    if (bind(listen_fd, (struct sockaddr*) &server, sizeof(server)) < 0) {
        panic("bind failed");
    }


    if (listen(listen_fd, 3) < 0) { // backlog = 3: buffer of 3 connections held by
                                    // the kernel for us
        panic("listen failed");
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
        panic("epoll_ctl failed on adding listen_fd");
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
                puts("Received something from a client, ready to read it.");
                if (!handle_client(events[i].data.fd)) {
                    epoll_ctl(epoll_file_descriptor, EPOLL_CTL_DEL, events[i].data.fd, 0);
                    close(events[i].data.fd);
                    client_socket_states[events[i].data.fd].buffer = NULL;
                }
            }
        }
    }
}
