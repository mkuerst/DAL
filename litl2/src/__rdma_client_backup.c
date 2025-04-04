#include "rdma_common.h"

/*******************************************************************/
/******************** EVENTS & CHANNELS ****************************/
/*******************************************************************/
static struct rdma_event_channel *cm_event_channel[THREADS_PER_CLIENT];
static struct rdma_cm_id *cm_client_id[THREADS_PER_CLIENT];
static struct rdma_cm_event *cm_event[THREADS_PER_CLIENT];
static struct ibv_pd *pd = NULL;

static struct ibv_comp_channel *io_completion_channel[THREADS_PER_CLIENT];
static struct ibv_wc wc[THREADS_PER_CLIENT];

static struct ibv_cq *client_cq[THREADS_PER_CLIENT];
static struct ibv_qp *client_qp[THREADS_PER_CLIENT];

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

/*******************************************************************/
/******************** THREAD LOCAL ************************************/
/*******************************************************************/
__thread int rdma_task_id, curr_rlock_id,
curr_byte_offset, curr_byte_data_len, curr_elem_offset;

__thread struct ibv_qp *thread_qp;
__thread struct ibv_wc thread_wc;
__thread struct ibv_comp_channel *thread_io_comp_chan;
__thread struct ibv_send_wr thread_cas_wr, *thread_bad_wr, thread_w_wr, thread_data_wr;
__thread struct ibv_sge *thread_cas_sge, *thread_w_sge, *thread_data_sge;

__thread task_t *thread_task;
__thread uint64_t *thread_cas_result;
__thread char* thread_byte_data;
__thread int* thread_int_data;

void set_rdma_client_meta(task_t* task, rdma_client_meta* client_meta, int cid, int tid)
{
	rdma_task_id = tid;
	rdma_client_id = cid;
	thread_qp = client_meta->qp[tid];
	thread_cas_wr = client_meta->cas_wr[tid];
	thread_cas_sge = &client_meta->cas_sge[tid];
	thread_w_wr = client_meta->w_wr[tid];
	thread_w_sge = &client_meta->w_sge[tid];
	thread_data_wr = client_meta->data_wr[tid];
	thread_data_sge = &client_meta->data_sge[tid];
	thread_bad_wr = client_meta->bad_wr[tid];
	thread_io_comp_chan = client_meta->io_comp_chan[tid];
	thread_wc = client_meta->wc[tid];
	thread_task = task;

	thread_byte_data = client_meta->data[tid];
	thread_int_data = (int *) client_meta->data[tid];
	thread_cas_result = client_meta->cas_result[tid];

	rlock_addr = client_meta->rlock_addr;
	rlock_rkey = client_meta->rlock_rkey;
	data_addr = client_meta->data_addr;
	data_rkey = client_meta->data_rkey;
	unlock_val = client_meta->unlock_val;
}

