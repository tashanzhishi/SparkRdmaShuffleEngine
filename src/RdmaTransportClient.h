/*
 * RdmaTransportClient.h
 *
 *  Created on: May 9, 2016
 *      Author: yurujie
 */

#ifndef RDMATRANSPORTCLIENT_H_
#define RDMATRANSPORTCLIENT_H_

#include "RdmaUtils.h"
#include "RdmaTransportServer.h"
#include "RdmaMsgHeader.h"

int sendMsg(char *host, char *msg, int len);
int sendMsgWithHeader(char *host, char *header, int hlen, char *body, int blen);

#endif /* RDMATRANSPORTCLIENT_H_ */
