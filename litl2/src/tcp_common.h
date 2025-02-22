#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <pthread.h>
#include "utils.h"
#include <numa.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#define SERVER_PORT 8080 
#define MAX_EVENTS 1000
#define MAX_CONNECTIONS 1000
#define BUFFER_SIZE 128 

#define tcp_error(msg, args...) do {\
	fprintf(stderr, "\033[1;31m%s : %d : ERROR : \033[0m"msg, __FILE__, __LINE__, ## args);\
	fprintf(stderr, "\n");\
	exit(EXIT_FAILURE);\
}while(0);

#define tcp_client_error(socket_fd, msg, args...) do {\
	fprintf(stderr, "\033[1;31m%s : %d : ERROR : \033[0m"msg, __FILE__, __LINE__, ## args);\
	fprintf(stderr, "\n");\
	close(socket_fd);\
	exit(EXIT_FAILURE);\
}while(0);

#define _fail(msg, args...) do {\
	fprintf(stderr, "\033[1;31m%s : %d : ERROR : \033[0m"msg, __FILE__, __LINE__, ## args);\
	fprintf(stderr, "\n");\
}while(0);

#define clean_up(server_fd, epoll_fd) do {\
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_fd, NULL);\
    shutdown(server_fd, SHUT_RDWR);\
    close(server_fd);\
    close(epoll_fd);\
    fprintf(stderr, "Server shutdown complete\n");\
}while(0);

int establish_tcp_connection(unsigned int tid, char* addr);