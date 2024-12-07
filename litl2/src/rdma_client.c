/*
 * An example RDMA client side code. 
 * Author: Animesh Trivedi 
 *         atrivedi@apache.org
 */

#include "rdma_common.h"

/* These are basic RDMA resources */
/* These are RDMA connection related resources */
static struct rdma_event_channel *cm_event_channel = NULL;
static struct rdma_cm_id *cm_client_id = NULL;
static struct ibv_pd *pd = NULL;
static struct ibv_comp_channel *io_completion_channel = NULL;
static struct ibv_cq *client_cq = NULL;
static struct ibv_qp_init_attr qp_init_attr;
static struct ibv_qp *client_qp;
static struct ibv_wc wc[2];
/* These are memory buffers related resources */
static struct ibv_mr *client_metadata_mr = NULL, 
		     *client_src_mr = NULL, 
		     *client_dst_mr = NULL, 
		     *server_metadata_mr = NULL;
static struct rdma_buffer_attr client_metadata_attr, server_metadata_attr;
static struct ibv_send_wr client_send_wr, *bad_client_send_wr = NULL;
static struct ibv_recv_wr server_recv_wr, *bad_server_recv_wr = NULL;
static struct ibv_sge client_send_sge, server_recv_sge;
/* Source and Destination buffers, where RDMA operations source and sink */
int id;
char buffer_msg[MESSAGE_SIZE] = {0};


static int client_prepare_connection(struct sockaddr_in *s_addr)
{
	struct rdma_cm_event *cm_event = NULL;
	int ret = -1;
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
	client_cq = ibv_create_cq(cm_client_id->verbs /* which device*/, 
			CQ_CAPACITY /* maximum capacity*/, 
			NULL /* user context, not used here */,
			io_completion_channel /* which IO completion channel */, 
			0 /* signaling vector, not used here*/);
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
	if ((ret = rdma_create_qp(cm_client_id, pd, &qp_init_attr))) {
		rdma_error("Failed to create QP, errno: %d \n", -errno);
		return -errno;
	}
	client_qp = cm_client_id->qp;
	return 0;
}

static int client_prep_buffers(int tid)
{
	int ret = -1;
	server_metadata_mr = rdma_buffer_register(pd, &server_metadata_attr, sizeof(server_metadata_attr), (IBV_ACCESS_LOCAL_WRITE));
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
	// if (ret = ibv_post_recv(client_qp, &server_recv_wr, &bad_server_recv_wr)) {
	// 	rdma_error("Failed to pre-post the receive buffer, errno: %d \n", ret);
	// 	return ret;
	// }
	client_src_mr = rdma_buffer_register(pd,
			buffer_msg,
			MESSAGE_SIZE,
			(IBV_ACCESS_LOCAL_WRITE|
			 IBV_ACCESS_REMOTE_READ|
			 IBV_ACCESS_REMOTE_WRITE));
	if(!client_src_mr){
		rdma_error("Thread %d failed to register the first buffer, ret = %d \n", id, ret);
		return ret;
	}
	// client_metadata_attr.address = (uint64_t) client_src_mr->addr; 
	// client_metadata_attr.length = client_src_mr->length; 
	// client_metadata_attr.stag.local_stag = client_src_mr->lkey;
	// client_metadata_mr = rdma_buffer_register(pd,
	// 		&client_metadata_attr,
	// 		sizeof(client_metadata_attr),
	// 		IBV_ACCESS_LOCAL_WRITE);
	// if(!client_metadata_mr) {
	// 	rdma_error("Thread %d failed to register the client metadata buffer, ret = %d \n", id, ret);
	// 	return ret;
	// }
	client_send_sge.addr = (uint64_t) client_src_mr->addr;
	client_send_sge.length = (uint32_t) client_src_mr->length;
	client_send_sge.lkey = client_src_mr->lkey;
	bzero(&client_send_wr, sizeof(client_send_wr));
	client_send_wr.sg_list = &client_send_sge;
	client_send_wr.num_sge = 1;
	client_send_wr.opcode = IBV_WR_SEND;
	client_send_wr.send_flags = IBV_SEND_SIGNALED;
	return 0;
}

