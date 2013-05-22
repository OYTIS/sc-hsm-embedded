/*
 *  ---------
 * |.**> <**.|  CardContact
 * |*       *|  Software & System Consulting
 * |*       *|  Minden, Germany
 * |.**> <**.|  Copyright (c) 2013. All rights reserved
 *  ---------
 *
 * See file LICENSE for details on licensing
 *
 * Abstract :       Main API interface according to MKT specification
 *
 * Author :         Andreas Schwier, Frank Thater
 *
 * Last modified:   2013-04-22
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ctapi.h"
#include "ctbcs.h"
#include "scr.h"

extern int ccidT1Term (struct scr *ctx);

/* Handle up to 8 USB CCID readers */

scr_t *readerTable[MAX_READER] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

/*
 * Locate matching card terminal number in table of active readers.
 *
 */

static int LookupReader(unsigned short ctn)
{
	int i;

	for (i = 0; i < MAX_READER; i++) {
		if (readerTable[i] && (readerTable[i]->ctn == ctn)) {
			return i;
		}
	}

	return -1;
}



/**
 * Initialize the interface to the card reader ctn attached
 * to the port number specified in pn
 *
 * @param ctn Card terminal number
 * @param pn Port number
 * @return Status code \ref OK, \ref ERR_INVALID, \ref ERR_CT, \ref ERR_TRANS, \ref ERR_MEMORY, \ref ERR_HOST, \ref ERR_HTSI
 */
signed char CT_init(unsigned short ctn, unsigned short pn)
{
	int rc, indx;
	scr_t *ctx;

	rc = LookupReader(ctn);

	if (rc < 0) {

		/*
		 * Determine next free position in reader list
		 */
		for (indx = 0; (indx < MAX_READER) && readerTable[indx]; indx++) {
			;
		}

		if (indx == MAX_READER) {
			return ERR_MEMORY;
		}

		ctx = (scr_t *)calloc(1, sizeof(scr_t));

		if (!ctx) {
			return ERR_MEMORY;
		}

		/*
		 * No active reader yet - try to find one
		 */
		rc = USB_Open(pn, &(ctx->device));

		if (rc != USB_OK) {
			free(ctx);

			if (rc == ERR_NO_READER) {
				return ERR_CT;
			} else {
				return ERR_HOST; /* USB transmission error */
			}
		}

		ctx->ctn = ctn;
		ctx->pn = pn;

		readerTable[indx] = ctx;
	}

	return OK;
}



/**
 * Close the interface and free all allocated resources
 *
 * @param ctn Card terminal number
 * @return Status code \ref OK, \ref ERR_INVALID, \ref ERR_CT, \ref ERR_TRANS, \ref ERR_MEMORY, \ref ERR_HOST, \ref ERR_HTSI
 */
signed char CT_close(unsigned short ctn)
{

	int rc;
	scr_t *ctx;

	rc = LookupReader(ctn);

	if (rc < 0) {
		return ERR_CT;
	}

	ctx = readerTable[rc];

	if (!ctx) {
		return ERR_CT;
	}

	if (ctx->t1) {
		ccidT1Term(ctx);
	}

	USB_Close(&(ctx->device));

	free(ctx);
	readerTable[rc] = NULL;

	return OK;
}



/**
 * Pass a command to the reader driver and receive the response
 *
 * @param ctn Card terminal number
 * @param dad Destination address
 * @param sad Source address
 * @param lc Length of command data
 * @param cmd Command data
 * @param lr Size of response data buffer
 * @param rsp Response data
 * @return Status code \ref OK, \ref ERR_INVALID, \ref ERR_CT, \ref ERR_TRANS, \ref ERR_MEMORY, \ref ERR_HOST, \ref ERR_HTSI
 */
signed char CT_data(unsigned short ctn, unsigned char *dad, unsigned char *sad,
					unsigned short lc, unsigned char *cmd, unsigned short *lr,
					unsigned char *rsp)
{

	int rc;
	unsigned int ilr;
	scr_t *ctx;

	rc = LookupReader(ctn);

	if (rc < 0) {
		return ERR_CT;
	}

	ctx = readerTable[rc];

	if (!ctx) {
		return ERR_CT;
	}

	ilr = (int) *lr; /* Overcome problem with lr size     */

	rc = 0;

	if (*dad == 1) {
		*sad = 1; /* Source Reader    */
		*dad = 2; /* Destination Host */

		/*******************/
		/* CT-BCS Commands */
		/*******************/

		if (cmd[0] == 0x20) {
			switch (cmd[1]) {
			case 0x12: /* Request ICC                       */
				rc = RequestICC(ctx, lc, cmd, &ilr, rsp);
				break;

			case 0x11: /* Resets the card/terminal and return ATR */
				rc = ResetCard(ctx, lc, cmd, &ilr, rsp);
				break;

			case 0x13: /* Get Status - Gets reader status   */
				rc = GetStatus(ctx, cmd, &ilr, rsp);
				break;

			case 0x15: /* Eject ICC - Deactivate reader    */
				rc = EjectICC(ctx, lc, cmd, &ilr, rsp);
				break;

			default: /* Wrong instruction for CTAPI */
				rsp[0] = HIGH(WRONG_INSTRUCTION);
				rsp[1] = LOW(WRONG_INSTRUCTION);
				ilr = 2;
				break;
			}
		} else { /* Wrong class for CTAPI */
			rsp[0] = HIGH(CLASS_NOT_SUPPORTED);
			rsp[1] = LOW(CLASS_NOT_SUPPORTED);
			ilr = 2;
		}

	} else if (*dad == 0) { /* This command goes to the card     */

		/* 	Don't get confused here this is for the return saying

			the source was the card and the destination the host */

		*sad = 0; /* Source Smartcard */
		*dad = 2; /* Destination Host */

		if (ctx->CTModFunc) {
			rc = (*ctx->CTModFunc)(ctx, lc, cmd, &ilr, rsp);

			if (rc < 0) {
				rc = ERR_TRANS;
			}
		} else {

			*sad = 1; /* Shows that response comes from CTAPI */
			*dad = 2;
			rc = 0;
			rsp[0] = HIGH(COMMUNICATION_NOT_POSSIBLE);
			rsp[1] = LOW(COMMUNICATION_NOT_POSSIBLE);
			ilr = 2;
		}

	} else {
		rc = ERR_INVALID; /* Invalid SAD/DAD Address */
		ilr = 0;
	}

	*lr = ilr;
	return rc;
}

