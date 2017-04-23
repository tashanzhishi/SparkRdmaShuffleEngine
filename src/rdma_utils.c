#include "rdma_utils.h"

#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "thread.h"


struct rdma_context *g_rdma_context;


static void free_hash_data(gpointer kv);
static void free_context_hash_value(gpointer data);

static union ibv_gid get_gid(struct ibv_context *context);
static uint16_t get_local_lid(struct ibv_context *context);


int rdma_context_init() {
  LOG(DEBUG, "rdma_context_init begin");
  struct ibv_device **dev_list;
  struct ibv_device *ib_dev;

  g_rdma_context = (struct rdma_context *) calloc(1, sizeof(struct rdma_context));
  GPR_ASSERT(g_rdma_context);

  srand((unsigned int) time(NULL));

  dev_list = ibv_get_device_list(NULL);
  GPR_ASSERT(dev_list);

  ib_dev = dev_list[0];
  GPR_ASSERT(ib_dev);

  g_rdma_context->context = ibv_open_device(ib_dev);
  GPR_ASSERT(g_rdma_context->context);

  g_rdma_context->pd = ibv_alloc_pd(g_rdma_context->context);
  GPR_ASSERT(g_rdma_context->pd);

  //pthread_mutex_init(&g_rdma_context->cq_lock, NULL);
  //g_rdma_context->cq_num = 0;

  g_rdma_context->rbp = (struct rdma_buffer_pool *) malloc(sizeof(struct rdma_buffer_pool));
  GPR_ASSERT(g_rdma_context->rbp);
  if (init_rdma_buffer_pool(g_rdma_context->rbp, g_rdma_context->pd) < 0) {
    LOG(ERROR, "failed to initiate buffer pool");
    abort();
  }

  pthread_mutex_init(&g_rdma_context->hash_lock, NULL);
  g_rdma_context->hash_table = g_hash_table_new_full(g_str_hash, g_int64_equal, free_hash_data, free_hash_data);
  g_rdma_context->host2ipstr = g_hash_table_new_full(g_str_hash, g_str_equal, free_hash_data, free_hash_data);

  LOG(DEBUG, "rdma_context_init end");
  return 0;
}

void rdma_context_destroy(struct rdma_context *context)
{
  destroy_rdma_buffer_pool(context->rbp);

  GPR_ASSERT(context->pd);
  ibv_dealloc_pd(context->pd);
  context->pd = NULL;

  GPR_ASSERT(context->context);
  ibv_close_device(context->context);
  context->context = NULL;

  GPR_ASSERT(context->hash_table);
  // must be called after ibv_destroy_qp(), because the value (struct rdma_transport *)'s
  // element: rc_qp should be free firstly
  g_hash_table_destroy(context->hash_table);
  context->hash_table = NULL;
  g_hash_table_destroy(context->host2ipstr);
  pthread_mutex_destroy(&context->hash_lock);

  free(context);
  LOG(INFO, "destroy rdma success");
}

// client and server exchange information, it is a blocking process
int exchange_info(int sfd, struct qp_attr *local_qp_attr, struct qp_attr *remote_qp_attr,
                  struct ibv_qp *qp, bool is_client)
{
  LOG(DEBUG, "exchange_info begin");

  //union ibv_gid gid = get_gid(g_rdma_context->context);
  //local_qp_attr->gid_global_interface_id = gid.global.interface_id;
  //local_qp_attr->gid_global_subnet_prefix = gid.global.subnet_prefix;
  local_qp_attr->lid = get_local_lid(g_rdma_context->context);
  local_qp_attr->qpn = qp->qp_num;
  //local_qp_attr->psn = rand() & 0xffffff;
  local_qp_attr->psn = 0;
  struct qp_attr tmp;

  if (is_client) {
    tmp.lid = htons(local_qp_attr->lid);
    tmp.qpn = htonl(local_qp_attr->qpn);
    tmp.psn = htonl(local_qp_attr->psn);
    if (write(sfd, &tmp, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "client exchange_info: failed to write information");
      return -1;
    }

    if (read(sfd, &tmp, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "client exchange_info: failed to read information");
      return -1;
    }
    remote_qp_attr->lid = ntohs(tmp.lid);
    remote_qp_attr->qpn = ntohl(tmp.qpn);
    remote_qp_attr->psn = ntohl(tmp.psn);
  } else {
    if (read(sfd, &tmp, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "server exchange_info: failed to read information");
      return -1;
    }
    remote_qp_attr->lid = ntohs(tmp.lid);
    remote_qp_attr->qpn = ntohl(tmp.qpn);
    remote_qp_attr->psn = ntohl(tmp.psn);

    tmp.lid = htons(local_qp_attr->lid);
    tmp.qpn = htonl(local_qp_attr->qpn);
    tmp.psn = htonl(local_qp_attr->psn);
    if (write(sfd, &tmp, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "server exchange_info: failed to write information");
      return -1;
    }
  }

