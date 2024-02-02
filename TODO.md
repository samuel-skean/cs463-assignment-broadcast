- Make the code error when it fails to send a whole message.
- Make the code gracefully handle the error it can get when a socket is closed "out from under" it.

- Use EPOLLOUT correctly, and build up a queue of messages to send. Ideally a bounded queue, so this server isn't just handing out all its memeory for free.