/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#ifndef _XNTLM_MD4_H
#define _XNTLM_MD4_H

void xntlm_md4sum (const unsigned char *in, int nbytes,
		   unsigned char digest[16]);

#endif /* _XNTLM_MD4_H */
