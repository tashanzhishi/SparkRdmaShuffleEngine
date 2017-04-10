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

static int connect_server(const char *ip_str, uint16_t port);
//static struct rdma_transport_client *get_transport_from_ip(const char *ip_str, uint16_t port);
static void copy_msg_to_rdma(struct rdma_chunk *chunk_list, uint32_t chunk_num,
                             uint8_t *msg, uint32_t len, uint32_t data_id);
static void copy_header_and_body_to_rdma(struct rdma_chunk *chunk_list, uint32_t chunk_num,
                                         uint8_t *header, uint32_t head_len, uint8_t *body, uint32_t body_len,
                                         uint32_t data_id);
static void rdma_send_msg(struct rdma_transport *transport,
                          struct rdma_chunk *chunk_list, uint32_t chunk_num, uint32_t len);


int send_msg(const char *host, uint16_t port, uint8_t *msg, uint32_t len) {
  LOG(DEBUG, "send message host:%s port:%u len:%u", host, port, len);

  //char *remote_ip_str = (char *)calloc(1, IP_CHAR_SIZE);
  char remote_ip_str[IP_CHAR_SIZE] = {'\0'};
  set_ip_from_host(host, remote_ip_str);
  struct rdma_transport_client *client =
      (struct rdma_transport_client *)get_transport_from_ip(remote_ip_str, port, connect_server);
  GPR_ASSERT(client);
  uint32_t data_id = client->data_id;

  uint32_t chunk_num = len / RDMA_CHUNK_SIZE + (len % RDMA_CHUNK_SIZE == 0 ? 0 : 1);
  struct rdma_chunk *chunk_list = get_rdma_chunk_list_from_pool(g_rdma_context->rbp, chunk_num);
  if (chunk_list == NULL) {
    LOG(ERROR, "send_msg error: get chunk list from pool failed, local ip %s, remote ip %s",
        client->local_ip, client->remote_ip);
    abort();
  }
  LOG(DEBUG, "get %d chunks from buffer pool", chunk_num);

  copy_msg_to_rdma(chunk_list, chunk_num, msg, len, data_id);
  LOG(DEBUG, "send_msg: copy msg to rdma success");

  rdma_send_msg(&client->transport, chunk_list, chunk_num, len);

  //free(remote_ip_str);
  LOG(DEBUG, "send_msg: send msg success");
  return 0;
}

int send_msg_with_header(const char *host, uint16_t port,
                         uint8_t *header, uint32_t head_len, uint8_t *body, uint32_t body_len) {
  LOG(DEBUG, "send message with header host:%s port:%u head_len:%u body_len:%u", host, port, head_len, body_len);

  //char *remote_ip_str = (char *)calloc(1, IP_CHAR_SIZE);
  char remote_ip_str[IP_CHAR_SIZE] = {'\0'};
  set_ip_from_host(host, remote_ip_str);
  struct rdma_transport_client *client =
      (struct rdma_transport_client *)get_transport_from_ip(remote_ip_str, port, connect_server);
  GPR_ASSERT(client);
  uint32_t data_id = client->data_id;

  uint32_t len = head_len + body_len;
  uint32_t chunk_num = len / RDMA_CHUNK_SIZE + (len % RDMA_CHUNK_SIZE == 0 ? 0 : 1);
  struct rdma_chunk *chunk_list = get_rdma_chunk_list_from_pool(g_rdma_context->rbp, chunk_num);
  if (chunk_list == NULL) {
    LOG(ERROR, "send_msg error: get chunk list from pool failed, local ip %s, remote ip %s",
        client->local_ip, client->remote_ip);
    abort();
  }
  LOG(DEBUG, "get %d chunks from buffer pool", chunk_num);

  copy_header_and_body_to_rdma(chunk_list, chunk_num, header, head_len, body, body_len, data_id);
  LOG(DEBUG, "copy_header_and_body_to_rdma: copy header and body to rdma success");

  rdma_send_msg(&client->transport, chunk_list, chunk_num, len);

  //free(remote_ip_str);
  LOG(DEBUG, "send_msg: send msg success");
  return 0;
}


/************************************************************************/
/*                          local function                              */
/************************************************************************/


static void rdma_send_msg(struct rdma_transport *transport,
                          struct rdma_chunk *chunk_list, uint32_t chunk_num, uint32_t len) {
  struct rdma_chunk *chunk = chunk_list;
  for (uint32_t i=0; i<chunk_num; i++) {
    struct rdma_work_chunk *send_wc = (struct rdma_work_chunk *)malloc(sizeof(struct rdma_work_chunk));
    send_wc->transport = transport;
    send_wc->chunk = chunk;
    send_wc->len = (i == chunk_num-1 ? RDMA_CHUNK_SIZE : len%RDMA_CHUNK_SIZE);
    rdma_transport_send(transport, send_wc);
    chunk = chunk->next;
  }
  GPR_ASSERT(chunk);
}

