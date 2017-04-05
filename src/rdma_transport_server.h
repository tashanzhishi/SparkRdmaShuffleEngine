#ifndef RDMA_TRANSPORT_SERVER_H_
#define RDMA_TRANSPORT_SERVER_H_

#include "rdma_utils.h"
#include "thread.h"
#include "RdmaMsgHeader.h"
#include <jni.h>
#include "jni_common.h"


#define REMOTE_TABLE_SIZE			1024
#define RECEIVING_TABLE_SIZE		1024

#define HOST_NAME_LEN				32

#define MAX_CHUNK_COUNT_ONCE		8

#define REQ_RECVS_ONCE				16

typedef void (*msg_handler)(char *msg, int len);

typedef struct RemoteInfo {

	struct ibv_ah		*ah;
	uint32_t			addr;
	int					qpn;

	int					recvs;		// receive buffers that remote host allocates from local host
	int					requestingRecvs;

	struct RemoteInfo 	*next;
	pthread_mutex_t		lock;

}RemoteInfo;

typedef struct ChunksBuf {

	char				*buf;
	int					bytes;
	int					chunks_totle;
	int					chunks_recved;
	jobject				jbbuf;
}ChunksBuf;

typedef struct CallbackParam {
	uint32_t	 	iaddr;
	ChunksBuf		*cbuf;
	struct CallbackParam *next;
}CallbackParam;

HASH_TABLE_TYPE_DEF(RemoteInfo *, remote)
HASH_TABLE_TYPE_DEF(ChunksBuf *, receiving)

MUTEX_LINK_QUEUE_TYPE_DEF(CallbackParam, cb)

struct transport_server_rdma {

	int						ready;

	int 					sfd; // server socket fd
	char 					*host;
	int 					accepting;

	int						postedRecvs;

	struct ibv_context		*ibcxt;
	struct ibv_qp			*qp;
	struct ibv_pd			*pd;
	struct ibv_cq			*cq;
	int						polling;

	struct qp_attr local_qp_attr;

	HASH_TABLE_TYPE(remote) remotePtrTable;
	RemoteInfo				*remoteTable;

	int						msgId;
	pthread_mutex_t			lock;

	LINK_QUEUE_TYPE(cb)		cbQueue[HANDLE_RECV_THREADS];
	int						curThreadId;

	HASH_TABLE_TYPE(receiving)	receivingTable;

	struct rdma_buffer_pool		rbp;

	pthread_mutex_t			connLock;

};


int initServer(char *host, int port);
void destroyServer();


int applyforRecvs(RemoteInfo *remoteInfo, int chunks, int *req);
uint64_t getMsgId();


#endif /* RDMA_TRANSPORT_SERVER_H_ */
