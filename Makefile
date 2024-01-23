COPT:=-g -Wall -Wextra
server: server.c
	gcc ${COPT} -o $@ $^ -lpthread

epoll_server: epoll_server.c
	gcc ${COPT} -o $@ $^

clean:
	rm server epoll_server
