#include "tcp_common.h"


int establish_tcp_connection(unsigned int tid, char* addr) {
    int sock;
    struct sockaddr_in server_addr;

    // Create a socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        tcp_error("socket creation failed\n");
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    // Convert IP address to binary and assign to server address structure
    if (inet_pton(AF_INET, addr, &server_addr.sin_addr) <= 0) {
        tcp_error("Invalid address or address not supported\n");
    }

    // Convert back to a string for printing
    char ip_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &server_addr.sin_addr, ip_str, INET_ADDRSTRLEN) == NULL) {
        tcp_error("inet_ntop failed\n");
    }

    // fprintf(stderr,"Connecting to IP address: %s:%d\n", ip_str, SERVER_PORT);
    // Connect to the server
    int try = 0;
    int max_tries = 3;
    while ((connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) && (try < max_tries)) {
        fprintf(stderr, "Thread %d failed at establishing connection: try %d\n", tid, try);
        try++;
    }

    if (try == max_tries) 
        tcp_client_error(sock, "Thread %d failed at connecting\n", tid);

    DEBUG("Thread %d: Connected to server.\n", tid);

    return sock;
}


int request_lock(int sockfd, int tid)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    int ret = 0;
    sprintf(msg, "l%d", tid);

    DEBUG("Thread %d requests the lock: %s on socket %d\n", tid, msg, sockfd);
    if ((ret = send(sockfd, msg, BUFFER_SIZE, 0)) < 0)
        tcp_client_error(sockfd, "Thread %d failed at sending lock request\n", tid);
    
    if ((ret = read(sockfd, buffer, BUFFER_SIZE)) < 0)
        tcp_client_error(sockfd, "Thread %d failed at receiving answer for lock request\n", tid);
    
    DEBUG("Thread %d received msg %s\n", tid, buffer);
    return ret;
}

int release_lock(int sockfd, int tid)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    int ret = 0;
    sprintf(msg, "r%d", tid);
    if ((ret = send(sockfd, msg, strlen(msg), 0)) < 0)
        tcp_client_error(sockfd, "Thread %d failed at releasing lock\n", tid);

    if ((ret = read(sockfd, buffer, BUFFER_SIZE)) < 0)
        tcp_client_error(sockfd, "Thread %d failed at receiving answer for lock request\n", tid);

    // if (strcmp(buffer, "released lock") == 0) {
    //     tcp_client_error(sockfd, "Thread %d expected released msg, got '%s'\n", tid, buffer);
    // }
    DEBUG("Thread %d released lock to memory server\n", tid);
    return ret;
}

int run_complete(int sockfd, int tid)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    int ret = 0;
    sprintf(msg, "d%d", tid);
    if ((ret = send(sockfd, msg, strlen(msg), 0)) < 0)
        tcp_client_error(sockfd, "Thread %d failed at notifying run complete\n", tid);

    if ((ret = read(sockfd, buffer, BUFFER_SIZE)) < 0)
        tcp_client_error(sockfd, "Thread %d failed at receiving answer for run complete\n", tid);

    DEBUG("Thread %d successfully notified run complete\n", tid);
    return ret;
}