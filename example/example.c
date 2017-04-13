#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#include "../src/rdma_transport_client.h"
#include "../src/rdma_transport_server.h"

#define MSG_SIZE (1024 * 1024)

char local_ip[]  = "127.0.0.1";
char remote_ip[] = "172.18.0.12";
uint16_t port = 12345;

void *server_thread(void *arg) {
  init_server(local_ip, port);
  return NULL;
}
void *client_thread(void *arg) {
  char msg[] = "hello, world";
  send_msg(remote_ip, port, msg, strlen(msg));
  return NULL;
}

int main(int argc, char **argv)
{

  pthread_t pid1, pid2;
  pthread_attr_t attr;
  int ret;

  pthread_attr_init(&attr);
  if ((ret = pthread_create(&pid1, &attr, server_thread, NULL)) < 0) {
    printf("Can't create thread: %s", strerror(ret));
    abort();
  }
  /*sleep(1);
  if ((ret = pthread_create(&pid2, &attr, client_thread, NULL)) < 0) {
    printf("Can't create thread: %s", strerror(ret));
    abort();
  }*/

  sleep(100);
	return 0;
}
