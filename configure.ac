# Process this file with autoconf to produce a configure script.
AC_PREREQ(2.52)

m4_define([eex_version], [2.29.1])

AC_INIT(evolution-exchange, [eex_version], http://bugzilla.gnome.org/enter_bug.cgi?product=Evolution%20Exchange)
AC_CONFIG_SRCDIR(camel)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

# Required Package Versions
m4_define([glib_minimum_version], [2.16.0])
m4_define([gtk_minimum_version], [2.10.0])
m4_define([eds_minimum_version], [eex_version])
m4_define([evo_minimum_version], [eex_version])
m4_define([gconf_minimum_version], [2.0.0])             # XXX Just a Guess
m4_define([libxml_minimum_version], [2.0.0])            # XXX Just a Guess
m4_define([libsoup_minimum_version], [2.3.0])

AM_CONFIG_HEADER(config.h)

# Automake 1.11 - Silent Build Rules
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_ARG_WITH(e2k-debug,     [  --with-e2k-debug              Allow debugging])
case $withval in
no)
	;;
*)
	AC_DEFINE(E2K_DEBUG, 1, [Define if you want E2K_DEBUG to be available])
	;;
esac

dnl Update these for every new development release of evolution. These numbers
dnl actually correspond to the next stable release number
EVOLUTION_API_VERSION=2.30
EDS_BASE_VERSION=2.30

LIBEXCHANGE_STORAGE_CURRENT=3
LIBEXCHANGE_STORAGE_REVISION=0
LIBEXCHANGE_STORAGE_AGE=0

AC_SUBST(LIBEXCHANGE_STORAGE_CURRENT)
AC_SUBST(LIBEXCHANGE_STORAGE_REVISION)
AC_SUBST(LIBEXCHANGE_STORAGE_AGE)

AC_MSG_CHECKING(Evolution version)
EVOLUTION_VERSION=`pkg-config --modversion evolution-shell 2>/dev/null`
if test -z "$EVOLUTION_VERSION"; then
	AC_MSG_ERROR(Evolution development libraries not installed)
fi
AC_SUBST(EVOLUTION_VERSION)
AC_MSG_RESULT($EVOLUTION_VERSION)

EVOLUTION_BASE_VERSION=$EVOLUTION_API_VERSION
AC_DEFINE_UNQUOTED(EVOLUTION_BASE_VERSION,"$EVOLUTION_BASE_VERSION",Evolution version)

BASE_VERSION=$EVOLUTION_API_VERSION
AC_DEFINE_UNQUOTED(BASE_VERSION, "$BASE_VERSION", Connector base version)
AC_SUBST(BASE_VERSION)

AC_SUBST(EDS_BASE_VERSION)

dnl Initialize maintainer mode
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AC_STDC_HEADERS
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

dnl Initialize libtool
AM_PROG_LIBTOOL

# Compiler Warning Flags

AS_COMPILER_FLAGS(WARNING_FLAGS,
	"-DG_DISABLE_DEPRECATED
	-DPANGO_DISABLE_DEPRECATED
	-DGDK_DISABLE_DEPRECATED
	-DGDK_PIXBUF_DISABLE_DEPRECATED
	-DGTK_DISABLE_DEPRECATED
	-DG_DISABLE_SINGLE_INCLUDES
	-DGTK_DISABLE_SINGLE_INCLUDES
	-Wall -Wextra
	-Wno-missing-field-initializers
	-Wno-sign-compare
	-Wno-unused-parameter
	-Wdeclaration-after-statement
	-Werror-implicit-function-declaration
	-Wformat-security -Winit-self
	-Wmissing-declarations -Wmissing-include-dirs
	-Wmissing-noreturn -Wnested-externs -Wpointer-arith
	-Wredundant-decls -Wundef -Wwrite-strings")
AC_SUBST(WARNING_FLAGS)

# Other useful compiler warnings for test builds only.
# These may produce warnings we have no control over.
#
#	-Wformat-nonliteral
#	-Wmissing-format-attribute
#	-Wshadow

CFLAGS="$CFLAGS $WARNING_FLAGS"

dnl ****
dnl i18n
dnl ****

IT_PROG_INTLTOOL([0.35.5])
AM_GLIB_GNU_GETTEXT

GETTEXT_PACKAGE=evolution-exchange-${BASE_VERSION}
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Package name for gettext])

localedir='$(prefix)/$(DATADIRNAME)/locale'
AC_SUBST(localedir)

