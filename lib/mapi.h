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

/* PR_ACTION values */
#define MAPI_ACTION_REPLIED              261
#define MAPI_ACTION_REPLIED_S           "261"
#define MAPI_ACTION_FORWARDED            262
#define MAPI_ACTION_FORWARDED_S         "262"

/* PR_ACTION_FLAG values */
#define MAPI_ACTION_FLAG_REPLIED_TO_SENDER    102
#define MAPI_ACTION_FLAG_REPLIED_TO_SENDER_S "102"
#define MAPI_ACTION_FLAG_REPLIED_TO_ALL       103
#define MAPI_ACTION_FLAG_REPLIED_TO_ALL_S    "103"
#define MAPI_ACTION_FLAG_FORWARDED            104
#define MAPI_ACTION_FLAG_FORWARDED_S         "104"

/* PR_FLAG_STATUS values */
#define MAPI_FOLLOWUP_UNFLAGGED    0
#define MAPI_FOLLOWUP_UNFLAGGED_S "0"
#define MAPI_FOLLOWUP_COMPLETED    1
#define MAPI_FOLLOWUP_COMPLETED_S "1"
#define MAPI_FOLLOWUP_FLAGGED      2
#define MAPI_FOLLOWUP_FLAGGED_S   "2"

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MAPI_H__ */
