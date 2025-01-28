#include "rdma_common.h"
#include "pthread.h"

/*******************************************************************/
/******************** EVENTS & CHANNELS ****************************/
/*******************************************************************/
/*MN EVENTS & CHANNELS*/
static struct rdma_event_channel *cm_event_channel[THREADS_PER_CLIENT];
static struct rdma_cm_id *cm_client_id[THREADS_PER_CLIENT];
static struct rdma_cm_event *cm_event[THREADS_PER_CLIENT];
static struct ibv_pd *pd = NULL;

static struct ibv_comp_channel *io_completion_channel[THREADS_PER_CLIENT];
static struct ibv_wc wc[THREADS_PER_CLIENT];

static struct ibv_cq *client_cq[THREADS_PER_CLIENT];
static struct ibv_qp *client_qp[THREADS_PER_CLIENT];

/*PEER EVENTS & CHANNELS*/
static struct rdma_event_channel *peer_cm_event_channel[MAX_CLIENTS][THREADS_PER_CLIENT];
static struct rdma_cm_id *peer_cm_client_id[MAX_CLIENTS][THREADS_PER_CLIENT];
static struct rdma_cm_event *peer_cm_event[MAX_CLIENTS][THREADS_PER_CLIENT];

static struct ibv_comp_channel *peer_io_chans[MAX_CLIENTS][THREADS_PER_CLIENT];

static struct ibv_cq *peer_cqs[MAX_CLIENTS][THREADS_PER_CLIENT];
static struct ibv_qp *peer_qps[MAX_CLIENTS][THREADS_PER_CLIENT];

/*******************************************************************/
/******************** MEMORY REGIONS *******************************/
/*******************************************************************/
static struct ibv_mr 
*local_cas_mr[THREADS_PER_CLIENT],
*local_data_mr[THREADS_PER_CLIENT],
*local_unlock_mr = NULL;

/*******************************************************************/
/******************** WORK REQUESTS & SGEs *************************/
/*******************************************************************/
static struct ibv_send_wr *bad_client_send_wr[THREADS_PER_CLIENT];
struct ibv_send_wr 
cas_wr[THREADS_PER_CLIENT], 
w_wr[THREADS_PER_CLIENT],
data_wr[THREADS_PER_CLIENT],
*bad_wr[THREADS_PER_CLIENT];


struct ibv_sge 
cas_sge[THREADS_PER_CLIENT],
w_sge[THREADS_PER_CLIENT], 
data_sge[THREADS_PER_CLIENT];

/*******************************************************************/
/******************** TASK INFO ************************************/
/*******************************************************************/
int rdma_client_id;

uint64_t rlock_addr;
uint32_t rlock_rkey;
uint64_t data_addr;
uint32_t data_rkey;
uint64_t *cas_result[THREADS_PER_CLIENT];
char *data[THREADS_PER_CLIENT];
int *int_data[THREADS_PER_CLIENT];
uint64_t *unlock_val;

uint64_t peer_cas_addrs[MAX_CLIENTS];
uint32_t peer_cas_rkeys[MAX_CLIENTS];
uint64_t peer_data_addrs[MAX_CLIENTS];
uint32_t peer_data_rkeys[MAX_CLIENTS];

/*******************************************************************/
/******************** PEER META ************************************/
/*******************************************************************/
rdma_server_meta *peers;

typedef struct {
	struct sockaddr_in sockaddr;
	int nclients, nthreads, cid;
	struct ibv_pd *pd;
} peer_listener;

/*******************************************************************/
/******************** THREAD LOCAL ************************************/
/*******************************************************************/
__thread int rdma_task_id, curr_rlock_id,
curr_byte_offset, curr_byte_data_len, curr_elem_offset;

__thread struct ibv_qp *thread_server_qp;
__thread struct ibv_wc thread_wc;
__thread struct ibv_comp_channel *thread_server_io_chan;
__thread struct ibv_send_wr thread_cas_wr, *thread_bad_wr, thread_w_wr, thread_data_wr;
__thread struct ibv_sge *thread_cas_sge, *thread_w_sge, *thread_data_sge;

__thread task_t *thread_task;
__thread volatile uint64_t *thread_cas_result;
__thread char* thread_byte_data;
__thread int* thread_int_data;

__thread uint64_t thread_peer_cas_addrs[MAX_CLIENTS];
__thread uint64_t thread_peer_data_addrs[MAX_CLIENTS];
__thread uint32_t thread_peer_cas_rkeys[MAX_CLIENTS];
__thread uint32_t thread_peer_data_rkeys[MAX_CLIENTS];

/*PEER THREAD LOCAL*/
__thread struct ibv_qp *thread_peer_qps[MAX_CLIENTS];
__thread struct ibv_comp_channel *thread_peer_io_chans[MAX_CLIENTS];


void set_rdma_client_meta(task_t* task, rdma_client_meta* client_meta, int nclients, int tid, int cid)
{
	rdma_task_id = tid;
	rdma_client_id = cid;
	thread_server_qp = client_meta->qp[tid];
	thread_server_io_chan = client_meta->io_comp_chan[tid];
	thread_cas_wr = client_meta->cas_wr[tid];
	thread_cas_sge = &client_meta->cas_sge[tid];
	thread_w_wr = client_meta->w_wr[tid];
	thread_w_sge = &client_meta->w_sge[tid];
	thread_data_wr = client_meta->data_wr[tid];
	thread_data_sge = &client_meta->data_sge[tid];
	thread_bad_wr = client_meta->bad_wr[tid];
	thread_wc = client_meta->wc[tid];
	thread_task = task;

	thread_byte_data = (char *) client_meta->data_sge[tid].addr;
	thread_int_data = (int *) client_meta->data_sge[tid].addr;
	thread_cas_result = (uint64_t *) client_meta->cas_sge[tid].addr;
	*thread_cas_result = 0;

	rlock_addr = client_meta->rlock_addr;
	rlock_rkey = client_meta->rlock_rkey;
	data_addr = client_meta->data_addr;
	data_rkey = client_meta->data_rkey;
	unlock_val = client_meta->unlock_val;

	for (int c = 0; c < nclients; c++) {
		thread_peer_qps[c] = client_meta->peer_qps[c][tid];
		thread_peer_io_chans[c] = client_meta->peer_io_chans[c][tid];

		thread_peer_cas_addrs[c] = client_meta->peer_cas_addrs[c];
		thread_peer_cas_rkeys[c] = client_meta->peer_cas_rkeys[c];
		thread_peer_data_addrs[c] = client_meta->peer_data_addrs[c];
		thread_peer_data_rkeys[c] = client_meta->peer_data_rkeys[c];
	}
}

