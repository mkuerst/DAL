#include "rdma_common.h"

/* Default port where the RDMA server is listening */
#define DEFAULT_RDMA_PORT (20886)
#define SERVER_ADDR ("10.233.0.21")

static struct ibv_pd *pd = NULL;

// Function to create context by device name
struct ibv_context* createContext(const char* device_name) 
{
  struct ibv_context* context = NULL;
  int num_devices;
  struct ibv_device** device_list = ibv_get_device_list(&num_devices);

  for (int i = 0; i < num_devices; i++) {
      // Match device name, open the device, and return it
      if (strcmp(device_name, ibv_get_device_name(device_list[i])) == 0) {
          context = ibv_open_device(device_list[i]);
          break;
      }
  }

  // Free device list to avoid memory leaks
  ibv_free_device_list(device_list);
  if (context == NULL) {
      fprintf(stderr, "Unable to find the device %s\n", device_name);
  }
  return context;
}

// Function to create Queue Pair
struct ibv_qp* createQueuePair(struct ibv_pd* pd, struct ibv_cq* cq) 
{
  struct ibv_qp_init_attr queue_pair_init_attr;
  memset(&queue_pair_init_attr, 0, sizeof(queue_pair_init_attr));
  queue_pair_init_attr.qp_type = IBV_QPT_RC;
  queue_pair_init_attr.sq_sig_all = 1;
  queue_pair_init_attr.send_cq = cq;
  queue_pair_init_attr.recv_cq = cq;
  queue_pair_init_attr.cap.max_send_wr = 1;
  queue_pair_init_attr.cap.max_recv_wr = 1;
  queue_pair_init_attr.cap.max_send_sge = 1;
  queue_pair_init_attr.cap.max_recv_sge = 1;

  return ibv_create_qp(pd, &queue_pair_init_attr);
}

// Function to change QP state to INIT
int changeQueuePairStateToInit(struct ibv_qp* queue_pair, uint8_t port_num) 
{
  struct ibv_qp_attr init_attr;
  memset(&init_attr, 0, sizeof(init_attr));
  init_attr.qp_state = IBV_QPS_INIT;
  init_attr.port_num = port_num;
  init_attr.pkey_index = 0;
  init_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

  return ibv_modify_qp(queue_pair, &init_attr,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS) == 0;
}

// Function to change QP state to RTR
int changeQueuePairStateToRTR(struct ibv_qp* queue_pair, int ib_port, uint32_t destination_qp_number, uint16_t destination_local_id) 
{
  struct ibv_qp_attr rtr_attr;
  memset(&rtr_attr, 0, sizeof(rtr_attr));
  rtr_attr.qp_state = IBV_QPS_RTR;
  rtr_attr.path_mtu = IBV_MTU_1024;
  rtr_attr.rq_psn = 0;
  rtr_attr.max_dest_rd_atomic = 1;
  rtr_attr.min_rnr_timer = 0x12;
  rtr_attr.ah_attr.is_global = 0;
  rtr_attr.ah_attr.sl = 0;
  rtr_attr.ah_attr.src_path_bits = 0;
  rtr_attr.ah_attr.port_num = ib_port;

  rtr_attr.dest_qp_num = destination_qp_number;
  rtr_attr.ah_attr.dlid = destination_local_id;

  return ibv_modify_qp(queue_pair, &rtr_attr,
                        IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                        IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER) == 0;
}

// Function to change QP state to RTS
int changeQueuePairStateToRTS(struct ibv_qp* queue_pair) 
{
  struct ibv_qp_attr rts_attr;
  memset(&rts_attr, 0, sizeof(rts_attr));
  rts_attr.qp_state = IBV_QPS_RTS;
  rts_attr.timeout = 0x12;
  rts_attr.retry_cnt = 7;
  rts_attr.rnr_retry = 7;
  rts_attr.sq_psn = 0;
  rts_attr.max_rd_atomic = 1;

  return ibv_modify_qp(queue_pair, &rts_attr,
                      IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                      IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC) == 0;
}

// Function to register a memory region
struct ibv_mr* registerMemoryRegion(struct ibv_pd* pd, void* buffer, size_t size) 
{
    return ibv_reg_mr(pd, buffer, size,
                      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
}

// Function to post a send request
int postSendRequest(struct ibv_qp* queue_pair, struct ibv_sge* sg_list, int num_sge) 
{
  struct ibv_send_wr send_wr, *bad_wr = NULL;
  memset(&send_wr, 0, sizeof(send_wr));

  send_wr.sg_list = sg_list;
  send_wr.num_sge = num_sge;
  send_wr.wr_id = 200;
  send_wr.opcode = IBV_WR_SEND;
  send_wr.send_flags = IBV_SEND_SIGNALED;
  send_wr.next = NULL;

  return ibv_post_send(queue_pair, &send_wr, &bad_wr) == 0;
}

// Function to poll completion
int pollCompletion(struct ibv_cq* cq) 
{
  struct ibv_wc wc;
  int result;

  do {
      result = ibv_poll_cq(cq, 1, &wc);
  } while (result == 0);

  if (result > 0 && wc.status == IBV_WC_SUCCESS) {
      return 1; // success
  }

  fprintf(stderr, "Poll failed with status %d (work request ID: %llu)\n", wc.status, (unsigned long long)wc.wr_id);
  return 0;
}

// Function to get local ID
uint16_t getLocalId(struct ibv_context* context, int ib_port) 
{
  struct ibv_port_attr port_attr;
  ibv_query_port(context, ib_port, &port_attr);
  return port_attr.lid;
}

// Function to get Queue Pair number
uint32_t getQueuePairNumber(struct ibv_qp* qp) {
    return qp->qp_num;
}



int main(int argc, char **argv) 
{
  int ret;
	struct sockaddr_in server_sockaddr;
	bzero(&server_sockaddr, sizeof server_sockaddr);
	server_sockaddr.sin_family = AF_INET; /* standard IP NET address */
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY); /* passed address */
  ret = get_addr(SERVER_ADDR, (struct sockaddr*) &server_sockaddr);
  if (ret) {
      rdma_error("Invalid IP\n");
        return ret;
  }

	if(!server_sockaddr.sin_port) {
		/* If still zero, that mean no port info provided */
		server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT); /* use default port */
  }

}