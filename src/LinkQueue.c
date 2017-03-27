/*
 * LinkQueue.c
 *
 *  Created on: May 27, 2016
 *      Author: yurujie
 */

#include "LinkQueue.h"
#include "RdmaCommon.h"

int initMutexLinkQueue(mutex_link_queue *que, int nodeSize) {

	que->head = que->tail = NULL;
	que->nodeSize = nodeSize;
	pthread_mutex_init(&que->lock, 0);
	return 0;
}

void destroyMutexLinkQueue(mutex_link_queue *que) {

	pthread_mutex_destroy(&que->lock);
}

void enQueue(mutex_link_queue *que, void *node) {

	if (!node) {
		printf("invalid parameter.\n");
		return;
	}

	*(void **)(node + que->nodeSize - sizeof(void *)) = 0;
	pthread_mutex_lock(&que->lock);
	if (!que->tail) {
		que->head = que->tail = node;
	}
	else {
		//memcpy(que->tail+que->nodeSize-sizeof(void *), &node, sizeof(void *));
		*(void **)(que->tail + que->nodeSize - sizeof(void *)) = node;
		que->tail = node;
	}
	pthread_mutex_unlock(&que->lock);
}

void *deQueue(mutex_link_queue *que) {

	void *ret;

	pthread_mutex_lock(&que->lock);
	ret = que->head;
	if (ret) {
		que->head = *(void **)(que->head + que->nodeSize - sizeof(void *));
		*(void **)(ret + que->nodeSize - sizeof(void *)) = 0;
		if (!que->head) {
			que->tail = NULL;
		}
	}
	pthread_mutex_unlock(&que->lock);

	return ret;
}
