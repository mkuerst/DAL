/*
 * Header file for the common RDMA routines used in the server/client example 
 * program. 
 *
 * Author: Animesh Trivedi 
 *          atrivedi@apache.org 
 *
 */

#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include <netdb.h>
#include <netinet/in.h>	
#include <arpa/inet.h>
#include <sys/socket.h>

#include <rdma_cma.h>
#include <infiniband/verbs.h>
#include "utils.h"

#define rdma_error(msg, args...) do {\
	fprintf(stderr, "\033[1;31m%s : %d : ERROR : \033[0m"msg, __FILE__, __LINE__, ## args);\
	fprintf(stderr, "\n");\
}while(0);

#define CQ_CAPACITY (16)
#define MAX_SGE (2)
#define MAX_WR (8)
#define DEFAULT_RDMA_PORT (20051)
// #define DEFAULT_RDMA_PORT (20049)
// #define DEFAULT_RDMA_PORT (20886)
// #define DEFAULT_RDMA_PORT (18515)
// #define DEFAULT_RDMA_PORT (8080)

#define MESSAGE_SIZE 8 
#define META_SIZE 256

/* resolves a given destination name to sin_addr */
int get_addr(char *dst, struct sockaddr *addr);

/* prints RDMA buffer info structure */
void show_rdma_buffer_attr(struct rdma_buffer_attr *attr);

int check_qp_state(struct ibv_qp *qp);
/* 
 * Processes an RDMA connection management (CM) event. 
 * @echannel: CM event channel where the event is expected. 
 * @expected_event: Expected event type 
 * @cm_event: where the event will be stored 
 */
int process_rdma_cm_event(struct rdma_event_channel *echannel, 
		enum rdma_cm_event_type expected_event,
		struct rdma_cm_event **cm_event);

/* Allocates an RDMA buffer of size 'length' with permission permission. This 
 * function will also register the memory and returns a memory region (MR) 
 * identifier or NULL on error. 
 * @pd: Protection domain where the buffer should be allocated 
 * @length: Length of the buffer 
 * @permission: OR of IBV_ACCESS_* permissions as defined for the enum ibv_access_flags
 */
struct ibv_mr* rdma_buffer_alloc(struct ibv_pd *pd, 
		uint32_t length, 
		enum ibv_access_flags permission);

/* Frees a previously allocated RDMA buffer. The buffer must be allocated by 
 * calling rdma_buffer_alloc();
 * @mr: RDMA memory region to free 
 */
void rdma_buffer_free(struct ibv_mr *mr);

/* This function registers a previously allocated memory. Returns a memory region 
 * (MR) identifier or NULL on error.
 * @pd: protection domain where to register memory 
 * @addr: Buffer address 
 * @length: Length of the buffer 
 * @permission: OR of IBV_ACCESS_* permissions as defined for the enum ibv_access_flags
 */
struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd, 
		void *addr, 
		uint32_t length, 
		enum ibv_access_flags permission);
/* Deregisters a previously register memory 
 * @mr: Memory region to deregister 
 */
void rdma_buffer_deregister(struct ibv_mr *mr);

/* Processes a work completion (WC) notification. 
 * @comp_channel: Completion channel where the notifications are expected to arrive 
 * @wc: Array where to hold the work completion elements 
 * @max_wc: Maximum number of expected work completion (WC) elements. wc must be 
 *          atleast this size.
 */
int process_work_completion_events(struct ibv_comp_channel *comp_channel, 
		struct ibv_wc *wc, 
		int max_wc);

/* prints some details from the cm id */
void show_rdma_cmid(struct rdma_cm_id *id);

#endif /* RDMA_COMMON_H */

void* establish_rdma_connection(int tid, char* addr);