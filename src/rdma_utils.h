#ifndef RDMA_UTILS_H_
#define RDMA_UTILS_H_

#include <stdbool.h>
#include <glib.h>

#include "rdma_buffer_pool.h"
#include "rdma_log.h"

#define MAX_POLL_THREAD 4
#define IB_PORT_NUM 1
#define MAX_CQE 1024
#define IB_SERVER_PORT 6789

struct qp_attr {
  uint64_t gid_global_interface_id;	// Store the gid fields separately because I
  uint64_t gid_global_subnet_prefix; 	// don't like unions. Needed for RoCE only
  int lid; // A queue pair is identified by the local id (lid)
  int qpn; // of the device port and its queue pair number (qpn)
  int psn;
};

struct rdma_context {
  struct ibv_context *context;
  struct ibv_pd *pd;
  struct ibv_cq *cq[MAX_POLL_THREAD];
  int cq_num;
  pthread_mutex_t cq_lock;
  struct rdma_buffer_pool *rbp;
  GHashTable *hash_table;
};

struct rdma_transport {
  struct qp_attr local_qp_attr;
  struct qp_attr remote_qp_attr;
  int cq_id;
  struct ibv_qp *rc_qp;
};


int rdma_context_init();
void rdma_context_destroy(struct rdma_context *context);
int exchange_info(int sfd, struct rdma_transport *transport, bool is_client);
struct rdma_transport *rdma_transport_create();


#endif /* RDMA_UTILS_H_ */
