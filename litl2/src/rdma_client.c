/*
 * An example RDMA client side code. 
 * Author: Animesh Trivedi 
 *         atrivedi@apache.org
 */

#include "rdma_common.h"

static struct rdma_event_channel *cm_event_channel = NULL;
static struct rdma_cm_id *cm_client_id = NULL;
static struct ibv_pd *pd = NULL;
static struct ibv_comp_channel *io_completion_channel = NULL;
static struct ibv_cq *client_cq = NULL;
static struct ibv_qp_init_attr qp_init_attr;
static struct ibv_qp *client_qp;
static struct ibv_wc *wc;
static struct ibv_mr 
			// *client_metadata_mr = NULL, 
		     *client_src_mr = NULL, 
		    // *client_dst_mr = NULL, 
		     *server_metadata_mr = NULL;
// static struct rdma_buffer_attr 
// client_metadata_attr, 
// server_metadata_attr;
static struct ibv_send_wr client_send_wr, *bad_client_send_wr = NULL;
static struct ibv_recv_wr server_recv_wr, *bad_server_recv_wr = NULL;
static struct ibv_sge 
// client_send_sge, 
server_recv_sge;

struct ibv_send_wr cas_wr, *bad_wr;
struct ibv_sge cas_sge;
uint64_t *cas_result;


int id;
static char buffer_msg[MESSAGE_SIZE] = {0};
static char server_metadata[META_SIZE] = {0};

static uint64_t rlock_addr;
static uint32_t rkey;

void client_prep_cas(rlock_meta* rlock) {
	client_qp = rlock->qp;
	cas_wr = rlock->cas_wr;
	bad_wr = rlock->bad_wr;
	io_completion_channel = rlock->io_comp_chan;
	wc = rlock->wc;
	cas_result = rlock->cas_result;
}

int client_prepare_connection(struct sockaddr_in *s_addr)
{
	int ret = -1;
	struct rdma_cm_event *cm_event = NULL;
	cm_event_channel = rdma_create_event_channel();
	if (!cm_event_channel) {
		rdma_error("Creating cm event channel failed, errno: %d \n", -errno);
		return -errno;
	}
	if ((ret = rdma_create_id(cm_event_channel, &cm_client_id, NULL, RDMA_PS_TCP))) {
		rdma_error("Creating cm id failed with errno: %d \n", -errno); 
		return -errno;
	}
	if ((ret = rdma_resolve_addr(cm_client_id, NULL, (struct sockaddr*) s_addr, 2000))) {
		rdma_error("Failed to resolve address, errno: %d \n", -errno);
		return -errno;
	}
	if ((ret  = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ADDR_RESOLVED, &cm_event))) {
		rdma_error("Failed to receive a valid event, ret = %d \n", ret);
		return ret;
	}
	if ((ret = rdma_ack_cm_event(cm_event))) {
		rdma_error("Failed to acknowledge the CM event, errno: %d\n", -errno);
		return -errno;
	}
	if ((ret = rdma_resolve_route(cm_client_id, 2000))) {
		rdma_error("Failed to resolve route, erno: %d \n", -errno);
	       return -errno;
	}
	if ((ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ROUTE_RESOLVED, &cm_event))) {
		rdma_error("Failed to receive a valid event, ret = %d \n", ret);
		return ret;
	}
	if ((ret = rdma_ack_cm_event(cm_event))) {
		rdma_error("Failed to acknowledge the CM event, errno: %d \n", -errno);
		return -errno;
	}
	pd = ibv_alloc_pd(cm_client_id->verbs);
	if (!pd) {
		rdma_error("Failed to alloc pd, errno: %d \n", -errno);
		return -errno;
	}
	io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
	if (!io_completion_channel) {
		rdma_error("Failed to create IO completion event channel, errno: %d\n", -errno);
		return -errno;
	}
	client_cq = ibv_create_cq(cm_client_id->verbs, CQ_CAPACITY, NULL, io_completion_channel, 0);
	if (!client_cq) {
		rdma_error("Failed to create CQ, errno: %d \n", -errno);
		return -errno;
	}
	if ((ret = ibv_req_notify_cq(client_cq, 0))) {
		rdma_error("Failed to request notifications, errno: %d\n", -errno);
		return -errno;
	}

	bzero(&qp_init_attr, sizeof qp_init_attr);
	qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
	qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
	qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
	qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
	qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
	qp_init_attr.recv_cq = client_cq; /* Where should I notify for receive completion operations */
	qp_init_attr.send_cq = client_cq; /* Where should I notify for send completion operations */

	if (rdma_create_qp(cm_client_id, pd, &qp_init_attr)) {
		rdma_error("Failed to create QP, errno: %d \n", -errno);
		return -errno;
	}
	client_qp = cm_client_id->qp;
	return 0;
}

