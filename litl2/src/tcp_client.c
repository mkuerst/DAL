#include "tcp_common.h"


// TODO: rename tid -> to task_id to prevent confusion
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
    int max_tries = 10;
    while ((connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) && (try < max_tries)) {
        DEBUG("Thread %d failed at establishing connection: try %d\n", tid, try);
        sleep(0.5);
        try++;
    }
    if (try == max_tries)
        tcp_client_error(sock, "Thread %d failed at connecting\n", tid);

    DEBUG("Thread %d: Connected to server.\n", tid);

    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    sprintf(msg, "%d", tid);
    if (send(sock, msg, BUFFER_SIZE, 0) < 0)
        tcp_client_error(sock, "Task %d failed at sending task_id\n", tid);
    if (read(sock, msg, BUFFER_SIZE) < 0)
        tcp_client_error(sock, "Task %d failed at receiving task_id ack\n", tid);
    // TODO: WHY SLEEP AND WHY ALL THIS BLOCKING COMM?
    // HYPOTHESIS: THE SEND OF THE FIRST LOCK REQ HAPPENS BEFORE THE TCP SERVER
    // CAN START THE THREAD THAT LISTENS TO MSGES FROM ITS CORRESPONDING CLIENT
    sleep(1);
    // int flag = 1;
    // setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    return sock;
}


int tcp_request_lock(int sockfd, int client_id, int task_id)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    int ret = 0;
    sprintf(msg, "l%d.%d", client_id, task_id);

    DEBUG("Client.Task %d.%d requests the lock: %s on socket %d\n", client_id, task_id, msg, sockfd);
    if ((ret = send(sockfd, msg, BUFFER_SIZE, 0)) < 0)
        tcp_client_error(sockfd, "Client.Task %d.%d failed at sending lock request\n", client_id, task_id);

    if ((ret = read(sockfd, buffer, BUFFER_SIZE)) < 0)
        tcp_client_error(sockfd, "Client.Task %d.%d failed at receiving answer for lock request\n", client_id, task_id);
    
    DEBUG("Client.Task %d.%d received msg %s\n", client_id, task_id, buffer);
    return ret;
}

int tcp_release_lock(int sockfd, int client_id, int task_id)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    int ret = 0;
    sprintf(msg, "r%d.%d", client_id, task_id);
    if ((ret = send(sockfd, msg, strlen(msg), 0)) < 0)
        tcp_client_error(sockfd, "Client.Task %d.%d failed at releasing lock\n", client_id, task_id);

    if ((ret = read(sockfd, buffer, BUFFER_SIZE)) < 0)
        tcp_client_error(sockfd, "Client.Task %d.%d failed at receiving answer for lock request\n", client_id, task_id);

    // if (strcmp(buffer, "released lock") == 0) {
    //     tcp_client_error(sockfd, "Thread %d expected released msg, got '%s'\n", task_id, buffer);
    // }
    DEBUG("Client.Task %d.%d released lock to memory server\n", client_id, task_id);
    return ret;
}

int run_complete(int sockfd, int tid)
{
    int ret = 0;
#ifdef TCP
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    sprintf(msg, "d%d", tid);
    if ((ret = send(sockfd, msg, strlen(msg), 0)) < 0)
        tcp_client_error(sockfd, "Thread %d failed at notifying run complete\n", tid);

    // if ((ret = read(sockfd, buffer, BUFFER_SIZE)) < 0)
    //     tcp_client_error(sockfd, "Thread %d failed at receiving answer for run complete\n", tid);

    DEBUG("Thread %d successfully notified run complete\n", tid);
#endif
    return ret;
}