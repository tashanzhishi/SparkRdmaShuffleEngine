#ifndef SPARKRDMASHUFFLEENGINE_RDMA_TRANSPORT_SERVER_H
#define SPARKRDMASHUFFLEENGINE_RDMA_TRANSPORT_SERVER_H

#include <stdint.h>

/*struct rdma_transport_server {
  struct rdma_transport transport;
  char local_ip[IP_CHAR_SIZE];
  char remote_ip[IP_CHAR_SIZE];
  GQueue *cache;
  pthread_mutex_t cache_lock;
};*/


int init_server(const char *host, uint16_t port);
void destroy_server();

#endif /* SPARKRDMASHUFFLEENGINE_RDMA_TRANSPORT_SERVER_H */
