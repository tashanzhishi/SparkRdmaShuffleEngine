/*
 * LinkQueue.h
 *
 *  Created on: May 27, 2016
 *      Author: yurujie
 */

#ifndef LINKQUEUE_H_
#define LINKQUEUE_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define MUTEX_LINK_QUEUE_TYPE_DEF(elemType, name) \
	typedef struct name##_queue { \
		elemType			*head; \
		elemType			*tail; \
		int					nodeSize; \
		pthread_mutex_t		lock; \
	}name##_queue;

#define LINK_QUEUE_TYPE(name) name##_queue

typedef struct mutex_link_queue {
	void				*head;
	void				*tail;
	int					nodeSize;
	pthread_mutex_t		lock;
}mutex_link_queue;


int initMutexLinkQueue(mutex_link_queue *que, int nodeSize);
void destroyMutexLinkQueue(mutex_link_queue *que);
void enQueue(mutex_link_queue *que, void *node);
void *deQueue(mutex_link_queue *que);


#endif /* LINKQUEUE_H_ */
