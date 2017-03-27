/*
 * Thread.c
 *
 *  Created on: May 17, 2016
 *      Author: yurujie
 */

#include "Thread.h"

void createThread(ThreadProc threadProc, void *arg) {

    pthread_t       thread;
    pthread_attr_t  attr;
    int             ret;

    pthread_attr_init(&attr);

    if ((ret = pthread_create(&thread, &attr, threadProc, arg)) != 0) {
        rdma_debug("Can't create thread: %s\n", strerror(ret));
    }
}
