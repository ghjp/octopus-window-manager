#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT([octopus], [0.2.0], [pfefferl@gmx.net])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([auxdir])
AC_CONFIG_HEADER([config.h])

AM_MAINTAINER_MODE
AM_INIT_AUTOMAKE([octopus], [0.2.0], no-define)
AM_DISABLE_STATIC

# Checks for programs.
AM_PROG_LIBTOOL
AC_PROG_MAKE_SET

if test "$CC" = gcc
then
	CFLAGS="$CFLAGS -Wall -pipe"
fi

AC_MSG_CHECKING(for full debugging)
AC_ARG_ENABLE(full-debug,
	      AC_HELP_STRING([--enable-full-debug],[Turn on full debugging output (default=no)]),
	      [
	       case "${enableval}" in
		       yes) full_debug=yes ;;
		       no)  full_debug=no ;;
		       *)   AC_MSG_ERROR(bad value ${enableval} for --enable-full-debug) ;;
	       esac
	       ], [ full_debug=no ])
AC_MSG_RESULT($full_debug)
test x$full_debug = xyes &&
AC_DEFINE(FULL_DEBUG, 0, Turn on full debugging output)

# Checks for libraries.
AM_PATH_GLIB_2_0([2.2.0],,,)
PKG_CHECK_MODULES([GDK_PIXBUF_XLIB], [gdk-pixbuf-xlib-2.0])
PKG_CHECK_MODULES([CAIRO], [cairo >= 1.0.0])
AC_PATH_XTRA
AM_PATH_LIBXOSD

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([locale.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for library functions.
AC_CHECK_FUNCS([gethostname memset setlocale sigaction strtol])

# Check for XShape extension
AC_MSG_CHECKING([whether to build support for the XShape extension])
AC_ARG_ENABLE(xshape,
	      AC_HELP_STRING([--enable-xshape],
			     [enable support for the XShape extension [default=yes]]), ,
			     [enable_xshape=yes])
if test "x$enable_xshape" = "xyes"; then
	AC_MSG_RESULT([yes])
	AC_CHECK_LIB(Xext, XShapeQueryExtension,
		     AC_DEFINE(HAVE_XSHAPE, [], [Define to 1 if you have an XShape capable server])
		     X_LIBS="$X_LIBS -lXext", , $X_CFLAGS $X_PRE_LIBS $X_EXTRA_LIBS $X_LIBS)
else
	AC_MSG_RESULT([no])
fi

# Check for XFree86-VidModeExtension extension
AC_MSG_CHECKING([whether to build support for the XFree86-VidModeExtension extension])
AC_ARG_ENABLE(xf86vm,
	      AC_HELP_STRING([--enable-xf86vm],
			     [enable support for the XFree86-VidModeExtension [default=yes]]), ,
			     [enable_xf86vm=yes])
if test "x$enable_xf86vm" = "xyes"; then
	AC_MSG_RESULT([yes])
	AC_CHECK_LIB(Xxf86vm, XF86VidModeQueryExtension,
		     AC_DEFINE(HAVE_XF86VM, [], [Define to 1 if you have an XFree86-VidModeExtension capable server])
		     X_LIBS="$X_LIBS -lXxf86vm", , $X_CFLAGS $X_PRE_LIBS $X_EXTRA_LIBS $X_LIBS)
else
	AC_MSG_RESULT([no])
fi

### Check for XRANDR support
##AC_MSG_CHECKING([wheter to build support for the XRANDR extension])
##AC_ARG_ENABLE(xrandr,
##	      AC_HELP_STRING([--enable-xrandr],
##			     [enable support for the XRANDR extension [default=yes]]), ,
##			     [enable_xrandr=yes])
##if test "x$enable_xrandr" = "xyes"; then
##	AC_MSG_RESULT([yes])
##	AC_CHECK_LIB(Xrandr, XRRQueryExtension,
##		     AC_DEFINE(HAVE_XRANDR, [], [Define to 1 if you have an XRANDR capable server])
##		     LIBS="$LIBS -lXrender -lXrandr", , $X_CFLAGS $X_PRE_LIBS $X_EXTRA_LIBS -lXrender $X_LIBS)
##	else
##		AC_MSG_RESULT([no])
##		fi

AC_CONFIG_FILES([Makefile src/Makefile utils/Makefile])
AC_OUTPUT
