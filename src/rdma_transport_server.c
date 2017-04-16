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
#include <signal.h>

#include "rdma_utils.h"
#include "thread.h"


extern struct rdma_context *g_rdma_context;


struct accept_arg {
  int sfd;
  char ip_str[IP_CHAR_SIZE];
  uint16_t port;
};

static int connid[MAX_POLL_THREAD];


static int create_transport_server(const char *ip_str, uint16_t port);
static int init_server_socket(const char *host, uint16_t port);
static void *accept_thread(void *arg);
static void *poll_thread(void *arg);
static void handle_send_event(struct ibv_wc *wc);
static void handle_recv_event(struct ibv_wc *wc);
static void work_thread(gpointer data, gpointer user_data);

static void shutdown_qp(gpointer key, gpointer value ,gpointer user_data);
static void quit_thread(int signo);



int init_server(const char *host, uint16_t port) {
  rdma_context_init();

  for (int i=0; i<MAX_POLL_THREAD; i++)
    connid[i] = i;

  if (init_server_socket(host, port) < 0) {
    LOG(ERROR, "init_server_socket failed");
    abort();
  }

  // g_thread_init has been deprecated since version 2.32
#if GLIB_MINOR_VERSION < 32
  g_thread_init(NULL);
#endif
  g_rdma_context->thread_pool = g_thread_pool_new(work_thread, NULL, THREAD_POOL_SIZE, TRUE, NULL);
  LOG(DEBUG, "new thread pool of %d", THREAD_POOL_SIZE);

  return 0;
}



void destroy_server() {
  LOG(INFO, "destroy server and will free all resource");

  // stop all work thread
  g_thread_pool_free(g_rdma_context->thread_pool, 0, 1);

  // shutdown all connection of hash table
  g_hash_table_foreach(g_rdma_context->hash_table, shutdown_qp, NULL);

  // stop accept thread
  shutdown(g_rdma_context->sfd, SHUT_RDWR);
  close(g_rdma_context->sfd);

  // kill poll thread
  for (int i=0; i<MAX_POLL_THREAD; i++) {
    pthread_kill(g_rdma_context->pid[i], SIGQUIT);
  }

  rdma_context_destroy(g_rdma_context);
  sleep(1);
}



/************************************************************************/
/*                          local function                              */
/************************************************************************/

static void shutdown_qp(gpointer key, gpointer value ,gpointer user_data) {
  struct rdma_transport *transport = (struct rdma_transport *)value;
  rdma_shutdown_connect(transport);
}

static void quit_thread(int signo) {
  LOG(DEBUG, "thread %lu will exit", pthread_self()%100);
  pthread_exit(NULL);
}

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
  srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  //srv_addr.sin_addr.s_addr = inet_addr(ip_str);

  int sfd;
  if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    LOG(ERROR, "socket error: %s", strerror(errno));
    return -1;
  }
  LOG(DEBUG, "scoket success");
  g_rdma_context->sfd = sfd;

  int reuse = 1;
  if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(int)) < 0) {
    LOG(ERROR, "setsockopt reuse ip %s error, %s", ip_str, strerror(errno));
    return -1;
  }
  LOG(DEBUG, "setsockopt success");

  if (bind(sfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
    LOG(ERROR, "bind ip %s error, %s", ip_str, strerror(errno));
    return -1;
  }
  LOG(DEBUG, "bind success");

  if (listen(sfd, 1024) < 0) {
    LOG(ERROR, "listen ip %s error, %s", ip_str, strerror(errno));
    return -1;
  }
  LOG(DEBUG, "listen success");

  struct accept_arg *arg = (struct accept_arg *)malloc(sizeof(struct accept_arg));
  arg->sfd = sfd;
  arg->port = port;
  strcpy(arg->ip_str, ip_str);

  // create a accept thread to accept connect from client,
  // and it will create server (or it is existing?)
  create_thread(accept_thread, arg);

  for (int i=0; i<MAX_POLL_THREAD; i++) {
    g_rdma_context->pid[i] = create_thread(poll_thread, &connid[i]);
  }
  LOG(DEBUG, "create %d poll thread", MAX_POLL_THREAD);

  LOG(DEBUG, "init server end");
  return 0;
}



