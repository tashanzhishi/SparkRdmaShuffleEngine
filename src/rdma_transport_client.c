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


#include "rdma_utils.h"
#include "rdma_transport.h"

extern struct rdma_context *g_rdma_context;

static int connect_server(const char *ip_str, uint16_t port);
static void copy_msg_to_rdma(varray_t *chunk_array, uint8_t *msg, uint32_t len);
static void copy_header_and_body_to_rdma(varray_t *chunk_array, uint8_t *header, uint32_t head_len,
                                         uint8_t *body, uint32_t body_len);
static void rdma_send_msg(varray_t *chunk_array, uint32_t len);
static struct rdma_transport *get_transport_from_ip(const char *host, uint16_t port);


int send_msg(const char *host, uint16_t port, uint8_t *msg, uint32_t len) {
  LOG(DEBUG, "send message host:%s port:%u len:%u", host, port, len);

  struct rdma_transport *client = get_transport_from_ip(host, port);
  GPR_ASSERT(client != NULL);


  pthread_mutex_lock(&client->id_lock);
  uint32_t data_id = client->data_id++;
  pthread_mutex_unlock(&client->id_lock);

  uint32_t chunk_num = len / RDMA_BODY_SIZE + (len % RDMA_BODY_SIZE == 0 ? 0 : 1);
  struct rdma_chunk *chunk_list = get_rdma_chunk_list_from_pool(g_rdma_context->rbp, chunk_num);
  if (chunk_list == NULL) {
    LOG(ERROR, "send_msg error: get chunk list from pool failed, local ip %s, remote ip %s",
        client->local_ip, client->remote_ip);
    abort();
  }
  LOG(DEBUG, "get %d chunks from buffer pool", chunk_num);

  varray_t *chunk_array = VARRY_MALLOC0(chunk_num);
  chunk_array->size = chunk_num;
  chunk_array->data_id = data_id;
  chunk_array->transport = client;
  struct rdma_chunk *chunk = chunk_list;
  for (int i=0; i<chunk_num; i++) {
    chunk_array->data[i] = chunk;
    chunk = chunk->next;
  }
  GPR_ASSERT(chunk==NULL);

  copy_msg_to_rdma(chunk_array, msg, len);
  //LOG(DEBUG, "send_msg: copy msg to rdma success");

  rdma_send_msg(chunk_array, len);
  LOG(DEBUG, "send_msg success: %u:%u:%s", chunk_array->data_id, len, host);

  free(chunk_array);
  return 0;
}

int send_msg_with_header(const char *host, uint16_t port,
                         uint8_t *header, uint32_t head_len, uint8_t *body, uint32_t body_len) {
  LOG(DEBUG, "send message with header host:%s port:%u head_len:%u body_len:%u", host, port, head_len, body_len);

  struct rdma_transport *client = get_transport_from_ip(host, port);
  GPR_ASSERT(client != NULL);
  pthread_mutex_lock(&client->id_lock);
  uint32_t data_id = client->data_id++;
  pthread_mutex_unlock(&client->id_lock);

  uint32_t len = head_len + body_len;
  uint32_t chunk_num = len / RDMA_BODY_SIZE + (len % RDMA_BODY_SIZE == 0 ? 0 : 1);
  struct rdma_chunk *chunk_list = get_rdma_chunk_list_from_pool(g_rdma_context->rbp, chunk_num);
  if (chunk_list == NULL) {
    LOG(ERROR, "send_msg error: get chunk list from pool failed, local ip %s, remote ip %s",
        client->local_ip, client->remote_ip);
    abort();
  }
  LOG(DEBUG, "get %d chunks from buffer pool", chunk_num);

  varray_t *chunk_array = VARRY_MALLOC0(chunk_num);
  chunk_array->size = chunk_num;
  chunk_array->data_id = data_id;
  chunk_array->transport = client;
  struct rdma_chunk *chunk = chunk_list;
  for (int i=0; i<chunk_num; i++) {
    chunk_array->data[i] = chunk;
    chunk = chunk->next;
  }
  GPR_ASSERT(chunk==NULL);

  copy_header_and_body_to_rdma(chunk_array, header, head_len, body, body_len);
  //LOG(DEBUG, "copy_header_and_body_to_rdma: copy header and body to rdma success");

  rdma_send_msg(chunk_array, len);
  LOG(DEBUG, "send_msg_with_header success: %u:%u:%s", chunk_array->data_id, len, host);

  free(chunk_array);
  return 0;
}


