/*
 * RdmaCommon.c
 *
 *  Created on: May 14, 2016
 *      Author: yurujie
 */

#include "RdmaCommon.h"

FILE *fp = NULL;


union ibv_gid get_gid(struct ibv_context *context) {

	union ibv_gid ret_gid;

	ibv_query_gid(context, IB_PHYS_PORT, 0, &ret_gid);

	return ret_gid;
}

uint16_t get_local_lid(struct ibv_context *context) {

	struct ibv_port_attr attr;

	if (ibv_query_port(context, IB_PHYS_PORT, &attr))
		return 0;

	return attr.lid;
}

int exchangeAttrCtoS(char *serverAddr, qp_attr *localQpAttr, qp_attr *remoteQpAttr) {

	int sfd;
	struct sockaddr_in serv_addr;
	struct hostent *he;

	sfd = socket(AF_INET, SOCK_STREAM, 0);
	he = gethostbyname(serverAddr);
	if (!he) {
		rdma_debug("get host by name failed.\n");
		return -1;
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(IP_PORT_NUM);
	bcopy((char *)he->h_addr, (char *)&serv_addr.sin_addr.s_addr,
			he->h_length);

	if(connect(sfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) {
		rdma_debug("connecting error.\n");
		return -1;
	}

	if(write(sfd, localQpAttr, sizeof(qp_attr)) < 0) {
		rdma_debug("failed to write local queue pair attribute to socket\n");
		return -1;
	}

	if(read(sfd, remoteQpAttr, sizeof(qp_attr)) < 0) {
		rdma_debug("failed to read remote queue pair attribute from socket\n");
		return -1;
	}

	close(sfd);

	return 0;
}

int exchangeAttrStoC(int sfd, qp_attr *localQpAttr, qp_attr *remoteQpAttr) {

	if(read(sfd, remoteQpAttr, sizeof(qp_attr)) < 0) {
		rdma_debug("failed to read remote queue pair attribute from socket\n");
		return -1;
	}

	if(write(sfd, localQpAttr, sizeof(qp_attr)) < 0) {
		rdma_debug("failed to write local queue pair attribute to socket\n");
		return -1;
	}

	return 0;
}

int recvAttrfromRemote(int sfd, qp_attr *remoteQpAttr) {

	if(read(sfd, remoteQpAttr, sizeof(qp_attr)) < 0) {
		rdma_debug("failed to read remote queue pair attribute from socket\nerrno=%d\n", errno);
		return -1;
	}

	return 0;
}

int sendAttrtoRemote(int sfd, qp_attr *localQpAttr) {

	if(write(sfd, localQpAttr, sizeof(qp_attr)) < 0) {
		rdma_debug("failed to write local queue pair attribute to socket\n");
		return -1;
	}

	return 0;
}

struct ibv_qp *createQp(struct ibv_cq *cq, struct ibv_pd *pd) {

	struct ibv_qp_init_attr init_attr = {
		.send_cq = cq,
		.recv_cq = cq,
		.cap     = {
			.max_send_wr  = Q_DEPTH,
			.max_recv_wr  = Q_DEPTH,
			.max_send_sge = 1,
			.max_recv_sge = 1,
			.max_inline_data = 256
		},
		.qp_type = IBV_QPT_UD
	};

	return ibv_create_qp(pd, &init_attr);
}

int modifyQptoInit(struct ibv_qp *qp) {

	struct ibv_qp_attr qp_attr = {
		.qp_state		= IBV_QPS_INIT,
		.pkey_index		= 0,
		.port_num		= IB_PHYS_PORT,
		.qkey 			= RDMA_QKEY
	};

	if (ibv_modify_qp(qp, &qp_attr,
		IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY)) {
		rdma_debug("failed to modify UD QP to INIT\n");
		return -1;
	}

	return 0;
}

int modifyQptoRts(struct ibv_qp *qp, struct qp_attr *localQpAttr) {

	struct ibv_qp_attr qp_attr = {
		.qp_state			= IBV_QPS_RTR,
	};

	if (ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE)) {
		rdma_debug("failed to modify QP to RTR, errno = %d\n", errno);
		return -1;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.qp_state		= IBV_QPS_RTS;
	qp_attr.sq_psn			= localQpAttr->psn;

	if(ibv_modify_qp(qp,
		&qp_attr, IBV_QP_STATE | IBV_QP_SQ_PSN)) {
		rdma_debug("failed to modify QP to RTS, errno = %d\n", errno);
		return -1;
	}

	return 0;
}
