#include "tcp_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <topology.h>
#include <stdbool.h>

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

thread_data threads[MAX_THREADS];
client_data clients[MAX_CLIENTS];

int epoll_fd, server_fd;
pthread_mutex_t mutex;
int cur_thread_id = 0;
int mode;
int num_connections = 0;
int nthreads;
bool bench_running = true;
int run = 0;
int lat_run =0;

char buffer[BUFFER_SIZE];
char granted_msg[BUFFER_SIZE];
char released_msg[BUFFER_SIZE];
char ok_msg[BUFFER_SIZE];

void _read(int task_id, int socket, char *buf) {
    int bytes_read;
    size_t size=0;
    do {
        bytes_read = read(socket, buf+size, BUFFER_SIZE-size);
        if(bytes_read < 0) {
            tcp_client_error(socket, "Failed to read data from task_id %d", task_id);
        } else if (bytes_read == 0) {
            DEBUG("Thread task_id %d disconnected: socket fd %d\n", task_id, socket);
            close(socket);
            pthread_exit(EXIT_SUCCESS);
        }
        size += bytes_read;
    } while(strchr(buf, '\n') == NULL && size < BUFFER_SIZE);
}

void *run_lock_impl(void *_arg)
{
    thread_data* thread = (thread_data*) _arg;
    int client_socket = thread->sockfd;
    int server_tid = thread->server_tid;
    int task_id = thread->task_id;
    int j = 0;
    int l = 0;
    int mode = thread->mode;

    pin_thread(task_id, nthreads);
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
        int bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_read == -1) {
            tcp_client_error(client_socket, "Read failed on server thread %d on socket %d", server_tid, client_socket);
        } else if (bytes_read == 0) {
            DEBUG("Task %d disconnected: socket fd %d\n", task_id, client_socket);
            close(client_socket);
            pthread_exit(EXIT_SUCCESS);
        } 
        // _read(task_id, client_socket, buffer);

        DEBUG("Server %d Received message on socket %d: %s\n", server_tid, client_socket, buffer);
        char cmd;
        int id, ret = 0;
        if (sscanf(buffer, "%c%d", &cmd, &id) == 2) {
            if (cmd == 'l') {
                thread->client_tid = id;
                ull now = rdtscp();
                pthread_mutex_lock(&mutex);
                thread->wait_acq[j][l] += rdtscp() - now;
                if ((ret = send(client_socket, granted_msg, strlen(granted_msg), 0)) < 0)
                    tcp_client_error(client_socket, "lock acquisition notice failed for task_id %d", task_id);
                DEBUG("Granted lock to task %d over socket %d\n", task_id, client_socket);
            }
            else if (cmd == 'r') {
                ull now = rdtscp();
                pthread_mutex_unlock(&mutex);
                thread->wait_rel[j][l] += rdtscp() - now;
                if (mode == 1)
                    l++;
                if ((ret = send(client_socket, released_msg, strlen(released_msg), 0)) < 0)
                    tcp_client_error(client_socket, "lock acquisition notice failed for task %d", task_id);
                DEBUG("Released lock on server for task %d\n", task_id);
            }
            else if (cmd == 'd') {
                j++;
                l = 0;
                DEBUG("Received run complete from task %d\n", task_id);
                // if ((ret = send(client_socket, ok_msg, strlen(ok_msg), 0)) < 0)
                //     tcp_client_error(client_socket, "lock acquisition notice failed for thread %d", id);
            }
        } else {
            _fail("Failed to parse the string from task %d, got: %s\n", task_id, buffer);
        }
        memset(buffer, 0, BUFFER_SIZE);
    }
}

/* 
==========================================
TCP PROXY MAIN
==========================================
*/
// int main(int argc, char *argv[]) {
//     int nthreads = atoi(argv[1]);
//     mode = atoi(argv[2]);
//     int nclients = atoi(argv[3]);
//     int lat_runs = mode == 1 ? NUM_LAT_RUNS : 1;
//     int server_fd, client_fd, epoll_fd;
//     struct sockaddr_in server_addr, client_addr;
//     socklen_t addr_len = sizeof(client_addr);
//     struct epoll_event event, events[MAX_EVENTS];
//     // task_t *tasks = malloc(sizeof(task_t) * nthreads);

