#include "tcp_common.h"


int establish_tcp_connection(unsigned int tid, char* addr) {
    int sock;
    struct sockaddr_in server_addr;

    // Create a socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        tcp_error("tcp_client socket creation failed");
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    // Convert IP address to binary and assign to server address structure
    if (inet_pton(AF_INET, addr, &server_addr.sin_addr) <= 0) {
        tcp_error("@tcp_client: Invalid address or address not supported");
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        tcp_client_error(sock, "Thread %d failed at connecting", tid);
    }

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
        tcp_client_error(sockfd, "Thread %d failed at sending lock request", tid);
    
    if ((ret = read(sockfd, buffer, BUFFER_SIZE)) < 0)
        tcp_client_error(sockfd, "Thread %d failed at receiving answer for lock request", tid);
    
    DEBUG("Thread %d received msg %s\n", tid, buffer);
    return ret;
}

int release_lock(int sockfd, int tid)
{
    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    int ret = 0;
    sprintf(msg, "r%d", tid);
    if ((ret = send(sockfd, msg, strlen(msg), 0)) < 0)
        tcp_client_error(sockfd, "Thread %d failed at releasing lock", tid);
    DEBUG("Thread %d released lock to memory server\n", tid);
    return ret;
}