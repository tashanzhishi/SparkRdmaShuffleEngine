#include "rdma_buffer_pool.h"

#include <stdlib.h>
#include <string.h>

static int append_rdma_buffer_pool(struct rdma_buffer_pool *rbp, uint32_t size);

int init_rdma_buffer_pool(struct rdma_buffer_pool *rbp, struct ibv_pd	*pd) {
	memset(rbp, 0, sizeof(struct rdma_buffer_pool));
	rbp->pd = pd;
	pthread_mutex_init(&rbp->lock, 0);
	return append_rdma_buffer_pool(rbp, RDMA_BUFFER_SIZE);
}

void destroy_rdma_buffer_pool(struct rdma_buffer_pool *rbp) {
	struct link_node *p, *tmp;
	p = rbp->link_head;
	while(p) {
		ibv_dereg_mr(p->buffer->mr);
		free(p->buffer);
		tmp = p;
		p = p->next;
		free(tmp);
	}
	rbp->free_list = NULL;
	rbp->free_size = rbp->total_size = 0;
	pthread_mutex_destroy(&rbp->lock);
  LOG(INFO, "destroy buffer pool");
}

struct rdma_chunk *get_rdma_chunk_from_pool(struct rdma_buffer_pool *rbp) {
	struct rdma_chunk *chunk;

	if (rbp->free_list == NULL) {
    if (append_rdma_buffer_pool(rbp, RDMA_BUFFER_SIZE) < 0) {
      LOG(ERROR, "when get chunk from pool, the free list is null, and append failed.");
      return NULL;
    }
	}

	pthread_mutex_lock(&rbp->lock);

  chunk = rbp->free_list;
	rbp->free_list = rbp->free_list->next;
  chunk->next = NULL;
	rbp->free_size --;

	pthread_mutex_unlock(&rbp->lock);

	return chunk;
}

struct rdma_chunk *get_rdma_chunk_list_from_pool(struct rdma_buffer_pool *rbp, uint32_t count) {
  if (count == 0) {
    return NULL;
  }

  // has bug
  while (rbp->free_size < count) {
    if (append_rdma_buffer_pool(rbp, RDMA_BUFFER_SIZE) < 0) {
      LOG(ERROR, "when get chunk from pool, the free list is null, and append failed.");
      pthread_mutex_unlock(&rbp->lock);
      return NULL;
    }
	}

  pthread_mutex_lock(&rbp->lock);

  struct rdma_chunk *chunk = rbp->free_list;
  struct rdma_chunk *now = chunk;
	for (uint32_t i = 0; i < count - 1; i++) {
		if (now == NULL) {
			LOG(ERROR, "get rdma buffer list failed");
			pthread_mutex_unlock(&rbp->lock);
			return NULL;
		}
		now = now->next;
	}
	rbp->free_list = now->next;
	now->next = NULL;
	rbp->free_size -= count;

	pthread_mutex_unlock(&rbp->lock);

	return chunk;
}

void release_rdma_chunk_to_pool(struct rdma_buffer_pool *rbp, struct rdma_chunk *chunk) {
	if (chunk == NULL) {
		return;
	}

	pthread_mutex_lock(&rbp->lock);

  chunk->next = rbp->free_list;
	rbp->free_list = chunk;
	rbp->free_size ++;

	pthread_mutex_unlock(&rbp->lock);
}


static int append_rdma_buffer_pool(struct rdma_buffer_pool *rbp, uint32_t size) {
  uint32_t buffer_cnt = (size / RDMA_BUFFER_SIZE) + ((size % RDMA_BUFFER_SIZE == 0)? 0: 1);
  size = buffer_cnt * RDMA_BUFFER_SIZE;
  LOG(DEBUG, "append rdma buffer pool: %u", size);
  if (size == 0) {
    return -1;
  }

  struct link_node *node = (struct link_node *)calloc(1, sizeof(struct link_node));
  node->buffer = (struct rdma_chunk *)malloc(size * sizeof(struct rdma_chunk));
  if (node->buffer == NULL) {
    free(node);
    LOG(ERROR, "malloc buffer failed.");
    return -1;
  }

  struct ibv_mr *mr = ibv_reg_mr(rbp->pd, node->buffer, sizeof(struct rdma_chunk) * size, RDMA_BUF_FLAG);
  if (mr == NULL) {
    LOG(ERROR, "failed to register memory region");
    return -1;
  }

  for (int i = 0; i < size - 1; i++) {
    node->buffer[i].mr = mr;
    node->buffer[i].next = &node->buffer[i+1];
  }
  node->buffer[size - 1].mr = mr;
  node->mr = mr;

  pthread_mutex_lock(&rbp->lock);

  node->buffer[size - 1].next = rbp->free_list;
  rbp->free_list = node->buffer;

  node->next = rbp->link_head;
  rbp->link_head = node;

  rbp->total_size += size;
  rbp->free_size += size;

  pthread_mutex_unlock(&rbp->lock);

  return 0;
}