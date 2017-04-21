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

#include "jni_common.h"


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

static void shutdown_connect(gpointer key, gpointer value, gpointer user_data);



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

  return 0;
}



void destroy_server() {
  LOG(INFO, "destroy server and will free all resource");

  // shutdown all connection of hash table
  g_hash_table_foreach(g_rdma_context->hash_table, shutdown_connect, NULL);

  // stop accept thread
  shutdown(g_rdma_context->sfd, SHUT_RDWR);
  close(g_rdma_context->sfd);

  rdma_context_destroy(g_rdma_context);
}



/************************************************************************/
/*                          local function                              */
/************************************************************************/

static void shutdown_connect(gpointer key, gpointer value, gpointer user_data) {
  struct rdma_transport *transport = (struct rdma_transport *)value;
  rdma_shutdown_connect(transport);
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

    pthread_mutex_lock(&g_rdma_context->hash_lock);

    char remote_ip[IP_CHAR_SIZE] = {'\0'};
    memcpy(&client_addr, &addr, sizeof(addr));
    strcpy(remote_ip, inet_ntoa(client_addr.sin_addr));
    LOG(DEBUG, "%s accept %s, fd=%d", local_ip, remote_ip, fd);

    struct rdma_transport *server = g_hash_table_lookup(g_rdma_context->hash_table, remote_ip);
    if (server != NULL) {
      pthread_mutex_unlock(&g_rdma_context->hash_lock);
      continue;
    }

    server = (struct rdma_transport *)calloc(1, sizeof(struct rdma_transport));
    GPR_ASSERT(server);
    strcpy(server->local_ip, local_ip);
    strcpy(server->remote_ip, remote_ip);

    rdma_create_connect(server);

    if (exchange_info(fd, server, false) < 0) {
      LOG(ERROR, "server exchange information failed");
      abort();
    }

    char *remote_ip_str = (char *)calloc(1, IP_CHAR_SIZE); // should not free yourself
    strcpy(remote_ip_str, remote_ip);
    g_hash_table_insert(g_rdma_context->hash_table, remote_ip_str, server);

    pthread_mutex_unlock(&g_rdma_context->hash_lock);
  }

  free(arg);
  return NULL;
}
