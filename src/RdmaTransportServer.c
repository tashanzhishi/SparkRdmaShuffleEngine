/*
 * RdmaTransportServer.c
 *
 *  Created on: May 17, 2016
 *      Author: yurujie
 */

#include "RdmaTransportServer.h"


static int initSocket(char *host, int port);
static void destroySocket();
static int initRdma();
static void destroyRdma();
static void* listenThreadProc(void *arg);
static void* poolCqThreadProc(void *arg);
static void* handleCallbackProc(void *arg);
static void handleSendComplete(struct ibv_wc *wc);
static void handleRecvComplete(struct ibv_wc *wc);
static void handleRecvBuf(rdma_buffer *rbuf);


TransportServerRdma srv = {0};



int initServer(char *host, int port) {

	int ret, i;

	if (srv.ready) {
		return  0;
	}

	memset(&srv, 0, sizeof(TransportServerRdma));

	ret = initHashTable((hash_table *)&srv.remotePtrTable, sizeof(HASH_NODE_TYPE(remote)), REMOTE_TABLE_SIZE);
	if (ret) {
		return -1;
	}

	ret = initHashTable((hash_table *)&srv.receivingTable, sizeof(HASH_NODE_TYPE(receiving)), RECEIVING_TABLE_SIZE);
	if (ret) {
		return -1;
	}

	for (i = 0; i < HANDLE_RECV_THREADS; i++) {
		ret = initMutexLinkQueue((mutex_link_queue *)&srv.cbQueue[i], sizeof(CallbackParam));
		if (ret) {
			return -1;
		}
	}

	ret = initSocket(host, port);
	if (ret) {
		return -1;
	}

	ret = initRdma();
	if (ret) {
		return -1;
	}

	pthread_mutex_init(&srv.lock, 0);
	pthread_mutex_init(&srv.connLock, 0);
	srv.ready = 1;
	srv.msgId = 1;
	srv.curThreadId = 0;

	return 0;
}

void destroyServer() {

	RemoteInfo *remoteInfo;
	int i;

	rdma_debug("enter destroyServer.\n");

	pthread_mutex_destroy(&srv.lock);
	pthread_mutex_destroy(&srv.connLock);
	destroyRdma();
	destroySocket();

	for (i = 0; i < HANDLE_RECV_THREADS; i++) {
		destroyMutexLinkQueue((mutex_link_queue *)&srv.cbQueue[i]);
	}

	// free remote information link table
	while (srv.remoteTable) {
		remoteInfo = srv.remoteTable;
		srv.remoteTable = srv.remoteTable->next;
		pthread_mutex_destroy(&remoteInfo->lock);
		ibv_destroy_ah(remoteInfo->ah);
		free(remoteInfo);
	}

	destroyHashTable((hash_table *)&srv.receivingTable);
	destroyHashTable((hash_table *)&srv.remotePtrTable);
}

// may cause short time blocked
int applyforRecvs(RemoteInfo *remoteInfo, int chunks, int *req) {

	int count = 0;

	if (chunks <= 0) {
		rdma_debug("error: apply count must be positive.\n");
		return -1;
	}

	while (!count) {
		pthread_mutex_lock(&remoteInfo->lock);
		if (remoteInfo->recvs == 0) {	// there is no recv buffers, wait for rdma control message
			pthread_mutex_unlock(&remoteInfo->lock);
			continue;
		}
		else if (chunks <= remoteInfo->recvs) {
			count = chunks;
			remoteInfo->recvs -= chunks;
			if (remoteInfo->recvs < INIT_RECV_NUMBER && remoteInfo->requestingRecvs <= 0) {
				*req = INIT_RECV_NUMBER - remoteInfo->recvs;
			}
			else {
				*req = 0;
			}
		}
		else {
			count = remoteInfo->recvs;
			remoteInfo->recvs = 0;
			*req = (chunks - count > REQ_RECVS_ONCE ? REQ_RECVS_ONCE : (chunks - count));
		}
		remoteInfo->requestingRecvs += *req;
		pthread_mutex_unlock(&remoteInfo->lock);
	}

	return count;
}

uint64_t getMsgId() {

	uint64_t id;
	uint64_t addr = srv.localQpAttr.addr;

	pthread_mutex_lock(&srv.lock);
	id = srv.msgId;
	srv.msgId++;
	pthread_mutex_unlock(&srv.lock);

	id = (id & 0x0ffff) | (addr & 0x0ffff0000);

	return id;
}



