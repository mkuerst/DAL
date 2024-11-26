#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>

// LOCAL
#define SERVER_IP "10.5.12.168"
#define SERVER_PORT 12345
#define PORT 12345
#define MAX_EVENTS 1000
#define MAX_CONNECTIONS 1000
#define BUFFER_SIZE 1024

#define tcp_error(msg, args...) do {\
	fprintf(stderr, "%s : %d : ERROR : "msg, __FILE__, __LINE__, ## args);\
	exit(EXIT_FAILURE);\
}while(0);

#define tcp_client_error(socket_fd, msg, args...) do {\
	fprintf(stderr, "%s : %d : ERROR : "msg, __FILE__, __LINE__, ## args);\
	close(socket_fd);\
	exit(EXIT_FAILURE);\
}while(0);


int establish_tcp_connection(unsigned int tid, char* addr);