/*
 * RdmaCommon.h
 *
 *  Created on: May 14, 2016
 *      Author: yurujie
 */

#ifndef RDMACOMMON_H_
#define RDMACOMMON_H_

#include <infiniband/verbs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>


#define RDMA_DEBUG 0

extern FILE *fp;

#if RDMA_DEBUG == 1
#define rdma_debug_init()\
	fp = fopen("rdma_engine_log.txt", "w+");
#define rdma_debug(msg, args...)\
	fprintf(fp, "%s : %d : "msg, __FILE__, __LINE__, ## args);\
	fflush(fp);
#elif RDMA_DEBUG == 2
#define rdma_debug(msg, args...)\
	printf("%s : %d : "msg, __FILE__, __LINE__, ## args);
#define rdma_debug_init()
#else
#define rdma_debug(msg, args...)
#define rdma_debug_init()
#endif /* RDMA_DEBUG */

#define Q_DEPTH 1024
#define IB_PHYS_PORT 1

#define IP_PORT_NUM 6789

#define RDMA_BUF_FLAG			(IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE)
#define RDMA_QKEY				0x11111111

#define INIT_RECV_NUMBER		32

#define HANDLE_RECV_THREADS		1

#define SAFE_RELEASE(p)			if (!p) free(p);

typedef struct stag {

	uint64_t buf;
	uint32_t size;
	uint32_t rkey;
}stag;

typedef struct qp_attr {

	uint32_t	addr;
	int lid;
	int qpn;
	int psn;
}qp_attr;


union ibv_gid get_gid(struct ibv_context *context);
uint16_t get_local_lid(struct ibv_context *context);
int exchangeAttrCtoS(char *serverAddr, qp_attr *localQpAttr, qp_attr *remoteQpAttr);
int exchangeAttrStoC(int sfd, qp_attr *localQpAttr, qp_attr *remoteQpAttr);
int sendAttrtoRemote(int sfd, qp_attr *localQpAttr);
int recvAttrfromRemote(int sfd, qp_attr *remoteQpAttr);
struct ibv_qp *createQp(struct ibv_cq *cq, struct ibv_pd *pd);
int modifyQptoInit(struct ibv_qp *qp);
int modifyQptoRts(struct ibv_qp *qp, struct qp_attr *localQpAttr);


#endif /* RDMACOMMON_H_ */