void populate_mn_client_meta(int nclients, int nthreads, rdma_client_meta* client_meta) {
	for (int i = 0; i < nthreads; i++) {
		cas_sge[i].addr   = (uint64_t) local_cas_mr[0]->addr + i*RLOCK_SIZE;
		cas_sge[i].length = sizeof(uint64_t);
		cas_sge[i].lkey   = local_cas_mr[0]->lkey;

		w_sge[i].addr   = (uint64_t) local_unlock_mr->addr;
		w_sge[i].length = sizeof(uint64_t);
		w_sge[i].lkey   = local_unlock_mr->lkey;

		data_sge[i].addr   = (uintptr_t) local_data_mr[0]->addr + i*MAX_ARRAY_SIZE;
		data_sge[i].length = MAX_ARRAY_SIZE;
		data_sge[i].lkey   = local_data_mr[0]->lkey;

		cas_wr[i].wr_id          = i;
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

		w_wr[i].wr_id          = i;
		w_wr[i].sg_list        = &w_sge[i];
		w_wr[i].num_sge        = 1;
		w_wr[i].opcode         = IBV_WR_ATOMIC_FETCH_AND_ADD;
		w_wr[i].send_flags     = IBV_SEND_SIGNALED;
		w_wr[i].wr.atomic.remote_addr = rlock_addr;
		w_wr[i].wr.atomic.rkey        = rlock_rkey;
		w_wr[i].wr.rdma.remote_addr = rlock_addr;
		w_wr[i].wr.rdma.rkey        = rlock_rkey;
		// w_wr[i].wr.atomic.compare_add = -1; 

		data_wr[i].wr_id          = i;
		data_wr[i].sg_list        = &data_sge[i];
		data_wr[i].num_sge        = 1;
		data_wr[i].opcode         = IBV_WR_RDMA_READ;
		data_wr[i].send_flags     = IBV_SEND_SIGNALED;
		data_wr[i].wr.rdma.remote_addr = data_addr;
		data_wr[i].wr.rdma.rkey        = data_rkey;

		client_meta->cas_result[i] = local_cas_mr[0]->addr + i*RLOCK_SIZE;
		client_meta->data[i] = local_data_mr[0]->addr + i*MAX_ARRAY_SIZE;
	}
	client_meta->qp = client_qp;
	client_meta->cas_wr = cas_wr;
	client_meta->cas_sge = cas_sge;
	client_meta->w_wr = w_wr;
	client_meta->w_sge = w_sge;
	client_meta->data_wr = data_wr;
	client_meta->data_sge = data_sge;
	client_meta->io_comp_chan = io_completion_channel;
	client_meta->wc = wc;
	client_meta->bad_wr = bad_client_send_wr;
	client_meta->rlock_addr = rlock_addr;
	client_meta->rlock_rkey = rlock_rkey;
	client_meta->data_addr = data_addr;
	client_meta->data_rkey = data_rkey;
}

void populate_peer_client_meta(int nclients, int nthreads, rdma_client_meta* client_meta)
{
	for (int c = 0; c < nclients; c++) {
		for (int t = 0; t < nthreads; t++) {
			client_meta->peer_qps[c][t] = peer_qps[c][t];
			client_meta->peer_io_chans[c][t] = peer_io_chans[c][t];

			client_meta->peer_cas_addrs[c] = peer_cas_addrs[c];
			client_meta->peer_cas_rkeys[c] = peer_cas_rkeys[c];
			client_meta->peer_data_addrs[c] = peer_data_addrs[c];
			client_meta->peer_data_rkeys[c] = peer_data_rkeys[c];
		}
	}
}

int write_metadata_to_file(int cid) {
	DEBUG("WRITING METADATA\n");
	char *cwd = NULL;
	cwd = getcwd(cwd, 128);
	char addresses_file[64] = {0};
	sprintf(addresses_file, "/metadata/C%d", cid);
	strcat(cwd, addresses_file);
	FILE* file = fopen(cwd, "w");
	if (!file) {
		DEBUG("Failed to open file %s\nTrying again with different dir\n", cwd);
		cwd = getcwd(cwd, 128);
		char addresses_file[64] = {0};
		sprintf(addresses_file, "/microbench/metadata/C%d", cid);
		strcat(cwd, addresses_file);
		file = fopen(cwd, "w");
		if (!file) {
			rdma_error("Failed at opening file from %s\n", cwd);
			return -errno;
		}
	}
	fprintf(file, "%lu %u\n", (uint64_t) local_data_mr[0]->addr, local_data_mr[0]->rkey);
	fprintf(file, "%lu %u\n", (uint64_t) local_cas_mr[0]->addr, local_cas_mr[0]->rkey);
	fclose(file);
	DEBUG("[%d] local_data_addr: %lu, key: %u\n", cid, (uint64_t) local_data_mr[0]->addr, local_data_mr[0]->rkey);
	DEBUG("[%d] local_cas_addr: %lu, key: %u\n", cid, (uint64_t) local_cas_mr[0]->addr, local_cas_mr[0]->rkey);
	return 0;
}

