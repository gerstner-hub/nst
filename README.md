Introduction
============

This is *nst*, the not (so) simple terminal emulator. It is a C++ port of
[st](https://st.suckless.org/), the simple terminal emulator for X that sucks
less.

Motivation for the Port
-----------------------

I have been already using *st* for a couple of years. I generally like lean
and mean software that is to the point. I also like the principle idea of "one
tool for one job", even though the decision not to have scroll support in
*st* could be seen as a little extreme by some.

Some time ago I looked deeper into *st* and wanted to understand it better in
terms of some functionality and in terms of its
[security](https://seclists.org/oss-sec/2017/q2/183). I soon found that *st's*
codebase is not as simple as the resulting program is. On top of this the C
programming language, for me personally, does not do the job anymore these
days in non-trivial userspace programs.

So I thought *why not port this to C++, it's not that big*. Well ... it turned
out that the original *st* code was tightly packed in a certain sense and
difficult to cut into more digestible pieces. That's what I did in a process
of continuous porting and refactoring of the codebase. As a result of some
sort of mission creep I also ended up putting together the libcosmos and
libX++ libraries in the process (see Dependencies below).

The final result is, I would say, a pretty clean C++ codebase that is about a
third larger in line count (mostly due to coding style and comments I
suppose). The not so simple experience of porting *st* and a hunch that for
some types of people the result is not as simple as before any more, led me to
the name of *nst* for the ported application.

Differences to the original *st*
--------------------------------

Currently this is mostly a vanilla port of the original *st* version 0.9. I
tried to stay as compatible as was feasible. Some minor changes in the command
line behaviour exists. I possibly fixed some edge cases and most likely
introduced some new bugs in the process of porting as is practically
unavoidable.

The one major change is in portability, since I decided to rely on *libcosmos*
which is Linux only. Thus other UNIX like operating systems are currently out
of the picture for *nst*.

The compile time configuration via the config header still exists in
`nst_config.hxx`. The configuration style in the header changed quite a bit,
naturally, due to the change of the programming language. I did not test any
other than the default configuration yet though. I believe that also the
original *st* still has a couple of issues lingering in less tested
configurations.

Dependencies
============

*nst* depends on the X libraries like the original st. Further it depends on
the [libcosmos](https://github.com/gerstner-hub/libcosmos/) and
[libX++](https://github.com/gerstner-hub/libXpp/) C++ helper libraries. These
libraries are integrated via Git submodules into this repository and
statically linked, so you shouldn't need to worry a lot about them.

Installation
============

*nst* uses the `SCons` build system. See
[libcosmos's README](https://github.com/gerstner-hub/libcosmos/blob/master/README.md)
for some hints about how to use it. In the default case simply run

    scons install

and you will find all installation artifacts in the `install` directory tree.

Because the `libcosmos` and `libX++` dependencies don't have a stable ABI
concept yet the linking is done statically for them. So you don't have to
worry about setting up the shared library path etc.

Hints for Developers
====================

I tried to document the workings of the terminal as best as I could in the
ported code. The smaller pieces and increased isolation of some code portions
should also make things clearer. The C++ code does not rely much on the more
nasty language features like templates, it only uses some template classes
from other libraries.

API documentation
-----------------

*nst* uses Doxygen inline source comments that can either be viewed
as plaintext directly in the sources or can be generated by building `scons
doxygen`, provided you have the Doxygen program installed on your system.

Otherwise you can find the generated HTML version of the API documentation on
the related [GitHub Page](https://gerstner-hub.github.io/nst).

Future Directions
=================

At the moment I'm testing the finished port of *st* to find any lingering
bugs.  After this I want to investigate some areas to possibly add new
features I personally like, or providing built-in scroll support and things
like that.

Contributing
============

Any bugfixes and improvements are welcome as pull requests. Keep in mind
*st's* [LEGACY philosophy](doc/LEGACY). The basic idea of a slim and robust
terminal program should remain, but finding a middle path to cover typical
user requirements is also good for me. Please refer to [the coding
style](https://github.com/gerstner-hub/libcosmos/blob/master/doc/coding_style.md)
for a rough style guide. Before working on larger changes it might be helpful
to contact me first to reach some common ground regarding the design etc.

By contributing you accept the same licensing conditions as the rest of the
project for your contribution. Your name will be added to an authors list in
the repository.

Credits
=======

Based on the original [st](https://st.suckless.org/) source code which in
turns is based on Aurélien APTEL <aurelien dot aptel at gmail dot com> bt
source code.
