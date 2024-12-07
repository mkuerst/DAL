/*
 * This is a RDMA server side code. 
 *
 * Author: Animesh Trivedi 
 *         atrivedi@apache.org 
 *
 * TODO: Cleanup previously allocated resources in case of an error condition
 */

#include "rdma_common.h"

rdma_thread threads[MAX_THREADS];
pthread_mutex_t mutex;
int num_connections = 0;
char buffer_msg[MESSAGE_SIZE];

void *run_lock_impl(void *_arg)
{
    rdma_thread* thread = (rdma_thread*) _arg;
	struct ibv_wc wc;
	int ret = -1;
	
	rdma_connection conn = thread->connection;
	struct ibv_qp *qp = conn.qp;
	struct ibv_comp_channel* io_comp_chan = conn.io_comp_chan;
	struct rdma_buffer_attr client_metadata_attr = conn.client_metadata_attr;
    struct rdma_buffer_attr server_metadata_attr = conn.server_metadata_attr;
	struct ibv_pd *pd = conn.pd;
    struct ibv_mr *client_mr = conn.client_mr;
    struct ibv_mr *server_mr = conn.server_mr;
    struct ibv_recv_wr client_recv_wr = conn.client_recv_wr;
    struct ibv_recv_wr *bad_client_recv_wr = conn.bad_client_recv_wr;
    struct ibv_send_wr server_send_wr = conn.server_send_wr; 
    struct ibv_send_wr *bad_server_send_wr = conn.bad_server_send_wr;
    struct ibv_sge client_recv_sge = conn.client_recv_sge;
    struct ibv_sge server_send_sge = conn.server_send_sge;
	unsigned int tid = thread->client_tid;
	int j = 0;
	pin_thread(tid);

	server_mr = rdma_buffer_register(pd,
			buffer_msg,
			MESSAGE_SIZE,
			(IBV_ACCESS_LOCAL_WRITE|
			 IBV_ACCESS_REMOTE_READ|
			 IBV_ACCESS_REMOTE_WRITE));
	if(!server_mr){
		rdma_error("Thread %d failed to register the server_mr buffer, ret = %d \n", tid, ret);
		exit(EXIT_FAILURE);
	}
	// server_mr = rdma_buffer_alloc(pd, 
	// 		64,
	// 		(IBV_ACCESS_LOCAL_WRITE|
	// 		IBV_ACCESS_REMOTE_READ|
	// 		IBV_ACCESS_REMOTE_WRITE));
	// if(!server_mr){
	// 	rdma_error("Server failed to create a server_mr for thread %d \n", tid);
	// 	exit(EXIT_FAILURE);
	// }
	// server_metadata_attr.address = (uint64_t) server_mr->addr;
	// server_metadata_attr.length = (uint32_t) server_mr->length;
	// server_metadata_attr.stag.local_stag = (uint32_t) server_mr->lkey;
	// struct ibv_mr *server_metadata_mr = rdma_buffer_register(pd, 
	// 		&server_metadata_attr, 
	// 		sizeof(server_metadata_attr),
	// 		IBV_ACCESS_LOCAL_WRITE);
	// if(!server_metadata_mr){
	// 	rdma_error("Server thread %d failed to create to hold server metadata \n", tid);
	// 	exit(EXIT_FAILURE);
	// }
	server_send_sge.addr = (uint64_t) server_mr->addr;
	server_send_sge.length = (uint32_t) server_mr->length;
	server_send_sge.lkey = server_mr->lkey;
	bzero(&server_send_wr, sizeof(server_send_wr));
	server_send_wr.sg_list = &server_send_sge;
	server_send_wr.num_sge = 1; // only 1 SGE element in the array 
	server_send_wr.opcode = IBV_WR_SEND; // This is a send request 
	server_send_wr.send_flags = IBV_SEND_SIGNALED; // We want to get notification 

    // char buffer[MESSAGE_SIZE];
    // memset(buffer, 0, MESSAGE_SIZE);

    // char granted_msg[BUFFER_SIZE];
    // memset(granted_msg, 0, BUFFER_SIZE);
    // sprintf(granted_msg, "granted lock");

    // char released_msg[BUFFER_SIZE];
    // memset(released_msg, 0, BUFFER_SIZE);
    // sprintf(released_msg, "released lock");

    // char ok_msg[BUFFER_SIZE];
    // memset(ok_msg, 0, BUFFER_SIZE);
    // sprintf(ok_msg, "ok");

	while(1){
		ret = ibv_post_recv(qp, &client_recv_wr, &bad_client_recv_wr);
		if (ret) {
			rdma_error("Tread %d failed to post the receive buffer, errno: %d \n", tid, ret);
			exit(EXIT_FAILURE);
		}
		ret = process_work_completion_events(io_comp_chan, &wc, 1);
		if (ret != 1) {
			rdma_error("Server failed to receive from thread %d, ret = %d \n", tid, ret);
			exit(EXIT_FAILURE);
		}
		if (ret > 0 && wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV) {
			char *buffer = (char *)client_mr->addr;
			printf("Received data: %s\n", buffer);

			char cmd;
			int id, ret = 0;
			if (sscanf(buffer, "%c%d", &cmd, &id) == 2) {
				if (cmd == 'l') {
					ull now = rdtscp();
					pthread_mutex_lock(&mutex);
					thread->lock_impl_time[j] += rdtscp() - now;
					ret = ibv_post_send(qp, &server_send_wr, &bad_server_send_wr);
					if (ret) {
						rdma_error("Thread %d failed to post lock grant, errno: %d \n", tid, -errno);
						exit(EXIT_FAILURE);
					}
					ret = process_work_completion_events(io_comp_chan, &wc, 1);
					if (ret != 1) {
						rdma_error("Thread %d failed to send lock grant, ret = %d \n", tid, ret);
						exit(EXIT_FAILURE);
					}
					DEBUG("Granted lock to thread %d over socket %d\n", id, client_socket);
				}
				else if (cmd == 'r') {
					ull now = rdtscp();
					pthread_mutex_unlock(&mutex);
					thread->lock_impl_time[j] += rdtscp() - now;
					DEBUG("Released lock on server for thread %d\n", id);
					ret = ibv_post_send(qp, &server_send_wr, &bad_server_send_wr);
					if (ret) {
						rdma_error("Thread %d failed to post lock release, errno: %d \n", tid, -errno);
						exit(EXIT_FAILURE);
					}
					ret = process_work_completion_events(io_comp_chan, &wc, 1);
					if (ret != 1) {
						rdma_error("Thread %d failed to send lock release, ret = %d \n", tid, ret);
						exit(EXIT_FAILURE);
					}
					DEBUG("Notified thread %d of lock release\n", tid);
				}
				else if (cmd == 'd') {
					j++;
					DEBUG("Received run complete from thread %d\n", id);
				}
			} else {
				DEBUG("Failed to parse the string from thread %d, got: %s\n", id, buffer);
			}
			// memset(buffer, 0, MESSAGE_SIZE);
		}
	}
}

