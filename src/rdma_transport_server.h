#ifndef RDMA_TRANSPORT_SERVER_H_
#define RDMA_TRANSPORT_SERVER_H_

#include "rdma_utils.h"
#include "thread.h"

#define MAX_EVENT_PER_POLL 64

typedef struct varray_t {
  uint32_t data_id;
  uint32_t len;
  uint32_t size;
  struct rdma_chunk* data[0];
} varray_t;
#define VARRY_MALLOC0(len) ((varray_t *)calloc(1, sizeof(varray_t)+(len)))

/*struct rdma_transport_server {
  struct rdma_transport transport;
  char local_ip[IP_CHAR_SIZE];
  char remote_ip[IP_CHAR_SIZE];
  GQueue *cache;
  pthread_mutex_t cache_lock;
};*/


int init_server(const char *host, uint16_t port);
void destroy_server();

#endif /* RDMA_TRANSPORT_SERVER_H_ */
