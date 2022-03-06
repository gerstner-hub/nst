#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "st.h"
#include "Selection.hxx"
#include "Term.hxx"

void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

void
toggleprinter(const Arg *)
{
	term.mode.flip(nst::Term::Mode::PRINT);
}

void
printscreen(const Arg *)
{
	term.dump();
}

void
printsel(const Arg *)
{
	g_sel.dump();
}
