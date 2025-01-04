#include "rdma_common.h"

/*******************************************************************/
/******************** EVENTS & CHANNELS ****************************/
/*******************************************************************/
static struct rdma_event_channel *cm_event_channel = NULL;
static struct rdma_cm_id *cm_client_id = NULL;
static struct ibv_pd *pd = NULL;

static struct ibv_comp_channel *io_completion_channel[THREADS_PER_CLIENT];
static struct ibv_wc wc[THREADS_PER_CLIENT];

static struct ibv_cq *client_cq[THREADS_PER_CLIENT];
static struct ibv_qp *client_qp[THREADS_PER_CLIENT];
static struct ibv_qp_init_attr qp_init_attr;

/*******************************************************************/
/******************** MEMORY REGIONS *******************************/
/*******************************************************************/
static struct ibv_mr 
				// *client_src_mr = NULL, 
				*local_cas_mr = NULL,
				// *data_mr = NULL,
				// **server_metadata_mr = NULL;
				*local_unlock_mr = NULL;

/*******************************************************************/
/******************** WORK REQUESTS & SGEs *************************/
/*******************************************************************/
static struct ibv_send_wr *bad_client_send_wr[THREADS_PER_CLIENT];
// static struct ibv_recv_wr server_recv_wr[THREADS_PER_CLIENT];
// bad_server_recv_wr[THREADS_PER_CLIENT];
struct ibv_send_wr cas_wr[THREADS_PER_CLIENT], *bad_wr[THREADS_PER_CLIENT], w_wr[THREADS_PER_CLIENT];

// static struct ibv_sge server_recv_sge[THREADS_PER_CLIENT];
struct ibv_sge cas_sge[THREADS_PER_CLIENT], w_sge[THREADS_PER_CLIENT];

/*******************************************************************/
/******************** TASK INFO ************************************/
/*******************************************************************/
int rdma_client_id;

uint64_t rlock_addr;
uint32_t rlock_rkey;
uint64_t data_addr;
uint32_t data_rkey;
uint64_t *cas_result;
uint64_t *unlock_val;
// TODO: keep it thread local or global?
char *data;

/*******************************************************************/
/******************** THREAD LOCAL ************************************/
/*******************************************************************/
__thread int rdma_task_id;
__thread struct ibv_qp *thread_qp;
__thread struct ibv_wc thread_wc;
__thread struct ibv_comp_channel *thread_io_comp_chan;
__thread struct ibv_send_wr thread_cas_wr, *thread_bad_wr, thread_w_wr;
__thread struct ibv_sge thread_cas_sge, thread_w_sge;
__thread int curr_rlock;

// TODO:
void set_rdma_client_meta(rdma_client_meta* client_meta, int cid, int tid)
{
	rdma_task_id = tid;
	rdma_client_id = cid;
	thread_qp = client_meta->qp[tid];
	thread_cas_wr = client_meta->cas_wr[tid];
	thread_cas_sge = client_meta->cas_sge[tid];
	thread_w_wr = client_meta->w_wr[tid];
	thread_w_sge = client_meta->w_sge[tid];
	thread_bad_wr = client_meta->bad_wr[tid];
	thread_io_comp_chan = client_meta->io_comp_chan[tid];
	thread_wc = client_meta->wc[tid];

	rlock_addr = client_meta->rlock_addr;
	rlock_rkey = client_meta->rlock_rkey;
	data_addr = client_meta->data_addr;
	data_rkey = client_meta->data_rkey;
	cas_result = client_meta->cas_result;
	unlock_val = client_meta->unlock_val;
	data = client_meta->data;
}