int client_prep_buffers(int tid)
{
	server_metadata_mr = rdma_buffer_register(pd, server_metadata, META_SIZE, (IBV_ACCESS_LOCAL_WRITE));
	if(!server_metadata_mr){
		rdma_error("Failed to setup the server metadata mr , -ENOMEM\n");
		return -ENOMEM;
	}
	server_recv_sge.addr = (uint64_t) server_metadata_mr->addr;
	server_recv_sge.length = (uint32_t) server_metadata_mr->length;
	server_recv_sge.lkey = (uint32_t) server_metadata_mr->lkey;
	bzero(&server_recv_wr, sizeof(server_recv_wr));
	server_recv_wr.sg_list = &server_recv_sge;
	server_recv_wr.num_sge = 1;
	// TODO: IS THIS PREPOST NEEDED?
	if (ibv_post_recv(client_qp, &server_recv_wr, &bad_server_recv_wr)) {
		rdma_error("Failed to pre-post the receive buffer, errno: %d \n", -errno);
		return -errno;
	}
	// cas_result = malloc(sizeof(uint64_t));
	// client_src_mr = rdma_buffer_register(pd,
	// 		cas_result,
	// 		sizeof(uint64_t),
	// 		(IBV_ACCESS_LOCAL_WRITE|
	// 		 IBV_ACCESS_REMOTE_READ|
	// 		 IBV_ACCESS_REMOTE_WRITE));
	// if(!client_src_mr){
	// 	rdma_error("Task %d failed to register the client_src_mr buffer, -errno = %d \n", id, -errno);
	// 	return -errno;
	// }
	// client_metadata_attr.address = (uint64_t) client_src_mr->addr; 
	// client_metadata_attr.length = client_src_mr->length; 
	// client_metadata_attr.stag.local_stag = client_src_mr->lkey;
	// client_metadata_mr = rdma_buffer_register(pd,
	// 		&client_metadata_attr,
	// 		sizeof(client_metadata_attr),
	// 		IBV_ACCESS_LOCAL_WRITE);
	// if(!client_metadata_mr) {
	// 	rdma_error("Task %d failed to register the client metadata buffer, ret = %d \n", id, ret);
	// 	return ret;
	// }
	// client_send_sge.addr = (uint64_t) client_src_mr->addr;
	// client_send_sge.length = (uint32_t) client_src_mr->length;
	// client_send_sge.lkey = client_src_mr->lkey;
	// bzero(&client_send_wr, sizeof(client_send_wr));
	// client_send_wr.sg_list = &client_send_sge;
	// client_send_wr.num_sge = 1;
	// client_send_wr.opcode = IBV_WR_SEND;
	// client_send_wr.send_flags = IBV_SEND_SIGNALED;
	return 0;
}

