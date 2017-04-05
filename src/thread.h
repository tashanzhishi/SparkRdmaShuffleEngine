/*
 * Thread.h
 *
 *  Created on: May 17, 2016
 *      Author: yurujie
 */

#ifndef THREAD_H_
#define THREAD_H_

#include <pthread.h>
#include "rdma_log.h"

typedef void * (*thread_func)(void *);

void create_thread(thread_func func, void *arg);

#endif /* THREAD_H_ */
