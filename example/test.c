/*
 * test.c
 *
 *  Created on: May 30, 2016
 *      Author: yurujie
 */

#include "RdmaTransportClient.h"

#define MSG_SIZE	(1024 * 1024)

int main(int argc, char **argv)
{

	char *localhost;
	char *remotehost;
	int ret;

	if (argc < 3) {
		rdma_debug("argc can not less than 3.\n");
		return -1;
	}
	localhost = argv[1];
	remotehost = argv[2];

	ret = initServer(localhost, 6666);
	if (ret) {
		rdma_debug("initialize server failed.\n");
		return -1;
	}

	char msg[MSG_SIZE];
	memset(msg, 'a', MSG_SIZE);
	sendMsg(remotehost, msg, MSG_SIZE);

	while (1) {
		;
	}

	return 0;
}
