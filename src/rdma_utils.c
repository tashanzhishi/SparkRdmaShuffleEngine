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



static void free_hash_data(gpointer kv);
static union ibv_gid get_gid(struct ibv_context *context);
static uint16_t get_local_lid(struct ibv_context *context);
static int modify_qp_to_init(struct rdma_transport *transport);
static int modify_qp_to_rts(struct rdma_transport *transport);
static int modify_qp_to_rtr(struct rdma_transport *transport);

struct rdma_context *g_rdma_context;

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

  for (int i=0; i<MAX_POLL_THREAD; i++) {
    g_rdma_context->comp_channel[i] = ibv_create_comp_channel(g_rdma_context->context);
    GPR_ASSERT(g_rdma_context->comp_channel[i]);
    g_rdma_context->cq[i] = ibv_create_cq(g_rdma_context->context, MAX_CQE + 1,
                                          NULL, g_rdma_context->comp_channel[i], 0);
    GPR_ASSERT(g_rdma_context->cq[i]);
    if (ibv_req_notify_cq(g_rdma_context->cq[i], 0) < 0) {
      LOG(ERROR, "ibv_req_notify_cq error, %s", strerror(errno));
      abort();
    }
  }

  pthread_mutex_init(&g_rdma_context->cq_lock, NULL);
  g_rdma_context->cq_num = 0;

  g_rdma_context->rbp = (struct rdma_buffer_pool *) malloc(sizeof(struct rdma_buffer_pool));
  GPR_ASSERT(g_rdma_context->rbp);
  if (init_rdma_buffer_pool(g_rdma_context->rbp, g_rdma_context->pd) < 0) {
    LOG(ERROR, "failed to initiate buffer pool");
    abort();
  }

  pthread_mutex_init(&g_rdma_context->hash_lock, NULL);
  g_rdma_context->hash_table = g_hash_table_new_full(g_str_hash, g_int64_equal, free_hash_data, free_hash_data);

  LOG(DEBUG, "rdma_context_init end");
  return 0;
}

void rdma_context_destroy(struct rdma_context *context)
{
  destroy_rdma_buffer_pool(context->rbp);

  for (int i=0; i<MAX_POLL_THREAD; i++) {
    GPR_ASSERT(context->cq[i]);
    GPR_ASSERT(context->comp_channel[i]);
    ibv_destroy_cq(context->cq[i]);
    ibv_destroy_comp_channel(context->comp_channel[i]);
    context->cq[i] = NULL;
    context->comp_channel[i] = NULL;
  }
  pthread_mutex_destroy(&context->cq_lock);

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
  pthread_mutex_destroy(&context->hash_lock);

  free(context);
  LOG(DEBUG, "destroy rdma success");
}

// client and server exchange information, it is a blocking process
int exchange_info(int sfd, struct rdma_transport *transport, bool is_client)
{
  LOG(DEBUG, "exchange_info begin");

  union ibv_gid gid = get_gid(g_rdma_context->context);
  //transport->local_qp_attr.gid_global_interface_id = gid.global.interface_id;
  //transport->local_qp_attr.gid_global_subnet_prefix = gid.global.subnet_prefix;
  transport->local_qp_attr.lid = get_local_lid(g_rdma_context->context);
  transport->local_qp_attr.qpn = transport->rc_qp->qp_num;
  //transport->local_qp_attr.psn = rand() & 0xffffff;
  transport->local_qp_attr.psn = 0;
  struct qp_attr tmp;

  if (is_client) {
    tmp.lid = htons(transport->local_qp_attr.lid);
    tmp.qpn = htonl(transport->local_qp_attr.qpn);
    tmp.psn = htonl(transport->local_qp_attr.psn);
    if (write(sfd, &tmp, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "client exchange_info: failed to write information");
      return -1;
    }
    //LOG(DEBUG, "%s lid = %u, qpn = %u", transport->local_ip, transport->local_qp_attr.lid, transport->local_qp_attr.qpn);

    if (read(sfd, &tmp, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "client exchange_info: failed to read information");
      return -1;
    }
    transport->remote_qp_attr.lid = ntohs(tmp.lid);
    transport->remote_qp_attr.qpn = ntohl(tmp.qpn);
    transport->remote_qp_attr.psn = ntohl(tmp.psn);
    //LOG(DEBUG, "%s lid = %u, qpn = %u", transport->remote_ip, transport->remote_qp_attr.lid, transport->remote_qp_attr.qpn);
  } else {
    if (read(sfd, &tmp, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "server exchange_info: failed to read information");
      return -1;
    }
    transport->remote_qp_attr.lid = ntohs(tmp.lid);
    transport->remote_qp_attr.qpn = ntohl(tmp.qpn);
    transport->remote_qp_attr.psn = ntohl(tmp.psn);
    //LOG(DEBUG, "%s lid = %u, qpn = %u", transport->remote_ip, transport->remote_qp_attr.lid, transport->remote_qp_attr.qpn);
    rdma_complete_connect(transport);

    tmp.lid = htons(transport->local_qp_attr.lid);
    tmp.qpn = htonl(transport->local_qp_attr.qpn);
    tmp.psn = htonl(transport->local_qp_attr.psn);
    if (write(sfd, &tmp, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "server exchange_info: failed to write information");
      return -1;
    }
    //LOG(DEBUG, "%s lid = %u, qpn = %u", transport->local_ip, transport->local_qp_attr.lid, transport->local_qp_attr.qpn);
  }

  LOG(DEBUG, "exchange_info end");
  return 0;
}

