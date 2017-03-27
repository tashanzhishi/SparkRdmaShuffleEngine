/*
 * Thread.h
 *
 *  Created on: May 17, 2016
 *      Author: yurujie
 */

#ifndef THREAD_H_
#define THREAD_H_

#include <pthread.h>
#include "RdmaCommon.h"

typedef void * (*ThreadProc)(void *);

void createThread(ThreadProc threadProc, void *arg);

#endif /* THREAD_H_ */
