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
int nthreads = 0;


int* cas_lock;

struct rdma_event_channel *cm_event_channel = NULL;
struct rdma_cm_event *cm_event = NULL;
struct rdma_cm_id *cm_server_id = NULL;
struct ibv_wc wc;

void _shutdown() {
	const char *command = "killall rdma_server";
	if (system(command)) {
			rdma_error("Failed to kill rdma listener. Please execute killall rdma_server manually on server-side machine.\n");
	} 
	for (int i = 0; i < nthreads; i++) {
		rdma_thread thread = threads[i]; 
		rdma_connection conn = thread.connection;
		if (conn.cm_client_id) {
			rdma_disconnect(conn.cm_client_id);
			ibv_destroy_qp(conn.qp);
			ibv_destroy_comp_channel(conn.io_comp_chan);
			ibv_dealloc_pd(conn.pd);
			ibv_dereg_mr(conn.client_mr);
			ibv_dereg_mr(conn.server_mr);
			rdma_destroy_id(conn.cm_client_id);
			rdma_destroy_id(cm_server_id);
			rdma_destroy_event_channel(cm_event_channel);
			free(meta_data);
		}
	}
	fprintf(stderr, "Finished server shutdown\n");
	server_running = false;
}

void handle_sigint(int sig) {
    fprintf(stderr, "\nCaught signal %d. Shutting down gracefully...\n", sig);
	_shutdown();
}

void register_sigint_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGKILL, &sa, NULL);
}

static int start_rdma_server(struct sockaddr_in *server_addr, int nthreads) 
{
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

	for (int i = 0; i < nthreads; i++) {
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

		conn->client_recv_sge.addr = (uint64_t) conn->client_mr->addr;
		conn->client_recv_sge.length = conn->client_mr->length;
		conn->client_recv_sge.lkey = conn->client_mr->lkey;
		bzero(&conn->client_recv_wr, sizeof(conn->client_recv_wr));
		conn->client_recv_wr.sg_list = &conn->client_recv_sge;
		conn->client_recv_wr.num_sge = 4;
		
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
	int option;
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
		rdma_error("RDMA server failed to start, -errno = %d \n", -errno);
		_shutdown();
	}

	while (server_running) {
		if (process_work_completion_events(threads[0].connection.io_comp_chan, &wc, 1) == IBV_WC_WR_FLUSH_ERR) {
			_shutdown();
			break;
		}
	}
	return 0;
}

// /home/kumichae/DAL/litl2/rdma_server -a 10.233.0.21 -t 1