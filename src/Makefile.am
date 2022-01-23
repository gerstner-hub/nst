AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = nst
nst_SOURCES = \
	      st.c st.h \
	      win.h \
	      x.c \
	      arg.h \
	      config.def.h
AM_CFLAGS = -Wall -Werror -Wextra -Wnull-dereference -Wdouble-promotion -Wshadow -Wformat=2 
AM_CFLAGS += -D_XOPEN_SOURCE=600
AM_CFLAGS += @freetype2_CFLAGS@ @fontconfig_CFLAGS@ @x11_CFLAGS@ @xft_CFLAGS@

if IS_GCC
AM_CFLAGS += -Wduplicated-cond -Wduplicated-branches -Wlogical-op
else
AM_CFLAGS += -Wno-unused-parameter
endif

AM_CXXFLAGS = ${AM_CFLAGS} -std=c++17

# use LDALL instead of LDFLAGS to make librarier appear AFTER the object
# files. Otherwise we get trouble on distros where as-needed linking is
# enabled
nst_LDADD = @freetype2_LIBS@ @fontconfig_LIBS@ @x11_LIBS@ @xft_LIBS@