int rdma_create_connect(struct rdma_transport *transport) {
  LOG(DEBUG, "rdma_create_connect begin");
  GPR_ASSERT(transport);

  // (data_id, varray) (int64_t*, varray_t *)
  //transport->cache = g_hash_table_new_full(g_int64_hash, g_int64_equal, free_hash_data, NULL);
  transport->work_queue = g_queue_new();
  transport->running = 0;
  pthread_mutex_init(&transport->queue_lock, NULL);

  pthread_mutex_lock(&g_rdma_context->cq_lock);
  struct ibv_cq *cq = g_rdma_context->cq[(g_rdma_context->cq_num++)%MAX_POLL_THREAD];
  pthread_mutex_unlock(&g_rdma_context->cq_lock);
  transport->cq = cq;

  LOG(INFO, "*** now use RC ***");

  struct ibv_qp_init_attr init_attr = {
      .qp_type = IBV_QPT_RC,
      .sq_sig_all = 1,
      .send_cq = cq,
      .recv_cq = cq,
      .srq = NULL,
      .cap = {
          .max_send_wr = MAX_CQE,
          .max_recv_wr = MAX_CQE,
          .max_send_sge = 1,
          .max_recv_sge = 1,
          .max_inline_data = 256,
      },
  };
  transport->rc_qp = ibv_create_qp(g_rdma_context->pd, &init_attr);
  GPR_ASSERT(transport->rc_qp);
  if (modify_qp_to_init(transport) < 0) {
    LOG(ERROR, "rdma_create_connect: failed to modify queue pair to initiate state");
    abort();
  }
  for (int i=0; i<MAX_PRE_RECV_QP; i++) {
    if (rdma_transport_recv(transport) < 0) {
      LOG(ERROR, "complete connect failed");
      abort();
    }
  }
  LOG(DEBUG, "rdma_create_connect end");
  return 0;
}

void rdma_complete_connect(struct rdma_transport *transport) {
  GPR_ASSERT(modify_qp_to_rtr(transport) == 0);
  GPR_ASSERT(modify_qp_to_rts(transport) == 0);
  /*for (int i=0; i<MAX_PRE_RECV_QP; i++) {
    if (rdma_transport_recv(transport) < 0) {
      LOG(ERROR, "complete connect failed");
      abort();
    }
  }*/
  LOG(DEBUG, "%s -> %s complete connect", transport->local_ip, transport->remote_ip);
}

void rdma_shutdown_connect(struct rdma_transport *transport) {
  if (transport->work_queue != NULL) {
    g_queue_free(transport->work_queue);
    pthread_mutex_destroy(&transport->queue_lock);
  }

  GPR_ASSERT(transport->rc_qp);
  if (ibv_destroy_qp(transport->rc_qp) < 0) {
    LOG(ERROR, "ibv_destroy_qp error, %s", strerror(errno));
    abort();
  }
  transport->rc_qp = NULL;

  LOG(DEBUG, "shutdown connect");
}

int rdma_transport_recv(struct rdma_transport *transport) {
  struct rdma_work_chunk *recv_wc = (struct rdma_work_chunk *)malloc(sizeof(struct rdma_work_chunk));
  GPR_ASSERT(recv_wc);
  recv_wc->chunk = get_rdma_chunk_from_pool(g_rdma_context->rbp);
  if (recv_wc->chunk == NULL) {
    LOG(ERROR, "rdma_transport_recv: can't get chunk from pool");
    return -1;
  }
  recv_wc->len = RDMA_CHUNK_SIZE;
  recv_wc->transport = transport;

  struct ibv_sge sge = {
      .addr   = (uint64_t)recv_wc->chunk,
      .length = recv_wc->len,
      .lkey   = recv_wc->chunk->mr->lkey,
  };

  // by the pointer recv_wc->transport, wo can get the pointer of rdma_transport_client/rdma_transport_server
  struct ibv_recv_wr recv_wr = {
      .wr_id   = (uint64_t)recv_wc,
      .sg_list = &sge,
      .num_sge = 1,
  };
  struct ibv_recv_wr *bad_wr = NULL;
  if (ibv_post_recv(transport->rc_qp, &recv_wr, &bad_wr) < 0) {
    LOG(ERROR, "ibv_post_recv error, %s", strerror(errno));
    return -1;
  }
  return 0;
}

