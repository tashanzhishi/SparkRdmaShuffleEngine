/*
 * Thread.h
 *
 *  Created on: May 17, 2016
 *      Author: yurujie
 */

#ifndef SPARKRDMASHUFFLEENGINE_THREAD_H
#define SPARKRDMASHUFFLEENGINE_THREAD_H

#include <pthread.h>
#include "rdma_log.h"

typedef void * (*thread_func)(void *);

pthread_t create_thread(thread_func func, void *arg);
void quit_thread(int signo);

#endif /* SPARKRDMASHUFFLEENGINE_THREAD_H */
