/*
 * This is a RDMA server side code. 
 *
 * Author: Animesh Trivedi 
 *         atrivedi@apache.org 
 *
 * TODO: Cleanup previously allocated resources in case of an error condition
 */

#include "rdma_common.h"
#include <stdbool.h>
#include <string.h>
#include <signal.h>

volatile bool server_running = true;
rdma_thread threads[MAX_THREADS];
pthread_mutex_t mutex;
int num_connections = 0;
char* meta_data;

int* cas_lock;
// void *run_lock_impl(void *_arg)
// {
//     rdma_thread* thread = (rdma_thread*) _arg;
// 	struct ibv_wc wc;
// 	int ret = -1;
	
// 	rdma_connection conn = thread->connection;
// 	struct ibv_comp_channel* io_comp_chan = conn.io_comp_chan;
// 	// struct rdma_buffer_attr client_metadata_attr = conn.client_metadata_attr;
//     // struct rdma_buffer_attr server_metadata_attr = conn.server_metadata_attr;
// 	struct ibv_qp *qp = conn.qp;
// 	struct ibv_pd *pd = conn.pd;
//     struct ibv_mr *client_mr = conn.client_mr;
//     struct ibv_mr *server_mr = conn.server_mr;
//     struct ibv_recv_wr client_recv_wr = conn.client_recv_wr;
//     struct ibv_recv_wr *bad_client_recv_wr = conn.bad_client_recv_wr;
//     struct ibv_send_wr server_send_wr = conn.server_send_wr; 
//     struct ibv_send_wr *bad_server_send_wr = conn.bad_server_send_wr;
//     // struct ibv_sge client_recv_sge = conn.client_recv_sge;
//     struct ibv_sge server_send_sge = conn.server_send_sge;
// 	unsigned int tid = thread->client_tid;
// 	int j = 0;
// 	pin_thread(tid);
// 	char buffer_msg[MESSAGE_SIZE];
// 	memset(buffer_msg, 0, MESSAGE_SIZE);

// 	server_mr = rdma_buffer_register(pd,
// 			buffer_msg,
// 			MESSAGE_SIZE,
// 			(IBV_ACCESS_LOCAL_WRITE|
// 			 IBV_ACCESS_REMOTE_READ|
// 			 IBV_ACCESS_REMOTE_WRITE));
// 	if(!server_mr){
// 		rdma_error("Thread %d failed to register the server_mr buffer, ret = %d \n", tid, ret);
// 		exit(EXIT_FAILURE);
// 	}
// 	server_send_sge.addr = (uint64_t) server_mr->addr;
// 	server_send_sge.length = (uint32_t) server_mr->length;
// 	server_send_sge.lkey = server_mr->lkey;
// 	bzero(&server_send_wr, sizeof(server_send_wr));
// 	server_send_wr.sg_list = &server_send_sge;
// 	server_send_wr.num_sge = 1; 
// 	server_send_wr.opcode = IBV_WR_SEND; 
// 	server_send_wr.send_flags = IBV_SEND_SIGNALED; 

// 	while(1){
// 		ret = ibv_post_recv(qp, &client_recv_wr, &bad_client_recv_wr);
// 		if (ret) {
// 			rdma_error("Tread %d failed to post the receive buffer, errno: %d \n", tid, ret);
// 			exit(EXIT_FAILURE);
// 		}
// 		ret = process_work_completion_events(io_comp_chan, &wc, 1);
// 		if (ret != 1) {
// 			rdma_error("Server failed to receive from thread %d, ret = %d \n", tid, ret);
// 			exit(EXIT_FAILURE);
// 		}
// 		if (ret > 0 && wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV) {
// 			char *buffer = (char *)client_mr->addr;
// 			printf("Received data: %s\n", buffer);

