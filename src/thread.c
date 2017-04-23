#include "thread.h"

#include <string.h>
#include <stdlib.h>

pthread_t create_thread(thread_func func, void *arg) {
  pthread_t thread;
  pthread_attr_t attr;
  int ret;

  pthread_attr_init(&attr);
  if ((ret = pthread_create(&thread, &attr, func, arg)) < 0) {
    LOG(ERROR, "Can't create thread: %s", strerror(ret));
    abort();
  }
  return thread;
}

void quit_thread(int signo) {
  LOG(DEBUG, "thread %lu will exit", pthread_self()%100);
  pthread_exit(NULL);
}