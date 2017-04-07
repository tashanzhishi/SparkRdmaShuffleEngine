#ifndef RDMA_TRANSPORT_CLIENT_H_
#define RDMA_TRANSPORT_CLIENT_H_

#include "rdma_utils.h"

#include <stdint.h>

//#include "rdma_transport_server.h"
//#include "RdmaMsgHeader.h"

struct rdma_transport_client {
  struct rdma_transport transport;
  int fd;
  char local_ip[IP_CHAR_SIZE], remote_ip[IP_CHAR_SIZE];
};

int send_msg(char *host, uint16_t port, uint8_t *msg, uint32_t len);
int send_msg_with_header(char *host, uint8_t *header, uint32_t head_len, uint8_t *body, uint32_t body_len);

#endif /* RDMA_TRANSPORT_CLIENT_H_ */
