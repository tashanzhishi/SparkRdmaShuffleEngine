//
// Created by wyb on 17-4-23.
//

#ifndef SPARKRDMASHUFFLEENGINE_RDMA_TRANSPORT_H
#define SPARKRDMASHUFFLEENGINE_RDMA_TRANSPORT_H

#include <stdbool.h>
#include <glib.h>

#include "rdma_buffer_pool.h"
#include "rdma_log.h"
#include "rdma_utils.h"


struct rdma_transport {
  uint8_t is_ready;

  struct qp_attr local_qp_attr;
  struct qp_attr remote_qp_attr;
  struct ibv_comp_channel *comp_channel;
  struct ibv_cq  *cq;
  struct ibv_qp  *rc_qp;

  char local_ip[IP_CHAR_SIZE];
  char remote_ip[IP_CHAR_SIZE];


  // for client //
  uint32_t data_id;
  pthread_mutex_t id_lock;


  // for server //
  pthread_t poll_pid;

  // handle thread <--> poll thread
  pthread_t handle_pid;
  GQueue *handle_quque;
  pthread_mutex_t handle_queue_lock;
  pthread_cond_t handle_cv;
  pthread_mutex_t handle_cv_lock;
  volatile int is_handle_running;

  // handle by work thread pool
  GQueue *work_queue;
  pthread_mutex_t work_queue_lock;
  volatile int is_work_running;
};

struct rdma_work_chunk {
  struct rdma_transport *transport;
  struct rdma_chunk     *chunk;
  uint32_t              len;
};

typedef struct varray_t {
  struct rdma_transport *transport;
  uint32_t data_id;
  uint32_t len;
  uint32_t size;
  struct rdma_chunk* data[0];
} varray_t;
#define VARRY_MALLOC0(len) ((varray_t *)calloc(1, sizeof(varray_t)+(sizeof(void*)*(len))))

typedef struct dynarray_t {
  int len;
  uint32_t size;
  uint64_t user_id;
  void* data[0];
} dynarray_t;
#define DYNARRY_MALLOC0(len) ((dynarray_t *)calloc(1, sizeof(dynarray_t)+(len)))
#define DYNARRY_MALLOC(len) ((dynarray_t *)malloc(sizeof(dynarray_t)+(len)))

struct ip_hash_value {
  struct rdma_transport *transport;
  pthread_mutex_t connect_lock;
};



int rdma_create_connect(struct rdma_transport *transport);
void rdma_complete_connect(struct rdma_transport *transport);
void rdma_shutdown_connect(struct rdma_transport *transport);

void rdma_transport_recv_with_num(struct rdma_transport *transport, uint32_t num);
int rdma_transport_recv(struct rdma_transport *transport);
int rdma_transport_send(struct rdma_transport *transport, struct rdma_work_chunk *send_wc);

void work_thread(gpointer data, gpointer user_data);


#endif //SPARKRDMASHUFFLEENGINE_RDMA_TRANSPORT_H
