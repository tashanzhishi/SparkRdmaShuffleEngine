/*
 * BufferPool.h
 *
 *  Created on: May 12, 2016
 *      Author: yurujie
 */

#ifndef RDMABUFFERPOOL_H_
#define RDMABUFFERPOOL_H_

#include "RdmaCommon.h"
#include "RdmaMsgHeader.h"


#define RECV_EXT				40

#define RDMA_BUFFER_SIZE		(4096 + RECV_EXT)

#define APPEND_POOL_SIZE		512

typedef struct rdma_buffer {

	uint8_t 			buf[RDMA_BUFFER_SIZE];
	struct ibv_mr		*mr;
	struct rdma_buffer 	*next;
}rdma_buffer;

typedef struct link_node {
	rdma_buffer			*rbufs;
	struct link_node	*next;
}link_node;

typedef struct rdma_buffer_pool {

	link_node		*link_head;
	rdma_buffer 	*freeList;
	int 			totalSize;
	int				freeSize;
	pthread_mutex_t lock;
	struct ibv_pd	*pd;
}rdma_buffer_pool;


int initRdmaBufferPool(rdma_buffer_pool *rbp, struct ibv_pd	*pd);
void destroyRdmaBufferPool(rdma_buffer_pool *rbp);
int appendRdmaBufferPool(rdma_buffer_pool *rbp, int size);
rdma_buffer *getRdmaBufferfromPool(rdma_buffer_pool *rbp);
rdma_buffer *getRdmaBufferListfromPool(rdma_buffer_pool *rbp, int count);
void returnRdmaBuffertoPool(rdma_buffer_pool *rbp, rdma_buffer *rbuf);


#endif /* RDMABUFFERPOOL_H_ */