// the arg should free in this function
static void *accept_thread(void *arg) {
  LOG(DEBUG, "accept thread begin");

  struct accept_arg * info = (struct accept_arg *)arg;
  int sfd = info->sfd;
  char *local_ip = info->ip_str;

  while (1) {
    struct sockaddr_in addr, client_addr;
    socklen_t socklen = sizeof(addr);
    int fd = accept(sfd, (struct sockaddr *)&addr, &socklen);
    if (fd == -1) {
      LOG(ERROR, "lcoal ip %s accept error, %s", local_ip, strerror(errno));
      break;
    }

    char remote_ip[IP_CHAR_SIZE] = {'\0'};
    memcpy(&client_addr, &addr, sizeof(addr));
    strcpy(remote_ip, inet_ntoa(client_addr.sin_addr));
    LOG(DEBUG, "%s accept %s, fd=%d", local_ip, remote_ip, fd);

    struct rdma_transport *server = get_transport_from_ip(remote_ip, info->port, create_transport_server);
    GPR_ASSERT(server);
    LOG(DEBUG, "server %p", server);

    strcpy(server->local_ip, local_ip);
    strcpy(server->remote_ip, remote_ip);

    rdma_create_connect(server);

    if (exchange_info(fd, server, false) < 0) {
      LOG(ERROR, "server exchange information failed");
      abort();
    }
    //rdma_complete_connect(server);
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
  int id = *(int *)arg;
  LOG(DEBUG, "poll thread %d", id);
  struct ibv_comp_channel *channel = g_rdma_context->comp_channel[id];
  struct ibv_cq *cq = g_rdma_context->cq[id];
  GPR_ASSERT(channel);
  GPR_ASSERT(cq);

  // register quit signal
  signal(SIGQUIT, quit_thread);

  int event_num = 0;
  struct ibv_cq *ev_cq;
  void *ev_ctx;
  struct ibv_wc wc[MAX_EVENT_PER_POLL];

  while (1) {
    LOG(DEBUG, "begin blocking ibv_get_cq_event");
    if (ibv_get_cq_event(channel, &ev_cq, &ev_ctx) < 0) {
      LOG(ERROR, "ibv_get_cq_event error, %s", strerror(errno));
      abort();
    }
    LOG(DEBUG, "get event, ev_cq %p, cq %p", ev_cq, cq);

    ibv_ack_cq_events(cq, 1);

    if (ibv_req_notify_cq(ev_cq, 0) < 0) {
      LOG(ERROR, "ibv_req_notify_cq error, %s", strerror(errno));
      abort();
    }

    do {
      event_num = ibv_poll_cq(cq, MAX_EVENT_PER_POLL, wc);
      LOG(DEBUG, "poll %d event", event_num);

      if (event_num < 0) {
        LOG(ERROR, "ibv_poll_cq poll error");
      } else {
        for (int i=0; i<event_num; i++) {
          if (wc[i].status != IBV_WC_SUCCESS) {
            LOG(ERROR, "ibv_wc.status error %d", wc[i].status);
            abort();
          } else {
            if (wc[i].opcode == IBV_WC_SEND) {
              LOG(DEBUG, "handle a send event begin");
              handle_send_event(&wc[i]);
              LOG(DEBUG, "handle a send event end");
            } else if (wc[i].opcode == IBV_WC_RECV) {
              LOG(DEBUG, "handle a recv event begin");
              handle_recv_event(&wc[i]);
              LOG(DEBUG, "handle a recv event end");
            } else {
              LOG(ERROR, "ibv_wc.opcode = %d, which is not send or recv", wc[i].opcode);
            }
          }
        }
      }
    } while (event_num);
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

    for (int i=0; i<now->size; i++) {
      rdma_transport_recv(server);
    }
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
    LOG(DEBUG, "push a data to thread pool");
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
  LOG(INFO, "%s get %u byte message, id %u from %s", server->local_ip,
      data_len, varray->data_id, server->remote_ip);
  //LOG(DEBUG, "message: %s", varray->data[0]->body);
  for (uint32_t i=0; i<varray->size; i++) {
    release_rdma_chunk_to_pool(g_rdma_context->rbp, varray->data[i]);
  }
  free(varray);
  LOG(DEBUG, "work thread success");
  //usleep(100);
}