/* Connects to the RDMA server */
static int client_connect_to_server(int tid) 
{
	struct rdma_conn_param conn_param;
	struct rdma_cm_event *cm_event = NULL;
	int ret = -1;
	bzero(&conn_param, sizeof(conn_param));
	conn_param.initiator_depth = 3;
	conn_param.responder_resources = 3;
	conn_param.retry_count = 3;
	conn_param.private_data = &tid;
	conn_param.private_data_len = sizeof(tid);
	cm_client_id->context = (void *)(long) tid;
	if ((ret = rdma_connect(cm_client_id, &conn_param))) {
		rdma_error("Thread %d failed to connect to remote host , errno: %d\n", tid, -errno);
		return -errno;
	}
	if ((ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ESTABLISHED,&cm_event))) {
		rdma_error("Thread %d failed to get cm event, ret = %d \n", tid, ret);
	       return ret;
	}
	if ((ret = rdma_ack_cm_event(cm_event))) {
		rdma_error("Thread %d failed to acknowledge cm event, errno: %d\n", tid, -errno);
		return -errno;
	}
	fprintf(stderr, "Thread %d connected \n", tid);
	return 0;
}

static int rdma_request_lock()
{
	int ret;
	sprintf(buffer_msg, "l%d", id);
	if ((ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr))) {
		rdma_error("Thread %d failed to send client metadata, errno: %d \n", id, -errno);
		return -errno;
	}
	ret = process_work_completion_events(io_completion_channel, wc, 1);
	if(ret != 1) {
		rdma_error("Thread %d failed to get 2 work completions for lock request, ret = %d \n", id, ret);
		return ret;
	}
	if ((ret = ibv_post_recv(client_qp, &server_recv_wr, &bad_server_recv_wr))) {
		rdma_error("Thread %d id failed to post the receive buffer, errno: %d \n", id, ret);
		return ret;
	}
	ret = process_work_completion_events(io_completion_channel, wc, 1);
	if(ret != 1) {
		rdma_error("Thread %d failed to get 2 work completions for lock request, ret = %d \n", id, ret);
		return ret;
	}

	// ret = process_work_completion_events(io_completion_channel, wc, 2);
	// if(ret != 2) {
	// 	rdma_error("Thread %d failed to get 2 work completions for lock request, ret = %d \n", id, ret);
	// 	return ret;
	// }
	debug("Thread %d got lock from server\n", id);
	return 0;
}

static int rdma_release_lock()
{
	int ret;
	sprintf(buffer_msg, "r%d", id);
	if ((ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr))) {
		rdma_error("Thread %d failed to post send release lock, errno: %d \n", id, -errno);
		return -errno;
	}
	ret = process_work_completion_events(io_completion_channel, wc, 1);
	if(ret != 1) {
		rdma_error("Thread %d failed to get 2 work completions for lock request, ret = %d \n", id, ret);
		return ret;
	}
	if ((ret = ibv_post_recv(client_qp, &server_recv_wr, &bad_server_recv_wr))) {
		rdma_error("Thread %d id failed post release lock, errno: %d \n", id, ret);
		return ret;
	}
	ret = process_work_completion_events(io_completion_channel, wc, 1);
	if(ret != 1) {
		rdma_error("Thread %d failed to get 2 work completions for lock request, ret = %d \n", id, ret);
		return ret;
	}

	// ret = process_work_completion_events(io_completion_channel, wc, 2);
	// if(ret != 2) {
	// 	rdma_error("Thread %d failed to get 2 work completions for lock release, ret = %d \n", id, ret);
	// 	return ret;
	// }
	debug("Thread %d released lock on server\n", id);
	return 0;
}

/* This function does :
 * 1) Prepare memory buffers for RDMA operations 
 * 1) RDMA write from src -> remote buffer 
 * 2) RDMA read from remote bufer -> dst
 */ 
