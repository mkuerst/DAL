#include "tcp_common.h"
#include "pthread.h"
#include "utils.h"

typedef struct locking_thread {
    pthread_t thread;
    unsigned int tid;
    pthread_cond_t cond;
    int sockfd;
};
struct locking_thread threads[MAX_THREADS];
pthread_mutex_t mutex;
int cur_thread_id = 0;

void *run_lock_impl(void *_arg)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    sprintf(msg, "granted lock");
    struct locking_thread* thread = (struct locking_thread*) _arg;
    int client_socket = thread->sockfd;
    int tid = thread->tid;
    while (1) {
        int bytes_read = read(client_socket, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            tcp_client_error(client_socket, "Read failed from thread %d on socket %d", tid, client_socket);
        } else if (bytes_read == 0) {
            fprintf(stderr, "Thread %d disconnected: socket fd %d\n", tid, client_socket);
            close(client_socket);
            pthread_exit(EXIT_SUCCESS);
        }

        printf("Received message from thread %d on socket %d: %s\n", tid, client_socket, buffer);
        char cmd;
        int id, ret = 0;
        if (sscanf(buffer, "%c%d", &cmd, &id) == 2) {
            // fprintf(stderr, "Command: %c\n", cmd);
            // fprintf(stderr, "tid: %d\n", id);
            if (cmd == 'l') {
                pthread_mutex_lock(&mutex);
                if ((ret = send(client_socket, msg, strlen(msg), 0)) < 0)
                    tcp_client_error(client_socket, "lock acquisition notice failed for thread %d", id);
                fprintf(stderr, "Granted lock to thread %d over socket %d\n", id, client_socket);
            }
            if (cmd == 'r') {
                pthread_mutex_unlock(&mutex);
                fprintf(stderr, "Released lock on server for thread %d\n", id);
            }
        } else {
            fprintf(stderr, "Failed to parse the string\n");
        }
        memset(buffer, 0, BUFFER_SIZE);
    }
}

// void *run_lock_impl(void *arg) {
//     int ret = 0;

//     struct locking_thread* thread = (struct locking_thread *) arg;
//     // int sockfd = *((int *)arg);
//     char buf[BUFFER_SIZE];
//     sprintf(buf, "granted lock");
//     pthread_mutex_lock(&mutex);
//     while (1) {
//         pthread_cond_wait(&thread->cond, &mutex);
//         pthread_mutex_lock(&mutex);
//         if ((ret = send(thread->sockfd, buf, strlen(buf), 0)) < 0)
//             tcp_client_error(thread->sockfd, "lock acquisition notice failed");

//         fprintf(stderr, "Sent lock notice to thread %d over socket %d", thread->tid, thread->sockfd);
//     }
//     return 0;
// }

int main() {
    int server_fd, client_fd, epoll_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    struct epoll_event event, events[MAX_EVENTS];
    // task_t *tasks = malloc(sizeof(task_t) * nthreads);

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        tcp_error("Socket failed");
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        tcp_error("Setsockopt failed");
    }

    // Bind to address and port
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        tcp_error("Bind failed");
    }

    // Set the server socket to non-blocking mode
    if (fcntl(server_fd, F_SETFL, O_NONBLOCK) < 0) {
        tcp_error("Setting server socket to non-blocking failed");
    }

    // Start listening for connections
    if (listen(server_fd, SOMAXCONN) == -1) {
        tcp_error("Listen failed");
    }
    printf("Server listening on port %d\n", PORT);

    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        tcp_error("Epoll_create1 failed");
    }

    // Add server socket to epoll
    event.events = EPOLLIN; // Wait for incoming connections
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        tcp_error("Epoll_ctl failed");
    }

    pthread_mutex_init(&mutex, NULL);
    int setup_done = 0;
    while (1) {
        // Wait for events
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (event_count == -1) {
            tcp_error("Epoll_wait failed");
        }

        for (int i = 0; i < event_count; i++) {
            if (events[i].data.fd == server_fd) {
                // Handle new connection
                client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
                if (client_fd == -1) {
                    tcp_error("Accept failed");
                    continue;
                }

                printf("New connection: socket fd %d, IP %s, port %d\n",
                       client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                // Add new client socket to epoll
                event.events = EPOLLIN | EPOLLET; // Enable edge-triggered mode
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("Epoll_ctl add client failed");
                    close(client_fd);
                }
                pthread_cond_init(&threads[cur_thread_id].cond, NULL);
                threads[cur_thread_id].sockfd = client_fd;
                threads[cur_thread_id].tid = cur_thread_id+1;
                pthread_create(&threads[cur_thread_id].thread, NULL, run_lock_impl, &threads[cur_thread_id]);
                cur_thread_id++;
            } 
            else {
                setup_done = 1;
                break;
            }

            // else {
            //     // Handle I/O on existing client socket
            //     int client_socket = events[i].data.fd;
            //     char buffer[BUFFER_SIZE];
            //     memset(buffer, 0, BUFFER_SIZE);

            //     int bytes_read = read(client_socket, buffer, sizeof(buffer));
            //     if (bytes_read == -1) {
            //         perror("Read failed");
            //         close(client_socket);
            //         continue;
            //     } else if (bytes_read == 0) {
            //         printf("Client disconnected: socket fd %d\n", client_socket);
            //         close(client_socket);
            //         continue;
            //     }

            //     printf("Received message from socket %d: %s\n", client_socket, buffer);
            //     char cmd;
            //     int tid;
            //     if (sscanf(buffer, "%c%d", &cmd, &tid) == 2) {
            //         fprintf(stderr, "Command: %c\n", cmd);
            //         fprintf(stderr, "tid: %d\n", tid);
            //         if (cmd == 'l') {
            //             pthread_cond_signal(&threads[tid].cond);
            //         }
            //         // if (cmd == 'r') {
            //         //     release?
            //         // }
            //     } else {
            //         fprintf(stderr, "Failed to parse the string\n");
            //     }
            // }
        }
        if (setup_done)
            break;
    }
    for (int i = 0; i < cur_thread_id; i++) {
        pthread_join(threads[i].thread, NULL);
    }
    // Cleanup
    close(server_fd);
    close(epoll_fd);
    return 0;
}