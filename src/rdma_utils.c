#include "rdma_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <errno.h>

#include "rdma_buffer_pool.h"


static union ibv_gid get_gid(struct ibv_context *context);
static uint16_t get_local_lid(struct ibv_context *context);
static int modify_qp_to_init(struct rdma_transport *transport);

struct rdma_context *g_rdma_context;

int rdma_context_init()
{
  LOG(DEBUG, "rdma_context_init begin");
  struct ibv_device **dev_list;
  struct ibv_device *ib_dev;

  g_rdma_context = (struct rdma_context *)malloc(sizeof(struct rdma_context));

  srand((unsigned int)time(NULL));

  dev_list = ibv_get_device_list(NULL);
  GPR_ASSERT(dev_list);

  ib_dev = dev_list[0];
  GPR_ASSERT(ib_dev);

  g_rdma_context->context = ibv_open_device(ib_dev);
  GPR_ASSERT(g_rdma_context->context);

  g_rdma_context->pd = ibv_alloc_pd(g_rdma_context->context);
  GPR_ASSERT(g_rdma_context->pd);

  for (int i=0; i<MAX_POLL_THREAD; i++) {
    g_rdma_context->cq[i] = ibv_create_cq(g_rdma_context->context, MAX_CQE + 1, NULL, NULL, 0);
    GPR_ASSERT(g_rdma_context->cq[i]);
  }

  pthread_mutex_init(&g_rdma_context->cq_lock, NULL);
  g_rdma_context->cq_num = 0;

  g_rdma_context->rbp = (struct rdma_buffer_pool*)malloc(sizeof(struct rdma_buffer_pool));
  GPR_ASSERT(g_rdma_context->rbp);
  if (init_rdma_buffer_pool(g_rdma_context->rbp, g_rdma_context->pd) == -1) {
    LOG(ERROR, "failed to initiate buffer pool");
    rdma_context_destroy(g_rdma_context);
    exit(1);
  }
  LOG(DEBUG, "rdma_context_init end");
  return 0;
}

void rdma_context_destroy(struct rdma_context *context)
{
  destroy_rdma_buffer_pool(context->rbp);

  for (int i=0; i<MAX_POLL_THREAD; i++) {
    GPR_ASSERT(context->cq[i]);
    ibv_destroy_cq(context->cq[i]);
    context->cq[i] = NULL;
  }

  GPR_ASSERT(context->pd);
  ibv_dealloc_pd(context->pd);
  context->pd = NULL;

  GPR_ASSERT(context->context);
  ibv_close_device(context->context);
  context->context = NULL;
  free(context);
  LOG(DEBUG, "destroy rdma success");
}

int exchange_info(int sfd, struct rdma_transport *transport, bool is_client)
{
  LOG(DEBUG, "exchange_info begin");

  union ibv_gid gid = get_gid(g_rdma_context->context);
  transport->local_qp_attr.gid_global_interface_id = gid.global.interface_id;
  transport->local_qp_attr.gid_global_subnet_prefix = gid.global.subnet_prefix;
  transport->local_qp_attr.lid = get_local_lid(g_rdma_context->context);
  transport->local_qp_attr.qpn = transport->rc_qp->qp_num;
  transport->local_qp_attr.psn = rand() & 0xffffff;

  if (is_client) {
    if (write(sfd, &transport->local_qp_attr, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "client exchange_info: failed to write infomation");
      return -1;
    }
    if (read(sfd, &transport->remote_qp_attr, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "client exchange_info: failed to read infomation");
      return -1;
    }
  } else {
    if (read(sfd, &transport->remote_qp_attr, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "server exchange_info: failed to read infomation");
      return -1;
    }
    if (write(sfd, &transport->local_qp_attr, sizeof(struct qp_attr)) < 0) {
      LOG(ERROR, "server exchange_info: failed to write infomation");
      return -1;
    }
  }

  LOG(DEBUG, "exchange_info end");
  return 0;
}

struct rdma_transport *rdma_transport_create() {
  LOG(DEBUG, "rdma_transport_create begin");

  struct rdma_transport *transport = (struct rdma_transport *)calloc(1, sizeof(struct rdma_transport));
  GPR_ASSERT(transport);

  pthread_mutex_lock(&g_rdma_context->cq_lock);
  transport->cq_id = (g_rdma_context->cq_num++)%MAX_POLL_THREAD;
  pthread_mutex_unlock(&g_rdma_context->cq_lock);
  struct ibv_qp_init_attr init_attr = {
      .qp_type = IBV_QPT_RC,
      .sq_sig_all = 0,
      .send_cq = g_rdma_context->cq[transport->cq_id],
      .recv_cq = g_rdma_context->cq[transport->cq_id],
      .srq = NULL,
      .cap = {
          .max_send_wr = MAX_CQE,
          .max_send_wr = MAX_CQE,
          .max_send_sge = 1,
          .max_recv_sge = 1,
          .max_inline_data = 0,
      },
  };
  transport->rc_qp = ibv_create_qp(g_rdma_context->pd, &init_attr);
  if (modify_qp_to_init(transport) < 0) {
    LOG(ERROR, "rdma_transport_create: failed to modify queue pair to initiate state");
    abort();
  }
  LOG(DEBUG, "rdma_transport_create end");
  return transport;
}



/************************************************************************/
/*                          local function                              */
/************************************************************************/


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