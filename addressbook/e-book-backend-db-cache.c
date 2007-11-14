/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* A class to cache address  book conents on local file system
 *
 * Copyright (C) 2004 Novell, Inc.
 *
 * Authors: Devashish Sharma <sdevashish@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "e-book-backend-db-cache.h"
#include <libedata-book/e-book-backend.h>
#include <libedata-book/e-book-backend-sexp.h>

void
string_to_dbt(const char *str, DBT *dbt)
{
	memset(dbt, 0, sizeof(dbt));
	dbt->data = (void *)str;
	dbt->size = strlen(str) + 1;
	dbt->flags = DB_DBT_USERMEM;
}

static char *
get_filename_from_uri (const char *uri)
{
	char *mangled_uri, *filename;
	int i;

	/* mangle the URI to not contain invalid characters */
	mangled_uri = g_strdup (uri);
	for (i = 0; i < strlen (mangled_uri); i++) {
		switch (mangled_uri[i]) {
		case ':' :
		case '/' :
			mangled_uri[i] = '_';
		}
	}

	/* generate the file name */
	filename = g_build_filename (g_get_home_dir (), ".evolution/cache/addressbook",
				     mangled_uri, "cache.db", NULL);

	/* free memory */
	g_free (mangled_uri);

	return filename;
}

/**
 * e_book_backend_db_cache_set_filename:
 * @db:  DB Handle
 * @filename: filename to be set
 *
 * Set the filename for db cacahe file.
 **/

void
e_book_backend_db_cache_set_filename(DB *db, const char *filename)
{
	DBT uid_dbt, vcard_dbt;
	int db_error;

	string_to_dbt ("filename", &uid_dbt);
	string_to_dbt (filename, &vcard_dbt);

	db_error = db->put (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db->put failed with %d", db_error);
	}

}

/**
 * e_book_backend_db_cache_get_filename:
 * @db:  DB Handle
 *
 * Get the filename for db cacahe file.
 **/

