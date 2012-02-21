/*
  libxbee - a C library to aid the use of Digi's XBee wireless modules
            running in API mode (AP=2).

  Copyright (C) 2009  Attie Grande (attie@attie.co.uk)

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../internal.h"
#include "../../xbee_int.h"
#include "../../mode.h"
#include "../../pkt.h"
#include "../../frame.h"
#include "../common.h"
#include "at.h"
#include "io.h"

xbee_err xbee_s1_at_rx_func(struct xbee *xbee, unsigned char identifier, struct xbee_buf *buf, struct xbee_frameInfo *frameInfo, struct xbee_conAddress *address, struct xbee_pkt **pkt) {
	struct xbee_pkt *iPkt;
	xbee_err ret;
	int addrLen;
	
	if (!xbee || !frameInfo || !buf || !address || !pkt) return XBEE_EMISSINGPARAM;
	
	if (buf->len < 1) return XBEE_ELENGTH;
	
	switch (buf->data[0]) {
		case 0x88: addrLen = 0;  break; /* Local AT  */
		case 0x97: addrLen = 10; break; /* Remote AT */
		default: return XBEE_EINVAL;
	}
	
	if (buf->len < addrLen + 5) return XBEE_ELENGTH;
	
	frameInfo->active = 1;
	frameInfo->id = buf->data[1];
	frameInfo->retVal = buf->data[addrLen + 4];
	
	if (frameInfo->retVal != 0) {
		*pkt = 0;
		return XBEE_ENONE;
	}
	
	if ((ret = xbee_pktAlloc(&iPkt, NULL, buf->len - 5)) != XBEE_ENONE) return ret;
	
	if (addrLen == 10) {
		address->addr64_enabled = 1;
		memcpy(address->addr64, &(buf->data[2]), 8);
		address->addr16_enabled = 1;
		memcpy(address->addr16, &(buf->data[10]), 2);
	}
	
	/* (... - 5) + 2 -> API Identifier + Frame ID + Status (not including the AT Command) */
	iPkt->dataLen = (buf->len - addrLen - 5) + 2;
	
	memcpy(iPkt->data, &(buf->data[addrLen + 2]) , 2); /* copy in the AT command */
	if (iPkt->dataLen > 2) {
		memcpy(&(iPkt->data[2]), &(buf->data[addrLen + 5]), iPkt->dataLen - 2); /* copy in the response value (if any) */
	}
	iPkt->data[iPkt->dataLen] = '\0';
	
	if (!strncasecmp((char*)iPkt->data, "IS", 2)) {
		xbee_s1_io_parseInputs(xbee, iPkt, &(iPkt->data[2]), iPkt->dataLen - 2);
	}
	
	*pkt = iPkt;
	
	return XBEE_ENONE;
}

xbee_err xbee_s1_at_tx_func(struct xbee *xbee, unsigned char identifier, unsigned char frameId, struct xbee_conAddress *address, struct xbee_conSettings *settings, unsigned char *buf, int len, struct xbee_buf **oBuf) {
	struct xbee_buf *iBuf;
	size_t bufLen;
	unsigned char *addr16;
	unsigned char *addr64;
	int pos;
	size_t memSize;
	
	if (!xbee || !address || !buf || !oBuf) return XBEE_EMISSINGPARAM;
	
	if (len < 2) return XBEE_ELENGTH; /* must have the AT command... */
	
	switch (identifier) {
		case 0x08: /* Local AT */
			if (settings->queueChanges) identifier = 0x09;
			addr64 = NULL;
			addr16 = NULL;
			break;
		case 0x17: /* Remote AT */
			if (address->addr64_enabled) {
				addr64 = &(address->addr64[0]);
				addr16 = (unsigned char[]){ 0xFF, 0xFE };
			} else if (address->addr16_enabled) {
				addr64 = (unsigned char[]){ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
				addr16 = &(address->addr16[0]);
			} else {
				return XBEE_EINVAL;
			}
			break;
		default: return XBEE_EINVAL;
	}
	
	memSize = 2;
	if (addr64 && addr16) memSize += 8 + 2;
	memSize += len;
	bufLen = memSize;
	
	memSize += sizeof(*iBuf);
	
	if ((iBuf = malloc(memSize)) == NULL) return XBEE_ENOMEM;
	
	pos = 0;
	iBuf->len = bufLen;
	iBuf->data[pos] = identifier;                          pos++;
	iBuf->data[pos] = frameId;                             pos++;
	if (addr64 && addr16) {
		memcpy(&(iBuf->data[pos]), addr64, 8);               pos += 8;
		memcpy(&(iBuf->data[pos]), addr16, 2);               pos += 2;
		iBuf->data[pos] = 0x00;
		if (!settings->queueChanges) {
			iBuf->data[pos] |= 0x02;
		}
		                                                     pos++;
	} else {
		iBuf->len--;
	}
	
	memcpy(&(iBuf->data[pos]), buf, len);                  pos += len;
	iBuf->data[pos] = '\0';
	
	*oBuf = iBuf;
	
	return XBEE_ENONE;
}

/* ######################################################################### */

const struct xbee_modeDataHandlerRx xbee_s1_localAt_rx  = {
	.identifier = 0x88,
	.func = xbee_s1_at_rx_func,
};
const struct xbee_modeDataHandlerTx xbee_s1_localAt_tx  = {
	.identifier = 0x08,
	.func = xbee_s1_at_tx_func,
};
const struct xbee_modeConType xbee_s1_localAt = {
	.name = "Local AT",
	.allowFrameId = 1,
	.useTimeout = 1,
	.timeout = {
		.tv_sec = 0,
		.tv_nsec = 250000000,
	},
	.rxHandler = &xbee_s1_localAt_rx,
	.txHandler = &xbee_s1_localAt_tx,
};

/* ######################################################################### */

const struct xbee_modeDataHandlerRx xbee_s1_remoteAt_rx  = {
	.identifier = 0x97,
	.func = xbee_s1_at_rx_func,
};
const struct xbee_modeDataHandlerTx xbee_s1_remoteAt_tx  = {
	.identifier = 0x17,
	.func = xbee_s1_at_tx_func,
};
const struct xbee_modeConType xbee_s1_remoteAt = {
	.name = "Remote AT",
	.allowFrameId = 1,
	.useTimeout = 1,
	.timeout = {
		.tv_sec = 0,
		.tv_nsec = 250000000,
	},
	.rxHandler = &xbee_s1_remoteAt_rx,
	.txHandler = &xbee_s1_remoteAt_tx,
};