static int initSocket(char *host, int port) {

	struct sockaddr_in serv_addr;
	struct hostent *he;
	int reuse = 1;

	rdma_debug("host = %s.\n", host);
	he = gethostbyname(host);
	if (!he) {
		rdma_debug("get host by name failed.\n");
		return 0;
	}

	if (-1 == port) {
		port = IP_PORT_NUM;
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	bcopy((char *)he->h_addr, (char *)&serv_addr.sin_addr.s_addr,
			he->h_length);

	srv.localQpAttr.addr = serv_addr.sin_addr.s_addr;

	srv.sfd = socket(AF_INET, SOCK_STREAM, 0);

	if (setsockopt(srv.sfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(int)) < 0) {
		rdma_debug("set socket reuse address failed.\n");
		return -1;
	}

	if (bind(srv.sfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		rdma_debug("bind address failed\n");
		return -1;
	}
	listen(srv.sfd, 1024);
	createThread(listenThreadProc, 0);

	return 0;
}

static void destroySocket() {

	srv.accepting = 0;
	close(srv.sfd);
}

static int initRdma() {

	int ret, i;
	struct ibv_device **devList;
	struct ibv_device *dev;

	srand48(time(NULL));

	devList = ibv_get_device_list(NULL);
	dev = devList[0];

	srv.ibcxt = ibv_open_device(dev);
	ibv_free_device_list(devList);
	srv.pd = ibv_alloc_pd(srv.ibcxt);
	srv.cq = ibv_create_cq(srv.ibcxt, Q_DEPTH + 1, NULL, NULL, 0);

	srv.qp = createQp(srv.cq, srv.pd);
	if (NULL == srv.qp) {
		rdma_debug("create qp failed.\n");
		return -1;
	}

	ret = modifyQptoInit(srv.qp);

	srv.localQpAttr.lid = get_local_lid(srv.ibcxt);
	srv.localQpAttr.qpn = srv.qp->qp_num;
	srv.localQpAttr.psn = lrand48() & 0xffffff;

	rdma_debug("pd = %p\n", srv.pd);
	ret = initRdmaBufferPool(&srv.rbp, srv.pd);
	if (ret) {
		rdma_debug("initialize rdma buffer pool failed.\n");
		return -1;
	}

	ret = modifyQptoRts(srv.qp, &srv.localQpAttr);
	if (ret) {
		return -1;
	}

	createThread(poolCqThreadProc, 0);
	for (i = 0; i < HANDLE_RECV_THREADS; i++) {
		createThread(handleCallbackProc, (void *)i);
	}

	return 0;
}

static void destroyRdma() {

	destroyRdmaBufferPool(&srv.rbp);

	if (srv.qp) {
		ibv_destroy_qp(srv.qp);
		srv.qp = NULL;
	}
	/*
	if (srv.cq) {
		ibv_destroy_cq(srv.cq);
		srv.cq = NULL;
	}

	if (srv.pd) {
		ibv_dealloc_pd(srv.pd);
		srv.pd = NULL;
	}

	if (srv.ibcxt) {
		ibv_close_device(srv.ibcxt);
		srv.ibcxt = NULL;
	}*/
}

static void* listenThreadProc(void *arg) {

	int newsfd, i, ret;
	struct sockaddr_in host;
	qp_attr remoteQpAttr;
	RemoteInfo *remoteInfo;
	struct ibv_ah_attr ah_attr;
	rdma_buffer *rbuf;
	int addr_len;

	ah_attr.is_global		= 0;
	ah_attr.sl				= 0;
	ah_attr.src_path_bits	= 0;
	ah_attr.port_num		= IB_PHYS_PORT;

	srv.accepting = 1;
	while(srv.accepting) {
		newsfd = accept(srv.sfd, (struct sockaddr *)&host, &addr_len);
		ret = recvAttrfromRemote(newsfd, &remoteQpAttr);
		if (ret) {
			continue;
		}

		// create remote information object, and put it into the remote link table
		remoteInfo = (RemoteInfo *)malloc(sizeof(RemoteInfo));
		pthread_mutex_lock(&srv.lock);
		remoteInfo->next = srv.remoteTable;
		srv.remoteTable = remoteInfo;
		pthread_mutex_unlock(&srv.lock);

		// fill information
		ah_attr.dlid = remoteQpAttr.lid;
		pthread_mutex_init(&remoteInfo->lock, 0);
		remoteInfo->ah = ibv_create_ah(srv.pd, &ah_attr);
		remoteInfo->qpn = remoteQpAttr.qpn;
		remoteInfo->recvs = INIT_RECV_NUMBER;
		remoteInfo->requestingRecvs = 0;
		remoteInfo->addr = remoteQpAttr.addr;

		// post receives
		for (i = 0; i < INIT_RECV_NUMBER; i++) {
			rbuf = getRdmaBufferfromPool(&srv.rbp);
			postRecv(srv.qp, rbuf);
		}
		pthread_mutex_lock(&srv.lock);
		srv.postedRecvs += INIT_RECV_NUMBER;
		pthread_mutex_unlock(&srv.lock);

		// put remote information object's pointer into hash table
		rdma_debug("addr = 0x%08x\n", remoteQpAttr.addr);
		put((hash_table *)&srv.remotePtrTable, ipHash, remoteQpAttr.addr, &remoteInfo, sizeof(RemoteInfo *));

		sendAttrtoRemote(newsfd, &srv.localQpAttr);
		close(newsfd);
	}

	return NULL;
}

static void* poolCqThreadProc(void *arg) {

	struct ibv_wc wc;
	int comps = 0;
	int i;

	srv.polling = 1;

	while (srv.polling) {

		comps = 0;

		while (!comps) {

			comps = ibv_poll_cq(srv.cq, 1, &wc);
		}

		if (wc.status != IBV_WC_SUCCESS) {
			rdma_debug("bad wc status, status=%d\n", wc.status);
		}
		else {
			switch(wc.opcode) {
			case IBV_WC_SEND: {
				handleSendComplete(&wc);
				break;
			}
			case IBV_WC_RECV: {
				handleRecvComplete(&wc);
				break;
			}
			default: {
				break;
			}
			}
		}
	}

	return NULL;
}


static void* handleCallbackProc(void *arg) {

	CallbackParam *param = NULL;
	int id = (int)arg;

	int times = 0;

	rdma_debug("thread id = %d\n", id);

	while (1) {
		param = (CallbackParam *)deQueue((mutex_link_queue *)&srv.cbQueue[id]);
		if (param == NULL) {
			continue;
		}
		times++;
		// rdma_debug("thread %d: dequeue times = %d\n", id, times);
		struct in_addr iaddr;
		iaddr.s_addr = param->iaddr;
		rdma_debug("thread %d: receive %d bytes from %s\n", id, param->cbuf->bytes, inet_ntoa(iaddr));
		jni_channelCallback(inet_ntoa(iaddr), param->cbuf->jbbuf, param->cbuf->bytes);
		free(param->cbuf);
		free(param);
	}

	return NULL;
}

static void handleRecvBuf(rdma_buffer *rbuf) {
	rdma_buffer *rbuf_s;
	rdma_msg_header *rmh;
	rdma_ctrl_msg *rcm;
	RemoteInfo *remoteInfo;
	uint8_t	*sig;
	int ret;
	int i, recv_posted = 0;;

	static int times = 0;

	if (rbuf) {
		rdma_debug("======== receive a message ========\n");
		sig = (uint8_t *)(rbuf->buf + RECV_EXT);
		if (*sig == MSG_SIG_USER) {	// this is a user message
			rdma_debug("receive a user message.\n");
			srv.postedRecvs--;
			rdma_debug("srv.postedRecvs = %d\n", srv.postedRecvs);
			rmh = (rdma_msg_header *)sig;
			rdma_debug("rmh->recvs_req = %d\n", rmh->recvs_req);
			if (rmh->recvs_req > 0) {	// this message contains request of receives buffers
				rdma_debug("receive a buffer requests: %d.\n", rmh->recvs_req);
				recv_posted = 0;
				for (i = 0; i < rmh->recvs_req; i++) {
					if (0 == postRecv(srv.qp, getRdmaBufferfromPool(&srv.rbp))) {
						recv_posted++;
					}
				}

				// send control message to client
				rbuf_s = getRdmaBufferfromPool(&srv.rbp);

				rcm = (rdma_ctrl_msg *)(rbuf_s->buf);
				rcm->sig = MSG_SIG_CTRL;
				rcm->addr = srv.localQpAttr.addr;
				rcm->recvs_alloc = recv_posted;

				ret = get((hash_table *)&srv.remotePtrTable, ipHash,
					rmh->addr, &remoteInfo, sizeof(remoteInfo));
				if (ret) {
					if (!remoteInfo) {
						rdma_debug("remote information structure pointer is null.\n");
						return;
					}

					pthread_mutex_lock(&srv.lock);
					srv.postedRecvs += rcm->recvs_alloc;
					pthread_mutex_unlock(&srv.lock);

					// int recvs, recvs_req;
					// recvs = applyforRecvs(remoteInfo, 1, &recvs_req);
					postSend(srv.qp, rbuf_s, sizeof(rdma_ctrl_msg), remoteInfo->qpn, remoteInfo->ah);
				}
			}

			ChunksBuf *cbuf = NULL;
			ret = get((hash_table *)&srv.receivingTable, ptrHash, rmh->id, &cbuf, sizeof(ChunksBuf *));

			struct in_addr iaddr;
			iaddr.s_addr = rmh->addr;
			rdma_debug("cbuf = %016p\n", cbuf);
			rdma_debug("msgId = %d, msgSeq = %d, srcAddr = %s\n", rmh->id, rmh->seq, inet_ntoa(iaddr));

			if (!cbuf) {	// if this is the first chunk received
				cbuf = (ChunksBuf *)malloc(sizeof(ChunksBuf));
				cbuf->buf = (char *)jni_allocDirectBuf(&(cbuf->jbbuf), (rmh->chunks * CHUNK_SIZE) * sizeof(char));
				cbuf->bytes = rmh->len;
				cbuf->chunks_totle = rmh->chunks;
				cbuf->chunks_recved = 1;
				memcpy(cbuf->buf + rmh->seq * CHUNK_SIZE, rbuf->buf + RECV_EXT + sizeof(rdma_msg_header), rmh->len);
				if (rmh->chunks > 1) {
					put((hash_table *)&srv.receivingTable, ptrHash, rmh->id, &cbuf, sizeof(ChunksBuf *));
				}
				else {
					CallbackParam *param = (CallbackParam *)malloc(sizeof(CallbackParam));

					param->iaddr = rmh->addr;
					param->cbuf = cbuf;
					struct in_addr iaddr;
					iaddr.s_addr = param->iaddr;
					rdma_debug("receive %d bytes from %s\n", param->cbuf->bytes, inet_ntoa(iaddr));
					enQueue((mutex_link_queue *)&srv.cbQueue[srv.curThreadId], param);
					srv.curThreadId = (srv.curThreadId + 1) % HANDLE_RECV_THREADS;
					times++;
					rdma_debug("enqueue times = %d\n", times);
				}
			}
			else {
				memcpy(cbuf->buf + rmh->seq * CHUNK_SIZE, rbuf->buf + RECV_EXT + sizeof(rdma_msg_header), rmh->len);
				cbuf->chunks_recved++;
				cbuf->bytes += rmh->len;
				rdma_debug("cbuf->chunks_recved = %d\n", cbuf->chunks_recved);
				if (cbuf->chunks_recved == cbuf->chunks_totle) {
					ChunksBuf *nullPtr = NULL;
					put((hash_table *)&srv.receivingTable, ptrHash, rmh->id, &nullPtr, sizeof(ChunksBuf *));

					CallbackParam *param = (CallbackParam *)malloc(sizeof(CallbackParam));

					param->iaddr = rmh->addr;
					param->cbuf = cbuf;
					struct in_addr iaddr;
					iaddr.s_addr = param->iaddr;
					rdma_debug("receive %d bytes from %s\n", param->cbuf->bytes, inet_ntoa(iaddr));
					enQueue((mutex_link_queue *)&srv.cbQueue[srv.curThreadId], param);
					srv.curThreadId = (srv.curThreadId + 1) % HANDLE_RECV_THREADS;
					times++;
					rdma_debug("enqueue times = %d\n", times);
				}
			}
		}
		else if (*sig == MSG_SIG_CTRL) {	// if this is a control message
			rdma_debug("receive a control message.\n");
			rcm = (rdma_ctrl_msg *)sig;
			ret = get((hash_table *)&srv.remotePtrTable, ipHash,
				rcm->addr, &remoteInfo, sizeof(remoteInfo));
			if (ret) {
				if (!remoteInfo) {
					rdma_debug("remote information structure pointer is null.\n");
					return;
				}
				pthread_mutex_lock(&remoteInfo->lock);
				remoteInfo->recvs += rcm->recvs_alloc;
				remoteInfo->requestingRecvs -= rcm->recvs_alloc;
				pthread_mutex_unlock(&remoteInfo->lock);
			}
		}
		else {
			rdma_debug("unsupported message.\n");
		}
		rbuf->next = NULL;
		returnRdmaBuffertoPool(&srv.rbp, rbuf);
	}
}

static void handleSendComplete(struct ibv_wc *wc) {

	rdma_buffer *rbuf;

	rbuf = (rdma_buffer *)wc->wr_id;
	rbuf->next = NULL;
	returnRdmaBuffertoPool(&srv.rbp, rbuf);
}

static void handleRecvComplete(struct ibv_wc *wc) {

	rdma_buffer *rbuf;

	static int times = 0;
	rdma_debug("handle receive complete times = %d\n", ++times);

	rbuf = (rdma_buffer *)wc->wr_id;
	rdma_debug("receive complete:	rbuf = 0x%016x\n", rbuf);
	handleRecvBuf(rbuf);
}
