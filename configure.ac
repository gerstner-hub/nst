dnl process this file with autoconf to produce a configure script

dnl require a current version of autoconf
AC_PREREQ([2.71])

dnl define the nst project
AC_INIT([nst],[0.10],[Matthias.Gerstner@nefkom.net])

dnl separate directory for some generated files
AC_CONFIG_AUX_DIR(ac_aux)

dnl define the name of the generated config header file
AC_CONFIG_HEADERS(config.h)

dnl pull in Automake in a current version
AM_INIT_AUTOMAKE([1.16.0 foreign])

dnl check for C and C++ compilers
AC_PROG_CC
AC_PROG_CXX

dnl differentiate between GCC and clang
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[#ifdef __clang__
	#error no gcc
#elif !defined(__GNUC__)
	#error no gcc
#endif
]])], [is_gcc=true], [is_gcc=false])

AM_CONDITIONAL([IS_GCC], $is_gcc)

dnl check for our dependencies
PKG_CHECK_MODULES([x11], [ x11 >= 1.7.2 ])
PKG_CHECK_MODULES([xft], [ xft >= 2.3.4 ])
PKG_CHECK_MODULES([freetype2], [ freetype2 >= 2.11 ])
PKG_CHECK_MODULES([fontconfig], [ fontconfig >= 2.13 ])

AC_SEARCH_LIBS(clock_gettime,[rt])
AC_SEARCH_LIBS(openpty,[util])

AC_SUBST([x11_CFLAGS])
AC_SUBST([x11_LIBS])
AC_SUBST([freetype2_CFLAGS])
AC_SUBST([freetype2_LIBS])
AC_SUBST([fontconfig_CFLAGS])
AC_SUBST([fontconfig_LIBS])

dnl generate the actual Makefile output
AC_CONFIG_FILES([Makefile])

dnl specify a Makefile for every sub-directory that needs one
AC_CONFIG_FILES([src/Makefile])
AC_OUTPUT
