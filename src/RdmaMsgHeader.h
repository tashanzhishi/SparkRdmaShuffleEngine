/*
 * RdmaMsgHeader.h
 *
 *  Created on: May 26, 2016
 *      Author: yurujie
 */

#ifndef RDMAMSGHEADER_H_
#define RDMAMSGHEADER_H_

#include <stdint.h>

#define MSG_SIG_USER		1
#define MSG_SIG_CTRL		2

typedef struct __attribute__((__packed__)) rdma_msg_header {

	uint8_t		sig;
	uint64_t 	id;
	uint32_t	seq;
	uint32_t	chunks;
	uint16_t	len;
	uint32_t	addr;
	uint8_t		recvs_req;

}rdma_msg_header;

typedef struct __attribute__((__packed__)) rdma_ctrl_msg {

	uint8_t		sig;
	uint32_t	addr;
	uint8_t		recvs_alloc;

}rdma_ctrl_msg;

#define RDMA_UD_MSG_SIZE	4096
#define CHUNK_SIZE			(RDMA_UD_MSG_SIZE - sizeof(rdma_msg_header))

#define MAX_SEND_ID			65536

#endif /* RDMAMSGHEADER_H_ */
