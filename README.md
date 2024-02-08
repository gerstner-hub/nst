Introduction
============

This is *nst*, the not (so) simple terminal emulator. It is a C++ port of
[st](https://st.suckless.org/), the simple terminal emulator for X that sucks
less.

Motivation for the Port
=======================

I have already been using *st* for a couple of years. I generally like lean
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
libxpp libraries in the process (see dependencies below).

The final result is, I would say, a pretty clean C++ codebase that is about a
third larger in line count (mostly due to coding style and comments I
suppose). The not so simple experience of porting *st* and a hunch that for
some types of people the result is not as simple as before any more, led me to
the name of *nst* for the ported application.

Differences to the original *st*
================================

The initial release of *nst* version 0.9 was mostly a vanilla port of the
original *st* version 0.9. I tried to stay as compatible as was feasible. Some
minor changes in the command line behaviour exist there. I possibly fixed some
edge cases and most likely introduced some new bugs in the process of porting
as is practically unavoidable.

The one major change is in portability, since I decided to rely on *libcosmos*
which is Linux only. Thus other UNIX like operating systems are currently out
of the picture for *nst*.

The compile time configuration via the config header still exists in
`nst_config.hxx`. The configuration style in the header changed quite a bit,
naturally, due to the change of the programming language. I did not test much
else than the default configuration yet though. I believe that also the
original *st* still has a couple of issues lingering in less tested
configurations.

Beginning with *nst* version 1.0 I started to give this project a more
individual touch. My aim is to find a balance between a slim terminal emulator
and enough features to avoid having to rely on other complex programs. For
example I don't want to run a terminal multiplexer like `tmux` or `screen`
just to get scrollback support. These programs are very complex consisting of
multiple tens of thousand lines of code. For this reason *nst* version 1.0 now
contains features for a history scrollback buffer and for processing the
history in external programs for searching. I also added some more logic in
the area of automatic selection expansion for better productivity. The major
features are explained in more detail in the following sections.

Scrollback Support
==================

The `scroll` program which is also offered on <http://suckless.org> was
supposed to become a small standalone utility to provide scroll support on top
of *st*. By now its authors noticed that its design has some limits, though,
that are hard to overcome. Thereforce I took the route to implement scrollback
support internally.

Starting with *nst* version 1.0 the terminal emulator offers builtin scrollback
support, mostly in the spirit of original *st*'s [scrollback ringbuffer
patch](http://st.suckless.org/patches/scrollback/st-scrollback-0.8.5.diff).

I did *not* implement the [reflow
patch](http://st.suckless.org/patches/scrollback/st-scrollback-reflow-0.8.5.diff)
though. Reflow it the process of rearranging automatic line breaks, when the
terminal window size changes. The patch for this seemed too complex for what
this feature gains.

Instead I chose a simpler approach: What I want to avoid is losing information
when making the emulator window smaller (and thus cutting of long lines). This
is something that can quickly happen as a side effect of using a tiling window
manager like i3, where automatic resizing of windows is an integral part of
the user experience. To avoid such information loss I implemented a simpler
logic that colums are never discarded upon window resize, except if the line
is overwritten in the history. This way, when the window size is increased
again, the previous columns will appear again and no information is lost.

This approach is not perfect, as lines can become incosistent if programs are
manipulating the content of already existing lines, while previous columns are
still hidden due to a smaller window size. When increasing the window size
again then old columns appear that no longer match the current line content.
It would be difficult and inefficient to catch all code paths where line
content is changed and hidden columns should be discarded.

The problem is not very severe in practice though. Programs that modify line
content programmatically are usually running on the alternative screen, where
this logic is disabled and where there is no scrollback buffer.

Searching in Scrollback History
===============================

Once scrollback history is available it would be really nice to support
searching in it to find specific older data. Doing this internally in the
terminal emulator is a real pain in terms of complexity though. It requires
string processing, regular expressions, additional input handling and also
some form of a status and command display to make the current search state
visible to the user. All of this just to implement logic that already
exists in much more powerful tools like `grep` on the command line or a fully
fledged editor like `vim`.

Therefore I decided just to add interfaces to *nst* to allow feeding the
existing buffer text to external programs. There are two mechanisms available:

1. You can pipe the complete terminal buffer contents to an external program
   via a special keyboard shortcut (ctrl-shift-b by default). The
   configuration for this is found in `nst_config.cxx`. Check out the call to
   `pipeBufferTo()`. By default `gvim` is invoked.
2. The separate `nst-msg` utility can be used to access the terminal buffer
   contents of the currently running terminal emulator. This can be used to
   construct command lines on the fly like `nst-msg -d | grep mystring`. Since
   the output of the utility modifies the terminal buffer itself it can be
   confusing when multiple subquent searches are performed this way. Therefore
   the utility allows to take a snapshot of the current terminal contents and
   operate on them, until a new snapshot is created.

Improved Text Selection
=======================

*nst* implements the following text selection enhancements:

- a double-click on a URI scheme like `http://` will select the complete URL.
  See `SEL_URI_SCHEMES` in `nst_config.hxx`.
- a double-click on a word delimiting character, while holding the `alt`
  modifier, will select the following word up until the next delimiter of the
  same kind. E.g. when clicking on the double quotes of `"a couple of words"`
  then the complete string delimited by the double quotes will be selected.
- if a selection already exists and you click left or right of the selection
  while holding the `alt` modifier, then the selection will be expanded to the
  left or right, respectively, until the next word delimiter is found. When
  clicking on the selection itself, then the selection will be extended in
  both directions.

Dependencies
============

*nst* depends on the X libraries like the original st. Further it depends on
the [libcosmos](https://github.com/gerstner-hub/libcosmos/) and
[libxpp](https://github.com/gerstner-hub/libxpp/) C++ helper libraries. These
libraries are integrated via Git submodules into this repository and
statically linked, so you shouldn't need to worry a lot about them.

For command line parsing the [TCLAP](https://tclap.sourceforge.net)
header-only library is used, which is also pulled in as a Git submodule.

Installation
============

*nst* uses the `SCons` build system. See
[libcosmos's README](https://github.com/gerstner-hub/libcosmos/blob/master/README.md)
for some hints about how to use it. In the default case simply run

    scons install

and you will find all installation artifacts in the `install` directory tree.

Because the `libcosmos` and `libxpp` dependencies don't have a stable ABI
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

I am planning to implement some of the more interesting ST patches that are
available. Also adding some level of configuration file support to make the
terminal emulator more accessible are on my feature list.

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
turn is based on Aurélien APTEL <aurelien dot aptel at gmail dot com> bt
source code.