//     server_fd = socket(AF_INET, SOCK_STREAM, 0);
//     if (server_fd == -1) {
//         tcp_error("Socket failed");
//     }
//     int opt = 1;
//     if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_KEEPALIVE, &opt, sizeof(opt)) == -1) {
//         tcp_error("Setsockopt failed");
//     }
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_addr.s_addr = INADDR_ANY;
//     server_addr.sin_port = htons(SERVER_PORT);
//     if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
//         tcp_error("Bind failed");
//     }
//     if (fcntl(server_fd, F_SETFL, O_NONBLOCK) < 0) {
//         tcp_error("Setting server socket to non-blocking failed");
//     }
//     if (listen(server_fd, SOMAXCONN) == -1) {
//         tcp_error("Listen failed");
//     }
//     fprintf(stderr, "Server listening on port %d\n", SERVER_PORT);

//     epoll_fd = epoll_create1(0);
//     if (epoll_fd == -1) {
//         tcp_error("Epoll_create1 failed");
//     }
//     event.events = EPOLLIN; // Wait for incoming connections
//     event.data.fd = server_fd;
//     if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
//         tcp_error("Epoll_ctl failed");
//     }

//     pthread_mutex_init(&mutex, NULL);
//     while (cur_thread_id < nthreads) {
//         int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
//         if (event_count == -1) {
//             tcp_error("Epoll_wait failed");
//             break;
//         }
//         for (int i = 0; i < event_count; i++) {
//             if (events[i].data.fd == server_fd) {
//                 // Handle new connection
//                 client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
//                 if (client_fd == -1) {
//                     tcp_error("Accept failed");
//                     continue;
//                 }
//                 char buffer[32] = {0};
//                 int bytes_read = recv(client_fd, buffer, 32, 0);
//                 if (bytes_read < 0) {
//                     tcp_client_error(client_fd, "Server @ cur_thread_id %d failed at receving task_id\n", cur_thread_id);
//                 }
//                 int task_id;
//                 if (sscanf(buffer, "%d", &task_id) == 1) {
//                     DEBUG("Received task_id %d\n", task_id);
//                     if (send(client_fd, buffer, strlen(buffer), 0) < 0)
//                         tcp_client_error(client_fd, "Server @ task_id %d failed at sending task_id response\n", task_id);
//                 } else {
//                     tcp_client_error(client_fd, "Failed to extract task_id for cur_thread_id %d.\n", cur_thread_id);
//                 }
//                 // int flag = 1;
//                 // setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
//                 int optval = 1;
//                 setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));

//                 fprintf(stderr, "New connection: socket fd %d, IP %s, port %d, thread %d\n",
//                        client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), cur_thread_id+1);

//                 // Add new client socket to epoll
//                 event.events = EPOLLIN | EPOLLET; // Enable edge-triggered mode
//                 event.data.fd = client_fd;
//                 if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
//                     fprintf(stderr, "Epoll_ctl add client failed\n");
//                     close(client_fd);
//                 }
//                 threads[cur_thread_id].sockfd = client_fd;
//                 threads[cur_thread_id].server_tid = cur_thread_id+1;
//                 threads[cur_thread_id].task_id = task_id;
//                 threads[cur_thread_id].mode = mode;
//                 for (int j = 0; j < NUM_RUNS; j++) {
//                     for (int l = 0; l < NUM_LAT_RUNS; l++) {
//                         threads[cur_thread_id].wait_acq[j][l] = 0;
//                         threads[cur_thread_id].wait_rel[j][l] = 0;
//                     }
//                 }
//                 pthread_create(&threads[cur_thread_id].thread, NULL, run_lock_impl, &threads[cur_thread_id]);
//                 cur_thread_id++;
//             } 
//         }
//     }

