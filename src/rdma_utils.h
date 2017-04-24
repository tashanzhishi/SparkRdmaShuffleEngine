#ifndef SPARKRDMASHUFFLEENGINE_RDMA_UTILS_H
#define SPARKRDMASHUFFLEENGINE_RDMA_UTILS_H

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
#define MAX_EVENT_PER_POLL 128

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
  struct rdma_buffer_pool *rbp;

  GHashTable *hash_table;
  GHashTable *host2ipstr;
  pthread_mutex_t hash_lock;

  GThreadPool *work_thread_pool;

  int sfd;
};

typedef int (*create_transport_fun)(const char *ip_str, uint16_t port);


int rdma_context_init();
void rdma_context_destroy(struct rdma_context *context);

int exchange_info(int sfd, struct qp_attr *local_qp_attr, struct qp_attr *remote_qp_attr,
                  struct ibv_qp *qp, bool is_client);
int modify_qp_to_init(struct ibv_qp *qp);
int modify_qp_to_rts(struct ibv_qp *qp, struct qp_attr *local_qp_attr);
int modify_qp_to_rtr(struct ibv_qp *qp, struct qp_attr *remote_qp_attr);

// socket util function
void set_ip_from_host(const char *host, char *ip_str);
void set_local_ip(char *ip_str);

#endif /* SPARKRDMASHUFFLEENGINE_RDMA_UTILS_H */