// TODO: are multiple sges required?
void *create_rdma_client_meta(int nthreads) {
	rdma_client_meta* client_meta = (rdma_client_meta *) malloc(sizeof(rdma_client_meta));
	for (int i = 0; i < nthreads; i++) {
		cas_sge[i].addr   = (uintptr_t)local_cas_mr->addr;
		cas_sge[i].length = sizeof(uint64_t);
		w_sge[i].addr   = (uintptr_t)local_unlock_mr->addr;
		cas_sge[i].lkey   = local_cas_mr->lkey;
		w_sge[i].length = sizeof(uint64_t);
		w_sge[i].lkey   = local_unlock_mr->lkey;

		cas_wr[i].wr_id          = 0;
		cas_wr[i].sg_list        = &cas_sge[i];
		cas_wr[i].num_sge        = 1;
		cas_wr[i].opcode         = IBV_WR_ATOMIC_CMP_AND_SWP;
		cas_wr[i].send_flags     = IBV_SEND_SIGNALED;
		cas_wr[i].wr.atomic.remote_addr = rlock_addr;
		cas_wr[i].wr.atomic.rkey        = rlock_rkey;
		cas_wr[i].wr.rdma.remote_addr = rlock_addr;
		cas_wr[i].wr.rdma.rkey        = rlock_rkey;
		cas_wr[i].wr.atomic.compare_add = 0;
		cas_wr[i].wr.atomic.swap        = 1;

		w_wr[i].wr_id          = 0;
		w_wr[i].sg_list        = &w_sge[i];
		w_wr[i].num_sge        = 1;
		w_wr[i].opcode         = IBV_WR_RDMA_WRITE;
		w_wr[i].send_flags     = IBV_SEND_SIGNALED;
		w_wr[i].wr.atomic.remote_addr = rlock_addr;
		w_wr[i].wr.atomic.rkey        = rlock_rkey;
		w_wr[i].wr.rdma.remote_addr = rlock_addr;
		w_wr[i].wr.rdma.rkey        = rlock_rkey;

		client_meta->qp = client_qp;
		client_meta->cas_wr = cas_wr;
		client_meta->cas_sge = cas_sge;
		client_meta->w_wr = w_wr;
		client_meta->w_sge = w_sge;
		client_meta->io_comp_chan = io_completion_channel;
		client_meta->wc = wc;
		client_meta->bad_wr = bad_client_send_wr;
		client_meta->cas_result = cas_result;
		client_meta->data = data;
		client_meta->rlock_addr = rlock_addr;
		client_meta->rlock_rkey = rlock_rkey;
		client_meta->data_addr = data_addr;
		client_meta->data_rkey = data_rkey;
	}
	return client_meta;
}

int client_prepare_connection(struct sockaddr_in *s_addr, int nthreads)
{
	for (int i = 0; i < nthreads; i++) {
		struct rdma_cm_event *cm_event = NULL;
		cm_event_channel = rdma_create_event_channel();
		if (!cm_event_channel) {
			rdma_error("Creating cm event channel failed, errno: %d \n", -errno);
			return -errno;
		}
		if (rdma_create_id(cm_event_channel, &cm_client_id, NULL, RDMA_PS_TCP)) {
			rdma_error("Creating cm id failed with errno: %d \n", -errno); 
			return -errno;
		}
		if (rdma_resolve_addr(cm_client_id, NULL, (struct sockaddr*) s_addr, 2000)) {
			rdma_error("Failed to resolve address, errno: %d \n", -errno);
			return -errno;
		}
		if (process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ADDR_RESOLVED, &cm_event)) {
			rdma_error("Failed to receive a valid event, -errno = %d \n", -errno);
			return -errno;
		}
		if (rdma_ack_cm_event(cm_event)) {
			rdma_error("Failed to acknowledge the CM event, errno: %d\n", -errno);
			return -errno;
		}
		if (rdma_resolve_route(cm_client_id, 2000)) {
			rdma_error("Failed to resolve route, erno: %d \n", -errno);
			return -errno;
		}
		if (process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ROUTE_RESOLVED, &cm_event)) {
			rdma_error("Failed to receive a valid event, -errno = %d \n", -errno);
			return -errno;
		}
		if (rdma_ack_cm_event(cm_event)) {
			rdma_error("Failed to acknowledge the CM event, errno: %d \n", -errno);
			return -errno;
		}
		pd = ibv_alloc_pd(cm_client_id->verbs);
		if (!pd) {
			rdma_error("Failed to alloc pd, errno: %d \n", -errno);
			return -errno;
		}
		io_completion_channel[i] = ibv_create_comp_channel(cm_client_id->verbs);
		if (!io_completion_channel[i]) {
			rdma_error("Failed to create IO completion event channel for task %d, errno: %d\n", i, -errno);
			return -errno;
		}
		client_cq[i] = ibv_create_cq(cm_client_id->verbs, CQ_CAPACITY, NULL, io_completion_channel[i], 0);
		if (!client_cq[i]) {
			rdma_error("Failed to create CQ for task %d, errno: %d \n", i, -errno);
			return -errno;
		}
		if (ibv_req_notify_cq(client_cq[i], 0)) {
			rdma_error("Failed to request notifications for task %d, errno: %d\n", i, -errno);
			return -errno;
		}

		bzero(&qp_init_attr, sizeof qp_init_attr);
		qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
		qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
		qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
		qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
		qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
		qp_init_attr.recv_cq = client_cq[i]; /* Where should I notify for receive completion operations */
		qp_init_attr.send_cq = client_cq[i]; /* Where should I notify for send completion operations */

		if (rdma_create_qp(cm_client_id, pd, &qp_init_attr)) {
			rdma_error("Failed to create QP for task %d, errno: %d \n", i, -errno);
			return -errno;
		}
		client_qp[i] = cm_client_id->qp;
	}

	debug("Connection prep done\n");
	return 0;
}

