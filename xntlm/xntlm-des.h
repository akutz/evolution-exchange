/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#ifndef _XNTLM_DES_H
#define _XNTLM_DES_H

typedef unsigned long XNTLM_DES_KS[16][2];

enum {
	XNTLM_DES_ENCRYPT = 0,
	XNTLM_DES_DECRYPT = 1
};

void xntlm_deskey (XNTLM_DES_KS, const unsigned char *, int);

void xntlm_des (XNTLM_DES_KS, unsigned char *);

#endif /* _XNTLM_DES_H */
