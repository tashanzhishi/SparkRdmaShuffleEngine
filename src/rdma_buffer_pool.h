#ifndef RDMABUFFERPOOL_H_
#define RDMABUFFERPOOL_H_

#include "rdma_log.h"

#include <stddef.h>
#include <pthread.h>
#include <stdint.h>
#include <infiniband/verbs.h>


#define RDMA_CHUNK_SIZE	  4096
#define RDMA_BUFFER_SIZE  4
#define RDMA_BUF_FLAG     (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE)

struct rdma_chunk_header {
  uint8_t flags;
  uint32_t chunk_len;
  uint32_t data_id;
  uint32_t chunk_num;
} __attribute__((__packed__));

#define RDMA_HEADER_SIZE sizeof(struct rdma_chunk_header)
#define RDMA_BODY_SIZE (RDMA_CHUNK_SIZE - RDMA_HEADER_SIZE)
struct rdma_chunk {
  union {
    uint8_t         chunk[RDMA_CHUNK_SIZE];
    struct {
      struct rdma_chunk_header header;
      uint8_t body[RDMA_BODY_SIZE];
    };
  };
	struct ibv_mr	    *mr;
	struct rdma_chunk *next;
} __attribute__((__packed__));

struct link_node {
	struct rdma_chunk *buffer;
  struct ibv_mr	    *mr;
	struct link_node  *next;
};

struct rdma_buffer_pool {
  struct link_node  *link_head;
	struct rdma_chunk *free_list;
	int               total_size;
	int               free_size;
  struct ibv_pd     *pd;
	pthread_mutex_t   lock;
};


int init_rdma_buffer_pool(struct rdma_buffer_pool *rbp, struct ibv_pd	*pd);
void destroy_rdma_buffer_pool(struct rdma_buffer_pool *rbp);
struct rdma_chunk *get_rdma_chunk_from_pool(struct rdma_buffer_pool *rbp);
struct rdma_chunk *get_rdma_chunk_list_from_pool(struct rdma_buffer_pool *rbp, int count);
void release_rdma_chunk_to_pool(struct rdma_buffer_pool *rbp, struct rdma_chunk *chunk);


#endif /* RDMABUFFERPOOL_H_ */