dnl ******************************
dnl Check for Win32
dnl ******************************

AC_MSG_CHECKING([for Win32])
case "$host" in
*-mingw*)
    os_win32=yes
    NO_UNDEFINED='-no-undefined'
    SOCKET_LIBS='-lws2_32'
    ;;
*)  os_win32=no
    NO_UNDEFINED='-no-undefined'
    SOCKET_LIBS=''
    ;;
esac
AC_MSG_RESULT([$os_win32])
AM_CONDITIONAL(OS_WIN32, [test $os_win32 = yes])
AC_SUBST(NO_UNDEFINED)
AC_SUBST(SOCKET_LIBS)

# Check for base dependencies early.
PKG_CHECK_MODULES(GNOME_PLATFORM,
	[glib-2.0 >= glib_minimum_version
	 gtk+-2.0 >= gtk_minimum_version
	 gconf-2.0 >= gconf_minimum_version
	 libxml-2.0 >= libxml_minimum_version
	 libsoup-2.4 >= libsoup_minimum_version])

PKG_CHECK_MODULES(EVOLUTION_DATA_SERVER,
	[libebook-1.2 >= eds_minimum_version
	 libecal-1.2 >= eds_minimum_version
	 libedataserver-1.2 >= eds_minimum_version
	 libedataserverui-1.2 >= eds_minimum_version
	 libegroupwise-1.2 >= eds_minimum_version
	 libebackend-1.2 >= eds_minimum_version])

dnl *************************
dnl CFLAGS and LIBS and stuff
dnl *************************

GNOME_COMPILE_WARNINGS(maximum)
CFLAGS="$CFLAGS $WARN_CFLAGS"
case $CFLAGS in
*-Wall*)
	CFLAGS="$CFLAGS -Wno-sign-compare"
	;;
esac

AC_EGREP_HEADER(socklen_t, sys/socket.h, :, AC_DEFINE(socklen_t, int, [Define to "int" if socklen_t is not defined]))

AM_PATH_GLIB_2_0
AM_PATH_ORBIT2
AC_PATH_PROG(GCONFTOOL, gconftool-2, no)
AM_GCONF_SOURCE_2

CONNECTOR_DATADIR='$(datadir)/evolution-exchange/$(BASE_VERSION)'
AC_SUBST(CONNECTOR_DATADIR)

plugindir=`$PKG_CONFIG --variable=plugindir evolution-plugin`
AC_SUBST(plugindir)

EVOLUTION_PLUGIN_errordir="`pkg-config --variable=errordir evolution-plugin`"
AC_SUBST(EVOLUTION_PLUGIN_errordir)

extensiondir="`pkg-config --variable=extensiondir evolution-data-server-1.2`"
AC_SUBST(extensiondir)

EVOLUTION_imagesdir="`pkg-config --variable=imagesdir evolution-shell`"
AC_SUBST(EVOLUTION_imagesdir)

CAMEL_providerdir="`pkg-config --variable=camel_providerdir camel-provider-1.2`"
AC_SUBST(CAMEL_providerdir)

dnl ***************************
dnl Check for evolution plugins
dnl ***************************
PKG_CHECK_MODULES(EVOLUTION_PLUGIN, evolution-plugin >= evo_minimum_version)
AC_SUBST(EVOLUTION_PLUGIN_CFLAGS)
AC_SUBST(EVOLUTION_PLUGIN_LIBS)

dnl *********************
dnl Pilot license support
dnl *********************
# This does not take leap years into account, but that's not
# important: having the build time be slightly too early is good
# to support slight clock skew anyway.
# 946684800 is 2000-01-01T00:00:00Z.
# Don't change this without testing under /bin/sh on Solaris.
abt=`eval expr \`date '+\( \( %y \* 365 \) + %j - 1 \) \* 24 \* 60 \* 60 + 946684800'\``
case $abt in
"")
	echo "Warning: build time check failed. (Are you on OS X?)."
	echo "Pilot licenses won't work."
	abt="0"
	;;
esac
AC_DEFINE_UNQUOTED(E2K_APPROX_BUILD_TIME, $abt, [Used to prevent clock-setting attacks against pilot licenses])