char *
e_book_backend_db_cache_get_filename(DB *db)
{
	DBT  uid_dbt, vcard_dbt;
	int db_error;
	char *filename;

	string_to_dbt ("filename", &uid_dbt);
	memset (&vcard_dbt, 0 , sizeof(vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db-<get failed with %d", db_error);
		return NULL;
	}
	else {
		filename = g_strdup (vcard_dbt.data);
		g_free (vcard_dbt.data);
		return filename;
	}
}

void
e_book_backend_db_cache_set_time(DB *db, const char *t)
{
	DBT uid_dbt, vcard_dbt ;
	int db_error ;

	string_to_dbt ("last_update_time", &uid_dbt);
	string_to_dbt (t, &vcard_dbt);

	db_error = db->put (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db->put failed with %d", db_error);
	}
}

char *
e_book_backend_db_cache_get_time (DB *db)
{
	DBT uid_dbt, vcard_dbt;
	int db_error;
	char *t;

	string_to_dbt ("last_update_time", &uid_dbt);
	memset (&vcard_dbt, 0, sizeof(vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db->get failed with %d", db_error);
		return NULL;
	}
	else {
		t = g_strdup (vcard_dbt.data);
		g_free (vcard_dbt.data);
		return t;
	}
}

/**
 * e_book_backend_db_cache_get_contact:
 * @db: DB Handle
 * @uid: a unique contact ID
 *
 * Get a cached contact. Note that the returned #EContact will be
 * newly created, and must be unreffed by the caller when no longer
 * needed.
 *
 * Return value: A cached #EContact, or %NULL if @uid is not cached.
 **/
EContact *
e_book_backend_db_cache_get_contact (DB *db, const char *uid)
{
	DBT        uid_dbt, vcard_dbt;
	int        db_error;
	const char *vcard_str;

	g_return_val_if_fail (uid != NULL, NULL);

	string_to_dbt (uid, &uid_dbt);
	memset (&vcard_dbt, 0 , sizeof(vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt,0);
	if (db_error != 0) {
		g_warning ("db->get failed with %d", db_error);
		return NULL;
	}

	vcard_str = g_strdup (vcard_dbt.data);
	g_free (vcard_dbt.data);

	return e_contact_new_from_vcard (vcard_str);
}

/**
 * e_book_backend_db_cache_add_contact:
 * @db: DB Handle
 * @contact: an #EContact
 *
 * Adds @contact to @cache.
 *
 * Return value: %TRUE if the contact was cached successfully, %FALSE otherwise.
 **/
gboolean
e_book_backend_db_cache_add_contact (DB *db,
				   EContact *contact)
{
	DBT        uid_dbt, vcard_dbt;
	int        db_error;
	char       *vcard_str;
	const char *uid;

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	if (!uid) {
		const gchar *name;
		const gchar *email;

		name = e_contact_get (contact, E_CONTACT_GIVEN_NAME);
		email = e_contact_get (contact, E_CONTACT_EMAIL_1);
		printf ("no uid\n");
		printf("name:%s, email:%s\n", name, email);
		return FALSE;
	}
	string_to_dbt (uid, &uid_dbt);

	vcard_str = e_vcard_to_string (E_VCARD(contact), EVC_FORMAT_VCARD_30);
	string_to_dbt (vcard_str, &vcard_dbt);

	//db_error = db->del (db, NULL, &uid_dbt, 0);
	db_error = db->put (db, NULL, &uid_dbt, &vcard_dbt, 0);

	g_free (vcard_str);

	if (db_error != 0) {
		g_warning ("db->put failed with %d", db_error);
		return FALSE;
	}
	else
		return TRUE;
}

/**
 * e_book_backend_db_cache_remove_contact:
 * @db: DB Handle
 * @uid: a unique contact ID
 *
 * Removes the contact identified by @uid from @cache.
 *
 * Return value: %TRUE if the contact was found and removed, %FALSE otherwise.
 **/
gboolean
e_book_backend_db_cache_remove_contact (DB *db,
				    const char *uid)

{
	DBT        uid_dbt;
	int        db_error;

	g_return_val_if_fail (uid != NULL, FALSE);

	string_to_dbt (uid, &uid_dbt);
	db_error = db->del (db, NULL, &uid_dbt, 0);

	if (db_error != 0) {
		g_warning ("db->del failed with %d", db_error);
		return FALSE;
	}
	else
		return TRUE;

}

/**
 * e_book_backend_db_cache_check_contact:
 * @db: DB Handle
 * @uid: a unique contact ID
 *
 * Checks if the contact identified by @uid exists in @cache.
 *
 * Return value: %TRUE if the cache contains the contact, %FALSE otherwise.
 **/
gboolean
e_book_backend_db_cache_check_contact (DB *db, const char *uid)
{
	DBT        uid_dbt, vcard_dbt;
	int        db_error;

	g_return_val_if_fail (uid != NULL, FALSE);

	string_to_dbt (uid, &uid_dbt);
	memset (&vcard_dbt, 0 , sizeof(vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt,0);
	if (db_error != 0)
		return FALSE;
 	else {
		free (vcard_dbt.data);
		return TRUE;
	}
}

/**
 * e_book_backend_db_cache_get_contacts:
 * @db: DB Handle
 * @query: an s-expression
 *
 * Returns a list of #EContact elements from @cache matching @query.
 * When done with the list, the caller must unref the contacts and
 * free the list.
 *
 * Return value: A #GList of pointers to #EContact.
 **/
GList *
e_book_backend_db_cache_get_contacts (DB *db, const char *query)
{
	DBC        *dbc;
	DBT        uid_dbt, vcard_dbt;
	int        db_error;
	GList *list = NULL;
        EBookBackendSExp *sexp = NULL;
	EContact *contact;

	if (query) {
		sexp = e_book_backend_sexp_new (query);
		if (!sexp)
			return NULL;
	}

	db_error = db->cursor (db, NULL, &dbc, 0);
	if (db_error != 0) {
		g_warning ("db->cursor failed with %d", db_error);
		return NULL;
	}

	memset(&vcard_dbt, 0 , sizeof(vcard_dbt));
	memset(&uid_dbt, 0, sizeof(uid_dbt));
	db_error = dbc->c_get(dbc, &uid_dbt, &vcard_dbt, DB_FIRST);

	while(db_error == 0) {
		if (vcard_dbt.data && !strncmp (vcard_dbt.data, "BEGIN:VCARD", 11))
		if (e_book_backend_sexp_match_vcard(sexp, vcard_dbt.data)) {
			contact = e_contact_new_from_vcard (vcard_dbt.data);
			list = g_list_prepend (list, contact);
		}
		db_error = dbc->c_get (dbc, &uid_dbt, &vcard_dbt, DB_NEXT);
	}

	db_error = dbc->c_close (dbc);
	if(db_error != 0)
		g_warning ("db->c_close failed with %d", db_error);

	if (sexp)
		g_object_unref (sexp);

        return list;
}

/**
 * e_book_backend_db_cache_search:
 * @backend: an #EBookBackend
 * @query: an s-expression
 *
 * Returns an array of pointers to unique contact ID strings for contacts
 * in @cache matching @query. When done with the array, the caller must
 * free the ID strings and the array.
 *
 * Return value: A #GPtrArray of pointers to contact ID strings.
 **/
GPtrArray *
e_book_backend_db_cache_search (DB *db, const char *query)
{
	GList *matching_contacts, *temp;
	GPtrArray *ptr_array;

	matching_contacts = e_book_backend_db_cache_get_contacts (db, query);
	ptr_array = g_ptr_array_new ();

	temp = matching_contacts;
	for (; matching_contacts != NULL; matching_contacts = g_list_next (matching_contacts)) {
		g_ptr_array_add (ptr_array, e_contact_get (matching_contacts->data, E_CONTACT_UID));
		g_object_unref (matching_contacts->data);
	}
	g_list_free (temp);

	return ptr_array;
}

/**
 * e_book_backend_db_cache_exists:
 * @uri: URI for the cache
 *
 * Checks if an #EBookBackendCache exists at @uri.
 *
 * Return value: %TRUE if cache exists, %FALSE if not.
 **/
gboolean
e_book_backend_db_cache_exists (const char *uri)
{
	char *file_name;
	gboolean exists = FALSE;
	file_name = get_filename_from_uri (uri);

	if (file_name && g_file_test (file_name, G_FILE_TEST_EXISTS)) {
		exists = TRUE;
		g_free (file_name);
	}

	return exists;
}

/**
 * e_book_backend_db_cache_set_populated:
 * @backend: an #EBookBackend
 *
 * Flags @cache as being populated - that is, it is up-to-date on the
 * contents of the book it's caching.
 **/
void
e_book_backend_db_cache_set_populated (DB *db)
{
	DBT        uid_dbt, vcard_dbt;
	int        db_error;

	string_to_dbt ("populated", &uid_dbt);
	string_to_dbt ("TRUE", &vcard_dbt);
	db_error = db->put (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db->put failed with %d", db_error);
	}

}

/**
 * e_book_backend_cache_is_populated:
 * @db: DB Handle
 *
 * Checks if @cache is populated.
 *
 * Return value: %TRUE if @cache is populated, %FALSE otherwise.
 **/
gboolean
e_book_backend_db_cache_is_populated (DB *db)
{
	DBT        uid_dbt, vcard_dbt;
	int        db_error;

	string_to_dbt ("populated", &uid_dbt);
	memset(&vcard_dbt, 0, sizeof(vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0){
		return FALSE;
	}
	else {
		free(vcard_dbt.data);
		return TRUE;
	}
}