//     float FACTOR = (float) (CYCLE_PER_US * 1e3);
//     for (int i = 0; i < nthreads; i++) {
//         pthread_join(threads[i].thread, NULL);
//     }
//     for (int j = 0; j < NUM_RUNS; j++) {
//         printf("RUN %d\n", j);
//         for (int i = 0; i < nthreads; i++) {
//             thread_data thread = (thread_data) threads[i];
//             for (int l = 0; l < lat_runs; l++)
//             printf("%03d,%10.6f,%10.6f\n", thread.client_tid,
//             thread.wait_acq[j][l] / FACTOR,
//             thread.wait_rel[j][l] / FACTOR);
//         }
//         printf("-----------------------------------------------------------------------------------------------\n\n");
//     }
//     clean_up(server_fd, epoll_fd);
//     return 0;
// }

/*
==========================================
TCP_SPINLOCK
==========================================
*/
int _recv(int fd, char* buffer, size_t size) {
    int bytes_read = recv(fd, buffer, sizeof(buffer), 0);
    DEBUG("Received message on socket %d: %s\n", fd, buffer);
    if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr, "errno == EAGAIN || EWOULDBLOCK\n");
            // All data has been read for now
        } else {
            fprintf(stderr, "Recv failed on socket %d\n", fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            bench_running = false;
        }
    } else if (bytes_read == 0) {
        fprintf(stderr, "Client disconnected: socket fd %d\n", fd);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        bench_running = false;
    } 
    return bytes_read;
}

int process_msg(int fd, char* buffer, size_t size) {
    char cmd;
    int client_id, task_id;
    if (sscanf(buffer, "%c%d.%d", &cmd, &client_id, &task_id) == 3) {
        if (cmd == 'l') {
            ull now = rdtscp();
            // clients[client_id].wait_acq[j][l] += rdtscp() - now;
            if (send(fd, granted_msg, strlen(granted_msg), 0) < 0)
                tcp_client_error(fd, "lock acquisition notice failed for client.task_id %d.%d", client_id, task_id);
            DEBUG("Granted lock to task %d over socket %d\n", task_id, fd);
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_read = _recv(fd, buffer, sizeof(buffer));
            if (bytes_read < 0) {
                tcp_client_error(fd, "lock release failed for client.task %d.%d", client_id, task_id);
            }
            if (send(fd, released_msg, strlen(released_msg), 0) < 0)
                tcp_client_error(fd, "lock release notice failed for client.task_id %d.%d", client_id, task_id);
            DEBUG("Released lock to client.task %d.%d over socket %d\n", client_id, task_id, fd);
            return 0;
        }
        // else if (cmd == 'r') {
        //     ull now = rdtscp();
        //     thread->wait_rel[j][l] += rdtscp() - now;
        //     if (mode == 1)
        //         l++;
        //     if ((ret = send(client_socket, released_msg, strlen(released_msg), 0)) < 0)
        //         tcp_client_error(client_socket, "lock acquisition notice failed for task %d", task_id);
        //     DEBUG("Released lock on server for task %d\n", task_id);
        // }
        else if (cmd == 'd') {
            DEBUG("Received run complete from client.task %d.%d\n", client_id, task_id);
            return 0;
        }
    } else {
        _fail("Failed to parse the string from client %d, got: %s\n", client_id, buffer);
        return 1;
    }
    memset(buffer, 0, BUFFER_SIZE);
    return 0;
}