static void copy_msg_to_rdma(struct rdma_chunk *chunk_list, uint32_t chunk_num,
                             uint8_t *msg, uint32_t len, uint32_t data_id) {
  struct rdma_chunk *chunk = chunk_list;
  uint32_t copy_len = 0;
  uint8_t *now = msg;
  for (uint32_t i=0; i<chunk_num; i++) {
    copy_len = (len > RDMA_BODY_SIZE ? RDMA_BODY_SIZE : len);
    chunk->header.data_id = data_id;
    chunk->header.chunk_num = chunk_num;
    chunk->header.flags = 0;
    memcpy(chunk->body, now, copy_len);
    chunk = chunk->next;
    now += copy_len;
    len -= RDMA_BODY_SIZE;
  }
  GPR_ASSERT(chunk);
  LOG(DEBUG, "copy %d chunks %u byte to rdma", chunk_num, len);
}

static void copy_header_and_body_to_rdma(struct rdma_chunk *chunk_list, uint32_t chunk_num,
                                        uint8_t *header, uint32_t head_len, uint8_t *body, uint32_t body_len,
                                         uint32_t data_id) {
  struct rdma_chunk *chunk = chunk_list;
  int is_header = 1;
  uint32_t len = head_len + body_len;
  uint32_t copy_len = 0;
  uint8_t *now = header;
  for (uint32_t i=0; i<chunk_num; i++) {
    chunk->header.data_id = data_id;
    chunk->header.chunk_num = chunk_num;
    chunk->header.flags = 0;
    copy_len = (len > RDMA_BODY_SIZE ? RDMA_BODY_SIZE : len);

    if (is_header) {
      if (head_len < RDMA_BODY_SIZE) {
        memcpy(chunk->body, now, head_len);
        is_header = 0;
        now = body;

        memcpy(chunk->body+head_len, now, copy_len - head_len);
        now += copy_len - head_len;
      } else {
        memcpy(chunk->body, now, RDMA_BODY_SIZE);
        head_len -= RDMA_BODY_SIZE;
        now += RDMA_BODY_SIZE;
      }
    } else {
      memcpy(chunk->body, now, copy_len);
      now += copy_len;
    }

    len -= copy_len;
    chunk = chunk->next;
  }
  GPR_ASSERT(chunk);
}

static int connect_server(const char *ip_str, uint16_t port) {
  LOG(DEBUG, "connect the ip = %s, port = %d", ip_str, port);
  if (port == 0) {
    port = IB_SERVER_PORT;
  }

  // if link is existing, return
  struct rdma_transport_client *ret = g_hash_table_lookup(g_rdma_context->hash_table, ip_str);
  if (ret != NULL) {
    LOG(INFO, "the link to %s is existing", ip_str);
    return 0;
  }

  struct sockaddr_in srv_addr;
  memset(&srv_addr, 0, sizeof(srv_addr));
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(port);
  srv_addr.sin_addr.s_addr = inet_addr(ip_str);

  struct rdma_transport_client *client =
      (struct rdma_transport_client *)calloc(1, sizeof(struct rdma_transport_client));
  client->data_id = 0;
  strcpy(client->remote_ip, ip_str);
  char *local_ip_str = (char *)calloc(1, IP_CHAR_SIZE);
  set_local_ip(local_ip_str);
  strcpy(client->local_ip, local_ip_str);
  free(local_ip_str);

  int client_fd;
  if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    LOG(ERROR, "socket error: %s", strerror(errno));
    free(client);
    return -1;
  }

  if (connect(client_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
    LOG(ERROR, "connect %s error: %s", ip_str, strerror(errno));
    goto error;
  }

  // create the hash table of (server ip-->client transport pointer)
  // the remote_ip_str must be malloc once, and don't free yourself.
  char *remote_ip_str = (char *)calloc(1, IP_CHAR_SIZE);
  strcpy(remote_ip_str, ip_str);
  g_hash_table_insert(g_rdma_context->hash_table, remote_ip_str, client);

  // create qp, it must call before exchange_info()
  rdma_create_connect(&client->transport);

  // this is a blocking function
  if (exchange_info(client_fd, &client->transport, true) < 0) {
    LOG(ERROR, "client exchange information failed");
    goto error;
  }

  rdma_complete_connect(&client->transport);

  close(client_fd);
  return 0;
error:
  if (client_fd > 0) {
    close(client_fd);
  }
  free(client);
  return -1;
}

/*static struct rdma_transport_client *get_transport_from_ip(const char *ip_str, uint16_t port) {
  struct rdma_transport_client *client =
      g_hash_table_lookup(g_rdma_context->hash_table, ip_str);
  if (client == NULL) {
    pthread_mutex_lock(&g_rdma_context->hash_lock);
    client = g_hash_table_lookup(g_rdma_context->hash_table, ip_str);
    if (client == NULL) {
      if (connect_server(ip_str, port) < 0) {
        LOG(ERROR, "connect server %s:%u failed", ip_str, port);
        pthread_mutex_unlock(&g_rdma_context->hash_lock);
        return NULL;
      }
      client = g_hash_table_lookup(g_rdma_context->hash_table, ip_str);
    }
    pthread_mutex_unlock(&g_rdma_context->hash_lock);
  }
  return client;
}*/