// 			char cmd;
// 			int id, ret = 0;
// 			if (sscanf(buffer, "%c%d", &cmd, &id) == 2) {
// 				if (cmd == 'l') {
// 					ull now = rdtscp();
// 					pthread_mutex_lock(&mutex);
// 					thread->lock_impl_time[j] += rdtscp() - now;
// 					ret = ibv_post_send(qp, &server_send_wr, &bad_server_send_wr);
// 					if (ret) {
// 						rdma_error("Thread %d failed to post lock grant, errno: %d \n", tid, -errno);
// 						exit(EXIT_FAILURE);
// 					}
// 					ret = process_work_completion_events(io_comp_chan, &wc, 1);
// 					if (ret != 1) {
// 						rdma_error("Thread %d failed to send lock grant, ret = %d \n", tid, ret);
// 						exit(EXIT_FAILURE);
// 					}
// 					DEBUG("Granted lock to thread %d\n", id);
// 				}
// 				else if (cmd == 'r') {
// 					ull now = rdtscp();
// 					pthread_mutex_unlock(&mutex);
// 					thread->lock_impl_time[j] += rdtscp() - now;
// 					DEBUG("Released lock on server for thread %d\n", id);
// 					ret = ibv_post_send(qp, &server_send_wr, &bad_server_send_wr);
// 					if (ret) {
// 						rdma_error("Thread %d failed to post lock release, errno: %d \n", tid, -errno);
// 						exit(EXIT_FAILURE);
// 					}
// 					ret = process_work_completion_events(io_comp_chan, &wc, 1);
// 					if (ret != 1) {
// 						rdma_error("Thread %d failed to send lock release, ret = %d \n", tid, ret);
// 						exit(EXIT_FAILURE);
// 					}
// 					DEBUG("Notified thread %d of lock release\n", tid);
// 				}
// 				else if (cmd == 'd') {
// 					j++;
// 					DEBUG("Received run complete from thread %d\n", id);
// 				}
// 			} else {
// 				DEBUG("Failed to parse the string from thread %d, got: %s\n", id, buffer);
// 			}
// 			// memset(buffer, 0, MESSAGE_SIZE);
// 		}
// 	}
// }


void handle_sigint(int sig) {
    printf("\nCaught signal %d (SIGINT). Shutting down gracefully...\n", sig);
    server_running = false;
}

void register_sigint_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);
}

