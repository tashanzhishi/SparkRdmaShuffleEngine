/*
 * RdmaUtils.c
 *
 *  Created on: May 17, 2016
 *      Author: yurujie
 */

#include "RdmaUtils.h"



int postRecv(struct ibv_qp *qp, rdma_buffer *rBuf) {

	struct ibv_recv_wr *bad_wr = NULL;

	struct ibv_sge sge = {
		.addr	= (uint64_t)rBuf->buf,
		.length = RDMA_BUFFER_SIZE,
		.lkey	= rBuf->mr->lkey
	};

	struct ibv_recv_wr recv_wr = {
		.sg_list    = &sge,
		.num_sge    = 1,
		.wr_id		= (uint64_t)rBuf
	};

	int ret = ibv_post_recv(qp, &recv_wr, &bad_wr);
	if(ret) {
		rdma_debug("Error %d posting receive.\n", ret);
		return -1;
	}

	return 0;
}

int postSend(struct ibv_qp *qp, rdma_buffer *rBuf, int len, int qpn, struct ibv_ah *ah) {

	//rdma_debug("post send:	*rBuf = 0x%02x\n", *(uint8_t *)rBuf);
	struct ibv_send_wr *bad_wr = NULL;

	struct ibv_sge sge = {
		.addr	= (uint64_t)rBuf->buf,
		.length = len,
		.lkey	= rBuf->mr->lkey
	};

	static int times = 0;
	rdma_debug("post send times = %d\n", ++times);

	struct ibv_send_wr send_wr;
	bzero(&send_wr, sizeof(send_wr));
	send_wr.sg_list 			= &sge;
	send_wr.num_sge 			= 1;
	send_wr.opcode 				= IBV_WR_SEND;
	send_wr.send_flags			= IBV_SEND_SIGNALED;
	if (len < 256) {
		send_wr.send_flags		= IBV_SEND_SIGNALED|IBV_SEND_INLINE;
	}
	send_wr.wr_id				= (uint64_t)rBuf;

	send_wr.wr.ud.ah 			= ah;
	send_wr.wr.ud.remote_qpn 	= qpn;
	send_wr.wr.ud.remote_qkey 	= RDMA_QKEY;

	int ret = ibv_post_send(qp, &send_wr, &bad_wr);
	if (ret) {
		rdma_debug("Error %d posting send.\n", ret);
		return -1;
	}

	return 0;
}
