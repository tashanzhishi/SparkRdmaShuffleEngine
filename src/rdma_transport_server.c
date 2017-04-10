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
static volatile int poll_loop = 1;

struct accept_arg {
  int sfd;
  char ip_str[IP_CHAR_SIZE];
  uint16_t port;
};


static int create_transport_server(const char *ip_str, uint16_t port);
static int init_server_socket(const char *host, uint16_t port);
static void *accept_thread(void *arg);



int init_server(const char *host, uint16_t port) {
  rdma_context_init();
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

    struct rdma_transport_server *server =
        (struct rdma_transport_server *)get_transport_from_ip(remote_ip, info->port, create_transport_server);
    strcpy(server->local_ip, local_ip);
    strcpy(server->remote_ip, remote_ip);

    rdma_create_connect(&server->transport);

    if (exchange_info(fd, &server->transport, false) < 0) {
      LOG(ERROR, "server exchange information failed");
      abort();
    }
    rdma_complete_connect(&server->transport);
  }

  free(arg);
  return NULL;
}

// a callback function
static int create_transport_server(const char *ip_str, uint16_t port) {
  struct rdma_transport_server *server =
      (struct rdma_transport_server *)calloc(1, sizeof(struct rdma_transport_server));
  char *remote_ip_str = (char *)calloc(1, IP_CHAR_SIZE); // should not free yourself
  strcpy(remote_ip_str, ip_str);
  g_hash_table_insert(g_rdma_context->hash_table, remote_ip_str, server);
  server->cache = g_queue_new();
  pthread_mutex_init(&server->cache_lock, NULL);
  return 0;
}

static int init_server_poll() {
  for (int i=0; i<MAX_POLL_THREAD; i++) {
    ;
  }
}

static void *poll_thread(void *arg) {
  int cq_id = *(int *)arg;
  struct ibv_cq *cq = g_rdma_context->cq[cq_id];
  int event_num = 0;
  struct ibv_wc wc[MAX_EVENT_PER_POLL];

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
            ;
          } else if (wc[i].opcode == IBV_WC_RECV) {
            ;
          } else {
            LOG(INFO, "ibv_wc.opcode = %d, which is not send or recv", wc[i].opcode);
          }
        }
      }
    }
  }
}

static void handle_send_event(struct ibv_wc *wc) {
  struct rdma_work_chunk *work_chunk = (struct rdma_work_chunk *)wc->wr_id;
  struct rdma_chunk *chunk = work_chunk->chunk;
  release_rdma_chunk_to_pool(g_rdma_context->rbp, chunk);
  free(work_chunk);
}

static void handle_recv_event(struct ibv_wc *wc) {
  struct rdma_work_chunk *work_chunk = (struct rdma_work_chunk *)wc->wr_id;
  struct rdma_transport_server *server = (struct rdma_transport_server *)
  varray_t *varray = g_queue_peek_head_link(work_chunk->transport->)
}