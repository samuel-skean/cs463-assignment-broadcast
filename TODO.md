- Make the code error when it fails to send a whole message.
- Make the code gracefully handle the error it can get when a socket is closed "out from under" it.

- Use EPOLLOUT correctly, and build up a queue of messages to send. Ideally a bounded queue, so this server isn't just handing out all its memeory for free.

BUGS:
- The C code is often crashing when a client disconnects. This was observed two times in a row on nodes by connecting two nc's and a Rust client and then killing one of the nc's.

- The C code sometimes fails to connect to a new client if the server is already busy (say, the Rust client joined first).