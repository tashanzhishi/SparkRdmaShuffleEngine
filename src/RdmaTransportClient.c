/*
 * RdmaTransportClient.c
 *
 *  Created on: May 9, 2016
 *      Author: yurujie
 */

#include "RdmaTransportClient.h"


static RemoteInfo* getRemoteInfo(char *host);



extern TransportServerRdma srv;


int sendMsg(char *host, char *msg, int len) {

	RemoteInfo *remoteInfo;
	uint32_t key;
	rdma_buffer *rbuf, *rbufList;
	rdma_msg_header *rmh;
	uint64_t id;
	int seq = 0;
	int i, ret, recvs = 0;
	uint32_t chunks;
	int count, recvs_req;

	if (len < 0) {
		return -1;
	}

	static int times = 0;
	rdma_debug("sendMsg times = %d\n", ++times);

	rdma_debug("host = %s, hlen = %d.\n", host, len);

	// get remote information from table
	remoteInfo = getRemoteInfo(host);
	if (!remoteInfo) {
		rdma_debug("get remote information failed.\n");
		return -1;
	}

	// cut message into chunks, then copy them to buffers
	chunks = len / CHUNK_SIZE + (len % CHUNK_SIZE != 0 ? 1 : 0);
	rbufList = getRdmaBufferListfromPool(&srv.rbp, chunks);
	rbuf = rbufList;
	id = getMsgId();
	for (i = 0; i < chunks; i++) {
		rmh = (rdma_msg_header *)(rbuf->buf);
		rmh->sig = MSG_SIG_USER;
		rmh->id = id;
		rmh->seq = seq++;
		rmh->chunks = chunks;
		rmh->len = (len > CHUNK_SIZE ? CHUNK_SIZE : len);
		rmh->addr = srv.localQpAttr.addr;
		rmh->recvs_req = 0;
		memcpy(rbuf->buf + sizeof(rdma_msg_header), msg + i * CHUNK_SIZE, rmh->len);
		rbuf = rbuf->next;
		len -= rmh->len;
	}

	rdma_debug("chunks = %d\n", chunks);

	// send chunks
	rbuf = rbufList;
	while (chunks > 0) {
		recvs = applyforRecvs(remoteInfo, chunks, &recvs_req);
		rdma_debug("recvs = %d, req = %d\n", recvs, recvs_req);

		for (i = 0; rbuf && (i < recvs); i++) {
			rmh = (rdma_msg_header *)(rbuf->buf);
			if (0 == i) {
				rmh->recvs_req = recvs_req;

				// post receive buffer for receiving control message
				if (recvs_req > 0) {
					postRecv(srv.qp, getRdmaBufferfromPool(&srv.rbp));
				}
			}
			ret = postSend(srv.qp, rbuf, sizeof(rdma_msg_header) + rmh->len, remoteInfo->qpn, remoteInfo->ah);
			if (ret) {
				rdma_debug("error: send chunk failed.\n");
				return -1;
			}
			rbuf = rbuf->next;
			chunks--;
		}
	}

	return 0;
}

int sendMsgWithHeader(char *host, char *header, int hlen, char *body, int blen) {

	RemoteInfo *remoteInfo;
	uint32_t key;
	rdma_buffer *rbuf, *rbufList;
	rdma_msg_header *rmh = NULL;
	uint64_t id;
	int seq = 0;
	int i, ret, recvs = 0;
	uint32_t chunks;
	int count, recvs_req;
	int len, leftlen;
	int isHeader = 1;
	char *mark;

	if (hlen < 0 || blen < 0) {
		return -1;
	}

	static int times = 0;
	rdma_debug("sendMsgWithHeader times = %d\n", ++times);

	len = hlen + blen;

	rdma_debug("host = %s, hlen = %d, blen = %d.\n", host, hlen, blen);
	// get remote information from table
	remoteInfo = getRemoteInfo(host);
	rdma_debug("7\n");
	if (!remoteInfo) {
		rdma_debug("get remote information failed.\n");
		return -1;
	}

	// cut message into chunks, then copy them to buffers
	chunks = len / CHUNK_SIZE + (len % CHUNK_SIZE != 0 ? 1 : 0);
	rbufList = getRdmaBufferListfromPool(&srv.rbp, chunks);
	rbuf = rbufList;
	id = getMsgId();

	rdma_debug("msgId = %d, chunks = %d\n", id, chunks);

	mark = header;
	leftlen = hlen;
	isHeader = 1;
	for (i = 0; i < chunks; i++) {
		rmh = (rdma_msg_header *)(rbuf->buf);
		rmh->sig = MSG_SIG_USER;
		rmh->id = id;
		rmh->seq = seq++;
		rmh->chunks = chunks;
		rmh->len = (len > CHUNK_SIZE ? CHUNK_SIZE : len);
		rmh->addr = srv.localQpAttr.addr;
		rmh->recvs_req = 0;

		if (isHeader) {
			if (leftlen >= CHUNK_SIZE) {
				rdma_debug("1: offset = %d\n", mark-header);
				memcpy(rbuf->buf + sizeof(rdma_msg_header), mark, CHUNK_SIZE);
				mark += CHUNK_SIZE;
				leftlen -= CHUNK_SIZE;
			} else {
				rdma_debug("2: offset = %d, len = %d\n", mark-header, rmh->len);
				memcpy(rbuf->buf + sizeof(rdma_msg_header), mark, leftlen);
				mark = body;
				rdma_debug("offset = %d, leftlen = %d\n", mark-body, leftlen);
				memcpy(rbuf->buf + sizeof(rdma_msg_header) + leftlen, mark, rmh->len - leftlen);
				mark += (rmh->len - leftlen);
				leftlen = blen - (rmh->len - leftlen);
				isHeader = 0;
			}
		} else {
			rdma_debug("3: offset = %d, len = %d, rbuf = 0x%016x\n", mark-body, rmh->len, rbuf);
			memcpy(rbuf->buf + sizeof(rdma_msg_header), mark, rmh->len);
			rdma_debug("4\n");
			mark += rmh->len;
		}

		len -= rmh->len;
		rdma_debug("5\n");
		rbuf = rbuf->next;
		rdma_debug("6: rbuf = %016p\n", rbuf);
	}

	rdma_debug("8\n");

	// send chunks
	rbuf = rbufList;
	rdma_buffer *nextRbuf;
	while (chunks > 0) {
		rdma_debug("applyforRecvs ......\n");
		recvs = applyforRecvs(remoteInfo, chunks, &recvs_req);
		rdma_debug("recvs = %d, req = %d\n", recvs, recvs_req);

		for (i = 0; rbuf && (i < recvs); i++) {
			rmh = (rdma_msg_header *)(rbuf->buf);
			rdma_debug("9\n");
			if (0 == i) {
				rmh->recvs_req = recvs_req;

				// post receive buffer for receiving control message
				if (recvs_req > 0) {
					postRecv(srv.qp, getRdmaBufferfromPool(&srv.rbp));
				}
			}
			nextRbuf = rbuf->next;
			ret = postSend(srv.qp, rbuf, sizeof(rdma_msg_header) + rmh->len, remoteInfo->qpn, remoteInfo->ah);
			if (ret) {
				rdma_debug("error: send chunk failed.\n");
				return -1;
			}
			rbuf = nextRbuf;
			chunks--;
		}
	}

	return 0;
}



