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
#include "rdma_transport.h"

#include "jni_common.h"


extern struct rdma_context *g_rdma_context;


struct accept_thread_arg {
  int sfd;
  char ip_str[IP_CHAR_SIZE];
  uint16_t port;
};


static int create_transport_server(const char *ip_str, uint16_t port);
static int init_server_socket(const char *host, uint16_t port);
static void *accept_thread(void *arg);

static void shutdown_connect(gpointer key, gpointer value, gpointer user_data);



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
  g_rdma_context->work_thread_pool = g_thread_pool_new(work_thread, NULL, THREAD_POOL_SIZE, TRUE, NULL);
  LOG(DEBUG, "new thread pool of %d", THREAD_POOL_SIZE);

  return 0;
}



void destroy_server() {
  LOG(INFO, "destroy server and will free all resource");

  g_thread_pool_free(g_rdma_context->work_thread_pool, 0, 1);

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
  struct ip_hash_value *hash_value = (struct ip_hash_value *)value;
  pthread_mutex_destroy(&hash_value->connect_lock);
  rdma_shutdown_connect(hash_value->transport);
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

  struct accept_thread_arg *arg = (struct accept_thread_arg *)malloc(sizeof(struct accept_thread_arg));
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

  struct accept_thread_arg * info = (struct accept_thread_arg *)arg;
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
    LOG(DEBUG, "accept %s, fd=%d", remote_ip, fd);

    pthread_mutex_lock(&g_rdma_context->hash_lock);
    struct ip_hash_value *value = g_hash_table_lookup(g_rdma_context->hash_table, remote_ip);
    struct rdma_transport *server;
    //if (value == NULL || value->transport->is_ready == 0) {
      if (value == NULL) {
        char *key = (char *) calloc(1, IP_CHAR_SIZE);
        strcpy(key, remote_ip);
        value = (struct ip_hash_value *) calloc(1, sizeof(struct ip_hash_value));
        server = value->transport = (struct rdma_transport *)calloc(1, sizeof(struct rdma_transport));
        pthread_mutex_init(&value->connect_lock, NULL);
        // server
        strcpy(server->local_ip, local_ip);
        strcpy(server->remote_ip, remote_ip);

        g_hash_table_insert(g_rdma_context->hash_table, key, value);
      }
      server = value->transport;
      pthread_mutex_unlock(&g_rdma_context->hash_lock);

      pthread_mutex_lock(&value->connect_lock);
      if (server->rc_qp == NULL) {
        rdma_create_connect(server);
      }
      pthread_mutex_unlock(&value->connect_lock);

      if (exchange_info(fd, &server->local_qp_attr, &server->remote_qp_attr, server->rc_qp, false) < 0) {
        LOG(ERROR, "server exchange information failed");
        abort();
      }

      pthread_mutex_lock(&value->connect_lock);
      if (server->is_ready == 0) {
        rdma_complete_connect(server);
        server->is_ready = 1;
      }
      pthread_mutex_unlock(&value->connect_lock);
    /*} else {
      pthread_mutex_unlock(&g_rdma_context->hash_lock);
    }*/
    close(fd);
  }

  free(arg);
  return NULL;
}
