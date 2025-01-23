/*
 * Implementation of the common RDMA functions. 
 *
 * Authors: Animesh Trivedi
 *          atrivedi@apache.org 
 */

#include "rdma_common.h"

void show_rdma_cmid(struct rdma_cm_id *id)
{
	if(!id){
		rdma_error("Passed ptr is NULL\n");
		return;
	}
	printf("RDMA cm id at %p \n", id);
	if(id->verbs && id->verbs->device)
		printf("dev_ctx: %p (device name: %s) \n", id->verbs, 
				id->verbs->device->name);
	if(id->channel)
		printf("cm event channel %p\n", id->channel);
	printf("QP: %p, port_space %x, port_num %u \n", id->qp, 
			id->ps,
			id->port_num);
}

void show_rdma_buffer_attr(struct rdma_buffer_attr *attr){
	if(!attr){
		rdma_error("Passed attr is NULL\n");
		return;
	}
	fprintf(stderr, "---------------------------------------------------------\n");
	fprintf(stderr, "buffer attr, addr: %p , len: %u , stag : 0x%x \n", 
			(void*) attr->address, 
			(unsigned int) attr->length,
			attr->stag.local_stag);
	fprintf(stderr, "---------------------------------------------------------\n");
}

struct ibv_mr* rdma_buffer_alloc(struct ibv_pd *pd, uint32_t size,
    enum ibv_access_flags permission) 
{
	struct ibv_mr *mr = NULL;
	if (!pd) {
		rdma_error("Protection domain is NULL \n");
		return NULL;
	}
	void *buf = calloc(1, size);
	if (!buf) {
		rdma_error("failed to allocate buffer, -ENOMEM\n");
		return NULL;
	}
	// debug("Buffer allocated: %p , len: %u \n", buf, size);
	mr = rdma_buffer_register(pd, buf, size, permission);
	if(!mr){
		free(buf);
		rdma_error("failed to register mr for buf: %p, len: %u\n", buf, size);
	}
	return mr;
}

struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd, 
		void *addr, uint32_t length, 
		enum ibv_access_flags permission)
{
	struct ibv_mr *mr = NULL;
	if (!pd) {
		DEBUG("Protection domain is NULL, ignoring \n");
		return NULL;
	}
	mr = ibv_reg_mr(pd, addr, length, permission);
	if (!mr) {
		DEBUG("Failed to register mr on buffer, errno: %d \n", -errno);
		return NULL;
	}
	return mr;
}

void rdma_buffer_free(struct ibv_mr *mr) 
{
	if (!mr) {
		DEBUG("Passed memory region is NULL, ignoring\n");
		return ;
	}
	void *to_free = mr->addr;
	rdma_buffer_deregister(mr);
	debug("Buffer %p free'ed\n", to_free);
	free(to_free);
}

void rdma_buffer_deregister(struct ibv_mr *mr) 
{
	if (!mr) { 
		DEBUG("Passed memory region is NULL, ignoring\n");
		return;
	}
	debug("Deregistered: %p , len: %u , stag : 0x%x \n", 
			mr->addr, 
			(unsigned int) mr->length, 
			mr->lkey);
	ibv_dereg_mr(mr);
}

int process_rdma_cm_event(struct rdma_event_channel *echannel, 
		enum rdma_cm_event_type expected_event,
		struct rdma_cm_event **cm_event)
{
	int ret = 1;
	ret = rdma_get_cm_event(echannel, cm_event);
	if (ret) {
		rdma_error("Failed to retrieve a cm event, errno: %d \n", -errno);
		return -errno;
	}
	if(0 != (*cm_event)->status){
		rdma_error("CM event has non zero status: %d\nevent: %s\n", (*cm_event)->status, rdma_event_str((*cm_event)->status));
		ret = -((*cm_event)->status);
		rdma_ack_cm_event(*cm_event);
		return ret;
	}
	if ((*cm_event)->event != expected_event) {
		rdma_error("Unexpected event received: %s [ expecting: %s ]", 
				rdma_event_str((*cm_event)->event),
				rdma_event_str(expected_event));
		rdma_ack_cm_event(*cm_event);
		return -1;
	}
	return ret;
}

int process_work_completion_events (struct ibv_comp_channel *comp_channel, struct ibv_wc *wc, int max_wc)
{
	struct ibv_cq *cq_ptr = NULL;
	void *context = NULL;
	int ret = -1, i, total_wc = 0;
       /* We wait for the notification on the CQ channel */
	ret = ibv_get_cq_event(comp_channel, &cq_ptr, &context);
       if (ret) {
	       rdma_error("Failed to get next CQ event due to %d \n", -errno);
	       return -errno;
       }
       /* Request for more notifications. */
       ret = ibv_req_notify_cq(cq_ptr, 0);
       if (ret){
	       rdma_error("Failed to request further notifications %d \n", -errno);
	       return -errno;
       }
       total_wc = 0;
       do {
	       ret = ibv_poll_cq(cq_ptr,
		       max_wc - total_wc, 
		       wc + total_wc);
	       if (ret < 0) {
		       rdma_error("Failed to poll cq for wc due to %d \n", ret);
		       return ret;
	       }
	       total_wc += ret;
       } while (total_wc < max_wc); 
    //    debug("%d WC are completed \n", total_wc);
       for( i = 0 ; i < total_wc ; i++) {
	       if (wc[i].status != IBV_WC_SUCCESS) {
		       rdma_error("Work completion (WC) has error status: %s at index %d\n", ibv_wc_status_str(wc[i].status), i);
		       return (wc[i].status);
	       }
       }
       ibv_ack_cq_events(cq_ptr, 1 /* we received one event notification. This is not number of WC elements */);
       return total_wc; 
}


/* Code acknowledgment: rping.c from librdmacm/examples */
// int get_addr(char *dst, struct sockaddr *addr)
// {
// 	struct addrinfo *res;
// 	int ret = -1;
// 	ret = getaddrinfo(dst, NULL, NULL, &res);
// 	if (ret) {
// 		rdma_error("getaddrinfo failed - invalid hostname or IP address\n");
// 		return ret;
// 	}
// 	memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in));
// 	freeaddrinfo(res);
// 	return ret;
// }


int check_qp_state(struct ibv_qp *qp) {
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    int ret;

    ret = ibv_query_qp(qp, &attr, IBV_QP_STATE, &init_attr);
    if (ret) {
        fprintf(stderr, "Failed to query QP state: %d\n", ret);
        return ret;
    }

    switch (attr.qp_state) {
        case IBV_QPS_RESET:
            printf("QP is in RESET state\n");
            break;
        case IBV_QPS_INIT:
            printf("QP is in INIT state\n");
            break;
        case IBV_QPS_RTR:
            printf("QP is in RTR (Ready to Receive) state\n");
            break;
        case IBV_QPS_RTS:
            printf("QP is in RTS (Ready to Send) state\n");
            break;
        case IBV_QPS_SQD:
            printf("QP is in SQD (Send Queue Drained) state\n");
            break;
        case IBV_QPS_SQE:
            printf("QP is in SQE (Send Queue Error) state\n");
            break;
        case IBV_QPS_ERR:
            printf("QP is in ERR (Error) state\n");
            break;
        default:
            printf("QP is in an unknown state: %d\n", attr.qp_state);
            break;
    }
    return 0;
}