// static int client_remote_memory_ops() 
// {
// 	struct ibv_wc wc;
// 	int ret = -1;
// 	client_dst_mr = rdma_buffer_register(pd,
// 			dst,
// 			strlen(src),
// 			(IBV_ACCESS_LOCAL_WRITE | 
// 			 IBV_ACCESS_REMOTE_WRITE | 
// 			 IBV_ACCESS_REMOTE_READ));
// 	if (!client_dst_mr) {
// 		rdma_error("We failed to create the destination buffer, -ENOMEM\n");
// 		return -ENOMEM;
// 	}
// 	/* Step 1: is to copy the local buffer into the remote buffer. We will 
// 	 * reuse the previous variables. */
// 	/* now we fill up SGE */
// 	client_send_sge.addr = (uint64_t) client_src_mr->addr;
// 	client_send_sge.length = (uint32_t) client_src_mr->length;
// 	client_send_sge.lkey = client_src_mr->lkey;
// 	/* now we link to the send work request */
// 	bzero(&client_send_wr, sizeof(client_send_wr));
// 	client_send_wr.sg_list = &client_send_sge;
// 	client_send_wr.num_sge = 1;
// 	client_send_wr.opcode = IBV_WR_RDMA_WRITE;
// 	client_send_wr.send_flags = IBV_SEND_SIGNALED;
// 	/* we have to tell server side info for RDMA */
// 	client_send_wr.wr.rdma.rkey = server_metadata_attr.stag.remote_stag;
// 	client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address;
// 	/* Now we post it */
// 	ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr);
// 	if (ret) {
// 		rdma_error("Failed to write client src buffer, errno: %d \n", -errno);
// 		return -errno;
// 	}
// 	/* at this point we are expecting 1 work completion for the write */
// 	ret = process_work_completion_events(io_completion_channel, &wc, 1);
// 	if(ret != 1) {
// 		rdma_error("We failed to get 1 work completions , ret = %d \n", ret);
// 	}
// 		return ret;
// 	debug("Client side WRITE is complete \n");

// 	/* Now we prepare a READ using same variables but for destination */
// 	// client_send_sge.addr = (uint64_t) client_dst_mr->addr;
// 	// client_send_sge.length = (uint32_t) client_dst_mr->length;
// 	// client_send_sge.lkey = client_dst_mr->lkey;
// 	// /* now we link to the send work request */
// 	// bzero(&client_send_wr, sizeof(client_send_wr));
// 	// client_send_wr.sg_list = &client_send_sge;
// 	// client_send_wr.num_sge = 1;
// 	// client_send_wr.opcode = IBV_WR_RDMA_READ;
// 	// client_send_wr.send_flags = IBV_SEND_SIGNALED;
// 	// /* we have to tell server side info for RDMA */
// 	// client_send_wr.wr.rdma.rkey = server_metadata_attr.stag.remote_stag;
// 	// client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address;
// 	// /* Now we post it */
// 	// ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr);
// 	// if (ret) {
// 	// 	rdma_error("Failed to read client dst buffer from the master, errno: %d \n", -errno);
// 	// 	return -errno;
// 	// }
// 	// /* at this point we are expecting 1 work completion for the write */
// 	// ret = process_work_completion_events(io_completion_channel, &wc, 1);
// 	// if(ret != 1) {
// 	// 	rdma_error("We failed to get 1 work completions , ret = %d \n", ret);
// 	// 	return ret;
// 	// }
// 	// debug("Client side READ is complete \n");
// 	return 0;
// }

// int rdma_request_lock(int tid)
// {
// 	char send_buffer[32];
// 	sprintf(send_buffer, "l%d", tid);

// 	// Set up the send work request
// 	struct ibv_sge send_sge = {
// 		.addr = (uintptr_t)send_buffer,
// 		.length = sizeof(send_buffer),
// 		.lkey = client_src_mr->lkey // memory region key for the buffer
// 	};

