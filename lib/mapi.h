/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#ifndef __MAPI_H__
#define __MAPI_H__

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define MAPI_ACCESS_MODIFY		(1 << 0)
#define MAPI_ACCESS_READ		(1 << 1)
#define MAPI_ACCESS_DELETE		(1 << 2)
#define MAPI_ACCESS_CREATE_HIERARCHY	(1 << 3)
#define MAPI_ACCESS_CREATE_CONTENTS	(1 << 4)
#define MAPI_ACCESS_CREATE_ASSOCIATED	(1 << 5)

enum CdoInstanceTypes {
	cdoSingle = 0,		/* non-recurring appointment */
	cdoMaster = 1,		/* recurring appointment */
	cdoInstance = 2,	/* single instance of recurring appointment */
	cdoException = 3	/* exception to recurring appointment */
};	

/* Object type */
#define MAPI_STORE	0x1	/* Message Store */
#define MAPI_ADDRBOOK	0x2	/* Address Book */
#define MAPI_FOLDER	0x3	/* Folder */
#define MAPI_ABCONT	0x4	/* Address Book Container */
#define MAPI_MESSAGE	0x5	/* Message */
#define MAPI_MAILUSER	0x6	/* Individual Recipient */
#define MAPI_ATTACH	0x7	/* Attachment */
#define MAPI_DISTLIST	0x8	/* Distribution List Recipient */
#define MAPI_PROFSECT	0x9	/* Profile Section */
#define MAPI_STATUS	0xA	/* Status Object */
#define MAPI_SESSION	0xB	/* Session */
#define MAPI_FORMINFO	0xC	/* Form Information */

/* PR_DISPLAY_TYPEs */
/*  For address book contents tables */
#define DT_MAILUSER		0x00000000
#define DT_DISTLIST		0x00000001
#define DT_FORUM		0x00000002
#define DT_AGENT		0x00000003
#define DT_ORGANIZATION		0x00000004
#define DT_PRIVATE_DISTLIST	0x00000005
#define DT_REMOTE_MAILUSER	0x00000006
/*  For address book hierarchy tables */
#define DT_MODIFIABLE		0x00010000
#define DT_GLOBAL		0x00020000
#define DT_LOCAL		0x00030000
#define DT_WAN			0x00040000
#define DT_NOT_SPECIFIC		0x00050000
/*  For folder hierarchy tables */
#define DT_FOLDER		0x01000000
#define DT_FOLDER_LINK		0x02000000
#define DT_FOLDER_SPECIAL	0x04000000

/* PR_RECIPIENT_TYPE */
#define MAPI_ORIG   0           /* Recipient is message originator          */
#define MAPI_TO     1           /* Recipient is a primary recipient         */
#define MAPI_CC     2           /* Recipient is a copy recipient            */
#define MAPI_BCC    3           /* Recipient is blind copy recipient        */

/* PR_MESSAGE_FLAGS */
#define MAPI_MSGFLAG_READ	     0x0001
#define MAPI_MSGFLAG_UNMODIFIED	     0x0002
#define MAPI_MSGFLAG_SUBMIT	     0x0004
#define MAPI_MSGFLAG_UNSENT	     0x0008
#define MAPI_MSGFLAG_HASATTACH	     0x0010
#define MAPI_MSGFLAG_FROMME	     0x0020
#define MAPI_MSGFLAG_ASSOCIATED	     0x0040
#define MAPI_MSGFLAG_RESEND	     0x0080
#define MAPI_MSGFLAG_RN_PENDING	     0x0100
#define MAPI_MSGFLAG_NRN_PENDING     0x0200
#define MAPI_MSGFLAG_ORIGIN_X400     0x1000
#define MAPI_MSGFLAG_ORIGIN_INTERNET 0x2000
#define MAPI_MSGFLAG_ORIGIN_MISC_EXT 0x8000

/* PR_ACTION values */
#define MAPI_ACTION_REPLIED              261
#define MAPI_ACTION_FORWARDED            262

/* PR_ACTION_FLAG values */
#define MAPI_ACTION_FLAG_REPLIED_TO_SENDER    102
#define MAPI_ACTION_FLAG_REPLIED_TO_ALL       103
#define MAPI_ACTION_FLAG_FORWARDED            104

/* PR_FLAG_STATUS values */
#define MAPI_FOLLOWUP_UNFLAGGED    0
#define MAPI_FOLLOWUP_COMPLETED    1
#define MAPI_FOLLOWUP_FLAGGED      2

/* PR_PRIORITY values */
#define MAPI_PRIO_URGENT	 1
#define MAPI_PRIO_NORMAL	 0
#define MAPI_PRIO_NONURGENT	-1

/* PR_SENSITIVITY values */
#define MAPI_SENSITIVITY_NONE			0
#define MAPI_SENSITIVITY_PERSONAL		1
#define MAPI_SENSITIVITY_PRIVATE		2
#define MAPI_SENSITIVITY_COMPANY_CONFIDENTIAL	3

/* PR_IMPORTANCE values */
#define MAPI_IMPORTANCE_LOW	0
#define MAPI_IMPORTANCE_NORMAL	1
#define MAPI_IMPORTANCE_HIGH	2

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MAPI_H__ */
