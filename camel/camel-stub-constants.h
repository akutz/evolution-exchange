/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

/* camel-stub-constants.h: shared between client and server */

#ifndef CAMEL_STUB_CONSTANTS_H
#define CAMEL_STUB_CONSTANTS_H 1

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

typedef enum {
	CAMEL_STUB_CMD_CONNECT,
	CAMEL_STUB_CMD_GET_FOLDER,
	CAMEL_STUB_CMD_GET_TRASH_NAME,
	CAMEL_STUB_CMD_SYNC_FOLDER,
	CAMEL_STUB_CMD_REFRESH_FOLDER,
	CAMEL_STUB_CMD_SYNC_COUNT,
	CAMEL_STUB_CMD_EXPUNGE_UIDS,
	CAMEL_STUB_CMD_APPEND_MESSAGE,
	CAMEL_STUB_CMD_SET_MESSAGE_FLAGS,
	CAMEL_STUB_CMD_SET_MESSAGE_TAG,
	CAMEL_STUB_CMD_GET_MESSAGE,
	CAMEL_STUB_CMD_SEARCH_FOLDER,
	CAMEL_STUB_CMD_TRANSFER_MESSAGES,
	CAMEL_STUB_CMD_GET_FOLDER_INFO,
	CAMEL_STUB_CMD_SEND_MESSAGE,
	CAMEL_STUB_CMD_CREATE_FOLDER,
	CAMEL_STUB_CMD_DELETE_FOLDER,
	CAMEL_STUB_CMD_RENAME_FOLDER,
	CAMEL_STUB_CMD_SUBSCRIBE_FOLDER,
	CAMEL_STUB_CMD_UNSUBSCRIBE_FOLDER,
	CAMEL_STUB_CMD_IS_SUBSCRIBED_FOLDER
} CamelStubCommand;

typedef enum {
	CAMEL_STUB_ARG_END,
	CAMEL_STUB_ARG_RETURN,

	CAMEL_STUB_ARG_UINT32,
	CAMEL_STUB_ARG_STRING,
	CAMEL_STUB_ARG_BYTEARRAY,
	CAMEL_STUB_ARG_STRINGARRAY,
	CAMEL_STUB_ARG_FOLDER,
	CAMEL_STUB_ARG_UINT32ARRAY
} CamelStubArgType;

typedef enum {
	CAMEL_STUB_RETVAL_OK,
	CAMEL_STUB_RETVAL_RESPONSE,
	CAMEL_STUB_RETVAL_EXCEPTION,
	CAMEL_STUB_RETVAL_NEW_MESSAGE,
	CAMEL_STUB_RETVAL_REMOVED_MESSAGE,
	CAMEL_STUB_RETVAL_CHANGED_MESSAGE,
	CAMEL_STUB_RETVAL_CHANGED_FLAGS,

	/* This is used to undelete a message for which the operation failed.
	   But can also be used to specifically set or unset a flag. */
	CAMEL_STUB_RETVAL_CHANGED_FLAGS_EX,
	CAMEL_STUB_RETVAL_CHANGED_TAG,
	CAMEL_STUB_RETVAL_PROGRESS,
	CAMEL_STUB_RETVAL_FREEZE_FOLDER,
	CAMEL_STUB_RETVAL_THAW_FOLDER,
	CAMEL_STUB_RETVAL_FOLDER_CREATED,
	CAMEL_STUB_RETVAL_FOLDER_DELETED,
	CAMEL_STUB_RETVAL_FOLDER_RENAMED,
	CAMEL_STUB_RETVAL_FOLDER_SET_READONLY,
	CAMEL_STUB_RETVAL_FOLDER_SET_ARTICLE_NUM
} CamelStubRetval;

typedef enum {
	CAMEL_STUB_FOLDER_READONLY    = (1<<0),
	CAMEL_STUB_FOLDER_FILTER      = (1<<1),
	CAMEL_STUB_FOLDER_POST        = (1<<2),
	CAMEL_STUB_FOLDER_NOSELECT    = (1<<4),
	CAMEL_STUB_FOLDER_FILTER_JUNK = (1<<5),
	CAMEL_STUB_FOLDER_SYSTEM      = (1<<6),
	CAMEL_STUB_FOLDER_TYPE_INBOX  = (1<<7),
	CAMEL_STUB_FOLDER_SUBSCRIBED  = (1<<8),
	CAMEL_STUB_FOLDER_NOCHILDREN  = (1<<9),
	CAMEL_STUB_FOLDER_TYPE_TRASH  = (1<<10),
	CAMEL_STUB_FOLDER_TYPE_SENT   = (1<<11)
} CamelStubFolderFlags;

typedef enum {
#ifndef CAMEL_DISABLE_DEPRECATED
	CAMEL_STUB_STORE_FOLDER_INFO_FAST	= (1<<0),
#endif /* CAMEL_DISABLE_DEPRECATED */
	CAMEL_STUB_STORE_FOLDER_INFO_RECURSIVE	= (1<<1),
	CAMEL_STUB_STORE_FOLDER_INFO_SUBSCRIBED	= (1<<2),
	CAMEL_STUB_STORE_FOLDER_INFO_NO_VIRTUAL	= (1<<3),
	CAMEL_STUB_STORE_FOLDER_INFO_SUBSCRIPTION_LIST	= (1<<4)
} CamelStubStoreFlags;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_STUB_CONSTANTS_H */