void _poll(int epoll_fd, struct epoll_event *events) {
    while (bench_running) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (event_count == -1) {
            tcp_error("Epoll_wait failed");
            break;
        }
        for (int i = 0; i < event_count; i++) {
            int fd = events[i].data.fd;
            while (bench_running) {
                memset(buffer, 0, sizeof(buffer));
                int bytes_read = _recv(fd, buffer, sizeof(buffer));
                if (bytes_read <= 0) {
                    break;
                }
                else {
                    buffer[bytes_read] = '\0'; // Null-terminate the string
                    if (process_msg(fd, buffer, strlen(buffer))) {
                        // tcp_error("Failed to process msg %s\n", buffer);
                    }
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    nthreads = atoi(argv[1]);
    mode = atoi(argv[2]);
    int nclients = atoi(argv[3]);
    int lat_runs = mode == 1 ? NUM_LAT_RUNS : 1;
    int client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    struct epoll_event event, events[MAX_EVENTS];
    // task_t *tasks = malloc(sizeof(task_t) * nthreads);
    memset(buffer, 0, BUFFER_SIZE);
    memset(granted_msg, 0, BUFFER_SIZE);
    sprintf(granted_msg, "granted lock");
    memset(released_msg, 0, BUFFER_SIZE);
    sprintf(released_msg, "released lock");
    memset(ok_msg, 0, BUFFER_SIZE);
    sprintf(ok_msg, "ok");

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        tcp_error("Socket failed");
    }
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_KEEPALIVE, &opt, sizeof(opt)) == -1) {
        tcp_error("Setsockopt failed");
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        tcp_error("Bind failed");
    }
    if (fcntl(server_fd, F_SETFL, O_NONBLOCK) < 0) {
        tcp_error("Setting server socket to non-blocking failed");
    }
    if (listen(server_fd, SOMAXCONN) == -1) {
        tcp_error("Listen failed");
    }
    fprintf(stderr, "Server listening on port %d\n", SERVER_PORT);

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        tcp_error("Epoll_create1 failed");
    }
    event.events = EPOLLIN; // Wait for incoming connections
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        tcp_error("Epoll_ctl failed");
    }

    // pthread_mutex_init(&mutex, NULL);
    while (num_connections < nclients) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (event_count == -1) {
            tcp_error("Epoll_wait failed");
            break;
        }
        for (int i = 0; i < event_count; i++) {
            if (events[i].data.fd == server_fd) {
                client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
                if (client_fd == -1) {
                    tcp_error("Accept failed");
                    continue;
                }
                char buffer[32] = {0};
                int bytes_read = recv(client_fd, buffer, 32, 0);
                if (bytes_read < 0) {
                    tcp_client_error(client_fd, "Server failed at receving client_id\n");
                }
                int client_id;
                if (sscanf(buffer, "%d", &client_id) == 1) {
                    DEBUG("Received client_id %d\n", client_id);
                    if (send(client_fd, buffer, strlen(buffer), 0) < 0)
                        tcp_client_error(client_fd, "Server %d failed at sending client_id response\n", client_id);
                } else {
                    tcp_client_error(client_fd, "Failed to extract client_id.\n");
                }
                // int flag = 1;
                // setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
                int optval = 1;
                setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));

                fprintf(stderr, "New connection: socket fd %d, IP %s, port %d, client_id %d\n",
                       client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_id);

                // Add new client socket to epoll
                event.events = EPOLLIN | EPOLLET; // Enable edge-triggered mode
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    fprintf(stderr, "Epoll_ctl add client %d failed\n", client_id);
                    close(client_fd);
                }
                clients[client_id].sockfd = client_fd;
                clients[client_id].mode = mode;
                for (int j = 0; j < NUM_RUNS; j++) {
                    for (int l = 0; l < NUM_LAT_RUNS; l++) {
                        clients[client_id].wait_acq[j][l] = 0;
                        clients[client_id].wait_rel[j][l] = 0;
                    }
                    for (int l = 0; l < NUM_MEM_RUNS; l++) {
                        clients[client_id].wait_acq_mem[j][l] = 0;
                        clients[client_id].wait_rel_mem[j][l] = 0;
                    }
                }
            } 
        }
        num_connections++;
    }
    _poll(epoll_fd, events);

    // float FACTOR = (float) (CYCLE_PER_US * 1e3);
    // for (int i = 0; i < nthreads; i++) {
    //     pthread_join(threads[i].thread, NULL);
    // }
    // for (int j = 0; j < NUM_RUNS; j++) {
    //     printf("RUN %d\n", j);
    //     for (int i = 0; i < nthreads; i++) {
    //         thread_data thread = (thread_data) threads[i];
    //         for (int l = 0; l < lat_runs; l++)
    //         printf("%03d,%10.6f,%10.6f\n", thread.client_tid,
    //         thread.wait_acq[j][l] / FACTOR,
    //         thread.wait_rel[j][l] / FACTOR);
    //     }
    //     printf("-----------------------------------------------------------------------------------------------\n\n");
    // }
    clean_up(server_fd, epoll_fd);
    return 0;
}