/************************************************************************/
/*                          local function                              */
/************************************************************************/


static void rdma_send_msg(varray_t *chunk_array, uint32_t len) {
  uint32_t chunk_num = chunk_array->size;
  struct rdma_transport *transport = chunk_array->transport;
  struct rdma_chunk *chunk = NULL;
  uint32_t copy_len = 0;
  struct rdma_work_chunk *send_wc = NULL;

  LOG(DEBUG, "send id %u %s", chunk_array->data_id, transport->remote_ip);

  for (int i=0; i<chunk_num; i++) {
    chunk = chunk_array->data[i];
    copy_len = (len > RDMA_BODY_SIZE ? RDMA_BODY_SIZE : len);
    send_wc = (struct rdma_work_chunk *)malloc(sizeof(struct rdma_work_chunk));
    send_wc->transport = transport;
    send_wc->chunk = chunk;
    send_wc->len = copy_len;
    rdma_transport_send(transport, send_wc);
    len -= RDMA_BODY_SIZE;
  }
}

static void copy_msg_to_rdma(varray_t *chunk_array, uint8_t *msg, uint32_t len) {
  LOG(DEBUG, "copy %u chunks %u byte to rdma", chunk_array->size, len);
  uint32_t copy_len = 0, now_len = len;
  uint32_t data_id = chunk_array->data_id;
  uint32_t chunk_num = chunk_array->size;
  uint8_t *now = msg;
  struct rdma_chunk *chunk = NULL;
  for (uint32_t i=0; i<chunk_num; i++) {
    chunk = chunk_array->data[i];
    copy_len = (now_len > RDMA_BODY_SIZE ? RDMA_BODY_SIZE : now_len);
    chunk->header.data_id = data_id;
    chunk->header.chunk_num = chunk_num;
    chunk->header.flags = 0;
    chunk->header.chunk_len = copy_len;
    chunk->header.data_len = len;
    chunk->header.chunk_id = i;
    memcpy(chunk->body, now, copy_len);
    now += copy_len;
    now_len -= RDMA_BODY_SIZE;
  }
}

static void copy_header_and_body_to_rdma(varray_t *chunk_array, uint8_t *header, uint32_t head_len,
                                         uint8_t *body, uint32_t body_len) {
  uint32_t data_id = chunk_array->data_id;
  uint32_t chunk_num = chunk_array->size;
  struct rdma_chunk *chunk = NULL;
  int is_header = 1;
  uint32_t len = head_len + body_len;
  uint32_t copy_len = 0, now_len = len;
  uint8_t *now = header;
  for (uint32_t i=0; i<chunk_num; i++) {
    chunk = chunk_array->data[i];
    copy_len = (now_len > RDMA_BODY_SIZE ? RDMA_BODY_SIZE : now_len);
    chunk->header.data_id = data_id;
    chunk->header.chunk_num = chunk_num;
    chunk->header.flags = 0;
    chunk->header.chunk_len = copy_len;
    chunk->header.data_len = len;
    chunk->header.chunk_id = i;

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

    now_len -= copy_len;
  }
}

static struct rdma_transport *get_transport_from_ip(const char *host, uint16_t port) {
  pthread_mutex_lock(&g_rdma_context->hash_lock);