void* client_connect_to_server(int tid) 
{
	struct rdma_conn_param conn_param;
	struct rdma_cm_event *cm_event = NULL;
	bzero(&conn_param, sizeof(conn_param));
	conn_param.initiator_depth = 3;
	conn_param.responder_resources = 3;
	conn_param.retry_count = 3;
	conn_param.private_data = &tid;
	conn_param.private_data_len = sizeof(tid);
	cm_client_id->context = (void *)(long) tid;
	if (rdma_connect(cm_client_id, &conn_param)) {
		rdma_error("Task %d failed to connect to remote host , errno: %d\n", tid, -errno);
		return NULL;
	}
	if (process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ESTABLISHED,&cm_event)) {
		rdma_error("Task %d failed to get cm event, -errno = %d \n", tid, -errno);
	       return NULL;
	}
	if (rdma_ack_cm_event(cm_event)) {
		rdma_error("Task %d failed to acknowledge cm event, errno: %d\n", tid, -errno);
		return NULL;
	}

	// if (ibv_post_recv(client_qp, &server_recv_wr, &bad_server_recv_wr)) {
	// 	rdma_error("Task %d id failed to pre-post the receive buffer for mr metadata, errno: %d \n", id, -errno);
	// 	return -errno;
	// }
	// Receive the server metadata from the prepost in client_prep_buffers
	wc = malloc(sizeof(struct ibv_wc));
	if(process_work_completion_events(io_completion_channel, wc, 1) != 1) {
		rdma_error("Failed to get mr metadata, -errno = %d", -errno);
		return NULL;
	}

	char *buffer = (char *)server_metadata_mr->addr;
	DEBUG("Received data: %s\n", buffer);

	if (sscanf(buffer, "remote_addr: %lu\nrkey: %u\n", &rlock_addr, &rkey) == 2) {
		DEBUG("Received remote_addr: %lu | rkey: %u", rlock_addr, rkey);
	}
	else {
		rdma_error("Failed to receive/parse remote_addr and rkey: got %s\n", buffer);
		return NULL;
	}

	cas_result = aligned_alloc(sizeof(uint64_t), sizeof(uint64_t));
	*cas_result = 0;
	memset(&cas_wr, 0, sizeof(cas_wr));
	client_src_mr = rdma_buffer_register(pd,
			cas_result,
			sizeof(uint64_t),
			(IBV_ACCESS_LOCAL_WRITE|
			 IBV_ACCESS_REMOTE_READ|
			 IBV_ACCESS_REMOTE_WRITE|
			 IBV_ACCESS_REMOTE_ATOMIC));
	if(!client_src_mr){
		rdma_error("Task %d failed to register the client_src_mr buffer, -errno = %d \n", id, -errno);
		return NULL;
	}
	cas_sge.addr   = (uintptr_t)client_src_mr->addr;
	cas_sge.length = sizeof(uint64_t);
	cas_sge.lkey   = client_src_mr->lkey;

	cas_wr.wr_id          = 0;
	cas_wr.sg_list        = &cas_sge;
	cas_wr.num_sge        = 1;
	cas_wr.opcode         = IBV_WR_ATOMIC_CMP_AND_SWP;
	cas_wr.send_flags     = IBV_SEND_SIGNALED;
	cas_wr.wr.atomic.remote_addr = rlock_addr;
	cas_wr.wr.atomic.rkey        = rkey;
	cas_wr.wr.atomic.compare_add = 0;
	cas_wr.wr.atomic.swap        = 1;

	rlock_meta* rlock = (rlock_meta *) malloc(sizeof(rlock_meta));
	rlock->rlock_addr = rlock_addr;
	rlock->rkey = rkey;
	rlock->qp = client_qp;
	rlock->cas_wr = cas_wr;
	rlock->io_comp_chan = io_completion_channel;
	rlock->wc = wc;
	rlock->bad_wr = bad_client_send_wr;
	rlock->cas_result = cas_result;

	fprintf(stderr, "Task %d connected to RDMA server\n", tid);
	do {
		if (ibv_post_send(client_qp, &cas_wr, &bad_wr)) {
			rdma_error("Failed to post CAS wr to remote_addr: 0x%lx rkey: %u, -errno %d\n", cas_wr.wr.atomic.remote_addr,cas_wr.wr.atomic.rkey, -errno);
			fprintf(stderr, "Failed to post CAS WR, errno = %d\n", errno);
			if (bad_wr) {
				fprintf(stderr, "Bad WR details:\n");
				fprintf(stderr, "  Opcode: %d\n", bad_wr->opcode);
				fprintf(stderr, "  WR ID: %lu\n", bad_wr->wr_id);
				if (bad_wr->sg_list) {
					fprintf(stderr, "  SGE Address: 0x%lx, Length: %u\n", bad_wr->sg_list->addr, bad_wr->sg_list->length);
				}
				fprintf(stderr, "  Number of SGEs: %d\n", bad_wr->num_sge);
				fprintf(stderr, "  Send Flags: %x\n", bad_wr->send_flags);
			}
			if (wc->status != IBV_WC_SUCCESS) {
				fprintf(stderr, "CAS operation failed with status: %d\n", wc->status);
			}
			// exit(EXIT_FAILURE);
		}

		if (process_work_completion_events(io_completion_channel, wc, 1)) {
			rdma_error("Failed to poll CAS completion, -errno %d\n", -errno);
			return NULL;
		}
	} while(*cas_result);
	return rlock;
}

