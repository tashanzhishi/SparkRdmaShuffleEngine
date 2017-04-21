#ifndef RDMA_UTILS_H_
#define RDMA_UTILS_H_

#include <stdbool.h>
#include <glib.h>

#include "rdma_buffer_pool.h"
#include "rdma_log.h"

#define MAX_POLL_THREAD 4
#define IB_PORT_NUM 1
#define MAX_CQE 2048
#define MAX_PRE_RECV_QP (1024)
#define IB_SERVER_PORT 6789
#define IP_CHAR_SIZE 20
#define THREAD_POOL_SIZE 10
#define MAX_EVENT_PER_POLL 64

struct qp_attr {
  //uint64_t gid_global_interface_id;	  // Store the gid fields separately because I
  //uint64_t gid_global_subnet_prefix; 	// don't like unions. Needed for RoCE only
  uint16_t lid;                       // A queue pair is identified by the local id (lid)
  uint32_t qpn;                       // of the device port and its queue pair number (qpn)
  uint32_t psn;
};

struct rdma_context {
  struct ibv_context *context;
  struct ibv_pd *pd;

  //int cq_num;
  //pthread_mutex_t cq_lock;
  //struct ibv_cq *cq[MAX_POLL_THREAD];
  //struct ibv_comp_channel *comp_channel[MAX_POLL_THREAD];
  //pthread_t poll_ids[MAX_POLL_THREAD];
  //pthread_t work_ids[MAX_POLL_THREAD];
  //GQueue *work_queue[MAX_POLL_THREAD];
  //pthread_mutex_t queue_lock[MAX_POLL_THREAD];

  struct rdma_buffer_pool *rbp;

  GHashTable *hash_table;
  GHashTable *host2ipstr;
  pthread_mutex_t hash_lock;

  GThreadPool *thread_pool;

  int sfd;
};

struct rdma_transport {
  uint8_t creating;
  struct qp_attr local_qp_attr;
  struct qp_attr remote_qp_attr;
  int            cq_id;
  struct ibv_comp_channel *comp_channel;
  struct ibv_cq  *cq;
  struct ibv_qp  *rc_qp;

  char local_ip[IP_CHAR_SIZE];
  char remote_ip[IP_CHAR_SIZE];

  // for client
  uint32_t data_id;
  pthread_mutex_t id_lock;

  // for server
  pthread_t poll_id;
  GQueue *work_queue;
  pthread_mutex_t queue_lock;
  pthread_t work_id;
  pthread_cond_t cv;
  pthread_mutex_t cv_lock;
  volatile int running;

  GHashTable *cache;
  void *recvk_array;
};


struct ip_hash_value {
  struct rdma_transport *transport;
  pthread_mutex_t connect_lock;
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

typedef int (*create_transport_fun)(const char *ip_str, uint16_t port);


int rdma_context_init();
void rdma_context_destroy(struct rdma_context *context);

int exchange_info(int sfd, struct rdma_transport *transport, bool is_client);

int rdma_create_connect(struct rdma_transport *transport);
void rdma_complete_connect(struct rdma_transport *transport);
void rdma_shutdown_connect(struct rdma_transport *transport);

int rdma_transport_recv(struct rdma_transport *transport);
int rdma_transport_send(struct rdma_transport *transport, struct rdma_work_chunk *send_wc);

// socket util function
void set_ip_from_host(const char *host, char *ip_str);
void set_local_ip(char *ip_str);

//struct rdma_transport *get_transport_from_ip(const char *ip_str, uint16_t port,
//                                             create_transport_fun create_transport);


#endif /* RDMA_UTILS_H_ */