  char *remote_ip_str = g_hash_table_lookup(g_rdma_context->host2ipstr, host);
  if (remote_ip_str == NULL) {
    char *key = (char *)calloc(1, IP_CHAR_SIZE);
    strcpy(key, host);
    char *value = (char *)calloc(1, IP_CHAR_SIZE);
    set_ip_from_host(host, value);
    g_hash_table_insert(g_rdma_context->host2ipstr, key, value);
    remote_ip_str = g_hash_table_lookup(g_rdma_context->host2ipstr, host);
  }
  GPR_ASSERT(remote_ip_str);

  struct ip_hash_value *value =
      g_hash_table_lookup(g_rdma_context->hash_table, remote_ip_str);
  struct rdma_transport *client;
  if (value == NULL) {
    char *key = (char *)calloc(1, IP_CHAR_SIZE);
    strcpy(key, remote_ip_str);
    value = (struct ip_hash_value *)calloc(1, sizeof(struct ip_hash_value));
    client = value->transport = (struct rdma_transport *)calloc(1, sizeof(struct rdma_transport));
    pthread_mutex_init(&value->connect_lock, NULL);
    // client
    client->data_id = 0;
    pthread_mutex_init(&client->id_lock, NULL);
    strcpy(client->remote_ip, remote_ip_str);
    char local_ip_str[IP_CHAR_SIZE];
    set_local_ip(local_ip_str);
    strcpy(client->local_ip, local_ip_str);

    g_hash_table_insert(g_rdma_context->hash_table, key, value);

    pthread_mutex_unlock(&g_rdma_context->hash_lock);

    pthread_mutex_lock(&value->connect_lock);
    if (client->rc_qp == NULL) {
      rdma_create_connect(client);
    }
    pthread_mutex_unlock(&value->connect_lock);

    if (connect_server(remote_ip_str, port) < 0) {
      LOG(ERROR, "connect server %s:%u failed", remote_ip_str, port);
      abort();
    }

    pthread_mutex_lock(&value->connect_lock);
    if (client->is_ready == 0) {
      rdma_complete_connect(client);
      client->is_ready = 1;
    }
    pthread_mutex_unlock(&value->connect_lock);
  } else if (value->transport->is_ready == 0) {
    int is_ready = value->transport->is_ready;
    pthread_mutex_unlock(&g_rdma_context->hash_lock);

    while (is_ready == 0) {
      pthread_mutex_lock(&value->connect_lock);
      is_ready = value->transport->is_ready;
      pthread_mutex_unlock(&value->connect_lock);
      usleep(100);
    }
  } else {
    pthread_mutex_unlock(&g_rdma_context->hash_lock);
  }

  GPR_ASSERT(value->transport->is_ready == 1);
  return value->transport;
}

static int connect_server(const char *ip_str, uint16_t port) {
  LOG(DEBUG, "connect the ip = %s, port = %d", ip_str, port);
  if (port == 0) {
    port = IB_SERVER_PORT;
  }

  // if link is existing, return
  struct ip_hash_value *value = g_hash_table_lookup(g_rdma_context->hash_table, ip_str);
  GPR_ASSERT(value);
  struct rdma_transport *client = value->transport;
  GPR_ASSERT(client);
  if (client->rc_qp == NULL) {
    LOG(ERROR, "not create the qp to %s", ip_str);
    return -1;
  }

  struct sockaddr_in srv_addr;
  memset(&srv_addr, 0, sizeof(srv_addr));
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(port);
  srv_addr.sin_addr.s_addr = inet_addr(ip_str);

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

  // this is a blocking function
  if (exchange_info(client_fd, &client->local_qp_attr, &client->remote_qp_attr, client->rc_qp, true) < 0) {
    LOG(ERROR, "client exchange information failed");
    goto error;
  }

  close(client_fd);
  return 0;
error:
  if (client_fd > 0) {
    close(client_fd);
  }
  free(client);
  return -1;
}

