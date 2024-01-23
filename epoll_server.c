#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h> // TODO: What's the difference between this and sys/socket.h,
                        // and why should I care?

void unrecoverable_error(char* error_string) {
    perror(error_string);
    exit(EXIT_FAILURE);
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

    int server_socket, new_socket;
        // server_socket: socket that listens and accepts
        // new_socket: created for each client, maintains connection
    int* new_sock_ptr;
    struct sockaddr_in server, client;

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0); 
        // domain = AF_INET: IPv4
        // type = SOCK_STREAM: two-way stream
        // protocol = 0: use the default protocol associated with this domain 
    if (server_socket == -1) {
        unrecoverable_error("Could not create socket");
    }

    int reuseaddr_opt = 1; // we want to set the reuseaddr option to true
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt))) {
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
    if (bind(server_socket, (struct sockaddr*) &server, sizeof(server)) < 0) {
        unrecoverable_error("bind failed");
    }


    listen(server_socket, 3); // backlog = 3: buffer of 3 connections held by
                              // the kernel for us

    // Accept incoming connections
    puts("Waiting for incoming connections...");
    socklen_t socket_length = sizeof(struct sockaddr_in);

    while ((new_socket = accept(server_socket, (struct sockaddr*) &client, &socket_length))) {
        puts("Accepting connection.");
    }

}
