#include "rdma_common.h"
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

volatile bool server_running = true;
rdma_server_meta *clients;
pthread_mutex_t mutex;
int nclients = 0;
int num_connections = 0;
int nthreads = 0;
int nlocks = 0;

uint64_t *rlocks;
int *int_data;
char *byte_data;
uint64_t *shutdown_signal;

struct ibv_pd *pd = NULL;
struct rdma_event_channel *cm_event_channel = NULL;
struct rdma_cm_event *cm_event = NULL;
struct rdma_cm_id *cm_server_id = NULL;
struct ibv_wc wc;
struct ibv_mr *rlock_mr, *data_mr, *shutdown_mr;

void _shutdown() {
	for (int i = 0; i < nclients; i++) {
		rdma_server_meta *client = &clients[i]; 
		for (int j = 0; j < nthreads; j++) {
			rdma_connection *conn = &client->connections[j];
			if (conn->cm_client_id) {
				rdma_disconnect(conn->cm_client_id);
				ibv_destroy_qp(conn->qp);
				ibv_destroy_comp_channel(conn->io_comp_chan);
				ibv_dealloc_pd(conn->pd);
				rdma_destroy_id(conn->cm_client_id);
			}
		}
		free(client->connections);
	}
	free(clients);
	server_running = false;
	int sum = 0;
	for (int i = 0; i < nlocks*MAX_ARRAY_SIZE/sizeof(int); i++) {
		sum += int_data[i];
	}
	fprintf(stderr, "SERVER COUNTS %d TOTAL LOCK ACQUISITIONS\n", sum);

	rdma_buffer_free(rlock_mr);
	rdma_buffer_free(data_mr);
	rdma_buffer_free(shutdown_mr);
	rdma_destroy_id(cm_server_id);
	rdma_destroy_event_channel(cm_event_channel);// 
	fprintf(stderr, "Finished server shutdown\n");
}

void signal_handler(int signum) {
    fprintf(stderr, "\nCaught signal %d. Shutting down gracefully...\n", signum);
	_shutdown();
}

int write_metadata_to_file() {
	DEBUG("WRITING METADATA\n");
	char *cwd = NULL;
	cwd = getcwd(cwd, 128);
	// char addresses_file[64] = "/DAL/microbench/metadata/MN";
	// strcat(cwd, addresses_file);
	char addresses_file[64] = "/nfs/DAL/microbench/metadata/MN";
	FILE* file = fopen(addresses_file, "w");
	if (!file) {
		DEBUG("Failed to open file %s\nTrying again with different dir\n", cwd);
		cwd = getcwd(cwd, 128);
		char addresses_file[64] = "/microbench/metadata/MN";
		strcat(cwd, addresses_file);
		file = fopen(cwd, "w");
		if (!file) {
			rdma_error("Failed at opening file from %s\n", cwd);
			return -errno;
		}
	}
	fprintf(file, "%lu %u\n", (uint64_t) data_mr->addr, data_mr->rkey);
	fprintf(file, "%lu %u\n", (uint64_t) rlock_mr->addr, rlock_mr->rkey);
	fprintf(file, "%lu %u\n", (uint64_t) shutdown_mr->addr, shutdown_mr->rkey);
	fclose(file);
	DEBUG("data_addr: %lu, key: %u\n", (uint64_t) data_mr->addr, data_mr->rkey);
	DEBUG("rlock_addr: %lu, key: %u\n", (uint64_t) rlock_mr->addr, rlock_mr->rkey);
	DEBUG("shutdown_addr: %lu, key: %u\n", (uint64_t) shutdown_mr->addr, shutdown_mr->rkey);
	return 0;
}

int prep_rdma_conn(rdma_connection* conn, int nlocks)
{
	conn->data_sge.addr = (uint64_t) data_mr->addr;
	conn->data_sge.length = data_mr->length;
	conn->data_sge.lkey = data_mr->lkey;
	bzero(&conn->data_wr, sizeof(conn->data_wr));
	conn->data_wr.sg_list = &conn->data_sge;
	conn->data_wr.num_sge = 4;

	conn->rlock_sge.addr = (uint64_t) rlock_mr->addr;
	conn->rlock_sge.length = rlock_mr->length;
	conn->rlock_sge.lkey = rlock_mr->lkey;
	bzero(&conn->rlock_wr, sizeof(conn->rlock_wr));
	conn->rlock_wr.sg_list = &conn->rlock_sge;
	conn->rlock_wr.num_sge = 4;

	return 0;
}