static int start_rdma_server(struct sockaddr_in *server_addr, int nthreads) 
{
	struct rdma_event_channel *cm_event_channel = NULL;
	struct rdma_cm_event *cm_event = NULL;
	struct rdma_cm_id *cm_server_id = NULL;

	cm_event_channel = rdma_create_event_channel();
	if (!cm_event_channel) {
		rdma_error("Creating cm event channel failed with errno : (%d)", -errno);
		return -errno;
	}
	if (rdma_create_id(cm_event_channel, &cm_server_id, NULL, RDMA_PS_TCP)) {
		rdma_error("Creating server cm id failed with errno: %d ", -errno);
		return -errno;
	}
	if (rdma_bind_addr(cm_server_id, (struct sockaddr*) server_addr)) {
		rdma_error("Failed to bind server address, errno: %d \n", -errno);
		return -errno;
	}
	if (rdma_listen(cm_server_id, nthreads)) {
		rdma_error("rdma_listen failed to listen on server address, errno: %d ", -errno);
		return -errno;
	}
	fprintf(stderr, "Server is listening successfully at: %s , port: %d \n",
			inet_ntoa(server_addr->sin_addr),
			ntohs(server_addr->sin_port));

	for (int i = 0; i < 1; i++) {
		struct rdma_connection* conn = &threads[i].connection;
		debug("Waiting for conn establishment %d\n", i);
		if (process_rdma_cm_event(cm_event_channel, 
				RDMA_CM_EVENT_CONNECT_REQUEST,
				&cm_event)) {
			rdma_error("Failed to get cm event CONNECT_REQUEST, -errno = %d \n" , -errno);
			return -errno;
		}
		conn->cm_client_id = cm_event->id;
		struct ibv_pd *pd = NULL;
		struct ibv_comp_channel *io_completion_channel = NULL;
		struct ibv_cq *cq = NULL;
		struct ibv_qp_init_attr qp_init_attr;
		struct ibv_qp *client_qp = NULL;
		struct rdma_cm_id* cm_client_id = conn->cm_client_id;
		unsigned int task_id = *(unsigned int *) (cm_event->param.conn.private_data);

		if(!cm_client_id){
			rdma_error("Client id is still NULL \n");
			return -EINVAL;
		}
		pd = ibv_alloc_pd(cm_client_id->verbs);
		if (!pd) {
			rdma_error("Failed to allocate a protection domain errno: %d\n", -errno);
			return -errno;
		}
		io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
		if (!io_completion_channel) {
			rdma_error("Failed to create an I/O completion event channel, %d\n", -errno);
			return -errno;
		}
		cq = ibv_create_cq(cm_client_id->verbs, CQ_CAPACITY, NULL, io_completion_channel, 0);
		if (!cq) {
			rdma_error("Failed to create a completion queue (cq), errno: %d\n", -errno);
			return -errno;
		}
		if (ibv_req_notify_cq(cq, 0)) {
			rdma_error("Failed to request notifications on CQ errno: %d \n", -errno);
			return -errno;
		}
		bzero(&qp_init_attr, sizeof qp_init_attr);
		qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
		qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
		qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
		qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
		qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
		qp_init_attr.recv_cq = cq; /* Where should I notify for receive completion operations */
		qp_init_attr.send_cq = cq; /* Where should I notify for send completion operations */

		if (rdma_create_qp(cm_client_id, pd, &qp_init_attr)) {
			rdma_error("Failed to create QP due to errno: %d\n", -errno);
			return -errno;
		}
		client_qp = cm_client_id->qp;
		conn->pd = pd;
		conn->io_comp_chan = io_completion_channel;
		conn->cq = cq;
		conn->qp = client_qp;
		struct rdma_conn_param conn_param;

		if (rdma_ack_cm_event(cm_event)) {
			rdma_error("Failed to acknowledge the cm event errno: %d \n", -errno);
			return -errno;
		}

		if(!cm_client_id || !client_qp) {
			rdma_error("Client resources are not properly setup\n");
			return -EINVAL;
		}

		char *global_lock = malloc(MESSAGE_SIZE);
		memset(global_lock, 0, MESSAGE_SIZE);
		conn->client_mr = rdma_buffer_register(
			pd, global_lock, MESSAGE_SIZE,
			IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC
		);
		if (!conn->client_mr) {
			rdma_error("Failed to register mr for lock, -errno %d\n", -errno);
			return -errno;
		}
		// conn->client_mr = rdma_buffer_register(pd /* which protection domain */, 
		// 		&conn->client_metadata_attr /* what memory */,
		// 		sizeof(conn->client_metadata_attr) /* what length */, 
		// 		(IBV_ACCESS_LOCAL_WRITE) /* access permissions */);
		// if(!conn->client_mr){
		// 	rdma_error("Failed to register client attr buffer\n");
		// 	return -ENOMEM;
		// }

		conn->client_recv_sge.addr = (uint64_t) conn->client_mr->addr;
		conn->client_recv_sge.length = conn->client_mr->length;
		conn->client_recv_sge.lkey = conn->client_mr->lkey;
		bzero(&conn->client_recv_wr, sizeof(conn->client_recv_wr));
		conn->client_recv_wr.sg_list = &conn->client_recv_sge;
		conn->client_recv_wr.num_sge = 4;
		
		


		// if (ibv_post_recv(client_qp, &conn->client_recv_wr, &conn->bad_client_recv_wr)) {
		// 	rdma_error("Failed to pre-post the receive buffer, errno: %d \n", -errno);
		// 	return -errno;
		// }
		







		memset(&conn_param, 0, sizeof(conn_param));
		conn_param.initiator_depth = 3;
		conn_param.responder_resources = 3;
		if (rdma_accept(cm_client_id, &conn_param)) {
			rdma_error("Failed to accept the connection, errno: %d \n", -errno);
			return -errno;
		}
			if (process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ESTABLISHED, &cm_event)) {
			rdma_error("Failed to get the cm event, errnp: %d \n", -errno);
			return -errno;
		}
		if (rdma_ack_cm_event(cm_event)) {
			rdma_error("Failed to acknowledge the cm event %d\n", -errno);
			return -errno;
		}
		fprintf(stderr, "A new connection is accepted from task %d \n", task_id);

		meta_data = aligned_alloc(sizeof(uint64_t), META_SIZE);
		struct ibv_mr *server_mr = conn->server_mr;
		server_mr = rdma_buffer_register(pd,
				meta_data,
				META_SIZE,
				(IBV_ACCESS_LOCAL_WRITE|
				IBV_ACCESS_REMOTE_READ|
				IBV_ACCESS_REMOTE_WRITE|
				IBV_ACCESS_REMOTE_ATOMIC));
		if(!server_mr){
			rdma_error("Server failed to register the server_mr buffer, -errno = %d \n", -errno);
			return -errno;
		}

		struct ibv_wc wc;
		// struct ibv_mr *client_mr = conn->client_mr;
		// struct ibv_recv_wr client_recv_wr = conn->client_recv_wr;
		// struct ibv_recv_wr *bad_client_recv_wr = conn->bad_client_recv_wr;
		struct ibv_send_wr server_send_wr = conn->server_send_wr; 
		struct ibv_send_wr *bad_server_send_wr = conn->bad_server_send_wr;
		// struct ibv_sge client_recv_sge = conn->client_recv_sge;
		struct ibv_sge server_send_sge = conn->server_send_sge;
		server_send_sge.addr = (uint64_t) server_mr->addr;
		server_send_sge.length = (uint32_t) server_mr->length;
		server_send_sge.lkey = server_mr->lkey;
		bzero(&server_send_wr, sizeof(server_send_wr));
		server_send_wr.sg_list = &server_send_sge;
		server_send_wr.num_sge = 1; 
		server_send_wr.opcode = IBV_WR_SEND; 
		server_send_wr.send_flags = IBV_SEND_SIGNALED; 


		uintptr_t remote_addr = (uintptr_t)server_mr->addr;
		uint32_t rkey = server_mr->rkey; 
		memset(meta_data, 0, META_SIZE);
		sprintf(meta_data, "remote_addr: %lu\nrkey: %u\n", remote_addr, rkey);

		if (ibv_post_send(conn->qp, &server_send_wr, &bad_server_send_wr)) {
			rdma_error("Server failed to send key and mr, -errno = %d\n" -errno);
			return -errno;
		}
		if (process_work_completion_events(conn->io_comp_chan, &wc, 1) != 1) {
			rdma_error("Server failed to process key,mr send req, -errno = %d \n", -errno);
			return -errno;
		}
		fprintf(stderr, "Sent server metadata remote_addr: 0x%lx rkey: %u\n", remote_addr, rkey);
		memset(meta_data, 0, META_SIZE);
	}
	return 0;
}

