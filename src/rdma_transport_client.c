#include "rdma_transport_client.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

extern struct rdma_context *g_rdma_context;

int connect(const char *host, int port) {
  LOG(DEBUG, "connect the host = %s, port = %d", host, port);

  if (port == -1) {
    port = IB_SERVER_PORT;
  }

  struct hostent *he = gethostbyname(host);
  if (he == NULL) {
    LOG(ERROR, "get host by name failed.");
    return -1;
  }
  char ip_str[32]={'\0'};
  inet_ntop(he->h_addrtype, he->h_addr, ip_str, sizeof(ip_str));

  struct sockaddr_in srv_addr;
  memset(&srv_addr, 0, sizeof(srv_addr));
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(port);
  srv_addr.sin_addr.s_addr = inet_addr(ip_str);

  struct rdma_transport_client *client =
      (struct rdma_transport_client *)malloc(sizeof(struct rdma_transport_client));
  memset(client, 0, sizeof(struct rdma_transport_client));

  if ((client->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    LOG(ERROR, "socket failed");
    return -1;
  }

  if (connect(client->fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
    LOG(ERROR, "connect error: %s", strerror(errno));
    goto error;
  }
error:
  if (client->fd > 0) {

  }
}

int send_msg(const char *host, uint8_t *msg, uint32_t len) {
;
}