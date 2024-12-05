#include "tcp_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <topology.h>

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

struct thread_data threads[MAX_THREADS];
pthread_mutex_t mutex;
int cur_thread_id = 0;

void *run_lock_impl(void *_arg)
{
    thread_data* thread = (thread_data*) _arg;
    int client_socket = thread->sockfd;
    int server_tid = thread->server_tid;
    // int node = server_tid / 2;
    int j = 0;

    pin_thread(server_tid);
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    char granted_msg[BUFFER_SIZE];
    memset(granted_msg, 0, BUFFER_SIZE);
    sprintf(granted_msg, "granted lock");

    char released_msg[BUFFER_SIZE];
    memset(released_msg, 0, BUFFER_SIZE);
    sprintf(released_msg, "released lock");

    char ok_msg[BUFFER_SIZE];
    memset(ok_msg, 0, BUFFER_SIZE);
    sprintf(ok_msg, "ok");

    while (1) {
        int bytes_read = read(client_socket, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            tcp_client_error(client_socket, "Read failed on server thread %d on socket %d", server_tid, client_socket);
        } else if (bytes_read == 0) {
            DEBUG("Thread on server thread %d disconnected: socket fd %d\n", server_tid, client_socket);
            close(client_socket);
            pthread_exit(EXIT_SUCCESS);
        }

        DEBUG("Server %d Received message on socket %d: %s\n", server_tid, client_socket, buffer);
        char cmd;
        int id, ret = 0;
        if (sscanf(buffer, "%c%d", &cmd, &id) == 2) {
            if (cmd == 'l') {
                thread->client_tid = id;
                ull now = rdtsc();
                pthread_mutex_lock(&mutex);
                thread->lock_impl_time[j] += rdtsc() - now;
                if ((ret = send(client_socket, granted_msg, strlen(granted_msg), 0)) < 0)
                    tcp_client_error(client_socket, "lock acquisition notice failed for thread %d", id);
                DEBUG("Granted lock to thread %d over socket %d\n", id, client_socket);
            }
            if (cmd == 'r') {
                ull now = rdtsc();
                pthread_mutex_unlock(&mutex);
                thread->lock_impl_time[j] += rdtsc() - now;
                DEBUG("Released lock on server for thread %d\n", id);
                if ((ret = send(client_socket, released_msg, strlen(released_msg), 0)) < 0)
                    tcp_client_error(client_socket, "lock acquisition notice failed for thread %d", id);
            }
            if (cmd == 'd') {
                j++;
                DEBUG("Received run complete from thread %d\n", id);
                // if ((ret = send(client_socket, ok_msg, strlen(ok_msg), 0)) < 0)
                //     tcp_client_error(client_socket, "lock acquisition notice failed for thread %d", id);
            }
        } else {
            DEBUG("Failed to parse the string from thread %d, got: %s\n", id, buffer);
        }
        memset(buffer, 0, BUFFER_SIZE);
    }
}

int main(int argc, char *argv[]) {
    int nthreads = atoi(argv[1]);
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
        if (event_count == -1) {
            tcp_error("Epoll_wait failed");
            break;
        }
        for (int i = 0; i < event_count; i++) {
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
                threads[cur_thread_id].server_tid = cur_thread_id;
                for (int j = 0; j < NUM_RUNS; j++) {
                    threads[cur_thread_id].lock_impl_time[j] = 0;
                }
                pthread_create(&threads[cur_thread_id].thread, NULL, run_lock_impl, &threads[cur_thread_id]);
                cur_thread_id++;
            } 
        }
    }

    for (int i = 0; i < cur_thread_id; i++) {
        pthread_join(threads[i].thread, NULL);
    }
    for (int j = 0; j < NUM_RUNS; j++) {
        printf("RUN %d\n", j);
        for (int i = 0; i < cur_thread_id; i++) {
            thread_data thread = (thread_data) threads[i];
            printf("%03d,%10.3f\n", thread.client_tid, thread.lock_impl_time[j] / (float) (CYCLE_PER_US * 1000));
        }
        printf("-----------------------------------------------------------------------------------------------\n\n");
    }
    clean_up(server_fd, epoll_fd);
    return 0;
}