void usage() 
{
	printf("Usage:\n");
	printf("rdma_server: [-a <server_addr>] [-p <server_port>] [-t <nthreads>]\n");
	printf("(default port is %d)\n", DEFAULT_RDMA_PORT);
	exit(1);
}

int main(int argc, char **argv) 
{
	register_sigint_handler();
	int option, nthreads;
	struct sockaddr_in server_sockaddr;
	bzero(&server_sockaddr, sizeof server_sockaddr);
	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
	while ((option = getopt(argc, argv, "a:p:t:")) != -1) {
		switch (option) {
			case 'a':
				if (get_addr(optarg, (struct sockaddr*) &server_sockaddr)) {
					rdma_error("Invalid IP \n");
					return -errno;
				}
				break;
			case 'p':
				server_sockaddr.sin_port = htons(strtol(optarg, NULL, 0)); 
				break;
			case 't':
				nthreads = atoi(optarg);
				break;
			default:
				usage();
				break;
		}
	}
	if(!server_sockaddr.sin_port) {
		server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT); 
	}
	if(!nthreads) {
		usage();
		return -1;
	}
	if (start_rdma_server(&server_sockaddr, nthreads)) {
		rdma_error("RDMA server failed to start cleanly, ret = %d \n", -errno);
		return -errno;
	}

	while (server_running) {
		continue;
	}
	return 0;
}
