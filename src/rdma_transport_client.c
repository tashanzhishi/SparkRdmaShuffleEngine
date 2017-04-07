#include "rdma_transport_client.h"

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

static int connect_server(char *host, uint16_t port);
static void copy_msg_to_rdma(struct rdma_chunk *chunk_list, int chunk_num, uint8_t *msg, uint32_t len);
static void rdma_send_msg(struct rdma_transport *transport, struct rdma_chunk *chunk_list, int chunk_num, uint32_t len);

int send_msg(char *host, uint16_t port, uint8_t *msg, uint32_t len) {
  LOG(DEBUG, "send message host:%s port:%u len:%u", host, port, len);
  if (len < 0) {
    LOG(ERROR, "send message's length < 0");
    return -1;
  }

  struct rdma_transport_client *client =
      g_hash_table_lookup(g_rdma_context->hash_table, host);
  if (client == NULL) {
    pthread_mutex_lock(&g_rdma_context->hash_lock);
    client = g_hash_table_lookup(g_rdma_context->hash_table, host);
    if (client == NULL) {
      if (connect_server(host, port) < 0) {
        LOG(ERROR, "send message failed, because connect server %s:%u failed", host, port);
        pthread_mutex_unlock(&g_rdma_context->hash_lock);
        return -1;
      }
      client = g_hash_table_lookup(g_rdma_context->hash_table, host);
    }
    pthread_mutex_unlock(&g_rdma_context->hash_lock);
  }
  GPR_ASSERT(client);

  int chunk_num = len / RDMA_CHUNK_SIZE + (len % RDMA_CHUNK_SIZE) == 0 ? 0 : 1;
  struct rdma_chunk *chunk_list = get_rdma_chunk_list_from_pool(g_rdma_context->rbp, chunk_num);
  if (chunk_list == NULL) {
    LOG(ERROR, "send_msg error: get chunk list from pool failed, local ip %s, remote ip %s",
        client->local_ip, client->remote_ip);
    abort();
  }
  LOG(DEBUG, "get %d chunks from buffer pool", chunk_num);

  copy_msg_to_rdma(chunk_list, chunk_num, msg, len);
  LOG(DEBUG, "send_msg: copy msg to rdma success");

  rdma_send_msg(&client->transport, chunk_list, chunk_num, len);
  LOG(DEBUG, "send_msg: send msg success");
  return 0;
}

int send_msg_with_header(char *host, uint8_t *header, uint32_t head_len, uint8_t *body, uint32_t body_len) {
  
}


/************************************************************************/
/*                          local function                              */
/************************************************************************/


static void rdma_send_msg(struct rdma_transport *transport, struct rdma_chunk *chunk_list, int chunk_num, uint32_t len) {
  struct rdma_chunk *chunk = chunk_list;
  for (int i=0; i<chunk_num; i++) {
    struct rdma_work_chunk *send_wc = (struct rdma_work_chunk *)malloc(sizeof(struct rdma_work_chunk));
    send_wc->transport = transport;
    send_wc->chunk = chunk;
    send_wc->len = (i == chunk_num-1 ? RDMA_CHUNK_SIZE : len%RDMA_CHUNK_SIZE);
    rdma_transport_send(transport, send_wc);
    chunk = chunk->next;
  }
  GPR_ASSERT(chunk);
}

static void copy_msg_to_rdma(struct rdma_chunk *chunk_list, int chunk_num, uint8_t *msg, uint32_t len) {
  struct rdma_chunk *chunk = chunk_list;
  int need_copy = 0;
  for (int i=0; i<chunk_num; i++) {
    need_copy = (len > RDMA_CHUNK_SIZE ? RDMA_CHUNK_SIZE : len);
    memcpy(chunk->chunk, msg + i*RDMA_CHUNK_SIZE, need_copy);
    chunk = chunk->next;
    len -= RDMA_CHUNK_SIZE;
  }
  GPR_ASSERT(chunk);
  LOG(DEBUG, "copy %d chunks %u byte to rdma", chunk_num, len);
}

static int connect_server(char *host, uint16_t port) {
  LOG(DEBUG, "connect the host = %s, port = %d", host, port);
  if (port == 0) {
    port = IB_SERVER_PORT;
  }

  // if link is existing, return
  struct rdma_transport_client *ret = g_hash_table_lookup(g_rdma_context->hash_table, host);
  if (ret != NULL) {
    LOG(INFO, "the link to %s is existing, and the rdma_transport_client pointer is %p", host, *ret);
    return 0;
  }

  struct hostent *he = gethostbyname(host);
  if (he == NULL) {
    LOG(ERROR, "get host by name failed.");
    return -1;
  }
  char local_ip_str[IP_CHAR_SIZE]={'\0'};
  inet_ntop(he->h_addrtype, he->h_addr, local_ip_str, sizeof(local_ip_str));

  struct sockaddr_in srv_addr;
  memset(&srv_addr, 0, sizeof(srv_addr));
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(port);
  srv_addr.sin_addr.s_addr = inet_addr(local_ip_str);

  struct rdma_transport_client *client =
      (struct rdma_transport_client *)malloc(sizeof(struct rdma_transport_client));
  memset(client, 0, sizeof(struct rdma_transport_client));
  memcpy(client->local_ip, local_ip_str, strlen(local_ip_str));
  memcpy(client->remote_ip, host, strlen(host));

  if ((client->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    LOG(ERROR, "socket error: %s", strerror(errno));
    free(client);
    return -1;
  }

  if (connect(client->fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
    LOG(ERROR, "connect %s error: %s", strerror(errno));
    goto error;
  }
  // create the hash table of (server ip-->client transport point)
  g_hash_table_insert(g_rdma_context->hash_table, host, client);

  // create qp, it must call before exchange_info()
  rdma_create_connect(&client->transport);

  // this is a blocking function
  if (exchange_info(client->fd, &client->transport, true) < 0) {
    LOG(ERROR, "client exchange information failed");
    goto error;
  }

  rdma_complete_connect(&client->transport);

  return 0;
error:
  if (client->fd > 0) {
    close(client->fd);
  }
  free(client);
  return -1;
}