  LOG(DEBUG, "exchange_info end");
  return 0;
}

int modify_qp_to_init(struct ibv_qp *qp) {
  struct ibv_qp_attr init_attr;
  memset(&init_attr, 0, sizeof(init_attr));
  init_attr.qp_state        = IBV_QPS_INIT;
  init_attr.pkey_index      = 0;
  init_attr.port_num        = IB_PORT_NUM;
  init_attr.qp_access_flags = RDMA_BUF_FLAG;
  int init_flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

  if (ibv_modify_qp(qp, &init_attr, init_flags) < 0) {
    LOG(ERROR, "modify_qp_to_init: failed to modify QP to INIT, %s", strerror(errno));
    return -1;
  }
  return 0;
}

int modify_qp_to_rts(struct ibv_qp *qp, struct qp_attr *local_qp_attr) {
  struct ibv_qp_attr rts_attr;
  memset(&rts_attr, 0, sizeof(rts_attr));
  rts_attr.qp_state = IBV_QPS_RTS;
  rts_attr.timeout  = 3;
  rts_attr.retry_cnt = 3;
  rts_attr.rnr_retry = 3;
  rts_attr.max_rd_atomic = 0; // rc must add it
  rts_attr.sq_psn   = local_qp_attr->psn;

  int rts_flags = IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC;
  if (ibv_modify_qp(qp, &rts_attr, rts_flags) < 0) {
    LOG(ERROR, "modify QP to RTS error, %s", strerror(errno));
    return -1;
  }
  return 0;
}

int modify_qp_to_rtr(struct ibv_qp *qp, struct qp_attr *remote_qp_attr) {
  struct ibv_qp_attr rtr_attr;
  memset(&rtr_attr, 0, sizeof(rtr_attr));
  rtr_attr.qp_state = IBV_QPS_RTR;
  rtr_attr.path_mtu = IBV_MTU_4096;
  rtr_attr.min_rnr_timer = 3;
  rtr_attr.max_dest_rd_atomic = 0; // rc must add it
  rtr_attr.dest_qp_num = remote_qp_attr->qpn;
  rtr_attr.rq_psn = remote_qp_attr->psn;
  rtr_attr.ah_attr.is_global = 0;
  rtr_attr.ah_attr.dlid = remote_qp_attr->lid;
  rtr_attr.ah_attr.sl = 0;
  rtr_attr.ah_attr.src_path_bits = 0;
  rtr_attr.ah_attr.port_num = IB_PORT_NUM;

  int rtr_flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
      IBV_QP_RQ_PSN | IBV_QP_MIN_RNR_TIMER | IBV_QP_MAX_DEST_RD_ATOMIC;
  if (ibv_modify_qp(qp, &rtr_attr, rtr_flags) < 0) {
    LOG(ERROR, "modify QP to RTS error, %s", strerror(errno));
    return -1;
  }
  return 0;
}


// the return is ip, but the user must free it yourself.
void set_ip_from_host(const char *host, char *ip_str) {
  struct hostent *he = gethostbyname(host);
  if (he == NULL) {
    LOG(ERROR, "gethostbyname: %s failed.", host);
    abort();
  }
  inet_ntop(he->h_addrtype, he->h_addr, ip_str, IP_CHAR_SIZE);
}

void set_local_ip(char *ip_str) {
  char host_name[32] = {'\0'};
  if (gethostname(host_name, sizeof(host_name)) < 0) {
    LOG(ERROR, "gethostname error, %s", strerror(errno));
    abort();
  }
  set_ip_from_host(host_name, ip_str);
}

/************************************************************************/
/*                          local function                              */
/************************************************************************/


static void free_hash_data(gpointer kv) {
  g_free(kv);
}

static union ibv_gid get_gid(struct ibv_context *context) {
  union ibv_gid ret_gid;
  if (ibv_query_gid(context, IB_PORT_NUM, 0, &ret_gid) < 0) {
    LOG(ERROR, "ibv_query_gid error: %s", strerror(errno));
  }
  return ret_gid;
}

static uint16_t get_local_lid(struct ibv_context *context) {
  struct ibv_port_attr attr;
  if (ibv_query_port(context, IB_PORT_NUM, &attr) < 0) {
    LOG(ERROR, "ibv_query_port error: %s", strerror(errno));
    return 0;
  }
  return attr.lid;
}