AC_DEFINE(HANDLE_LIBICAL_MEMORY, 1, [Define it once memory returned by libical is free'ed properly])

dnl *************
dnl pthread check
dnl *************
EVO_PTHREAD_CHECK

dnl *********************
dnl OpenLDAP NTLM support
dnl *********************
if test "$os_win32" != yes; then
EVO_SUNLDAP_CHECK(no)
case $with_sunldap in
no)
	EVO_LDAP_CHECK(yes)
	case $with_openldap in
	no)
		AC_ERROR(LDAP support is required for Connector)
		;;
	*)
		case $with_static_ldap in
		yes)
			msg_ldap="$with_openldap (static)"
			;;
		*)
			msg_ldap="$with_openldap (dynamic)"
			;;
		esac
	esac
	;;
*)
	case $with_static_sunldap in
	yes)
		msg_ldap="$with_sunldap (static)"
		;;
	*)
		msg_ldap="$with_sunldap (dynamic)"
		;;
	esac
esac

SAVE_CFLAGS="$CFLAGS"
SAVE_LIBS="$LIBS"
LDAP_CFLAGS="$LDAP_CFLAGS -DLDAP_DEPRECATED"
CFLAGS="$CFLAGS $LDAP_CFLAGS"
LIBS="$LIBS $LDAP_LIBS"
AC_CHECK_FUNCS(ldap_ntlm_bind)
CFLAGS="$SAVE_CFLAGS"
LIBS="$SAVE_LIBS"
else # Win32
LDAP_CFLAGS="$LDAP_CFLAGS -DLDAP_DEPRECATED"
LDAP_LIBS="-lwldap32"
AC_SUBST(LDAP_CFLAGS)
AC_SUBST(LDAP_LIBS)
AC_DEFINE(HAVE_LDAP,1,[Define if you have LDAP support])
AM_CONDITIONAL(ENABLE_LDAP, true)
fi # Win32

AM_CONDITIONAL(SUNLDAP, test "$with_sunldap" != no)

AC_MSG_CHECKING(for LDAP Paged Control)
AC_TRY_RUN([
#include <ldap.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
#ifdef LDAP_CONTROL_PAGEDRESULTS	
		exit(0);
#else		
		exit(1);
#endif		
}],[
AC_DEFINE(HAVE_LDAP_PAGED, 1, [Supports Paged results])
ac_cv_ldappaged=yes
],ac_cv_ldappaged="no (GAL Caching disabled)",ac_cv_ldappaged="no (GAL Caching disabled)",[
AC_DEFINE(HAVE_LDAP_PAGED, 1, [Supports Paged results])
ac_cv_ldappaged=yes
])
AC_MSG_RESULT($ac_cv_ldappaged)

dnl *******
dnl gtk-doc
dnl *******
dnl Putting a space before the macro call here makes gnome-autogen not
dnl notice it, which makes it not run gtkdocize and copy gtk-doc.make
dnl into the top level where I don't want it.
 GTK_DOC_CHECK([1.0])

dnl **************************************************
dnl * libdb checking - taken from EDS
dnl **************************************************

AC_ARG_WITH(libdb, AC_HELP_STRING([--with-libdb],[Prefix where libdb is installed]), dynamic_libdb=yes, dynamic_libdb=no)
if test "x${dynamic_libdb}" = "xyes"; then
        DB_CFLAGS="-I$withval/include"
        DB_LIBS="-L$withval/lib -ldb"
else
	DB_CFLAGS="$CFLAGS -I /usr/include"
	DB_LIBS="$LIBS -ldb"
fi

