/*
 * RdmaUtils.h
 *
 *  Created on: May 17, 2016
 *      Author: yurujie
 */

#ifndef RDMAUTILS_H_
#define RDMAUTILS_H_

#include "RdmaCommon.h"
#include "RdmaBufferPool.h"


int postRecv(struct ibv_qp *qp, rdma_buffer *rbuf);
int postSend(struct ibv_qp *qp, rdma_buffer *rbuf, int len, int qpn, struct ibv_ah *ah);


#endif /* RDMAUTILS_H_ */
