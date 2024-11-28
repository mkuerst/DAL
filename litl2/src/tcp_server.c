#include "tcp_common.h"
// #include <pthread.h>
// #include <sched.h>
// #include <numa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

typedef struct thread_data {
    pthread_t thread;
    unsigned int tid;
    int sockfd;
    int nnuma;
    int ncpu;
} thread_data;
struct thread_data threads[MAX_THREADS];
pthread_mutex_t mutex;
int cur_thread_id = 0;

void *run_lock_impl(void *_arg)
{
    thread_data* thread = (thread_data*) _arg;
    int client_socket = thread->sockfd;
    int tid = thread->tid;
    int node = tid % thread->nnuma;
    // fprintf(stderr,"RUNNING ON %d ndoes\n", thread->nnuma);

    if (thread->ncpu != 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET((tid-1)/2, &cpuset);
        int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (ret != 0) {
            tcp_error("pthread_set_affinity_np");
        }
        if (numa_run_on_node(node) != 0) {
            tcp_error("numa_run_on_node %d failed", node);
        }
    }

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    sprintf(msg, "granted lock");
    while (1) {
        int bytes_read = read(client_socket, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            tcp_client_error(client_socket, "Read failed from thread %d on socket %d", tid, client_socket);
        } else if (bytes_read == 0) {
            DEBUG("Thread %d disconnected: socket fd %d\n", tid, client_socket);
            close(client_socket);
            pthread_exit(EXIT_SUCCESS);
        }

        DEBUG("Received message from thread %d on socket %d: %s\n", tid, client_socket, buffer);
        char cmd;
        int id, ret = 0;
        if (sscanf(buffer, "%c%d", &cmd, &id) == 2) {
            // fprintf(stderr, "Command: %c\n", cmd);
            // fprintf(stderr, "tid: %d\n", id);
            if (cmd == 'l') {
                pthread_mutex_lock(&mutex);
                if ((ret = send(client_socket, msg, strlen(msg), 0)) < 0)
                    tcp_client_error(client_socket, "lock acquisition notice failed for thread %d", id);
                DEBUG("Granted lock to thread %d over socket %d\n", id, client_socket);
            }
            if (cmd == 'r') {
                pthread_mutex_unlock(&mutex);
                DEBUG("Released lock on server for thread %d\n", id);
            }
        } else {
            DEBUG("Failed to parse the string\n");
        }
        memset(buffer, 0, BUFFER_SIZE);
    }
}

// void *run_lock_impl(void *arg) {
//     int ret = 0;

//     struct thread_data* thread = (struct thread_data *) arg;
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

int main(int argc, char *argv[]) {
    int nthreads = atoi(argv[1]);
    int ncpu = atoi(argv[2]);
    int nnuma = atoi(argv[3]);
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
    server_addr.sin_port = htons(SERVER_PORT);

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
    fprintf(stderr, "Server listening on port %d\n", SERVER_PORT);

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
    while (cur_thread_id < nthreads) {
        // Wait for events
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        fprintf(stderr, "T0\n");
        if (event_count == -1) {
            tcp_error("Epoll_wait failed");
            break;
        }
        for (int i = 0; i < event_count; i++) {
            fprintf(stderr, "T1\n");
            if (events[i].data.fd == server_fd) {
                // Handle new connection
                client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
                if (client_fd == -1) {
                    tcp_error("Accept failed");
                    continue;
                }

                fprintf(stderr, "New connection: socket fd %d, IP %s, port %d, thread %d\n",
                       client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), cur_thread_id+1);

                // Add new client socket to epoll
                event.events = EPOLLIN | EPOLLET; // Enable edge-triggered mode
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    fprintf(stderr, "Epoll_ctl add client failed\n");
                    close(client_fd);
                }
                threads[cur_thread_id].sockfd = client_fd;
                threads[cur_thread_id].nnuma = nnuma;
                threads[cur_thread_id].ncpu = ncpu;
                threads[cur_thread_id].tid = cur_thread_id+1;
                pthread_create(&threads[cur_thread_id].thread, NULL, run_lock_impl, &threads[cur_thread_id]);
                cur_thread_id++;
            } 
        }
    }

    for (int i = 0; i < cur_thread_id; i++) {
        pthread_join(threads[i].thread, NULL);
    }
    // Cleanup
    // epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_fd, NULL);
    // shutdown(server_fd, SHUT_RDWR);
    // close(server_fd);
    // close(epoll_fd);
    // fprintf(stderr, "Server shutdown complete\n");
    clean_up(server_fd, epoll_fd);
    return 0;
}