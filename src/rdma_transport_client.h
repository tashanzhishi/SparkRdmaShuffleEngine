#ifndef RDMA_TRANSPORT_CLIENT_H_
#define RDMA_TRANSPORT_CLIENT_H_

#include "rdma_utils.h"

#include <stdint.h>


/*struct rdma_transport_client {
  struct rdma_transport transport;
  char local_ip[IP_CHAR_SIZE];
  char remote_ip[IP_CHAR_SIZE];
  uint32_t data_id;
};*/

int send_msg(const char *host, uint16_t port, uint8_t *msg, uint32_t len);
int send_msg_with_header(const char *host, uint16_t port,
                         uint8_t *header, uint32_t head_len, uint8_t *body, uint32_t body_len);

#endif /* RDMA_TRANSPORT_CLIENT_H_ */