static int start_rdma_server(struct sockaddr_in *server_addr, int nclients, int nthreads, int nlocks) 
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
	int retry = 0;
	while (rdma_bind_addr(cm_server_id, (struct sockaddr*) server_addr) && retry < 10) {
		rdma_error("Failed to bind server address %s:%d, errno: %d \n",
			inet_ntoa(server_addr->sin_addr),
			ntohs(server_addr->sin_port),
			-errno);
		retry++;
	}
	if (retry >= 10) {
		rdma_error("Failed to bind server address after 10 retries\n");
		return -errno;
	}
	if (rdma_listen(cm_server_id, nclients*nthreads)) {
		rdma_error("rdma_listen failed to listen on server address, errno: %d ", -errno);
		return -errno;
	}
	fprintf(stderr, "Server is listening successfully at: %s , port: %d \n",
			inet_ntoa(server_addr->sin_addr),
			ntohs(server_addr->sin_port));

	clients = malloc(nclients * sizeof(rdma_server_meta));
	rlocks = (uint64_t *) aligned_alloc(sizeof(uint64_t), nlocks*RLOCK_SIZE);
	byte_data = malloc(nlocks*MAX_ARRAY_SIZE);
	int_data = (int *) byte_data;
	shutdown_signal = (uint64_t *) malloc(sizeof(uint64_t));
	*shutdown_signal = 0;
	memset(rlocks, 0, nlocks*RLOCK_SIZE);
	memset(byte_data, 0, nlocks*MAX_ARRAY_SIZE);

	for (int i = 0; i < nclients; i++) {
		debug("Waiting for conn establishments of client %d\n", i);
		rdma_server_meta *client = &clients[i];
		client->connections = malloc(nthreads * sizeof(rdma_connection));
		for (int j = 0; j < nthreads; j++) {
			rdma_connection *conn = &client->connections[j];
			DEBUG("Waiting for [%d.%d] to request connection\n", i, j);
			if (process_rdma_cm_event(cm_event_channel, 
					RDMA_CM_EVENT_CONNECT_REQUEST,
					&cm_event)) {
				rdma_error("Failed to get cm event CONNECT_REQUEST, -errno = %d \n" , -errno);
				return -errno;
			}
			DEBUG("[%d.%d] CONNECT_REQUEST received\n", i, j);
			conn->cm_client_id = cm_event->id;
			// struct ibv_pd *pd = NULL;
			struct ibv_comp_channel *io_completion_channel = NULL;
			struct ibv_cq *cq = NULL;
			struct ibv_qp_init_attr qp_init_attr;
			struct ibv_qp *client_qp = NULL;
			struct rdma_cm_id* cm_client_id = conn->cm_client_id;
			///unsigned int task_id = *(unsigned int *) (cm_event->param.conn.private_data);

			if (j == 0 && i == 0) {
				pd = ibv_alloc_pd(cm_client_id->verbs);
				if (!pd) {
					rdma_error("Failed to allocate a protection domain errno: %d\n", -errno);
					return -errno;
				}
				rlock_mr = rdma_buffer_register(
					pd, rlocks, nlocks*sizeof(uint64_t),
					IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC
				);
				data_mr = rdma_buffer_register(
					pd, byte_data, nlocks*MAX_ARRAY_SIZE,
					IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ
				);
				shutdown_mr = rdma_buffer_register(
					pd, shutdown_signal, sizeof(uint64_t),
					IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC
				);
				if (!rlock_mr || !data_mr || !shutdown_mr) {
					rdma_error("Failed to register mrs, -errno %d\n", -errno);
					return -errno;
				}
				write_metadata_to_file();
			}
			if(!cm_client_id){
				rdma_error("Client id is still NULL \n");
				return -EINVAL;
			}
			io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
			if (!io_completion_channel) {
				rdma_error("Failed to create an I/O completion event channel, %d\n", -errno);
				return -errno;
			}
			cq = ibv_create_cq(cm_client_id->verbs, 16, NULL, io_completion_channel, 0);
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
			DEBUG("[%d.%d] Acknowledged CONNECT_REQUEST\n", i, j);

			if(!cm_client_id || !client_qp) {
				rdma_error("Client resources are not properly setup\n");
				return -EINVAL;
			}

			prep_rdma_conn(conn, nlocks);

			memset(&conn_param, 0, sizeof(conn_param));
			conn_param.initiator_depth = 16;
			conn_param.responder_resources = 16;
			conn_param.retry_count = 7;
			conn_param.rnr_retry_count = 7;
			// conn_param.flow_control = 1;
			if (rdma_accept(cm_client_id, &conn_param)) {
				rdma_error("Failed to accept the connection, errno: %d \n", -errno);
				return -errno;
			}
			if (process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ESTABLISHED, &cm_event)) {
				rdma_error("Failed to get the cm event, errno: %d \n", -errno);
				return -errno;
			}
			if (rdma_ack_cm_event(cm_event)) {
				rdma_error("Failed to acknowledge the cm event %d\n", -errno);
				return -errno;
			}
			fprintf(stderr, "Accepted new connection from [%d.%d] \n", i, j);
		}
	}
	return 0;
}

void usage() 
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "rdma_server: [-a <server_addr>] [-p <server_port>] [-c <nclients>] [-t <nthreads>] [-l <nlocks>]\n");
	fprintf(stderr, "(default port is %d)\n", DEFAULT_RDMA_PORT);
	exit(1);
}

int main(int argc, char **argv) 
{
	signal(SIGINT, signal_handler);  // Ctrl+C
    signal(SIGTERM, signal_handler); // VSCode "Stop Debugging" (Shift+F5)
    signal(SIGHUP, signal_handler);  // Terminal closed
    signal(SIGKILL, signal_handler);  // Process killed

	int option;
	struct sockaddr_in server_sockaddr;
	bzero(&server_sockaddr, sizeof server_sockaddr);
	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
	while ((option = getopt(argc, argv, "a:p:c:t:l:")) != -1) {
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
			case 'c':
				nclients = atoi(optarg);
				break;
			case 't':
				nthreads = atoi(optarg);
				break;
			case 'l':
				nlocks = atoi(optarg);
				break;
			default:
				usage();
				break;
		}
	}
	if(!server_sockaddr.sin_port) {
		server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT); 
	}
	if(!nclients || !nthreads || !nlocks) {
		usage();
		return -1;
	}
	if (start_rdma_server(&server_sockaddr, nclients, nthreads, nlocks)) {
		rdma_error("RDMA server failed to start, -errno = %d \n", -errno);
		_shutdown();
		return -errno;
	}
	while (true) {
		sleep(3);
	}
	_shutdown();
	return 0;
}