int read_from_metadata_file()
{
	// char *filepath = realpath(__FILE__, NULL);
	// if (!filepath) {
	// 	rdma_error("Failed at getting absoulte file path %s\n", __FILE__);
	// 	return -1;
	// }
	char *cwd = NULL;
	cwd = getcwd(cwd, 128);
	char *filename = "/microbench/metadata/addrs";
	strcat(cwd, filename);
	FILE *file = fopen(cwd, "r");
	if (!file) {
		rdma_error("Failed at opening metadata file %s\n", cwd);
		return -1;
	}
	char line[256];
	int i = 0;
	while (fgets(line, sizeof(line), file)) {
		if (i == 0) {
			sscanf(line, "%lu %u", &data_addr, &data_rkey);
		}
		else {
			sscanf(line, "%lu %u", &rlock_addr, &rlock_rkey);
		}
		i++;
	}
	DEBUG("remote data addr: %lu, key: %u\nrlock_addr: %lu, key: %u\n", 
	data_addr, data_rkey, rlock_addr, rlock_rkey);
	return 0;

}

void* client_connect_to_server(int cid, int nthreads, int nlocks) 
{
	struct rdma_conn_param conn_param;
	struct rdma_cm_event *cm_event = NULL;
	bzero(&conn_param, sizeof(conn_param));
	conn_param.initiator_depth = 3;
	conn_param.responder_resources = 3;
	conn_param.retry_count = 3;
	cas_result = aligned_alloc(sizeof(uint64_t), nlocks * sizeof(uint64_t));
	memset(cas_result, 0, nlocks * sizeof(uint64_t));
	local_cas_mr = aligned_alloc(sizeof(uint64_t), nlocks * sizeof(struct ibv_mr *));
	memset(local_cas_mr, 0, nlocks * sizeof(struct ibv_mr*));
	unlock_val = aligned_alloc(sizeof(uint64_t), sizeof(uint64_t));
	*unlock_val = 0;

	local_unlock_mr = rdma_buffer_register(pd,
			unlock_val,
			sizeof(uint64_t),
			(IBV_ACCESS_LOCAL_WRITE|
			IBV_ACCESS_REMOTE_READ|
			IBV_ACCESS_REMOTE_WRITE|
			IBV_ACCESS_REMOTE_ATOMIC));
	if(!local_unlock_mr){
		rdma_error("Client %d failed to register the local_unlock_mr buffer, -errno = %d \n", cid, -errno);
		return NULL;
	}
	local_cas_mr = rdma_buffer_register(pd,
			cas_result,
			nlocks*sizeof(uint64_t),
			(IBV_ACCESS_LOCAL_WRITE|
			IBV_ACCESS_REMOTE_READ|
			IBV_ACCESS_REMOTE_WRITE|
			IBV_ACCESS_REMOTE_ATOMIC));
	if(!local_cas_mr){
		rdma_error("Client %d failed to register local_cas_mr, -errno = %d \n", cid, -errno);
		return NULL;
	}

	conn_param.private_data = &cid;
	conn_param.private_data_len = sizeof(cid);
	cm_client_id->context = (void *)(long) cid;
	if (rdma_connect(cm_client_id, &conn_param)) {
		rdma_error("Client %d failed to connect to remote host , errno: %d\n", cid, -errno);
		return NULL;
	}
	if (process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ESTABLISHED, &cm_event)) {
		rdma_error("Client %d failed to get cm event, -errno = %d \n", cid, -errno);
		return NULL;
	}
	if (rdma_ack_cm_event(cm_event)) {
		rdma_error("Client %d failed to acknowledge cm event, errno: %d\n", cid, -errno);
		return NULL;
	}

	read_from_metadata_file();
	fprintf(stderr, "Client %d connected to RDMA server\n", cid);
	return create_rdma_client_meta(nthreads);
}