int rdma_request_lock()
{
	// sprintf(buffer_msg, "l%d", id);
	// if (ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr)) {
	// 	rdma_error("Task %d failed to send client metadata, errno: %d \n", id, -errno);
	// 	return -errno;
	// }
	// // ret = process_work_completion_events(io_completion_channel, wc, 1);
	// // if(ret != 1) {
	// // 	rdma_error("Task %d failed to get 2 work completions for lock request, ret = %d \n", id, ret);
	// // 	return ret;
	// // }
	// if (ibv_post_recv(client_qp, &server_recv_wr, &bad_server_recv_wr)) {
	// 	rdma_error("Task %d id failed to post the receive buffer, errno: %d \n", id, -errno);
	// 	return -errno;
	// }
	// if(process_work_completion_events(io_completion_channel, wc, 2) != 2) {
	// 	rdma_error("Task %d failed to get 2 work completions for lock request, ret = %d \n", id, -errno);
	// 	return -errno;
	// }

	// do {
	// 	if (ibv_post_send(client_qp, &cas_wr, &bad_wr)) {
	// 		rdma_error("Failed to post CAS wr to remote_addr: 0x%lx rkey: %u, -errno %d\n", cas_wr.wr.atomic.remote_addr,cas_wr.wr.atomic.rkey, -errno);
	// 		fprintf(stderr, "Failed to post CAS WR, errno = %d\n", errno);
	// 		if (bad_wr) {
	// 			fprintf(stderr, "Bad WR details:\n");
	// 			fprintf(stderr, "  Opcode: %d\n", bad_wr->opcode);
	// 			fprintf(stderr, "  WR ID: %lu\n", bad_wr->wr_id);
	// 			if (bad_wr->sg_list) {
	// 				fprintf(stderr, "  SGE Address: 0x%lx, Length: %u\n", bad_wr->sg_list->addr, bad_wr->sg_list->length);
	// 			}
	// 			fprintf(stderr, "  Number of SGEs: %d\n", bad_wr->num_sge);
	// 			fprintf(stderr, "  Send Flags: %x\n", bad_wr->send_flags);
	// 		}
	// 		if (wc->status != IBV_WC_SUCCESS) {
	// 			fprintf(stderr, "CAS operation failed with status: %d\n", wc->status);
	// 		}
	// 		// exit(EXIT_FAILURE);
	// 	}

	// 	if (process_work_completion_events(io_completion_channel, wc, 1)) {
	// 		rdma_error("Failed to poll CAS completion, -errno %d\n", -errno);
	// 		return -errno;
	// 	}
	// } while(*cas_result);

	debug("Task %d got rlock from server\n", id);
	return 0;
}

int rdma_release_lock()
{
	// sprintf(buffer_msg, "r%d", id);
	// if (ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr)) {
	// 	rdma_error("Task %d failed to post send release lock, -errno: %d \n", id, -errno);
	// 	return -errno;
	// }
	// // if(process_work_completion_events(io_completion_channel, wc, 1)) {
	// // 	rdma_error("Task %d failed to get 2 work completions for lock request, -errno = %d \n", id, -errno);
	// // 	return -errno;
	// // }
	// if (ibv_post_recv(client_qp, &server_recv_wr, &bad_server_recv_wr)) {
	// 	rdma_error("Task %d id failed post release lock, -errno: %d \n", id, -errno);
	// 	return -errno;
	// }
	// if(process_work_completion_events(io_completion_channel, wc, 2) != 2) {
	// 	rdma_error("Task %d failed to get 2 work completions for lock request, -errno = %d \n", id, -errno);
	// 	return -errno;
	// }
	// debug("Task %d released rlock on server\n", id);
	return 0;
}

void* establish_rdma_connection(int tid, char* addr)
{
	struct sockaddr_in server_sockaddr;
	rlock_meta* rlock;
	id = tid;
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
	if (client_prepare_connection(&server_sockaddr)) { 
		rdma_error("Failed to setup client connection , -errno = %d \n", -errno);
		return NULL;
	}
	if (client_prep_buffers(tid)) { 
		rdma_error("Failed to setup client connection , -errno = %d \n", -errno);
		return NULL;
	}
	if (!(rlock = client_connect_to_server(tid))) { 
		rdma_error("Failed to setup client connection , -errno = %d \n", -errno);
		return NULL;
	}
	return rlock;
}