static RemoteInfo* getRemoteInfo(char *host) {

	struct sockaddr_in serv_addr;
	struct hostent *he;
	int ret;
	int sfd;
	qp_attr remoteQpAttr;
	struct ibv_ah_attr ah_attr;
	rdma_buffer *rbuf;
	RemoteInfo *remoteInfo = NULL;
	int i;
	struct in_addr iaddr;

	rdma_debug("2\n");
	inet_aton(host, &iaddr);

	rdma_debug("3\n");
	pthread_mutex_lock(&srv.connLock);
	rdma_debug("4\n");
	ret = get((hash_table *)&srv.remotePtrTable, ipHash,
			iaddr.s_addr, &remoteInfo, sizeof(remoteInfo));

	if (!ret) {
		rdma_debug("5\n");
		sfd = socket(AF_INET, SOCK_STREAM, 0);

		he = gethostbyname(host);
		if (!he) {
			rdma_debug("get host by name failed.\n");
			pthread_mutex_unlock(&srv.connLock);
			return 0;
		}
		bzero((char *) &serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(IP_PORT_NUM);
		bcopy((char *)he->h_addr, (char *)&serv_addr.sin_addr.s_addr,
				he->h_length);

		if(connect(sfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) {
			rdma_debug("connecting error.\n");
			pthread_mutex_unlock(&srv.connLock);
			return 0;
		}

		rdma_debug("6\n");

		sendAttrtoRemote(sfd, &srv.localQpAttr);
		recvAttrfromRemote(sfd, &remoteQpAttr);

		// create remote information object, and put it into the remote link table
		remoteInfo = (RemoteInfo *)malloc(sizeof(RemoteInfo));
		remoteInfo->next = srv.remoteTable;
		srv.remoteTable = remoteInfo;

		// fill information
		bzero((char *) &ah_attr, sizeof(ah_attr));
		ah_attr.dlid = remoteQpAttr.lid;
		ah_attr.port_num = IB_PHYS_PORT;
		pthread_mutex_init(&remoteInfo->lock, 0);
		remoteInfo->ah = ibv_create_ah(srv.pd, &ah_attr);
		if (!remoteInfo->ah) {
			rdma_debug("create address handle failed.\n");
			free(remoteInfo);
			pthread_mutex_unlock(&srv.connLock);
			return NULL;
		}
		remoteInfo->qpn = remoteQpAttr.qpn;
		remoteInfo->recvs = INIT_RECV_NUMBER;
		remoteInfo->requestingRecvs = 0;
		remoteInfo->addr = serv_addr.sin_addr.s_addr;

		// post receives
		for (i = 0; i < INIT_RECV_NUMBER; i++) {
			rbuf = getRdmaBufferfromPool(&srv.rbp);
			postRecv(srv.qp, rbuf);
		}
		rdma_debug("6\n");
		srv.postedRecvs += INIT_RECV_NUMBER;

		put((hash_table *)&srv.remotePtrTable, ipHash, serv_addr.sin_addr.s_addr, &remoteInfo, sizeof(RemoteInfo*));

		close(sfd);
	}
	pthread_mutex_unlock(&srv.connLock);

	return remoteInfo;
}
