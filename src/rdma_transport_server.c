#include "rdma_transport_server.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <errno.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

extern struct rdma_context *g_rdma_context;

static volatile int accept_loop = 1;

static int poll_cq_id[MAX_EVENT_PER_POLL];

struct accept_arg {
  int sfd;
  char ip_str[IP_CHAR_SIZE];
  uint16_t port;
};


static int create_transport_server(const char *ip_str, uint16_t port);
static int init_server_socket(const char *host, uint16_t port);
static void *accept_thread(void *arg);
static void *poll_thread(void *arg);
static void handle_send_event(struct ibv_wc *wc);
static void handle_recv_event(struct ibv_wc *wc);
static void work_thread(gpointer data, gpointer user_data);



int init_server(const char *host, uint16_t port) {
  rdma_context_init();

  if (init_server_socket(host, port) < 0) {
    LOG(ERROR, "init_server_socket failed");
    abort();
  }

  // g_thread_init has been deprecated since version 2.32
#if GLIB_MINOR_VERSION < 32
  g_thread_init(NULL);
#endif
  g_rdma_context->thread_pool = g_thread_pool_new(work_thread, NULL, THREAD_POOL_SIZE, TRUE, NULL);

  return 0;
}



/************************************************************************/
/*                          local function                              */
/************************************************************************/


static int init_server_socket(const char *host, uint16_t port) {
  LOG(DEBUG, "connect the host = %s, port = %d", host, port);
  if (port == 0) {
    port = IB_SERVER_PORT;
  }

  char ip_str[IP_CHAR_SIZE];
  set_ip_from_host(host, ip_str);

  struct sockaddr_in srv_addr;
  memset(&srv_addr, 0, sizeof(srv_addr));
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(port);
  srv_addr.sin_addr.s_addr = inet_addr(ip_str);

  int sfd;
  if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    LOG(ERROR, "socket error: %s", strerror(errno));
    return -1;
  }

  int reuse = 1;
  if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(int)) < 0) {
    LOG(ERROR, "setsockopt reuse ip %s error, %s", ip_str, strerror(errno));
    return -1;
  }

  if (bind(sfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
    LOG(ERROR, "bind ip %s error, %s", ip_str, strerror(errno));
    return -1;
  }

  if (listen(sfd, 1024) < 0) {
    LOG(ERROR, "listen ip %s error, %s", ip_str, strerror(errno));
    return -1;
  }

  struct accept_arg *arg = (struct accept_arg *)malloc(sizeof(struct accept_arg));
  arg->sfd = sfd;
  arg->port = port;
  strcpy(arg->ip_str, ip_str);

  // create a accept thread to accept connect from client,
  // and it will create rdma_transport_server (or it is existing?)
  create_thread(accept_thread, arg);

  for (int i=0; i<MAX_POLL_THREAD; i++) {
    poll_cq_id[i] = i;
    create_thread(poll_thread, &poll_cq_id[i]);
  }

  return 0;
}

// the arg should free in this function
static void *accept_thread(void *arg) {
  struct accept_arg * info = (struct accept_arg *)arg;
  int sfd = info->sfd;
  char *local_ip = info->ip_str;
  struct sockaddr_in addr;
  socklen_t socklen;

  while (accept_loop) {
    int fd = accept(sfd, (struct sockaddr *)&addr, &socklen);
    if (fd == -1) {
      LOG(ERROR, "lcoal ip %s accept error, %s", local_ip, strerror(errno));
      abort();
    }

    struct sockaddr_in client_addr;
    char remote_ip[IP_CHAR_SIZE] = {'\0'};
    memcpy(&client_addr, &addr, sizeof(addr));
    strcpy(remote_ip, inet_ntoa(client_addr.sin_addr));

    struct rdma_transport *server = get_transport_from_ip(remote_ip, info->port, create_transport_server);
    strcpy(server->local_ip, local_ip);
    strcpy(server->remote_ip, remote_ip);

    rdma_create_connect(server);

    if (exchange_info(fd, server, false) < 0) {
      LOG(ERROR, "server exchange information failed");
      abort();
    }
    rdma_complete_connect(server);
  }

  free(arg);
  return NULL;
}

// a callback function
static int create_transport_server(const char *ip_str, uint16_t port) {
  struct rdma_transport *server =
      (struct rdma_transport *)calloc(1, sizeof(struct rdma_transport));
  char *remote_ip_str = (char *)calloc(1, IP_CHAR_SIZE); // should not free yourself
  strcpy(remote_ip_str, ip_str);
  g_hash_table_insert(g_rdma_context->hash_table, remote_ip_str, server);
  return 0;
}