void *create_rdma_client_meta(int cid, int nthreads, int nlocks) {
	rdma_client_meta* client_meta = (rdma_client_meta *) malloc(sizeof(rdma_client_meta));
	for (int i = 0; i < nthreads; i++) {
		cas_sge[i].addr   = (uintptr_t) local_cas_mr[i]->addr;
		cas_sge[i].length = sizeof(uint64_t);
		cas_sge[i].lkey   = local_cas_mr[i]->lkey;

		w_sge[i].addr   = (uintptr_t) local_unlock_mr->addr;
		w_sge[i].length = sizeof(uint64_t);
		w_sge[i].lkey   = local_unlock_mr->lkey;

		data_sge[i].addr   = (uintptr_t) local_data_mr[i]->addr;
		data_sge[i].length = MAX_ARRAY_SIZE;
		data_sge[i].lkey   = local_data_mr[i]->lkey;

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
		w_wr[i].opcode         = IBV_WR_RDMA_WRITE;
		w_wr[i].send_flags     = IBV_SEND_SIGNALED;
		w_wr[i].wr.atomic.remote_addr = rlock_addr;
		w_wr[i].wr.atomic.rkey        = rlock_rkey;
		w_wr[i].wr.rdma.remote_addr = rlock_addr;
		w_wr[i].wr.rdma.rkey        = rlock_rkey;

		data_wr[i].wr_id          = i;
		data_wr[i].sg_list        = &data_sge[i];
		data_wr[i].num_sge        = 1;
		data_wr[i].opcode         = IBV_WR_RDMA_READ;
		data_wr[i].send_flags     = IBV_SEND_SIGNALED;
		data_wr[i].wr.rdma.remote_addr = data_addr;
		data_wr[i].wr.rdma.rkey        = data_rkey;

		client_meta->cas_result[i] = cas_result[i];
		client_meta->data[i] = data[i];
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
	return client_meta;
}

int read_from_metadata_file()
{
	char *cwd = NULL;
	cwd = getcwd(cwd, 128);
	char *filename = "/metadata/addrs";
	strcat(cwd, filename);
	FILE *file = fopen(cwd, "r");
	if (!file) {
		DEBUG("Failed at opening metadata file %s\nTrying again with different dir\n", cwd);
		cwd = getcwd(cwd, 128);
		char *filename = "/microbench/metadata/addrs";
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
	fprintf(stderr, "remote data_addr: %lu, key: %u\n", data_addr, data_rkey);
	fprintf(stderr, "rlock_addr: %lu, key: %u\n", rlock_addr, rlock_rkey);
	return 0;

}

int client_prepare_connection(struct sockaddr_in *s_addr, int nthreads)
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


void* client_connect_to_server(int cid, int nthreads, int nlocks, int use_nodes) 
{
	struct rdma_conn_param conn_param;
	bzero(&conn_param, sizeof(conn_param));
	conn_param.initiator_depth = 16;
	conn_param.responder_resources = 16;
	conn_param.retry_count = 7;

	// cas_result = aligned_alloc(sizeof(uint64_t), nlocks * sizeof(uint64_t));
	// memset(cas_result, 0, nlocks * sizeof(uint64_t));
	// data = (char *) aligned_alloc(sizeof(uint64_t), MAX_ARRAY_SIZE);
	// memset(data, 0, MAX_ARRAY_SIZE);

	int node = 0;
	for (int i = 0; i < nthreads; i++) {
		if (use_nodes == 2) {
			node = i < nthreads / 2 ? 0 : 1;
		}
		cas_result[i] = numa_alloc_onnode(sizeof(uint64_t), node);
		*cas_result[i] = 0;
		data[i] = numa_alloc_onnode(MAX_ARRAY_SIZE, node);
		if (!data[i]) {
			rdma_error("Client %d failed to allocate data memory for thread %d\n", cid, i);
			return NULL;
		}

		local_cas_mr[i] = rdma_buffer_register(pd,
				cas_result[i],
				sizeof(uint64_t),
				(IBV_ACCESS_LOCAL_WRITE|
				IBV_ACCESS_REMOTE_READ|
				IBV_ACCESS_REMOTE_WRITE|
				IBV_ACCESS_REMOTE_ATOMIC));
		if(!local_cas_mr[i]){
			rdma_error("Client %d failed to register local_cas_mr, -errno = %d \n", cid, -errno);
			return NULL;
		}
		local_data_mr[i] = rdma_buffer_register(pd,
				data[i],
				MAX_ARRAY_SIZE,
				(IBV_ACCESS_LOCAL_WRITE|
				IBV_ACCESS_REMOTE_READ|
				IBV_ACCESS_REMOTE_WRITE));
		if(!local_data_mr[i]){
			rdma_error("Client %d failed to register local_data_mr, -errno = %d \n", cid, -errno);
			return NULL;
		}
	}
	DEBUG("SUCCESS AT ALLOCATING PER THREAD DATA\n");

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

	for (int i = 0; i < nthreads; i++) {
		// struct rdma_cm_event *cm_event = NULL;
		cm_event[i] = NULL;
		conn_param.private_data = &i;
		conn_param.private_data_len = sizeof(i);
		cm_client_id[i]->context = (void *)(long) i;
		DEBUG("[%d.%d] tries to connect\n", cid, i);
		if (rdma_connect(cm_client_id[i], &conn_param)) {
			rdma_error("Client %d failed to connect to remote host , errno: %d\n", cid, -errno);
			return NULL;
		}
		if (process_rdma_cm_event(cm_event_channel[i], RDMA_CM_EVENT_ESTABLISHED, &cm_event[i])) {
			rdma_error("Client %d failed to get cm event, -errno = %d \n", cid, -errno);
			return NULL;
		}
		if (rdma_ack_cm_event(cm_event[i])) {
			rdma_error("Client %d failed to acknowledge cm event, errno: %d\n", cid, -errno);
			return NULL;
		}
	}

	read_from_metadata_file();
	fprintf(stderr, "Client [%d] connected to RDMA server\n", cid);
	return create_rdma_client_meta(cid, nthreads, nlocks);
}

void* establish_rdma_connection(int cid, char* addr, int nthreads, int nlocks, int use_nodes)
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
	if (!(client_meta = client_connect_to_server(cid, nthreads, nlocks, use_nodes))) { 
		rdma_error("Failed to setup client connection , -errno = %d \n", -errno);
		return NULL;
	}
	return client_meta;
}

int perform_rdma_op(struct ibv_send_wr *wr)
{
	if (ibv_post_send(thread_qp, wr, &thread_bad_wr)) {
		rdma_error("[%d.%d] Failed to post wr to\nremote_addr: %lu rkey: %u, -errno %d\n",
		rdma_client_id, rdma_task_id, wr->wr.rdma.remote_addr, wr->wr.rdma.rkey, -errno);
		rdma_error("atomic remote_addr: %lu rkey: %u, -errno %d\n",
		wr->wr.atomic.remote_addr, wr->wr.atomic.rkey, -errno);
		return -1;
	}
	if (process_work_completion_events(thread_io_comp_chan, &thread_wc, 1) != 1) {
		rdma_error("[%d.%d] Failed to poll wr completion, -errno %d\n",
		rdma_client_id, rdma_task_id, -errno);
		return -1;
	}
	return 0;
}

/************************************/
/*************SPINLOCK***************/
/************************************/
// TODO: Specify size of one data elem
ull rdma_request_lock(disa_mutex_t* disa_mutex)
{
    DEBUG("[%d.%d] rdma_request_lock [%d]\n", rdma_client_id, rdma_task_id, disa_mutex->id);
	ull start = rdtscp();
	int tries = 0;
	curr_rlock_id = disa_mutex->id;
	curr_byte_offset = disa_mutex->offset;
	curr_byte_data_len = disa_mutex->data_len;
	// curr_cas_result = disa_mutex->cas_result;
	curr_elem_offset = curr_byte_offset / disa_mutex->elem_sz;

	thread_cas_wr.wr.atomic.remote_addr = disa_mutex->rlock_addr;
	thread_w_wr.wr.rdma.remote_addr = disa_mutex->rlock_addr;
	// thread_cas_sge->addr = (uintptr_t) curr_cas_result;

    unsigned int delay = BO;
    unsigned int i;
	do {
		tries++;
		if(perform_rdma_op(&thread_cas_wr)) {
			rdma_error("[%d.%d] rdma_cas failed\n", rdma_client_id, rdma_task_id);
			exit(EXIT_FAILURE);
		}
		for (i = 0; i < delay; i++)
			CPU_PAUSE();

		if (delay < MAX_BO)
			delay *= 2;

	} while(*thread_cas_result);
	DEBUG("[%d.%d] got rlock [%d] from server after %d tries\n",
	rdma_client_id, rdma_task_id, curr_rlock_id, tries);
	ull end_of_cas = rdtscp();
	thread_task->sgwait_acq[thread_task->idx] = end_of_cas - start;

	if (curr_byte_offset >= 0) {
		// thread_data_wr.wr.rdma.remote_addr = data_addr + curr_byte_offset;
		thread_data_wr.wr.rdma.remote_addr = disa_mutex->data_addr;
		// thread_data_sge->addr = (uintptr_t) (data + curr_byte_offset);
		thread_data_sge->length = curr_byte_data_len;
		thread_data_wr.opcode = IBV_WR_RDMA_READ;
		if(perform_rdma_op(&thread_data_wr)) {
			rdma_error("[%d.%d] rdma_read_data failed\n", rdma_client_id, rdma_task_id);
			exit(EXIT_FAILURE);
		}
		disa_mutex->byte_data = thread_byte_data;
		disa_mutex->int_data = thread_int_data;

		DEBUG("[%d.%d] read data[%d] =  [%d], lock_idx: [%d]\n",
		rdma_client_id, rdma_task_id, curr_elem_offset,
		thread_int_data[curr_elem_offset - curr_byte_data_len/disa_mutex->elem_sz*curr_rlock_id], curr_rlock_id);
	}
	ull end_of_read = rdtscp();
	thread_task->sdata_read[thread_task->idx] = end_of_read - end_of_cas;
	thread_task->sglock_tries[thread_task->idx] = tries;
	thread_task->data_read[thread_task->run] += end_of_read - end_of_cas;
	return tries;
}

int rdma_release_lock(disa_mutex_t *disa_mutex)
{
    DEBUG("[%d.%d] rdma_release_lock [%d]\n", rdma_client_id, rdma_task_id, curr_rlock_id);
	ull start = rdtscp();
	if (curr_byte_offset >= 0) {
		thread_data_wr.wr.rdma.remote_addr = disa_mutex->data_addr;
		thread_data_wr.opcode = IBV_WR_RDMA_WRITE;
		if(perform_rdma_op(&thread_data_wr)) {
			rdma_error("[%d.%d] rdma_write_back failed\n", rdma_client_id, rdma_task_id);
			exit(EXIT_FAILURE);
		}
		DEBUG("[%d.%d] written data[%d] =  [%d], lock_idx: [%d]\n",
		rdma_client_id, rdma_task_id, curr_elem_offset,
		thread_int_data[curr_elem_offset - curr_byte_data_len/disa_mutex->elem_sz*curr_rlock_id], curr_rlock_id);
	}
	ull end_of_data_write = rdtscp();
	if(perform_rdma_op(&thread_w_wr)) {
		rdma_error("[%d.%d] rdma_release failed\n", rdma_client_id, rdma_task_id);
		exit(EXIT_FAILURE);
	}
	thread_task->sgwait_rel[thread_task->idx] = rdtscp() - end_of_data_write;
	thread_task->sdata_write[thread_task->idx] = end_of_data_write - start;
	thread_task->data_write[thread_task->run] += end_of_data_write - start;
	DEBUG("[%d.%d] released rlock [%d] on server\n", rdma_client_id, rdma_task_id, curr_rlock_id);
	return 0;
}

// TODO: No awareness of other remote clients
/************************************/
/*************LEASE1**************/
/************************************/
ull rdma_request_lock_lease1(disa_mutex_t *disa_mutex)
{
    DEBUG("[%d.%d] rdma_request_lock_lease1 [%d]\n", rdma_client_id, rdma_task_id, disa_mutex->id);
	ull start = rdtscp();

	int tries = 0;
	curr_rlock_id = disa_mutex->id;
	curr_byte_offset = disa_mutex->offset;
	curr_byte_data_len = disa_mutex->data_len;
	curr_elem_offset = curr_byte_offset / disa_mutex->elem_sz;

	thread_cas_wr.wr.atomic.remote_addr = disa_mutex->rlock_addr;

    unsigned int delay = BO;
    unsigned int i;
	do {
		tries++;
		if(perform_rdma_op(&thread_cas_wr)) {
			rdma_error("[%d.%d] rdma_cas failed\n", rdma_client_id, rdma_task_id);
			exit(EXIT_FAILURE);
		}
		for (i = 0; i < delay; i++)
			CPU_PAUSE();

		if (delay < MAX_BO)
			delay *= 2;
	} while(*thread_cas_result);

	DEBUG("[%d.%d] got rlock [%d] from server after %d tries\n",
	rdma_client_id, rdma_task_id, curr_rlock_id, tries);
	ull end_of_cas = rdtscp();
	thread_task->sgwait_acq[thread_task->idx] = end_of_cas - start;

	if (curr_byte_offset >= 0) {
		thread_data_wr.wr.rdma.remote_addr = disa_mutex->data_addr;
		thread_data_sge->length = curr_byte_data_len;
		thread_data_wr.opcode = IBV_WR_RDMA_READ;
		if(perform_rdma_op(&thread_data_wr)) {
			rdma_error("[%d.%d] rdma_read_data failed\n", rdma_client_id, rdma_task_id);
			exit(EXIT_FAILURE);
		}
		disa_mutex->byte_data = thread_byte_data;
		disa_mutex->int_data = thread_int_data;

		DEBUG("[%d.%d] read data[%d] =  [%d], lock_idx: [%d]\n",
		rdma_client_id, rdma_task_id, curr_elem_offset,
		thread_int_data[curr_elem_offset - curr_byte_data_len/disa_mutex->elem_sz*curr_rlock_id], curr_rlock_id);
	}
	ull end_of_read = rdtscp();
	thread_task->sdata_read[thread_task->idx] = end_of_read - end_of_cas;
	thread_task->sglock_tries[thread_task->idx] = tries;
	thread_task->data_read[thread_task->run] += end_of_read - end_of_cas;
	return tries;
}

int rdma_release_lock_lease1(disa_mutex_t *disa_mutex)
{
    DEBUG("[%d.%d] rdma_release_lock [%d]\n", rdma_client_id, rdma_task_id, disa_mutex->id);
	ull start = rdtscp();
	curr_rlock_id = disa_mutex->id;
	curr_byte_offset = disa_mutex->offset;
	curr_byte_data_len = disa_mutex->data_len;
	// curr_cas_result = disa_mutex->cas_result;
	curr_elem_offset = curr_byte_offset / disa_mutex->elem_sz;

	if (curr_byte_offset >= 0) {
		thread_data_wr.wr.rdma.remote_addr = disa_mutex->data_addr;
		thread_data_wr.opcode = IBV_WR_RDMA_WRITE;
		// thread_data_sge->addr = (uintptr_t) (data + curr_byte_offset);
		thread_data_sge->length = curr_byte_data_len;
		if(perform_rdma_op(&thread_data_wr)) {
			rdma_error("[%d.%d] rdma_write_back failed\n", rdma_client_id, rdma_task_id);
			exit(EXIT_FAILURE);
		}
		DEBUG("[%d.%d] written data[%d] =  [%d], lock_idx: [%d]\n",
		rdma_client_id, rdma_task_id, curr_elem_offset,
		thread_int_data[curr_elem_offset - curr_byte_data_len/disa_mutex->elem_sz*curr_rlock_id], curr_rlock_id);
	}

	ull end_of_data_write = rdtscp();
	thread_w_wr.wr.rdma.remote_addr = disa_mutex->rlock_addr;
	if(perform_rdma_op(&thread_w_wr)) {
		rdma_error("[%d.%d] rdma_release failed\n", rdma_client_id, rdma_task_id);
		exit(EXIT_FAILURE);
	}

	thread_task->sgwait_rel[thread_task->idx] = rdtscp() - end_of_data_write;
	thread_task->sdata_write[thread_task->idx] = end_of_data_write - start;
	thread_task->data_write[thread_task->run] += end_of_data_write - start;
	DEBUG("[%d.%d] released rlock [%d] on server\n", rdma_client_id, rdma_task_id, curr_rlock_id);
	return 0;
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