#
# Copyright (C) 2009-2016 Olof Hagsand and Benny Holmgren
#
# This file is part of CLIXON.
#
# CLIXON is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# CLIXON is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CLIXON; see the file LICENSE.  If not, see
# <http://www.gnu.org/licenses/>.
#

# To rebuild the `configure' script from this, execute the command
# 	autoconf
# in the directory containing this script.
#

AC_INIT(lib/clixon/clixon.h.in)

# Default CFLAGS unless set by environment.
: ${CFLAGS="-O2"}

CLIXON_VERSION_MAJOR="3"
CLIXON_VERSION_MINOR="2"
CLIXON_VERSION_PATCH="0"
CLIXON_VERSION="\"${CLIXON_VERSION_MAJOR}.${CLIXON_VERSION_MINOR}.${CLIXON_VERSION_PATCH}\""
# Fix to specific version (eg 3.5) or head (3) 
CLIGEN_VERSION="3" 

AC_CONFIG_HEADERS([include/clixon_config.h lib/clixon/clixon.h])

AC_DEFINE_UNQUOTED(CLIXON_VERSION_STRING, $CLIXON_VERSION, [Clixon version string])
AC_DEFINE_UNQUOTED(CLIXON_VERSION_MAJOR, $CLIXON_VERSION_MAJOR, [Clixon major release])
AC_DEFINE_UNQUOTED(CLIXON_VERSION_MINOR, $CLIXON_VERSION_MINOR, [Clixon minor release])
AC_DEFINE_UNQUOTED(CLIXON_VERSION_PATCH, $CLIXON_VERSION_PATCH, [Clixon path version])

# clixon versions spread to Makefile's (.so files) and variable in build.c
AC_SUBST(CLIXON_VERSION)
AC_SUBST(CLIXON_VERSION_STRING)
AC_SUBST(CLIXON_VERSION_MAJOR)
AC_SUBST(CLIXON_VERSION_MINOR)
AC_SUBST(CLIGEN_VERSION) # Bind to specific CLIgen version


AC_MSG_RESULT(CLIXON version is ${CLIXON_VERSION}_PRE1)

AC_CANONICAL_TARGET
AC_SUBST(CC)
AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(INCLUDES)
AC_SUBST(CPPFLAGS)
AC_SUBST(LIBS)
AC_SUBST(OBJ_SUFFIX)
AC_SUBST(AR_SUFFIX)
AC_SUBST(SH_SUFFIX)
AC_SUBST(EXE_SUFFIX)
AC_SUBST(AR)
AC_SUBST(RANLIB)

# Some stuff installed in /usr/local/. Such as qdbm
LIBS="-L /usr/local/lib"

#
AC_PROG_CC()
AC_PROG_CPP

CPPFLAGS="-DHAVE_CONFIG_H ${CPPFLAGS}"

AC_MSG_RESULT(compiler is $CC)	

CFLAGS="${CFLAGS} -Wall" 

AC_MSG_RESULT(CPPFLAGS is $CPPFLAGS)	
AC_MSG_RESULT(CFLAGS is $CFLAGS)	

AC_PROG_YACC
AC_PROG_LEX
if test "$LEX" = ":"; then
   AC_MSG_ERROR(CLIXON does not find lex or flex.)
fi
if test "$YACC" != "bison -y"; then
   AC_MSG_ERROR(CLIXON does not find bison.  There are several problems with yacc and byacc. Please install bison.)
fi

if test "$prefix" = "NONE"; then
  CPPFLAGS="-I${ac_default_prefix}/include ${CPPFLAGS}"
  LDFLAGS="-L${ac_default_prefix}/lib ${LDFLAGS}"
else
  CPPFLAGS="-I${prefix}/include ${CPPFLAGS}"
  LDFLAGS="-L${prefix}/lib ${LDFLAGS}"
fi

LDFLAGS=""
AC_CHECK_LIB(m, main)
EXE_SUFFIX=""
OBJ_SUFFIX=".o"
AR_SUFFIX=".a"
SH_SUFFIX=".so"
AR="ar"

# This is for cligen
AC_CHECK_HEADERS(cligen/cligen.h,, AC_MSG_ERROR(cligen missing. Try: git clone https://github.com/olofhagsand/cligen.git))

AC_CHECK_LIB(:libcligen.so.${CLIGEN_VERSION}, cligen_init,, AC_MSG_ERROR([CLIgen${CLIGEN_VERSION} missing. Try: git clone https://github.com/olofhagsand/cligen.git]))

# This is for qdbm.
# Problem: depot.h may be in qdbm/depot.h. 
AC_CHECK_HEADERS(depot.h,,[AC_CHECK_HEADERS(qdbm/depot.h,,AC_MSG_ERROR(libqdbm-dev required))])
AC_CHECK_LIB(qdbm, dpopen,, AC_MSG_ERROR(libqdbm-dev required))

AC_CHECK_LIB(crypt, crypt)
AC_CHECK_HEADERS(crypt.h)

# user credentials for unix sockets
AC_CHECK_HEADERS([sys/ucred.h],[],[],
     [[# include <sys/param.h>]]
)

# This is for Linux vlan code
AC_CHECK_HEADERS(linux/if_vlan.h)

AC_CHECK_LIB(socket, socket)
AC_CHECK_LIB(nsl, xdr_char)
AC_CHECK_LIB(dl, dlopen)

# restconf uses libcurl (I think?)
AC_CHECK_LIB(curl, curl_global_init,, AC_MSG_ERROR([libcurl missing]))

AC_CHECK_FUNCS(inet_aton sigaction sigvec strlcpy strsep strndup alphasort versionsort strverscmp)

# Lives in libfcgi-dev
AC_CHECK_LIB(fcgi, FCGX_Init,, AC_MSG_ERROR([libfcgi-dev missing])) 

# Check if extra keys inserted for database lists containing content. Eg A.n.foo = 3
# means A.3 $!a=foo exists

AC_ARG_ENABLE(keycontent, [  --disable-keycontent    Disable reverse lookup content keys],[
  if test "$enableval" = no; then
    ac_enable_keycontent=no
  else
    ac_enable_keycontent=yes
  fi
  ],[ ac_enable_keycontent=yes])

AH_TEMPLATE([DB_KEYCONTENT], 
[ Check if extra keys inserted for database lists containing content. 
  Eg A.n.foo = 3 means A.3 $!a=foo exists])
if test "$ac_enable_keycontent" = "yes"; then
   AC_DEFINE(DB_KEYCONTENT)
fi

AH_BOTTOM([#include <clixon_custom.h>])

AC_OUTPUT(Makefile
	  lib/Makefile
	  lib/src/Makefile 
	  lib/clixon/Makefile 
	  apps/Makefile 
	  apps/cli/Makefile 
	  apps/backend/Makefile 
	  apps/netconf/Makefile
	  apps/restconf/Makefile
	  apps/dbctrl/Makefile
	  apps/xmldb/Makefile
	  include/Makefile
	  etc/Makefile
	  etc/clixonrc
	  example/Makefile
	  example/docker/Makefile
	  docker/Makefile 
	  docker/cli/Makefile 
	  docker/cli/Dockerfile
	  docker/backend/Makefile 
	  docker/backend/Dockerfile
	  docker/netconf/Makefile 
	  docker/netconf/Dockerfile
	  doc/Makefile 
)