static void *poll_thread(void *arg) {
  int cq_id = *(int *)arg;
  struct ibv_cq *cq = g_rdma_context->cq[cq_id];
  int event_num = 0;
  struct ibv_wc wc[MAX_EVENT_PER_POLL];

  volatile int poll_loop = 1;
  while (poll_loop) {
    event_num = 0;
    while (!event_num) {
      event_num = ibv_poll_cq(cq, MAX_EVENT_PER_POLL, wc);
    }
    if (event_num < 0) {
      LOG(ERROR, "ibv_poll_cq poll error");
    } else {
      for (int i=0; i<event_num; i++) {
        if (wc[i].status != IBV_WC_SUCCESS) {
          LOG(ERROR, "ibv_wc.status error %d", wc[i].status);
          abort();
        } else {
          if (wc[i].opcode == IBV_WC_SEND) {
            handle_send_event(&wc[i]);
          } else if (wc[i].opcode == IBV_WC_RECV) {
            handle_recv_event(&wc[i]);
          } else {
            LOG(ERROR, "ibv_wc.opcode = %d, which is not send or recv", wc[i].opcode);
          }
        }
      }
    }
  }
  return NULL;
}

static void handle_send_event(struct ibv_wc *wc) {
  struct rdma_work_chunk *work_chunk = (struct rdma_work_chunk *)wc->wr_id;
  struct rdma_chunk *chunk = work_chunk->chunk;
  release_rdma_chunk_to_pool(g_rdma_context->rbp, chunk);
  free(work_chunk);
}

// producer thread and the chunk of data is order
static void handle_recv_event(struct ibv_wc *wc) {
  struct rdma_work_chunk *work_chunk = (struct rdma_work_chunk *)wc->wr_id;
  struct rdma_transport *server = work_chunk->transport;
  struct rdma_chunk *chunk = work_chunk->chunk;

  if (chunk->header.chunk_len + RDMA_HEADER_SIZE != wc->byte_len) {
    LOG(ERROR, "remote_ip:%s local_ip:%s, the data (id:%u, num:%u) send chunk %u byte, but recv %u byte",
        server->remote_ip, server->local_ip, chunk->header.data_id, chunk->header.chunk_num,
        chunk->header.chunk_len + RDMA_HEADER_SIZE, wc->byte_len);
    abort();
  }

  // 目前设计只会有一个线程对chache操作
  // cache只是起着缓冲作用，向线程池提交的参数是可变长数组
  varray_t *now = server->recvk_array;
  if (now == NULL) {
    // free after channelRead0
    now = VARRY_MALLOC0(work_chunk->chunk->header.chunk_num);
    GPR_ASSERT(now);
    now->transport = server;
    now->data_id = chunk->header.data_id;
    now->size = chunk->header.chunk_num;
    now->data[now->len++] = chunk;
    server->recvk_array = now;
  } else if (now->data_id == chunk->header.data_id) {
    if (now->len >= now->size) {
      LOG(ERROR, "remote_ip:%s local_ip:%s, the data (id:%u, num:%u) when push chunk, len >= size (%u, %u)",
          server->remote_ip, server->local_ip, chunk->header.data_id, chunk->header.chunk_num, now->len, now->size);
      abort();
    }
    now->data[now->len++] = chunk;
  } else if (now->data_id != chunk->header.data_id) {
    LOG(ERROR, "remote_ip:%s local_ip:%s, data_id: %u != %u",
        server->remote_ip, server->local_ip, now->data_id, chunk->header.data_id);
    abort();
  } else {
    LOG(ERROR, "remote_ip:%s local_ip:%s, unknown error", server->remote_ip, server->local_ip);
    abort();
  }
  if (now->len == now->size) {
    g_thread_pool_push(g_rdma_context->thread_pool, now, NULL);
    server->recvk_array = NULL;
  }
  free(work_chunk);
}

// 1. copy rdma to jvm
// 2. call channelRead0 of spark
static void work_thread(gpointer data, gpointer user_data) {
  varray_t *varray = data;
  GPR_ASSERT(varray);
  if (varray->len != varray->size || varray->len == 0) {
    LOG(ERROR, "varray len != size (%u, %u)", varray->len, varray->size);
    abort();
  }
  uint32_t data_len = 0;
  for (uint32_t i=0; i<varray->size; i++) {
    data_len += varray->data[i]->header.chunk_len;
  }

  // test
  struct rdma_transport *server = varray->transport;
  LOG(INFO, "%s get %u byte message, id %u from %s", server->local_ip, data_len, varray->data_id, server->remote_ip);
  for (uint32_t i=0; i<varray->size; i++) {
    release_rdma_chunk_to_pool(g_rdma_context->rbp, varray->data[i]);
  }
  free(varray);
  usleep(10000);
}