// 	// struct ibv_send_wr send_wr = {
// 	// 	.wr_id = 1, // Arbitrary ID for this work request
// 	// 	.next = NULL,
// 	// 	.sg_list = &send_sge,  // List of scatter-gather elements
// 	// 	.num_sge = 1, // Number of SG elements
// 	// 	.opcode = IBV_WR_SEND,  // Send operation
// 	// 	.send_flags = IBV_SEND_SIGNALED, // Signal when complete
// 	// 	.wr = { .send = {} }, // Placeholder
// 	// };

// 	// // Post the send request
// 	// struct ibv_send_wr *bad_send_wr = NULL;
// 	// int ret = ibv_post_send(client_qp, &send_wr, &bad_send_wr);
// 	// if (ret) {
// 	// 	rdma_error("ibv_post_send failed\n");
// 	// 	return -1;
// 	// }
// 	return 1;
// }

/* This function disconnects the RDMA connection from the server and cleans up 
 * all the resources.
 */
// static int client_disconnect_and_clean()
// {
// 	struct rdma_cm_event *cm_event = NULL;
// 	int ret = -1;
// 	/* active disconnect from the client side */
// 	ret = rdma_disconnect(cm_client_id);
// 	if (ret) {
// 		rdma_error("Failed to disconnect, errno: %d \n", -errno);
// 		//continuing anyways
// 	}
// 	ret = process_rdma_cm_event(cm_event_channel, 
// 			RDMA_CM_EVENT_DISCONNECTED,
// 			&cm_event);
// 	if (ret) {
// 		rdma_error("Failed to get RDMA_CM_EVENT_DISCONNECTED event, ret = %d\n",
// 				ret);
// 		//continuing anyways 
// 	}
// 	ret = rdma_ack_cm_event(cm_event);
// 	if (ret) {
// 		rdma_error("Failed to acknowledge cm event, errno: %d\n", 
// 			       -errno);
// 		//continuing anyways
// 	}
// 	/* Destroy QP */
// 	rdma_destroy_qp(cm_client_id);
// 	/* Destroy client cm id */
// 	ret = rdma_destroy_id(cm_client_id);
// 	if (ret) {
// 		rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
// 		// we continue anyways;
// 	}
// 	/* Destroy CQ */
// 	ret = ibv_destroy_cq(client_cq);
// 	if (ret) {
// 		rdma_error("Failed to destroy completion queue cleanly, %d \n", -errno);
// 		// we continue anyways;
// 	}
// 	/* Destroy completion channel */
// 	ret = ibv_destroy_comp_channel(io_completion_channel);
// 	if (ret) {
// 		rdma_error("Failed to destroy completion channel cleanly, %d \n", -errno);
// 		// we continue anyways;
// 	}
// 	/* Destroy memory buffers */
// 	rdma_buffer_deregister(server_metadata_mr);
// 	rdma_buffer_deregister(client_metadata_mr);	
// 	rdma_buffer_deregister(client_src_mr);	
// 	rdma_buffer_deregister(client_dst_mr);	
// 	/* We free the buffers */
// 	free(src);
// 	free(dst);
// 	/* Destroy protection domain */
// 	ret = ibv_dealloc_pd(pd);
// 	if (ret) {
// 		rdma_error("Failed to destroy client protection domain cleanly, %d \n", -errno);
// 		// we continue anyways;
// 	}
// 	rdma_destroy_event_channel(cm_event_channel);
// 	printf("Client resource clean up is complete \n");
// 	return 0;
// }

int establish_rdma_connection(int tid, char* addr)
{
	struct sockaddr_in server_sockaddr;
	int ret;
	id = tid;
	bzero(&server_sockaddr, sizeof server_sockaddr);
	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	// fprintf(stderr, "Thread %d estasblishing RDMA connection\n", tid);
	if ((ret = get_addr(addr, (struct sockaddr*) &server_sockaddr))) {
		rdma_error("Invalid IP \n");
		return ret;
	}
	if (!server_sockaddr.sin_port) {
		server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT);
	}
	if ((ret = client_prepare_connection(&server_sockaddr))) { 
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}
	if ((ret = client_prep_buffers(tid))) { 
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}
	ret = client_connect_to_server(tid);
	if (ret) { 
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}
	return ret;
}
