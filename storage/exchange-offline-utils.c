#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "exchange-offline-utils.h"
#include "e2k-uri.h"
#include "e2k-path.h"

char *
exchange_offline_build_object_cache_file (E2kUri *e2kuri, const char *filename, gboolean has_relative_uri)
{
	char *prefix, *storage_dir, * physical_dir, * path, *prefix_uri;
	printf ("building the object cache file name\n");
	storage_dir = g_strdup_printf ("%s/.evolution/exchange/%s@%s",
						g_get_home_dir (),
						e2kuri->user, e2kuri->host);
	if (has_relative_uri) {
		prefix_uri = e2kuri->relative_uri;
	} else {
		prefix_uri = e2kuri->uri;
	}
	
	prefix = strchr (prefix_uri, '/');
	prefix--;

	physical_dir = e_path_to_physical (storage_dir, prefix);

	path = g_build_filename (physical_dir, filename, NULL);
	printf ("the object cache file name is : %s\n", path);
	
	return path;
}
