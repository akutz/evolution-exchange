# evolution/acinclude.m4
# shared configure.in hacks between Evolution and Connector

# EVO_PURIFY_SUPPORT
# Add --enable-purify. If the user turns it on, subst PURIFY and set
# the automake conditional ENABLE_PURIFY
AC_DEFUN(EVO_PURIFY_SUPPORT, [
	AC_ARG_ENABLE(purify, 
	[  --enable-purify=[no/yes]      Enable support for building executables with Purify.],,enable_purify=no)
	AC_PATH_PROG(PURIFY, purify, impure)
	AC_ARG_WITH(purify-options, [  --with-purify-options=OPTIONS      Options passed to the purify command line (defaults to PURIFYOPTIONS variable).])
	if test "x$with_purify_options" = "xno"; then
		with_purify_options="-always-use-cache-dir=yes -cache-dir=/gnome/lib/purify"
	fi
	if test "x$PURIFYOPTIONS" = "x"; then
		PURIFYOPTIONS=$with_purify_options
	fi
	AC_SUBST(PURIFY)
	AM_CONDITIONAL(ENABLE_PURIFY, test "x$enable_purify" = "xyes" -a "x$PURIFY" != "ximpure")
	PURIFY="$PURIFY $PURIFYOPTIONS"
])


# EVO_LDAP_CHECK(default)
# Add --with-openldap and --with-static-ldap options. --with-openldap
# defaults to the given value if not specified. If LDAP support is
# configured, HAVE_LDAP will be defined and the automake conditional
# ENABLE_LDAP will be set. LDAP_CFLAGS and LDAP_LIBS will be set
# appropriately.
AC_DEFUN(EVO_LDAP_CHECK, [
	default="$1"

	AC_ARG_WITH(openldap,     [  --with-openldap=[no/yes/PREFIX]      Enable LDAP support in evolution])
	AC_ARG_WITH(static-ldap,  [  --with-static-ldap=[no/yes]          Link LDAP support statically into evolution ])
	AC_CACHE_CHECK([for OpenLDAP], ac_cv_with_openldap, ac_cv_with_openldap="${with_openldap:=$default}")
	case $ac_cv_with_openldap in
	no|"")
		with_openldap=no
		;;
	yes)
		with_openldap=/usr
		;;
	*)
		with_openldap=$ac_cv_with_openldap
		LDAP_CFLAGS="-I$ac_cv_with_openldap/include"
		LDAP_LDFLAGS="-L$ac_cv_with_openldap/lib"
		;;
	esac

	if test "$with_openldap" != no; then
		AC_DEFINE(HAVE_LDAP, 1, [Define if you have OpenLDAP])

		case $with_static_ldap in
		no|"")
			if test -f $with_openldap/lib/libldap.la; then
				with_static_ldap=yes
			else
				with_static_ldap=no
			fi
			;;
		*)
			with_static_ldap=yes
			;;
		esac

		AC_CACHE_CHECK(if OpenLDAP is version 2.x, ac_cv_openldap_version2, [
			CPPFLAGS_save="$CPPFLAGS"
			CPPFLAGS="$CPPFLAGS $LDAP_CFLAGS"
			AC_EGREP_CPP(yes, [
				#include "ldap.h"
				#if LDAP_VENDOR_VERSION > 20000
				yes
				#endif
			], ac_cv_openldap_version2=yes, ac_cv_openldap_version2=no)
			CPPFLAGS="$CPPFLAGS_save"
		])
		if test "$ac_cv_openldap_version2" = no; then
			AC_MSG_ERROR(evolution requires OpenLDAP version >= 2)
		fi

		AC_CHECK_LIB(resolv, res_query, LDAP_LIBS="-lresolv")
		AC_CHECK_LIB(socket, bind, LDAP_LIBS="$LDAP_LIBS -lsocket")
		AC_CHECK_LIB(nsl, gethostbyaddr, LDAP_LIBS="$LDAP_LIBS -lnsl")
		AC_CHECK_LIB(lber, ber_get_tag, [
			if test "$with_static_ldap" = "yes"; then
				LDAP_LIBS="$with_openldap/lib/liblber.a $LDAP_LIBS"

				# libldap might depend on OpenSSL... We need to pull
				# in the dependency libs explicitly here since we're
				# not using libtool for the configure test.
				if test -f $with_openldap/lib/libldap.la; then
					LDAP_LIBS="`. $with_openldap/lib/libldap.la; echo $dependency_libs` $LDAP_LIBS"
				fi
			else
				LDAP_LIBS="-llber $LDAP_LIBS"
			fi
			AC_CHECK_LIB(ldap, ldap_open, [
					if test $with_static_ldap = "yes"; then
						LDAP_LIBS="$with_openldap/lib/libldap.a $LDAP_LIBS"
					else
						LDAP_LIBS="-lldap $LDAP_LIBS"
					fi],
				LDAP_LIBS="", $LDAP_LDFLAGS $LDAP_LIBS)
			LDAP_LIBS="$LDAP_LDFLAGS $LDAP_LIBS"
		], LDAP_LIBS="", $LDAP_LDFLAGS $LDAP_LIBS)

		if test -z "$LDAP_LIBS"; then
			AC_MSG_ERROR(could not find OpenLDAP libraries)
		fi

		AC_SUBST(LDAP_CFLAGS)
		AC_SUBST(LDAP_LIBS)
		AC_SUBST(LDAP_LDFLAGS)
	fi
	AM_CONDITIONAL(ENABLE_LDAP, test $with_openldap != no)
])