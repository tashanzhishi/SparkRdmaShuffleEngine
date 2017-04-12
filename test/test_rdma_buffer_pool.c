#include <stdio.h>
#include "rdma_buffer_pool.h"
#include "rdma_utils.h"

extern struct rdma_context *g_rdma_context;

void test_buffer_pool_size()
{
  int cnt = 0;
  struct link_node *next1 = g_rdma_context->rbp->link_head;
  while (next1) {
    cnt ++;
    next1 = next1->next;
  }
  printf("test: buffer pool nodes=%d ", cnt);
  struct rdma_chunk *next2 = g_rdma_context->rbp->free_list;
  cnt = 0;
  while (next2) {
    cnt ++;
    next2 = next2->next;
  }
  printf("free_size=(%d, %d), total_size=%d\n", \
      g_rdma_context->rbp->free_size, cnt, g_rdma_context->rbp->total_size);
}

int main()
{
  rdma_context_init();

  // test rdma buffer pool api
  test_buffer_pool_size();
  int len = RDMA_BUFFER_SIZE*2+1;
  struct rdma_chunk *chunks[RDMA_BUFFER_SIZE*2+1];
  for (int i=0; i<len; i++) {
    chunks[i] = get_rdma_chunk_from_pool(g_rdma_context->rbp);
    printf("test: now get a chunk from pool\n");
  }
  test_buffer_pool_size();
  for (int i=0; i<len; i++) {
    release_rdma_chunk_to_pool(g_rdma_context->rbp, chunks[i]);
    printf("test: now return a chunk to pool\n");
  }
  test_buffer_pool_size();
  len *= 2;
  struct rdma_chunk *chunk = get_rdma_chunk_list_from_pool(g_rdma_context->rbp, len);
  test_buffer_pool_size();
  for (int i=0; i<len; i++) {
    struct rdma_chunk *tmp = chunk->next;
    release_rdma_chunk_to_pool(g_rdma_context->rbp, chunk);
    chunk = tmp;
  }
  test_buffer_pool_size();

  rdma_context_destroy(g_rdma_context);
  return 0;
}