int rdma_transport_send(struct rdma_transport *transport, struct rdma_work_chunk *send_wc) {
  struct ibv_sge sge = {
      .addr = (uint64_t)send_wc->chunk,
      .length = send_wc->len + RDMA_HEADER_SIZE,
      .lkey = send_wc->chunk->mr->lkey,
  };
  struct ibv_send_wr send_wr = {
      .wr_id = (uint64_t)send_wc,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_SEND,
      .send_flags = IBV_SEND_SIGNALED,
  };
  struct ibv_send_wr *bad_wr = NULL;
  if (ibv_post_send(transport->rc_qp, &send_wr, &bad_wr) < 0) {
    LOG(ERROR, "ibv_post_send error, %s", strerror(errno));
    return -1;
  }
  //LOG(DEBUG, "post send %u byte", send_wc->len+RDMA_HEADER_SIZE);

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

struct rdma_transport *get_transport_from_ip(const char *ip_str, uint16_t port,
                                             create_transport_fun create_transport) {
  struct rdma_transport *client =
      g_hash_table_lookup(g_rdma_context->hash_table, ip_str);
  if (client == NULL) {
    pthread_mutex_lock(&g_rdma_context->hash_lock);
    client = g_hash_table_lookup(g_rdma_context->hash_table, ip_str);
    if (client == NULL) {
      if (create_transport(ip_str, port) < 0) {
        LOG(ERROR, "connect server %s:%u failed", ip_str, port);
        pthread_mutex_unlock(&g_rdma_context->hash_lock);
        return NULL;
      }
      client = g_hash_table_lookup(g_rdma_context->hash_table, ip_str);
      client->cq_id++;
    }
    pthread_mutex_unlock(&g_rdma_context->hash_lock);
  }
  return client;
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

static int modify_qp_to_init(struct rdma_transport *transport) {
  struct ibv_qp_attr init_attr;
  memset(&init_attr, 0, sizeof(init_attr));
  init_attr.qp_state        = IBV_QPS_INIT;
  init_attr.pkey_index      = 0;
  init_attr.port_num        = IB_PORT_NUM;
  init_attr.qp_access_flags = RDMA_BUF_FLAG;
  int init_flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

  if (ibv_modify_qp(transport->rc_qp, &init_attr, init_flags) < 0) {
    LOG(ERROR, "modify_qp_to_init: failed to modify QP to INIT, %s", strerror(errno));
    return -1;
  }
  return 0;
}

static int modify_qp_to_rts(struct rdma_transport *transport) {
  struct ibv_qp_attr rts_attr;
  memset(&rts_attr, 0, sizeof(rts_attr));
  rts_attr.qp_state = IBV_QPS_RTS;
  rts_attr.timeout  = 12;
  rts_attr.retry_cnt = 6;
  rts_attr.rnr_retry = 6;
  rts_attr.max_rd_atomic = 0; // rc must add it
  rts_attr.sq_psn   = transport->local_qp_attr.psn;

  int rts_flags = IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC;
  if (ibv_modify_qp(transport->rc_qp, &rts_attr, rts_flags) < 0) {
    LOG(ERROR, "modify QP to RTS error, %s", strerror(errno));
    return -1;
  }
  return 0;
}

static int modify_qp_to_rtr(struct rdma_transport *transport) {
  struct ibv_qp_attr rtr_attr;
  memset(&rtr_attr, 0, sizeof(rtr_attr));
  rtr_attr.qp_state = IBV_QPS_RTR;
  rtr_attr.path_mtu = IBV_MTU_4096;
  rtr_attr.min_rnr_timer = 12;
  rtr_attr.max_dest_rd_atomic = 0; // rc must add it
  rtr_attr.dest_qp_num = transport->remote_qp_attr.qpn;
  rtr_attr.rq_psn = transport->remote_qp_attr.psn;
  rtr_attr.ah_attr.is_global = 0;
  rtr_attr.ah_attr.dlid = transport->remote_qp_attr.lid;
  rtr_attr.ah_attr.sl = 0;
  rtr_attr.ah_attr.src_path_bits = 0;
  rtr_attr.ah_attr.port_num = IB_PORT_NUM;

  int rtr_flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
      IBV_QP_RQ_PSN | IBV_QP_MIN_RNR_TIMER | IBV_QP_MAX_DEST_RD_ATOMIC;
  if (ibv_modify_qp(transport->rc_qp, &rtr_attr, rtr_flags) < 0) {
    LOG(ERROR, "modify QP to RTS error, %s", strerror(errno));
    return -1;
  }
  return 0;
}