AC_MSG_CHECKING([Berkeley DB])
save_cflags=$CFLAGS; CFLAGS=$DB_CFLAGS
save_libs=$LIBS; LIBS="$DB_LIBS"
AC_LINK_IFELSE(
        [AC_LANG_PROGRAM([[#include <db.h>]],
                         [db_create(NULL, NULL, 0)])],
        check_db=yes,
        check_db=no)
AC_MSG_RESULT($check_db)
AM_CONDITIONAL(HAVE_LIBDB, [test $check_db = yes])

if test "$check_db" = "yes"; then
  AC_DEFINE(ENABLE_CACHE, 1,
	    [Enabling GAL Caching])
else
  AC_DEFINE(ENABLE_CACHE, 0,
	    [Disabling GAL Caching])
  DB_CFLAGS=""
  DB_LIBS=""
fi
CFLAGS=$save_cflags
LIBS=$save_libs
AC_SUBST(DB_CFLAGS)
AC_SUBST(DB_LIBS)

dnl ****************
dnl Kerberos Support
dnl ****************

AC_ARG_WITH(krb5,
  AC_HELP_STRING( [--with-krb5=DIR],
		  [Location of Kerberos 5 install dir]),
  with_krb5="$withval", with_krb5="no")

AC_ARG_WITH(krb5-libs,
  AC_HELP_STRING( [--with-krb5-libs=DIR],
		  [Location of Kerberos 5 libraries]),
  with_krb5_libs="$withval", with_krb5_libs="$with_krb5/lib")

AC_ARG_WITH(krb5-includes,
  AC_HELP_STRING( [--with-krb5-includes=DIR],
		  [Location of Kerberos 5 headers]),
  with_krb5_includes="$withval", with_krb5_includes="")

msg_krb5="no"
if test "x${with_krb5}" != "xno"; then
	LIBS_save="$LIBS"

	mitlibs="-lkrb5 -lk5crypto -lcom_err -lgssapi_krb5"
	heimlibs="-lkrb5 -lcrypto -lasn1 -lcom_err -lroken -lgssapi"
	sunlibs="-lkrb5 -lgss"
	AC_CACHE_CHECK([for Kerberos 5], ac_cv_lib_kerberos5,
	[
		LIBS="$LIBS -L$with_krb5_libs $mitlibs"
		AC_TRY_LINK([#include <krb5.h>],krb5_init_context, ac_cv_lib_kerberos5="$mitlibs",
		[
			LIBS="$LIBS_save -L$with_krb5_libs $heimlibs"
			AC_TRY_LINK_FUNC(krb5_init_context, ac_cv_lib_kerberos5="$heimlibs",
			[
				LIBS="$LIBS_save -L$with_krb5_libs $sunlibs"
				AC_TRY_LINK_FUNC(krb5_init_context, ac_cv_lib_kerberos5="$sunlibs", ac_cv_lib_kerberos5="no")
			])
		])
		LIBS="$LIBS_save"
	])
	if test "$ac_cv_lib_kerberos5" != "no"; then
		AC_DEFINE(HAVE_KRB5,1,[Define if you have Krb5])
		if test "$ac_cv_lib_kerberos5" = "$mitlibs"; then
			AC_DEFINE(HAVE_MIT_KRB5,1,[Define if you have MIT Krb5])
			if test -z "$with_krb5_includes"; then
				KRB5_CFLAGS="-I$with_krb5/include"
			else
				KRB5_CFLAGS="-I$with_krb5_includes"
			fi
			msg_krb5="yes (MIT)"
		else
			if test "$ac_cv_lib_kerberos5" = "$heimlibs"; then
				AC_DEFINE(HAVE_HEIMDAL_KRB5,1,[Define if you have Heimdal])
				if test -z "$with_krb5_includes"; then
					KRB5_CFLAGS="-I$with_krb5/include/heimdal"
				else
					KRB5_CFLAGS="-I$with_krb5_includes"
				fi
				msg_krb5="yes (Heimdal)"
			else
				AC_DEFINE(HAVE_SUN_KRB5,1,[Define if you have Sun Kerberosv5])
				if test -z "$with_krb5_includes"; then
					KRB5_CFLAGS="-I$with_krb5/include/kerberosv5"
				else
					KRB5_CFLAGS="-I$with_krb5_includes"
				fi
				msg_krb5="yes (Sun)"
			fi
		fi
		KRB5_LDFLAGS="-L$with_krb5_libs $ac_cv_lib_kerberos5"
		AC_MSG_RESULT(msg_krb5)
	else
		dnl AC_MSG_CHECKING([for Kerberos 5])
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([You specified with krb5, but it was not found.])
	fi
else
	AC_MSG_WARN([krb5 support disabled])
fi

AM_CONDITIONAL(ENABLE_KRB5, test x$with_krb5 != xno)

dnl ******************************
dnl Makefiles
dnl ******************************

AC_OUTPUT([
Makefile
evolution-exchange-zip
eplugin/Makefile
camel/Makefile
addressbook/Makefile
calendar/Makefile
tools/Makefile
docs/Makefile
docs/reference/Makefile
po/Makefile.in
server/Makefile
server/lib/Makefile
server/storage/Makefile
server/xntlm/Makefile
])

echo "
	evolution-exchange has been configured as follows:
	Kerberos 5:       $msg_krb5
	LDAP support:     $msg_ldap
"

case $ac_cv_func_ldap_ntlm_bind in
no)
	echo ""
	AC_MSG_WARN([
No NTLM support in OpenLDAP; Plaintext password authentication will be
used when connecting to the Global Catalog server. Consider installing
the evo-openldap package, or building OpenLDAP with the patch in
docs/openldap-ntlm.diff
])
	;;
esac
