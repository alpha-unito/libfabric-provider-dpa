dnl A libfabric provider for the A3CUBE Ronnie network.
dnl
dnl (C) Copyright 2015 - University of Torino, Italy
dnl
dnl This program is free software: you can redistribute it and/or modify
dnl it under the terms of the GNU Lesser General Public License as published by
dnl the Free Software Foundation, either version 3 of the License, or (at
dnl your option) any later version.
dnl 
dnl This program is distributed in the hope that it will be useful, but
dnl WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
dnl General Public License for more details.
dnl
dnl You should have received a copy of the GNU Lesser General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.
dnl
dnl This work is a part of Paolo Inaudi's MSc thesis at Computer Science
dnl Department of University of Torino, under the supervision of Prof.
dnl Marco Aldinucci. This is work has been made possible thanks to
dnl the Memorandum of Understanding (2014) between University of Torino and 
dnl A3CUBE Inc. that established a joint research lab at
dnl Computer Science Department of University of Torino, Italy.
dnl
dnl Author: Paolo Inaudi <p91paul@gmail.com>  
dnl       
dnl Contributors: 
dnl 
dnl     Emilio Billi (A3Cube Inc. CSO): hardware and DPAlib support
dnl     Paola Pisano (UniTO-A3Cube CEO): testing environment
dnl     Marco Aldinucci (UniTO-A3Cube CSO): code design supervision"
dnl
dnl 
AC_INIT([libfabric_prov_ronniee], [0.1], [p91paul@gmail.com or aldinuc@di.unito.it])
AC_CONFIG_SRCDIR([src/dpa_fabric.c])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_AR

AC_LANG([C])
AC_PROG_CC_C99

LT_INIT([disable-static])


macos=0
linux=0

case $host_os in
*darwin*)
	macos=1
	;;
*linux*)
	linux=1
	;;
*)
	AC_MSG_ERROR([libfabric only builds on Linux & OS X])
	;;
esac

AM_CONDITIONAL([MACOS], [test "x$macos" = "x1"])
AM_CONDITIONAL([LINUX], [test "x$linux" = "x1"])

AC_ARG_WITH([libfabric],
            AC_HELP_STRING([--with-libfabric], [Use non-default libfabric location - default NO]),
            [AS_IF([test -d $withval/lib64], [fab_libdir="lib64"], [fab_libdir="lib"])
             CPPFLAGS="-I $withval/include $CPPFLAGS"
             LDFLAGS="-L$withval/$fab_libdir $LDFLAGS"],
            [])
			
AC_ARG_WITH([dpalib],
            AC_HELP_STRING([--with-dpalib], [Use non-default dpalib location - default NO]),
            [AS_IF([test -d $withval/lib64], [dpa_libdir="lib64"], [dpa_libdir="lib"])
             CPPFLAGS="-I $withval/include -I $withval/include/a3c/ $CPPFLAGS"
             LDFLAGS="-L$withval/$dpa_libdir $LDFLAGS"],
            [])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
