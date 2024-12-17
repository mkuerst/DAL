#include "tcp_common.h"

__thread task_t *task;
__thread int client_id;
__thread int sockfd;
__thread int task_id;


char buffer[BUFFER_SIZE];
char lock_msg[BUFFER_SIZE];
char rel_msg[BUFFER_SIZE];

void init_tcp_client(task_t* client_task) {
    task = client_task;
    client_id = task->client_id;
    sockfd = task->sockfd;
    task_id = task->id;
}

int _recv(int fd) {
    int bytes_read = recv(fd, buffer, BUFFER_SIZE, 0);
    DEBUG("Received message on socket %d: %s\n", fd, buffer);
    if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr, "errno == EAGAIN || EWOULDBLOCK\n");
            // All data has been read for now
        } else {
            fprintf(stderr, "Recv failed on socket %d\n", fd);
            close(fd);
        }
    } else if (bytes_read == 0) {
        fprintf(stderr, "Server disconnected: socket fd %d\n", fd);
        close(fd);
    } 
    return bytes_read;    
}


//TODO: tid -> task_id rename
int establish_tcp_connection(unsigned int tid, char* addr) {
    int sock;
    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        tcp_error("socket creation failed\n");
    }

    // int flags = fcntl(sock, F_GETFL, 0);
    // if (flags == -1) {
    //     tcp_error("fcntl F_GETFL");
    // }
    // if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
    //     tcp_error("fcntl F_SETFL");
    // }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, addr, &server_addr.sin_addr) <= 0) {
        tcp_error("Invalid address or address not supported\n");
    }
    char ip_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &server_addr.sin_addr, ip_str, INET_ADDRSTRLEN) == NULL) {
        tcp_error("inet_ntop failed\n");
    }
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

    char msg[32];
    memset(msg, 0, 32);
    sprintf(msg, "%d", tid);
    if (send(sock, msg, 32, 0) < 0)
        tcp_client_error(sock, "Task %d failed at sending task_id\n", tid);
    if (read(sock, msg, 32) < 0)
        tcp_client_error(sock, "Task %d failed at receiving task_id ack\n", tid);
    // TODO: WHY SLEEP AND WHY ALL THIS BLOCKING COMM?
    // HYPOTHESIS: THE SEND OF THE FIRST LOCK REQ HAPPENS BEFORE THE TCP SERVER
    // CAN START THE THREAD THAT LISTENS TO MSGES FROM ITS CORRESPONDING CLIENT
    sleep(1);
    return sock;
}


int tcp_request_lock()
{
    memset(buffer, 0, BUFFER_SIZE);
    memset(lock_msg, 0, BUFFER_SIZE);
    sprintf(lock_msg, "l%d.%d.%d.%d", client_id, task_id, task->run, task->snd_run);
    DEBUG("Client.Task %d.%d requests the lock: %s on socket %d\n", client_id, task_id, lock_msg, sockfd);
    if (send(sockfd, lock_msg, BUFFER_SIZE, 0) < 0)
        tcp_client_error(sockfd, "Client.Task %d.%d failed at sending lock request\n", client_id, task_id);

    // while (_recv(sockfd)  >= 0) {
    //     if (buffer[0] != '\0') {
    //         break;
    //     }
    //     memset(buffer, 0, BUFFER_SIZE);
    // }
    if (recv(sockfd, buffer, BUFFER_SIZE, 0) < 0)
        tcp_client_error(sockfd, "Client.Task %d.%d failed at receiving answer for lock request\n", client_id, task_id);
    
    DEBUG("Client.Task %d.%d received msg %s\n", client_id, task_id, buffer);
    return 0;
}

int tcp_release_lock()
{
    memset(buffer, 0, BUFFER_SIZE);
    memset(rel_msg, 0, BUFFER_SIZE);
    sprintf(rel_msg, "r%d.%d.%d.%d", client_id, task_id, task->run, task->snd_run);
    if (send(sockfd, rel_msg, BUFFER_SIZE, 0) < 0)
        tcp_client_error(sockfd, "Client.Task %d.%d failed at releasing lock\n", client_id, task_id);

    // while (_recv(sockfd)  >= 0) {
    //     if (buffer[0] != '\0') {
    //         break;
    //     }
    //     memset(buffer, 0, BUFFER_SIZE);
    // }
    if (recv(sockfd, buffer, BUFFER_SIZE, 0) < 0)
        tcp_client_error(sockfd, "Client.Task %d.%d failed at receiving answer for lock request\n", client_id, task_id);

    // if (strcmp(buffer, "released lock") == 0) {
    //     tcp_client_error(sockfd, "Thread %d expected released msg, got '%s'\n", task_id, buffer);
    // }
    DEBUG("Client.Task %d.%d released lock to memory server: %s\n", client_id, task_id, buffer);
    return 0;
}