ull rdma_request_lock(int rlock_id)
{
	int tries = 0;
	curr_rlock = rlock_id;
	thread_cas_wr.wr.atomic.remote_addr = rlock_addr + rlock_id * 8;
	do {
		tries++;
		if (ibv_post_send(thread_qp, &thread_cas_wr, &thread_bad_wr)) {
			rdma_error("Failed to post CAS-LOCK wr to remote_addr: %lu rkey: %u, -errno %d\n",
			thread_cas_wr.wr.atomic.remote_addr,thread_cas_wr.wr.atomic.rkey, -errno);
			exit(EXIT_FAILURE);
		}

		if (process_work_completion_events(thread_io_comp_chan, &thread_wc, 1) != 1) {
			rdma_error("Failed to poll CAS-LOCK completion, -errno %d\n", -errno);
			exit(EXIT_FAILURE);
		}
	} while(cas_result[rlock_id]);
	debug("Client.Task %d.%d got rlock %d from server after %d tries\n",
	rdma_client_id, rdma_task_id, rlock_id, tries);
	return tries;
}

int rdma_release_lock()
{
	thread_w_wr.wr.rdma.remote_addr = rlock_addr + curr_rlock * 8;
	if (ibv_post_send(thread_qp, &thread_w_wr, &thread_bad_wr)) {
		rdma_error("Failed to post CAS-RELEASE wr to remote_addr: 0x%lx rkey: %u, -errno %d\n",
		thread_cas_wr.wr.atomic.remote_addr, thread_cas_wr.wr.atomic.rkey, -errno);
		exit(EXIT_FAILURE);
	}

	if (process_work_completion_events(thread_io_comp_chan, &thread_wc, 1) != 1) {
		rdma_error("Failed to poll CAS-RELEASE completion, -errno %d\n", -errno);
		exit(EXIT_FAILURE);
	}

	debug("Client.Task %d.%d released rlock on server\n", rdma_client_id, rdma_task_id);
	return 0;
}

void* establish_rdma_connection(int cid, char* addr, int nthreads, int nlocks)
{
	struct sockaddr_in server_sockaddr;
	rdma_client_meta* client_meta;
	bzero(&server_sockaddr, sizeof server_sockaddr);
	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (get_addr(addr, (struct sockaddr*) &server_sockaddr)) {
		rdma_error("Invalid IP \n");
		return NULL;
	}
	if (!server_sockaddr.sin_port) {
		server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT);
	}
	if (client_prepare_connection(&server_sockaddr, nthreads)) { 
		rdma_error("Failed to setup client connection , -errno = %d \n", -errno);
		return NULL;
	}
	if (!(client_meta = client_connect_to_server(cid, nthreads, nlocks))) { 
		rdma_error("Failed to setup client connection , -errno = %d \n", -errno);
		return NULL;
	}
	return client_meta;
}

// TODO: cleanup per thread
// int rdma_disc()
// {
//     ibv_destroy_qp(cm_client_id->qp);
//     ibv_dealloc_pd(cm_client_id->pd);
//     ibv_dereg_mr(client_src_mr);
//     ibv_dereg_mr(local_cas_mr);
//     ibv_dereg_mr(local_unlock_mr);
//     ibv_dereg_mr(server_metadata_mr);
//     ibv_destroy_cq(cm_client_id->send_cq);
//     ibv_destroy_cq(cm_client_id->recv_cq);
//     rdma_destroy_id(cm_client_id);
//     rdma_destroy_event_channel(cm_client_id->channel);
// 	return 0;
// }