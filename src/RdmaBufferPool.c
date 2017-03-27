/*
 * BufferPool.c
 *
 *  Created on: May 12, 2016
 *      Author: yurujie
 */

#include "RdmaBufferPool.h"
#include <stdlib.h>
#include <infiniband/verbs.h>

int initRdmaBufferPool(rdma_buffer_pool *rbp, struct ibv_pd	*pd) {

	memset(rbp, 0, sizeof(rdma_buffer_pool));
	rbp->pd = pd;
	pthread_mutex_init(&rbp->lock, 0);

	return appendRdmaBufferPool(rbp, APPEND_POOL_SIZE);
}

void destroyRdmaBufferPool(rdma_buffer_pool *rbp) {

	int i;
	link_node *p, *tmp;
	p = rbp->link_head;
	while(p) {
		ibv_dereg_mr(p->rbufs[0].mr);
		free(p->rbufs);
		tmp = p;
		p = p->next;
		free(tmp);
	}
	rbp->freeList = NULL;
	rbp->freeSize = rbp->totalSize = 0;
	pthread_mutex_destroy(&rbp->lock);
}

int appendRdmaBufferPool(rdma_buffer_pool *rbp, int size) {

	int i, nodeCount;
	struct ibv_mr *mr;
	link_node *node;

	nodeCount = (size / APPEND_POOL_SIZE) + ((size % APPEND_POOL_SIZE == 0) ? 0 : 1);
	size = nodeCount * APPEND_POOL_SIZE;

	if (size == 0) {
		return -1;
	}

	node = (link_node *)malloc(sizeof(link_node));
	node->rbufs = (rdma_buffer *)malloc(size * sizeof(rdma_buffer));
	if (node->rbufs == NULL) {
		free(node);
		return -1;
	}

	mr = ibv_reg_mr(rbp->pd, node->rbufs, sizeof(rdma_buffer) * size, RDMA_BUF_FLAG);
	if (!mr) {
		rdma_debug("failed to register memory region\n");
		return -1;
	}

	for (i = 0; i < size - 1; i++) {
		node->rbufs[i].mr = mr;
		node->rbufs[i].next = &node->rbufs[i+1];
	}
	node->rbufs[size - 1].mr = mr;

	pthread_mutex_lock(&rbp->lock);

	node->rbufs[size - 1].next = rbp->freeList;
	rbp->freeList = node->rbufs;
	rdma_debug("rbp->freeList = 0x%016x\n", rbp->freeList);

	node->next = rbp->link_head;
	rbp->link_head = node;

	rbp->totalSize += size;
	rbp->freeSize += size;

	/*
	rdma_buffer *test = rbp->freeList;
	int index = 0;
	while (test) {
		index++;
		if (index > rbp->freeSize) {
			rdma_debug("appendRdmaBufferPool: free list size don't match!\n");
			break;
		}
		test = test->next;
	}
	rdma_debug("appendRdmaBufferPool: free list size is %d\n", rbp->freeSize);*/

	pthread_mutex_unlock(&rbp->lock);

	return 0;
}

rdma_buffer *getRdmaBufferfromPool(rdma_buffer_pool *rbp) {

	rdma_buffer *rbuf;

	if (!rbp->freeList) {
		appendRdmaBufferPool(rbp, APPEND_POOL_SIZE);
	}

	pthread_mutex_lock(&rbp->lock);

	rbuf = rbp->freeList;
	rbp->freeList = rbp->freeList->next;
	rbuf->next = NULL;
	rbp->freeSize --;

	/*
	rdma_buffer *test = rbp->freeList;
	int index = 0;
	while (test) {
		index++;
		if (index > rbp->freeSize) {
			rdma_debug("getRdmaBufferfromPool: free list size don't match!\n");
			break;
		}
		test = test->next;
	}
	rdma_debug("getRdmaBufferfromPool: free list size is %d\n", rbp->freeSize);*/

	pthread_mutex_unlock(&rbp->lock);

	return rbuf;
}

rdma_buffer *getRdmaBufferListfromPool(rdma_buffer_pool *rbp, int count) {

	rdma_buffer *rbuf, *tmp;
	int i;

	if (rbp->freeSize < count) {
		appendRdmaBufferPool(rbp, count - rbp->freeSize);
	}

	pthread_mutex_lock(&rbp->lock);

	rbuf = tmp = rbp->freeList;
	for (i = 0; i < (count - 1); i++) {
		if (!tmp) {
			rdma_debug("get rdma buffer list failed\n");
			pthread_mutex_unlock(&rbp->lock);
			return NULL;
		}
		//rdma_debug("buffer list %d: %016p\n", i, tmp);
		tmp = tmp->next;
	}
	rbp->freeList = tmp->next;
	tmp->next = NULL;
	rbp->freeSize -= count;

	/*
	rdma_buffer *test = rbp->freeList;
	int index = 0;
	while (test) {
		index++;
		if (index > rbp->freeSize) {
			rdma_debug("getRdmaBufferListfromPool: free list size don't match!\n");
			break;
		}
		test = test->next;
	}
	rdma_debug("getRdmaBufferListfromPool: free list size is %d\n", rbp->freeSize);*/

	pthread_mutex_unlock(&rbp->lock);

	return rbuf;
}

void returnRdmaBuffertoPool(rdma_buffer_pool *rbp, rdma_buffer *rbuf) {

	if (!rbuf) {
		return;
	}

	pthread_mutex_lock(&rbp->lock);

	rbuf->next = rbp->freeList;
	rbp->freeList = rbuf;
	rbp->freeSize ++;

	pthread_mutex_unlock(&rbp->lock);
}