/*
* This is where, we prepare client connection before we accept it. This 
* mainly involve pre-posting a receive buffer to receive client side 
* RDMA credentials
*/

/* Starts an RDMA server by allocating basic connection resources per client connection*/
static int start_rdma_server(struct sockaddr_in *server_addr, int nthreads) 
{
	struct rdma_event_channel *cm_event_channel = NULL;
	struct rdma_cm_event *cm_event = NULL;
	struct rdma_cm_id *cm_server_id = NULL;
	int ret = -1;
	/*  Open a channel used to report asynchronous communication event */
	cm_event_channel = rdma_create_event_channel();
	if (!cm_event_channel) {
		rdma_error("Creating cm event channel failed with errno : (%d)", -errno);
		return -errno;
	}
	ret = rdma_create_id(cm_event_channel, &cm_server_id, NULL, RDMA_PS_TCP);
	if (ret) {
		rdma_error("Creating server cm id failed with errno: %d ", -errno);
		return -errno;
	}
	ret = rdma_bind_addr(cm_server_id, (struct sockaddr*) server_addr);
	if (ret) {
		rdma_error("Failed to bind server address, errno: %d \n", -errno);
		return -errno;
	}
	// debug("Server RDMA CM id is successfully binded \n");
	/* Now we start to listen on the passed IP and port. However unlike
	 * normal TCP listen, this is a non-blocking call. When a new client is 
	 * connected, a new connection management (CM) event is generated on the 
	 * RDMA CM event channel from where the listening id was created. Here we
	 * have only one channel, so it is easy. */
	ret = rdma_listen(cm_server_id, nthreads); /* backlog = 8 clients, same as TCP, see man listen*/
	if (ret) {
		rdma_error("rdma_listen failed to listen on server address, errno: %d ", -errno);
		return -errno;
	}
	fprintf(stderr, "Server is listening successfully at: %s , port: %d \n",
			inet_ntoa(server_addr->sin_addr),
			ntohs(server_addr->sin_port));

    pthread_mutex_init(&mutex, NULL);
	for (int i = 0; i < nthreads; i++) {
		struct rdma_connection* conn = &threads[i].connection;
		debug("Waiting for conn establishment %d\n", i);
		ret = process_rdma_cm_event(cm_event_channel, 
				RDMA_CM_EVENT_CONNECT_REQUEST,
				&cm_event);
		if (ret) {
			rdma_error("Failed to get cm event CONNECT_REQUEST, ret = %d \n" , ret);
			return ret;
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
		cq = ibv_create_cq(cm_client_id->verbs /* which device*/, 
				CQ_CAPACITY /* maximum capacity*/, 
				NULL /* user context, not used here */,
				io_completion_channel /* which IO completion channel */, 
				0 /* signaling vector, not used here*/);
		if (!cq) {
			rdma_error("Failed to create a completion queue (cq), errno: %d\n", -errno);
			return -errno;
		}
		ret = ibv_req_notify_cq(cq /* on which CQ */, 
				0 /* 0 = all event type, no filter*/);
		if (ret) {
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
		/*Lets create a QP */
		ret = rdma_create_qp(cm_client_id /* which connection id */,
				pd /* which protection domain*/,
				&qp_init_attr /* Initial attributes */);
		if (ret) {
			rdma_error("Failed to create QP due to errno: %d\n", -errno);
			return -errno;
		}
		client_qp = cm_client_id->qp;
		conn->pd = pd;
		conn->io_comp_chan = io_completion_channel;
		conn->cq = cq;
		conn->qp = client_qp;
		struct rdma_conn_param conn_param;

		ret = rdma_ack_cm_event(cm_event);
		if (ret) {
			rdma_error("Failed to acknowledge the cm event errno: %d \n", -errno);
			return -errno;
		}

		if(!cm_client_id || !client_qp) {
			rdma_error("Client resources are not properly setup\n");
			return -EINVAL;
		}

		conn->client_mr = rdma_buffer_register(pd /* which protection domain */, 
				&conn->client_metadata_attr /* what memory */,
				sizeof(conn->client_metadata_attr) /* what length */, 
				(IBV_ACCESS_LOCAL_WRITE) /* access permissions */);
		if(!conn->client_mr){
			rdma_error("Failed to register client attr buffer\n");
			return -ENOMEM;
		}
		/* We pre-post this receive buffer on the QP. SGE credentials is where we 
		* receive the metadata from the client */
		conn->client_recv_sge.addr = (uint64_t) conn->client_mr->addr; // same as &client_buffer_attr
		conn->client_recv_sge.length = conn->client_mr->length;
		conn->client_recv_sge.lkey = conn->client_mr->lkey;
		bzero(&conn->client_recv_wr, sizeof(conn->client_recv_wr));
		conn->client_recv_wr.sg_list = &conn->client_recv_sge;
		conn->client_recv_wr.num_sge = 1; // only one SGE
		
		



		ret = ibv_post_recv(client_qp /* which QP */,
				&conn->client_recv_wr /* receive work request*/,
				&conn->bad_client_recv_wr /* error WRs */);
		if (ret) {
			rdma_error("Failed to pre-post the receive buffer, errno: %d \n", ret);
			return ret;
		}
		







		memset(&conn_param, 0, sizeof(conn_param));
		conn_param.initiator_depth = 3;
		conn_param.responder_resources = 3;
		ret = rdma_accept(cm_client_id, &conn_param);
		if (ret) {
			rdma_error("Failed to accept the connection, errno: %d \n", -errno);
			return -errno;
		}
		ret = process_rdma_cm_event(cm_event_channel, 
				RDMA_CM_EVENT_ESTABLISHED,
				&cm_event);
			if (ret) {
			rdma_error("Failed to get the cm event, errnp: %d \n", -errno);
			return -errno;
		}
		ret = rdma_ack_cm_event(cm_event);
		if (ret) {
			rdma_error("Failed to acknowledge the cm event %d\n", -errno);
			return -errno;
		}

		/* Just FYI: How to extract connection information */
		// memcpy(&remote_sockaddr /* where to save */, 
		// 		rdma_get_peer_addr(cm_client_id) /* gives you remote sockaddr */, 
		// 		sizeof(struct sockaddr_in) /* max size */);

		fprintf(stderr, "A new connection is accepted from thread %d \n", task_id);

		for (int j = 0; j < NUM_RUNS; j++) {
			threads[i].lock_impl_time[j] = 0;
		}
		threads[i].rdma = 1;
		pthread_create(&threads[i].thread, NULL, run_lock_impl, &threads[i]);
	}
	return ret;
}


/* This is server side logic. Server passively waits for the client to call 
 * rdma_disconnect() and then it will clean up its resources */
// static int disconnect_and_cleanup()
// {
// 	struct rdma_cm_event *cm_event = NULL;
// 	int ret = -1;
//        /* Now we wait for the client to send us disconnect event */
//        debug("Waiting for cm event: RDMA_CM_EVENT_DISCONNECTED\n");
//        ret = process_rdma_cm_event(cm_event_channel, 
// 		       RDMA_CM_EVENT_DISCONNECTED, 
// 		       &cm_event);
//        if (ret) {
// 	       rdma_error("Failed to get disconnect event, ret = %d \n", ret);
// 	       return ret;
//        }
// 	/* We acknowledge the event */
// 	ret = rdma_ack_cm_event(cm_event);
// 	if (ret) {
// 		rdma_error("Failed to acknowledge the cm event %d\n", -errno);
// 		return -errno;
// 	}
// 	printf("A disconnect event is received from the client...\n");
// 	/* We free all the resources */
// 	/* Destroy QP */
// 	rdma_destroy_qp(cm_client_id);
// 	/* Destroy client cm id */
// 	ret = rdma_destroy_id(cm_client_id);
// 	if (ret) {
// 		rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
// 		// we continue anyways;
// 	}
// 	/* Destroy CQ */
// 	ret = ibv_destroy_cq(cq);
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
// 	rdma_buffer_free(server_buffer_mr);
// 	rdma_buffer_deregister(server_metadata_mr);	
// 	rdma_buffer_deregister(client_metadata_mr);	
// 	/* Destroy protection domain */
// 	ret = ibv_dealloc_pd(pd);
// 	if (ret) {
// 		rdma_error("Failed to destroy client protection domain cleanly, %d \n", -errno);
// 		// we continue anyways;
// 	}
// 	/* Destroy rdma server id */
// 	ret = rdma_destroy_id(cm_server_id);
// 	if (ret) {
// 		rdma_error("Failed to destroy server id cleanly, %d \n", -errno);
// 		// we continue anyways;
// 	}
// 	rdma_destroy_event_channel(cm_event_channel);
// 	printf("Server shut-down is complete \n");
// 	return 0;
// }


void usage() 
{
	printf("Usage:\n");
	printf("rdma_server: [-a <server_addr>] [-p <server_port>] [-t <nthreads>]\n");
	printf("(default port is %d)\n", DEFAULT_RDMA_PORT);
	exit(1);
}

int main(int argc, char **argv) 
{
	int ret, option, nthreads;
	struct sockaddr_in server_sockaddr;
	bzero(&server_sockaddr, sizeof server_sockaddr);
	server_sockaddr.sin_family = AF_INET; /* standard IP NET address */
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY); /* passed address */
	/* Parse Command Line Arguments, not the most reliable code */
	while ((option = getopt(argc, argv, "a:p:t:")) != -1) {
		switch (option) {
			case 'a':
				/* Remember, this will overwrite the port info */
				ret = get_addr(optarg, (struct sockaddr*) &server_sockaddr);
				if (ret) {
					rdma_error("Invalid IP \n");
					return ret;
				}
				break;
			case 'p':
				/* passed port to listen on */
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
		/* If still zero, that mean no port info provided */
		server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT); /* use default port */
	}
	if(!nthreads) {
		usage();
		return -1;
	}
	ret = start_rdma_server(&server_sockaddr, nthreads);
	if (ret) {
		rdma_error("RDMA server failed to start cleanly, ret = %d \n", ret);
		return ret;
	}
    for (int i = 0; i < nthreads; i++) {
        pthread_join(threads[i].thread, NULL);
    }
    for (int j = 0; j < NUM_RUNS; j++) {
        printf("RUN %d\n", j);
        for (int i = 0; i < nthreads; i++) {
            rdma_thread thread = (rdma_thread) threads[i];
            printf("%03d,%10.3f\n", thread.client_tid, thread.lock_impl_time[j] / (float) (CYCLE_PER_US * 1000));
        }
        printf("-----------------------------------------------------------------------------------------------\n\n");
    }
	// ret = setup_client_resources();
	// if (ret) { 
	// 	rdma_error("Failed to setup client resources, ret = %d \n", ret);
	// 	return ret;
	// }
	// ret = accept_client_connection();
	// if (ret) {
	// 	rdma_error("Failed to handle client cleanly, ret = %d \n", ret);
	// 	return ret;
	// }
	// ret = send_server_metadata_to_client();
	// if (ret) {
	// 	rdma_error("Failed to send server metadata to the client, ret = %d \n", ret);
	// 	return ret;
	// }
	// ret = disconnect_and_cleanup();
	// if (ret) { 
	// 	rdma_error("Failed to clean up resources properly, ret = %d \n", ret);
	// 	return ret;
	// }
	return 0;
}
