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

// LOCAL
#define SERVER_IP "10.5.12.168"
#define SERVER_PORT 12345
#define PORT 12345
#define MAX_EVENTS 1000
#define MAX_CONNECTIONS 1000
#define BUFFER_SIZE 1024

#ifndef NUMA_NODES
#define NUMA_NODES 1
#endif

#ifndef NCPU 
#define NCPU 1
#endif

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

// #ifdef DEBUG
// #define DEBUG(...)                        fprintf(stderr, ## __VA_ARGS__)
// #else
// #define DEBUG(...)
// #endif

int establish_tcp_connection(unsigned int tid, char* addr);