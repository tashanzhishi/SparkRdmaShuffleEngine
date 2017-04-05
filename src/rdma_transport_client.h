#ifndef RDMA_TRANSPORT_CLIENT_H_
#define RDMA_TRANSPORT_CLIENT_H_

#include "rdma_utils.h"

#include <stdint.h>

//#include "rdma_transport_server.h"
//#include "RdmaMsgHeader.h"

struct rdma_transport_client {
  struct rdma_transport transport;
  int fd;
};

int send_msg(const char *host, uint8_t *msg, uint32_t len);
int send_msg_with_header(const char *host, uint8_t *header, int hlen, char *body, int blen);

#endif /* RDMA_TRANSPORT_CLIENT_H_ */