int read_mn_metadata_file()
{
	char *cwd = NULL;
	cwd = getcwd(cwd, 128);
	char *filename = "/metadata/MN";
	strcat(cwd, filename);
	FILE *file = fopen(cwd, "r");
	if (!file) {
		DEBUG("Failed at opening metadata file %s\nTrying again with different dir\n", cwd);
		cwd = getcwd(cwd, 128);
		char *filename = "/microbench/metadata/MN";
		strcat(cwd, filename);
		file = fopen(cwd, "r");
		if (!file) {
			rdma_error("Failed at opening metadata file %s\n", cwd);
			return -1;
		}
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
	DEBUG("MN remote data_addr: %lu, key: %u\n", data_addr, data_rkey);
	DEBUG("MN rlock_addr: %lu, key: %u\n", rlock_addr, rlock_rkey);
	return 0;

}

int read_peer_metadata_files(int nclients)
{
	for (int c = 0; c < nclients; c++) {
		char *cwd = NULL;
		cwd = getcwd(cwd, 128);
		char addresses_file[64] = {0};
		sprintf(addresses_file, "/metadata/C%d", c);
		strcat(cwd, addresses_file);
		FILE *file = fopen(cwd, "r");
		if (!file) {
			DEBUG("Failed at opening metadata file %s\nTrying again with different dir\n", cwd);
			cwd = getcwd(cwd, 128);
			char addresses_file[64] = {0};
			sprintf(addresses_file, "/microbench/metadata/C%d", c);
			strcat(cwd, addresses_file);
			file = fopen(cwd, "r");
			if (!file) {
				rdma_error("Failed at opening peer metadata file %s\n", cwd);
				return -1;
			}
		}
		char line[256];
		int i = 0;
		while (fgets(line, sizeof(line), file)) {
			if (i == 0) {
				sscanf(line, "%lu %u", &peer_data_addrs[c], &peer_data_rkeys[c]);
			}
			else {
				sscanf(line, "%lu %u", &peer_cas_addrs[c], &peer_cas_rkeys[c]);
			}
			i++;
		}
		DEBUG("[%d] remote data_addr: %lu, key: %u\n", c, peer_data_addrs[c], peer_data_rkeys[c]);
		DEBUG("[%d] remote cas_addr: %lu, key: %u\n", c, peer_cas_addrs[c], peer_cas_rkeys[c]);
	}
	return 0;
}

int set_qp_timeout(struct ibv_qp *qp) {
    struct ibv_qp_attr attr = {};
    int flags = IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY;

	uint8_t timeout = 1;   // ~4.19 ms
	uint8_t retry_cnt = 7;  // Maximum retries
	uint8_t rnr_retry = 7;  // Infinite RNR retries

    attr.timeout = timeout;           // Set path timeout (4.096 µs × 2^timeout)
    attr.retry_cnt = retry_cnt;       // Number of retries before reporting an error
    attr.rnr_retry = rnr_retry;       // Number of RNR retries

	check_qp_state(qp);

    if (ibv_modify_qp(qp, &attr, flags)) {
        rdma_error("Failed to modify QP");
        return -errno;
    }
    return 0;
}

int client_prepare_server_connection(struct sockaddr_in *s_addr, int nthreads)
{
	for (int i = 0; i < nthreads; i++) {
		cm_event_channel[i] = rdma_create_event_channel();
		if (!cm_event_channel[i]) {
			rdma_error("Creating cm event channel failed, errno: %d \n", -errno);
			return -errno;
		}
		if (rdma_create_id(cm_event_channel[i], &cm_client_id[i], NULL, RDMA_PS_TCP)) {
			rdma_error("Creating cm id failed with errno: %d \n", -errno); 
			return -errno;
		}
		if (rdma_resolve_addr(cm_client_id[i], NULL, (struct sockaddr*) s_addr, 2000)) {
			rdma_error("Failed to resolve address, errno: %d \n", -errno);
			return -errno;
		}
		if (process_rdma_cm_event(cm_event_channel[i], RDMA_CM_EVENT_ADDR_RESOLVED, &cm_event[i])) {
			rdma_error("Failed to receive a valid event, -errno = %d \n", -errno);
			return -errno;
		}
		if (rdma_ack_cm_event(cm_event[i])) {
			rdma_error("Failed to acknowledge the CM event, errno: %d\n", -errno);
			return -errno;
		}
		if (rdma_resolve_route(cm_client_id[i], 2000)) {
			rdma_error("Failed to resolve route, erno: %d \n", -errno);
			return -errno;
		}
		if (process_rdma_cm_event(cm_event_channel[i], RDMA_CM_EVENT_ROUTE_RESOLVED, &cm_event[i])) {
			rdma_error("Failed to receive a valid event, -errno = %d \n", -errno);
			return -errno;
		}
		if (rdma_ack_cm_event(cm_event[i])) {
			rdma_error("Failed to acknowledge the CM event, errno: %d \n", -errno);
			return -errno;
		}
		if (i == 0) {
			pd = ibv_alloc_pd(cm_client_id[i]->verbs);
			if (!pd) {
				rdma_error("Failed to alloc pd, errno: %d \n", -errno);
				return -errno;
			}
		}
		io_completion_channel[i] = ibv_create_comp_channel(cm_client_id[i]->verbs);
		if (!io_completion_channel[i]) {
			rdma_error("Failed to create IO completion event channel for task %d, errno: %d\n", i, -errno);
			return -errno;
		}
		client_cq[i] = ibv_create_cq(cm_client_id[i]->verbs, CQ_CAPACITY, NULL, io_completion_channel[i], 0);
		if (!client_cq[i]) {
			rdma_error("Failed to create CQ for task %d, errno: %d \n", i, -errno);
			return -errno;
		}
		if (ibv_req_notify_cq(client_cq[i], 0)) {
			rdma_error("Failed to request notifications for task %d, errno: %d\n", i, -errno);
			return -errno;
		}

		struct ibv_qp_init_attr qp_init_attr;
		bzero(&qp_init_attr, sizeof(qp_init_attr));
		qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
		qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
		qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
		qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
		qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
		qp_init_attr.recv_cq = client_cq[i]; /* Where should I notify for receive completion operations */
		qp_init_attr.send_cq = client_cq[i]; /* Where should I notify for send completion operations */

		if (rdma_create_qp(cm_client_id[i], pd, &qp_init_attr)) {
			rdma_error("Failed to create QP for task %d, errno: %d \n", i, -errno);
			return -errno;
		}
		client_qp[i] = cm_client_id[i]->qp;
	}
	debug("Connection prep done\n");
	return 0;
}

int client_prepare_peer_connections(char peer_addrs[MAX_CLIENTS][MAX_IP_LENGTH], int nclients, int nthreads)
{
	for (int cid = 0; cid < nclients; cid++) {
		struct sockaddr_in peer_sockaddr;
		create_sockaddr(peer_addrs[cid], &peer_sockaddr, DEFAULT_RDMA_PEER_PORT);
		for (int i = 0; i < nthreads; i++) {
			peer_cm_event_channel[cid][i] = rdma_create_event_channel();

			if (!peer_cm_event_channel[cid][i]) {
				rdma_error("Creating cm event channel failed, errno: %d \n", -errno);
				return -errno;
			}
			if (rdma_create_id(peer_cm_event_channel[cid][i], &peer_cm_client_id[cid][i], NULL, RDMA_PS_TCP)) {
				rdma_error("Creating cm id failed with errno: %d \n", -errno); 
				return -errno;
			}
			if (rdma_resolve_addr(peer_cm_client_id[cid][i], NULL, (struct sockaddr*) &peer_sockaddr, 2000)) {
				rdma_error("Failed to resolve address, errno: %d \n", -errno);
				return -errno;
			}
			if (process_rdma_cm_event(peer_cm_event_channel[cid][i], RDMA_CM_EVENT_ADDR_RESOLVED, &peer_cm_event[cid][i])) {
				rdma_error("Failed to receive a valid event, -errno = %d \n", -errno);
				return -errno;
			}
			if (rdma_ack_cm_event(peer_cm_event[cid][i])) {
				rdma_error("Failed to acknowledge the CM event, errno: %d\n", -errno);
				return -errno;
			}
			if (rdma_resolve_route(peer_cm_client_id[cid][i], 2000)) {
				rdma_error("Failed to resolve route, erno: %d \n", -errno);
				return -errno;
			}
			if (process_rdma_cm_event(peer_cm_event_channel[cid][i], RDMA_CM_EVENT_ROUTE_RESOLVED, &peer_cm_event[cid][i])) {
				rdma_error("Failed to receive a valid event, -errno = %d \n", -errno);
				return -errno;
			}
			if (rdma_ack_cm_event(peer_cm_event[cid][i])) {
				rdma_error("Failed to acknowledge the CM event, errno: %d \n", -errno);
				return -errno;
			}
			peer_io_chans[cid][i] = ibv_create_comp_channel(peer_cm_client_id[cid][i]->verbs);
			if (!peer_io_chans[cid][i]) {
				rdma_error("Failed to create IO completion event channel for task %d, errno: %d\n", i, -errno);
				return -errno;
			}
			peer_cqs[cid][i] = ibv_create_cq(peer_cm_client_id[cid][i]->verbs, CQ_CAPACITY, NULL, peer_io_chans[cid][i], 0);
			if (!peer_cqs[cid][i]) {
				rdma_error("Failed to create CQ for task %d, errno: %d \n", i, -errno);
				return -errno;
			}
			if (ibv_req_notify_cq(peer_cqs[cid][i], 0)) {
				rdma_error("Failed to request notifications for task %d, errno: %d\n", i, -errno);
				return -errno;
			}

			struct ibv_qp_init_attr qp_init_attr;
			bzero(&qp_init_attr, sizeof(qp_init_attr));
			qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
			qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
			qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
			qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
			qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
			qp_init_attr.recv_cq = peer_cqs[cid][i]; /* Where should I notify for receive completion operations */
			qp_init_attr.send_cq = peer_cqs[cid][i]; /* Where should I notify for send completion operations */

			if (rdma_create_qp(peer_cm_client_id[cid][i], pd, &qp_init_attr)) {
				rdma_error("Failed to create QP for task %d, errno: %d \n", i, -errno);
				return -errno;
			}
			peer_qps[cid][i] = peer_cm_client_id[cid][i]->qp;
		}
	}
	debug("Peer connections prep done\n");
	return 0;
}


int client_connect_to_server(int cid, int nthreads, int nlocks, int use_nodes) 
{
	struct rdma_conn_param conn_param;
	bzero(&conn_param, sizeof(conn_param));
	conn_param.initiator_depth = 16;
	conn_param.responder_resources = 16;
	conn_param.retry_count = 7;
	conn_param.rnr_retry_count = 7;
	// conn_param.flow_control = 1;

	int node = 0;
	cas_result[0] = numa_alloc_onnode(nthreads*sizeof(uint64_t), node);
	data[0] = numa_alloc_onnode(nthreads*MAX_ARRAY_SIZE, node);
	if (!data[0]) {
		rdma_error("Client %d failed to allocate data memory, -errno: %d\n", cid, -errno);
		return -errno;
	}

	local_cas_mr[0] = rdma_buffer_register(pd,
			cas_result[0],
			nthreads*sizeof(uint64_t),
			(IBV_ACCESS_LOCAL_WRITE|
			IBV_ACCESS_REMOTE_READ|
			IBV_ACCESS_REMOTE_WRITE|
			IBV_ACCESS_REMOTE_ATOMIC));
	if(!local_cas_mr[0]){
		rdma_error("Client %d failed to register local_cas_mr, -errno = %d \n", cid, -errno);
		return -errno;
	}
	local_data_mr[0] = rdma_buffer_register(pd,
			data[0],
			nthreads*MAX_ARRAY_SIZE,
			(IBV_ACCESS_LOCAL_WRITE|
			IBV_ACCESS_REMOTE_READ|
			IBV_ACCESS_REMOTE_WRITE));
	if(!local_data_mr[0]){
		rdma_error("Client %d failed to register local_data_mr, -errno = %d \n", cid, -errno);
		return -errno;
	}
	DEBUG("SUCCESS AT ALLOCATING PER THREAD DATA\n");

	unlock_val = aligned_alloc(sizeof(uint64_t), sizeof(uint64_t));
	*unlock_val = 0;
	local_unlock_mr = rdma_buffer_register(pd,
			unlock_val,
			sizeof(uint64_t),
			(IBV_ACCESS_LOCAL_WRITE|
			IBV_ACCESS_REMOTE_WRITE|
			IBV_ACCESS_REMOTE_READ|
			IBV_ACCESS_REMOTE_ATOMIC));
	if(!local_unlock_mr){
		rdma_error("Client %d failed to register the local_unlock_mr buffer, -errno = %d \n", cid, -errno);
		return -errno;
	}

	for (int i = 0; i < nthreads; i++) {
		cm_event[i] = NULL;
		conn_param.private_data = &i;
		conn_param.private_data_len = sizeof(i);
		cm_client_id[i]->context = (void *)(long) i;
		DEBUG("[%d.%d] tries to connect to mn\n", cid, i);
		if (rdma_connect(cm_client_id[i], &conn_param)) {
			rdma_error("Client %d failed to connect to remote host , errno: %d\n", cid, -errno);
			return -errno;
		}
		if (process_rdma_cm_event(cm_event_channel[i], RDMA_CM_EVENT_ESTABLISHED, &cm_event[i])) {
			rdma_error("Client %d failed to get cm event, -errno = %d \n", cid, -errno);
			return -errno;
		}
		if (rdma_ack_cm_event(cm_event[i])) {
			rdma_error("Client %d failed to acknowledge cm event, errno: %d\n", cid, -errno);
			return -errno;
		}
	}

	read_mn_metadata_file();
	write_metadata_to_file(cid);
	fprintf(stderr, "[%d] connected to RDMA mn\n", cid);
	return 0;
}

int client_connect_to_peers(int cid, int nclients, int nthreads, int nlocks) 
{
	for (int c = 0; c < nclients; c++) {
		for (int i = 0; i < nthreads; i++) {
			struct rdma_conn_param conn_param;
			bzero(&conn_param, sizeof(conn_param));
			conn_param.initiator_depth = 16;
			conn_param.responder_resources = 16;
			conn_param.retry_count = 7;
			conn_param.rnr_retry_count = 7;
			// conn_param.flow_control = 1;

			peer_cm_event[c][i] = NULL;
			conn_param.private_data = &i;
			conn_param.private_data_len = sizeof(i);
			peer_cm_client_id[c][i]->context = (void *)(long) i;
			DEBUG("[%d.%d] tries to connect to [%d]\n", cid, i, c);
			if (rdma_connect(peer_cm_client_id[c][i], &conn_param)) {
				rdma_error("[%d.%d] failed to connect to [%d], errno: %d\n", cid, i, c, -errno);
				return -errno;
			}
			if (process_rdma_cm_event(peer_cm_event_channel[c][i], RDMA_CM_EVENT_ESTABLISHED, &peer_cm_event[c][i])) {
				rdma_error("[%d.%d] failed to get cm event from [%d], -errno = %d \n", cid, i, c, -errno);
				return -errno;
			}
			if (rdma_ack_cm_event(peer_cm_event[c][i])) {
				rdma_error("[%d.%d] failed to acknowledge cm event for [%d], errno: %d\n", cid, i, c, -errno);
				return -errno;
			}
			DEBUG("[%d.%d] connected to peer [%d]\n", cid, i, c);
		}
	}

	read_peer_metadata_files(nclients);
	fprintf(stderr, "[%d] connected to RDMA peers\n", cid);
	return 0;
}

void* listen_for_peer_connections(void* _args) 
{
	peer_listener *peer_info = (peer_listener *) _args;
	int cid = peer_info->cid;
	int nclients = peer_info->nclients;
	int nthreads = peer_info->nthreads;
	struct ibv_pd *pd = peer_info->pd;
	struct sockaddr_in *server_addr = &peer_info->sockaddr;
	DEBUG("DEBUG peer_sockaddr: %s:%d\n",
		inet_ntoa(peer_info->sockaddr.sin_addr),
		ntohs(peer_info->sockaddr.sin_port));
	DEBUG("DEBUG server_sockaddr: %s:%d\n",
		inet_ntoa(server_addr->sin_addr),
		ntohs(server_addr->sin_port));

	struct rdma_event_channel *cm_peer_event_channel = rdma_create_event_channel();
	struct rdma_cm_event *cm_peer_event;
	struct rdma_cm_id *cm_server_id = NULL;

	if (!cm_peer_event_channel) {
		rdma_error("[%d] Creating cm event channel failed with errno : (%d)", cid, -errno);
		exit(EXIT_FAILURE);
	}
	if (rdma_create_id(cm_peer_event_channel, &cm_server_id, NULL, RDMA_PS_TCP)) {
		rdma_error("[%d] Creating server cm id failed with errno: %d ", cid, -errno);
		exit(EXIT_FAILURE);
	}
	int retry = 0;
	while (rdma_bind_addr(cm_server_id, (struct sockaddr*) server_addr) && retry < 3) {
		rdma_error("[%d] failed to bind server address %s:%d, errno: %d \n", cid,
			inet_ntoa(server_addr->sin_addr),
			ntohs(server_addr->sin_port),
			-errno);
		retry++;
	}
	if (retry >= 3) {
		rdma_error("[%d] failed to bind server address after 3 retries\n", cid);
		exit(EXIT_FAILURE);
	}
	if (rdma_listen(cm_server_id, nclients*nthreads)) {
		rdma_error("[%d] rdma_listen failed to listen on server address, errno: %d ", cid, -errno);
		exit(EXIT_FAILURE);
	}
	DEBUG("[%d] Client is listening successfully at: %s , port: %d \n",
			cid,
			inet_ntoa(server_addr->sin_addr),
			ntohs(server_addr->sin_port));

	peers = malloc(nclients * sizeof(rdma_server_meta));

	for (int i = 0; i < nclients; i++) {
		debug("Waiting for conn establishments of client %d\n", i);
		rdma_server_meta *peer = &peers[i];
		peer->connections = malloc(nthreads * sizeof(rdma_connection));
		for (int j = 0; j < nthreads; j++) {
			rdma_connection *conn = &peer->connections[j];
			DEBUG("Waiting for [%d.%d] to request connection\n", i, j);
			if (process_rdma_cm_event(cm_peer_event_channel, 
					RDMA_CM_EVENT_CONNECT_REQUEST,
					&cm_peer_event)) {
				rdma_error("Failed to get cm event CONNECT_REQUEST, -errno = %d \n" , -errno);
				exit(EXIT_FAILURE);
			}
			DEBUG("[%d.%d] CONNECT_REQUEST received\n", i, j);
			conn->cm_client_id = cm_peer_event->id;
			struct ibv_comp_channel *io_completion_channel = NULL;
			struct ibv_cq *cq = NULL;
			struct ibv_qp_init_attr qp_init_attr;
			struct ibv_qp *client_qp = NULL;
			struct rdma_cm_id* cm_client_id = conn->cm_client_id;
			
			if(!cm_client_id){
				rdma_error("Client id is still NULL \n");
				exit(EXIT_FAILURE);
			}
			io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
			if (!io_completion_channel) {
				rdma_error("Failed to create an I/O completion event channel, %d\n", -errno);
				exit(EXIT_FAILURE);
			}
			cq = ibv_create_cq(cm_client_id->verbs, 16, NULL, io_completion_channel, 0);
			if (!cq) {
				rdma_error("Failed to create a completion queue (cq), errno: %d\n", -errno);
				exit(EXIT_FAILURE);
			}
			if (ibv_req_notify_cq(cq, 0)) {
				rdma_error("Failed to request notifications on CQ errno: %d \n", -errno);
				exit(EXIT_FAILURE);
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
				exit(EXIT_FAILURE);
			}
			client_qp = cm_client_id->qp;
			conn->pd = pd;
			conn->io_comp_chan = io_completion_channel;
			conn->cq = cq;
			conn->qp = client_qp;
			struct rdma_conn_param conn_param;

			if (rdma_ack_cm_event(cm_peer_event)) {
				rdma_error("Failed to acknowledge the cm event errno: %d \n", -errno);
				exit(EXIT_FAILURE);
			}
			DEBUG("[%d.%d] Acknowledged CONNECT_REQUEST\n", i, j);

			if(!cm_client_id || !client_qp) {
				rdma_error("Client resources are not properly setup\n");
				exit(EXIT_FAILURE);
			}

			memset(&conn_param, 0, sizeof(conn_param));
			conn_param.initiator_depth = 16;
			conn_param.responder_resources = 16;
			conn_param.retry_count = 7;
			conn_param.rnr_retry_count = 7;
			// conn_param.flow_control = 1;
			if (rdma_accept(cm_client_id, &conn_param)) {
				rdma_error("Failed to accept the connection, errno: %d \n", -errno);
				exit(EXIT_FAILURE);
			}
			if (process_rdma_cm_event(cm_peer_event_channel, RDMA_CM_EVENT_ESTABLISHED, &cm_peer_event)) {
				rdma_error("Failed to get the cm event, errno: %d \n", -errno);
				exit(EXIT_FAILURE);
			}
			if (rdma_ack_cm_event(cm_peer_event)) {
				rdma_error("Failed to acknowledge the cm event %d\n", -errno);
				exit(EXIT_FAILURE);
			}
			DEBUG("[%d] Accepted new connection from [%d.%d] \n", cid, i, j);
		}
	}
	DEBUG("[%d] SUCCESSFULLY ACCEPTED ALL CONNECTIONS\n listener_thread exits\n", cid);
	return 0;
}

int start_peer_listener(
	int cid, char peer_addrs[MAX_CLIENTS][MAX_IP_LENGTH],
	int nclients, int nthreads,
	pthread_t *thread)
{
	peer_listener *peer_info = malloc(sizeof(peer_listener));
	struct sockaddr_in own_sockaddr;
	if (create_sockaddr(peer_addrs[cid], &own_sockaddr, DEFAULT_RDMA_PEER_PORT)) {
		rdma_error("[%d] failed to create loopback sockaddr, errno: %d", cid, -errno);
		return -errno;
	};
	peer_info->cid = cid;
	peer_info->sockaddr = own_sockaddr;
	peer_info->nclients = nclients;
	peer_info->nthreads = nthreads;
	peer_info->pd = pd;
	DEBUG("DEBUG peer_sockaddr: %s:%d\n",
		inet_ntoa(peer_info->sockaddr.sin_addr),
		ntohs(peer_info->sockaddr.sin_port));
	DEBUG("[%d] Starting peer listener\n", cid);
	pthread_create(thread, NULL, listen_for_peer_connections, peer_info);
	return 0;
}

int establish_rdma_mn_connection(
	int cid, char *mn_addr,
	int nthreads, int nclients, int nlocks, int use_nodes,
	rdma_client_meta *client_meta)
{
	struct sockaddr_in mn_sockaddr; 
	if (create_sockaddr(mn_addr, &mn_sockaddr, DEFAULT_RDMA_PORT)) {
		rdma_error("[%d] failed to create MN sockaddr, -errno %d", cid, -errno);
		return -errno;
	}
	if (client_prepare_server_connection(&mn_sockaddr, nthreads)) { 
		rdma_error("Failed to setup client connection , -errno = %d \n", -errno);
		return -errno;
	}
	if (client_connect_to_server(cid, nthreads, nlocks, use_nodes)) { 
		rdma_error("Failed to setup client connection , -errno = %d \n", -errno);
		return -errno;
	}
	populate_mn_client_meta(nclients, nthreads, client_meta);
	return 0;
}

int establish_rdma_peer_connections(
	int cid, char peer_addrs[MAX_CLIENTS][MAX_IP_LENGTH],
	int nthreads, int nclients, int nlocks,
	rdma_client_meta* client_meta)
{
	if (client_prepare_peer_connections(peer_addrs, nclients, nthreads)) { 
		rdma_error("[%d] failed to prepare peer connections, -errno = %d \n", cid, -errno);
		return -errno;
	}
	if (client_connect_to_peers(cid, nclients, nthreads, nlocks)) { 
		rdma_error("[%d] failed to make peer connections, -errno = %d \n", cid, -errno);
		return -errno;
	}
	populate_peer_client_meta(nclients, nthreads, client_meta);
	return 0;
}

static inline int perform_rdma_op(struct ibv_qp* qp, struct ibv_comp_channel* io_chan, struct ibv_send_wr *wr)
{
	if (ibv_post_send(qp, wr, &thread_bad_wr)) {
		rdma_error("[%d.%d] Failed to post wr to\nremote_addr: %lu rkey: %u, -errno %d\n",
		rdma_client_id, rdma_task_id, wr->wr.rdma.remote_addr, wr->wr.rdma.rkey, -errno);
		rdma_error("atomic remote_addr: %lu rkey: %u, -errno %d\n",
		wr->wr.atomic.remote_addr, wr->wr.atomic.rkey, -errno);
		return -1;
	}
	if (process_work_completion_events(io_chan, &thread_wc, 1) != 1) {
		rdma_error("[%d.%d] Failed to poll wr completion, -errno %d\n",
		rdma_client_id, rdma_task_id, -errno);
		return -1;
	}
	return 0;
}

static inline void set_lock_meta(disa_mutex_t *disa_mutex)
{
	curr_rlock_id = disa_mutex->id;
	curr_byte_offset = disa_mutex->offset;
	curr_byte_data_len = disa_mutex->data_len;
	curr_elem_offset = curr_byte_offset / disa_mutex->elem_sz;
}

static inline ull rdma_bo_cas(uint64_t rlock_addr, uint32_t rkey, struct ibv_qp *qp, struct ibv_comp_channel *io_chan)
{
    unsigned int i;
	unsigned int delay = BO;
	ull tries = 0;
	thread_cas_wr.wr.atomic.remote_addr = rlock_addr;
	thread_cas_wr.wr.atomic.rkey = rkey;
	do {
		tries++;
		// if (tries > 100) {
		// 	rdma_error("[%d.%d] Exceeded tries for lock [%d]\n", rdma_client_id, rdma_task_id, curr_rlock_id);
		// 	exit(EXIT_FAILURE);
		// }
		DEBUG("[%d.%d] rdma_cas post, lock[%d]\n", rdma_client_id, rdma_task_id, curr_rlock_id);
		if(perform_rdma_op(qp, io_chan, &thread_cas_wr)) {
			rdma_error("[%d.%d] rdma_cas failed\n", rdma_client_id, rdma_task_id);
			exit(EXIT_FAILURE);
		}
		if (*thread_cas_result) {
			for (i = 0; i < delay; i++)
				CPU_PAUSE();

			if (delay < MAX_BO)
				delay *= 2;
		} else {
			DEBUG("[%d.%d] got rlock [%d] from server after %llu tries\n",
			rdma_client_id, rdma_task_id, curr_rlock_id, tries);
			return tries;
		}
	} while(1);
}

static inline ull rdma_cas(uint64_t rlock_addr, uint32_t rkey, struct ibv_qp *qp, struct ibv_comp_channel *io_chan)
{
	ull tries = 0;
	thread_cas_wr.wr.atomic.remote_addr = rlock_addr;
	thread_cas_wr.wr.atomic.rkey = rkey;
	do {
		tries++;
		DEBUG("[%d.%d] rdma_cas post, lock[%d]\n", rdma_client_id, rdma_task_id, curr_rlock_id);
		if(perform_rdma_op(qp, io_chan, &thread_cas_wr)) {
			rdma_error("[%d.%d] rdma_cas failed\n", rdma_client_id, rdma_task_id);
			exit(EXIT_FAILURE);
		}
	} while(*thread_cas_result);

	DEBUG("[%d.%d] got rlock [%d] from server after %llu tries\n",
	rdma_client_id, rdma_task_id, curr_rlock_id, tries);
	return tries;
}

static inline void rdma_read_data(disa_mutex_t* disa_mutex)
{
	if (curr_byte_offset >= 0) {
		thread_data_wr.wr.rdma.remote_addr = disa_mutex->data_addr;
		thread_data_sge->addr = (uint64_t) thread_byte_data;
		thread_data_sge->length = curr_byte_data_len;
		thread_data_wr.opcode = IBV_WR_RDMA_READ;
		DEBUG("[%d.%d] rdma_read post\n", rdma_client_id, rdma_task_id);
		if(perform_rdma_op(thread_server_qp, thread_server_io_chan, &thread_data_wr)) {
			rdma_error("[%d.%d] rdma_read_data failed\n", rdma_client_id, rdma_task_id);
			exit(EXIT_FAILURE);
		}
		disa_mutex->byte_data = thread_byte_data;
		disa_mutex->int_data = thread_int_data;

		DEBUG("[%d.%d] read data[%d] =  [%d], lock_idx: [%d]\n",
		rdma_client_id, rdma_task_id, curr_elem_offset,
		thread_int_data[curr_elem_offset - curr_byte_data_len/disa_mutex->elem_sz*curr_rlock_id], curr_rlock_id);
	}
}

static inline void rdma_write_data(disa_mutex_t* disa_mutex)
{
	if (curr_byte_offset >= 0) {
		thread_data_wr.wr.rdma.remote_addr = disa_mutex->data_addr;
		thread_data_wr.opcode = IBV_WR_RDMA_WRITE;
		thread_data_sge->addr = (uint64_t) disa_mutex->byte_data;
		thread_data_sge->length = curr_byte_data_len;
		DEBUG("[%d.%d] rdma_write_back post\n", rdma_client_id, rdma_task_id);
		if(perform_rdma_op(thread_server_qp, thread_server_io_chan, &thread_data_wr)) {
			rdma_error("[%d.%d] rdma_write_back failed\n", rdma_client_id, rdma_task_id);
			exit(EXIT_FAILURE);
		}
		DEBUG("[%d.%d] written data[%d] =  [%d], lock_idx: [%d]\n",
		rdma_client_id, rdma_task_id, curr_elem_offset,
		disa_mutex->int_data[curr_elem_offset - curr_byte_data_len/disa_mutex->elem_sz*curr_rlock_id], curr_rlock_id);
	}
}


static inline void rdma_release_rlock(disa_mutex_t *disa_mutex)
{
	thread_w_wr.opcode = IBV_WR_RDMA_WRITE;
	thread_w_wr.wr.rdma.remote_addr = disa_mutex->rlock_addr;
	DEBUG("[%d.%d] rdma_rel post\n", rdma_client_id, rdma_task_id);
	if(perform_rdma_op(thread_server_qp, thread_server_io_chan, &thread_w_wr)) {
		rdma_error("[%d.%d] rdma_release failed\n", rdma_client_id, rdma_task_id);
		exit(EXIT_FAILURE);
	}
	DEBUG("[%d.%d] released rlock [%d] on server\n", rdma_client_id, rdma_task_id, curr_rlock_id);
}

static inline void rdma_release_rlock_faa(disa_mutex_t *disa_mutex)
{
	thread_w_wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
	thread_w_wr.wr.atomic.remote_addr = disa_mutex->rlock_addr;
	DEBUG("[%d.%d] rdma_rel post\n", rdma_client_id, rdma_task_id);
	if(perform_rdma_op(thread_server_qp, thread_server_io_chan, &thread_w_wr)) {
		rdma_error("[%d.%d] rdma_release failed\n", rdma_client_id, rdma_task_id);
		exit(EXIT_FAILURE);
	}
	DEBUG("[%d.%d] released rlock [%d] on server\n", rdma_client_id, rdma_task_id, curr_rlock_id);
}

/************************************/
/*************SPINLOCK***************/
/************************************/
ull rdma_request_lock(disa_mutex_t* disa_mutex)
{
    DEBUG("[%d.%d] rdma_request_lock [%d]\n", rdma_client_id, rdma_task_id, disa_mutex->id);
	ull start = rdtscp();
	ull tries = 0;
	set_lock_meta(disa_mutex);
	thread_w_wr.wr.rdma.remote_addr = disa_mutex->rlock_addr;
	tries = rdma_cas(disa_mutex->rlock_addr, rlock_rkey, thread_server_qp, thread_server_io_chan);
	ull end_of_cas = rdtscp();

	rdma_read_data(disa_mutex);
	ull end_of_read = rdtscp();
	if(!*thread_task->stop) {
		thread_task->sgwait_acq[thread_task->idx] = end_of_cas - start;
		thread_task->sdata_read[thread_task->idx] = end_of_read - end_of_cas;
		thread_task->sglock_tries[thread_task->idx] = tries;
		thread_task->data_read[thread_task->run] += end_of_read - end_of_cas;
	}
	return tries;
}

int rdma_release_lock(disa_mutex_t *disa_mutex)
{
    DEBUG("[%d.%d] rdma_release_lock [%d]\n", rdma_client_id, rdma_task_id, curr_rlock_id);
	ull start = rdtscp();
	rdma_write_data(disa_mutex);
	ull end_of_data_write = rdtscp();
	rdma_release_rlock(disa_mutex);

	if(!*thread_task->stop) {
		thread_task->sgwait_rel[thread_task->idx] = rdtscp() - end_of_data_write;
		thread_task->sdata_write[thread_task->idx] = end_of_data_write - start;
		thread_task->data_write[thread_task->run] += end_of_data_write - start;
	}
	return 0;
}

/************************************/
/*************LEASE1**************/
/************************************/
ull rdma_request_lock_lease1(disa_mutex_t *disa_mutex)
{
    DEBUG("[%d.%d] rdma_request_lock_lease1 [%d]\n", rdma_client_id, rdma_task_id, disa_mutex->id);
	ull tries = 0;
	set_lock_meta(disa_mutex);
	ull start = rdtscp();
	tries = rdma_cas(disa_mutex->rlock_addr, rlock_rkey, thread_server_qp, thread_server_io_chan);
	ull end_of_cas = rdtscp();
	rdma_read_data(disa_mutex);
	ull end_of_read = rdtscp();

	if (!*thread_task->stop) {
		thread_task->sgwait_acq[thread_task->idx] = end_of_cas - start;
		thread_task->sdata_read[thread_task->idx] = end_of_read - end_of_cas;
		thread_task->sglock_tries[thread_task->idx] = tries;
		thread_task->data_read[thread_task->run] += end_of_read - end_of_cas;
	}
	return tries;
}

int rdma_release_lock_lease1(disa_mutex_t *disa_mutex)
{
    DEBUG("[%d.%d] rdma_release_lock [%d]\n", rdma_client_id, rdma_task_id, disa_mutex->id);
	ull start = rdtscp();
	set_lock_meta(disa_mutex);
	rdma_write_data(disa_mutex);
	ull end_of_data_write = rdtscp();
	rdma_release_rlock(disa_mutex);

	if (!*thread_task->stop) {
		thread_task->sgwait_rel[thread_task->idx] = rdtscp() - end_of_data_write;
		thread_task->sdata_write[thread_task->idx] = end_of_data_write - start;
		thread_task->data_write[thread_task->run] += end_of_data_write - start;
	}
	return 0;
}

int rdma_test_write() {
	rdma_cas(thread_peer_cas_addrs[1], thread_peer_cas_rkeys[1], thread_peer_qps[1], thread_peer_io_chans[1]);
	fprintf(stderr,"[%d] CASed @ %lu\n", rdma_client_id, thread_peer_cas_addrs[1]);
	fprintf(stderr, "[%d] thread_local_cas = %lu\n", rdma_client_id, *thread_cas_result);
	// fprintf(stderr, "[%d] CAS to [1] --> go check result\n", rdma_client_id);
	return 0;
}

#include <x86intrin.h>
int rdma_test_read() {
	do {
		_mm_clflush((void *)thread_cas_result);
		__sync_synchronize();
		fprintf(stderr, "[%d] thread_local_cas[0] addr: %lu\n", rdma_client_id, (uint64_t) thread_cas_result);
		fprintf(stderr, "[%d] local_cas[0] addr: %lu\n", rdma_client_id, thread_peer_cas_addrs[1]);
		fprintf(stderr, "[%d] read local_cas[0] = %lu\n", rdma_client_id, *thread_cas_result);
	} while(!*thread_cas_result);
	// uint64_t *cas_r = (uint64_t *) thread_peer_cas_addrs[1];
	// *cas_r += 1;
	// fprintf(stderr, "[%d] incremented local_cas[0] = %lu\n", rdma_client_id, *cas_r);
	// fprintf(stderr, "[%d] incremented thread_local_cas[0] = %lu\n", rdma_client_id, *thread_cas_result);
	return 0;
}
/************************************/
/*************XXX***************/
/************************************/

// int rdma_request_lock_x(disa_mutex_t *disa_mutex_t)
// {
//     DEBUG("[%d.%d] rdma_request_lock_x [%d]\n", rdma_client_id, rdma_task_id, disa_mutex->id);
// 	ull start = rdtscp();
// 	set_lock_meta(disa_mutex);

// }