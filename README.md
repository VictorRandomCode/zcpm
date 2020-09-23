zcpm
====

What The?
---------

Ever wanted to use a CP/M binary of [WordStar](https://en.wikipedia.org/wiki/WordStar) to edit your
resume on your Mac? Or felt the need to use a CP/M binary of [pip](https://en.wikipedia.org/wiki/Peripheral_Interchange_Program)
to rename files on your Debian system? That's ok, no-one else has either. But should you feel the
need, that's where `zcpm` might be useful.

`zcpm` is a program that runs CP/M binaries (8080/Z80) on a unix-like system, interacting with the
host filesystem and display. As opposed to a full system emulator which is a long-running self-contained
sandbox.

Context
-------

Obviously this isn't useful in the real world, I've written this as a bit of fun for myself, and
maybe someone else might enjoy experimenting with it.

I started this several years ago as `rcpm` which was a Ruby implementation, and then as `xcpm` which
was a C++11 implementation. Both of these tried to replace the BDOS with native code and used my own
Z80 core. This is technically feasible but over-ambitious; there's way too many tricky corner cases
to be practical.

So `zcpm` is another fresh start (C++17 this time), but this time I'm using an existing binary
BDOS and a native C++ BIOS with an "off-the-shelf" Z80 core which I've heavily modified. Since starting
this version, this now uses and requires C++20.

Approach
--------

CP/M was the first common operating system which allowed the same binaries to run on different
hardware, using a BIOS approach. A CP/M program (using either 8080 or Z80 instructions) uses
functions in a BDOS which in turn call functions in a BIOS. The BDOS is generic portable code,
whereas the BIOS is reimplemented for each target system.

`zcpm` uses a Z80 core to interpret the instructions in the CP/M binary, and intercepts calls
to the BDOS (or even direct calls to the BIOS in some cases), and maps between those and the
API of the host filesystem, screen, and keyboard etc.

The Z80 core is based on https://github.com/anotherlin/z80emu  That core is _"This code is free,
do whatever you want with it."_. I've since rewritten it as C++ and it is now hugely different to
work with, but the same concepts remain. Over time I'm gradually converting it to modern C++.

As for the BDOS (and CCP), `zcpm` loads a binary into `zcpm`'s memory map. This binary is assembled
from reconstructed Z80 source code of BDOS. See the `bdos` directory for this, eventually this
assembly process should be hooked into the overall build process. When the BDOS calls methods in
the BIOS, this happens via a jump table at known locations in the memory map. `zcpm` intercepts
these calls and then "does its magic".

Part of this "magic" is mapping a CP/M filesystem to the host filesystem, including sectors and
buffers and disk allocation tables etc. **So if you actually do want to use `zcpm` to access host
files, use caution, it's at your own risk!** But it does work ok for me

Screen display can (optionally) make use of [ncurses](https://en.wikipedia.org/wiki/Ncurses) so
that `zcpm` can intercept escape sequences and translate them to a host-independent screen
display.

Binaries
--------

Currently, two binaries are produced:

* `runner` which will load a CP/M binary (Z80 or 8080) and execute it on the host, in theory
  allowing it to interact with the host filesystem, display, and keyboard.
* `debugger` which will load a CP/M binary under debugger control, allowing the usual tools such
  as single-stepping, examining registers, and so on.

Both of these have various optional command line options, see the `--help` output for details.

By default, both of them look for needed files in `${HOME}/zcpm/` but this can be overridden
by using command line options. This directory is where `zcpm` looks for the BDOS binary and
also the (optional) BDOS symbol file.

`runner` can either use simplistic input/output, or can be told to use terminal emulation. The
simplistic display is simpler and faster, the terminal emulation allows for running programs
such as WordStar. This supports CP/M binaries which target a VT100 ("ANSI") console assuming
that you're running a CP/M binary of WordStar that has been configured to support VT100.
Initial support has been added for Televideo terminals, but this is incomplete.

If your host terminal program supports ANSI terminals, then you may be able to get away
with using the 'plain' terminal type, allowing the host terminal program to do any needed
handling of VT100 sequences. Otherwise, selecting e.g. `--terminal=vt100` tells `zcpm` to
translate any VT100 sequences to curses commands, allowing the CP/M display to correctly
render on any host system that is supported by curses.

The `debugger` makes use of the [replxx](https://github.com/AmokHuginnsson/replxx) library
to make the debugger interface more usable.

Status
------

At this stage this all largely works, but there's a lot more that could/should be done. For example I
haven't worried about performance too much yet. `zcpm` does lots of checking of memory accesses
at run time, this is reasonably expensive and could be optimised. Also the 

The use of `ncurses` could be optimised, to reduce the number of API calls by coalescing sequences
where sensible.

Not all BDOS and BIOS functions are implemented, I'm gradually improving this but trying to focus
just on those which are commonly used first.

`zcpm` implements CP/M 2.2. `zcpm` did initially target CP/M 3.x, but that was considerably more
work, and very little CP/M software needs CP/M 3 anyway.

The terminal translation code allows a CP/M binary which targets a VT100 "ANSI" terminal to work
on any host system which is supported by ncurses. Televideo support is a work in progress.

Longer term plans include a graphical debugger with integrated console using Qt. The earlier
`xcpm` implementation had this, I'd like to revisit this with `zcpm`.

Building
--------

This needs [boost](https://www.boost.org/) and [cmake](https://cmake.org/) and a modern C++ compiler.
And it can be handy to use [libc++](https://libcxx.llvm.org/) (which is LLVM's C++ library, as
opposed to [libstdc++](https://gcc.gnu.org/onlinedocs/libstdc++/) from GNU).

On macOS, I'm using [brew](https://brew.sh/) for boost/cmake, plus a downloaded version of current
stable [clang](https://releases.llvm.org/download.html).

On Debian 10 use this:

    sudo apt install build-essential clang libc++-dev libc++abi-dev cmake emacs valgrind gdb libboost-all-dev strace clang-tidy

On Fedora 32 use this:

    sudo dnf install cmake clang ncurses ncurses-devel gcc-g++ libcxx-devel z80asm z80dasm make
    sudo dnf install boost boost-devel boost-static
    sudo dnf install libasan libubsan

Additionally, you'll need to have [replxx](https://github.com/AmokHuginnsson/replxx)
installed. On my system I've installed into `~/local` like this:

    cd
    git clone https://github.com/AmokHuginnsson/replxx.git
    cd replxx
    mkdir -p build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${HOME}/local .. && make
    make install

If you install `replxx` somewhere else, edit `debugger/CMakeLists.txt` as needed.

Once that's all set up:

    mkdir build && cd build
    cmake ../zcpm

Or for clang like this:

    cd ~/Coding
    mkdir build && cd build
    cmake -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++ ../zcpm/

Note that (as of the time of writing, August 2020) the macOS-supplied clang does work, but it means
that you get ignorable warnings when you link against the brew-supplied boost libraries.

On macOS if you install gcc via `brew install gcc`, you can configure & build like this:

    cmake -D CMAKE_C_COMPILER=gcc-10 -D CMAKE_CXX_COMPILER=g++-10 ../zcpm/

Although you may run in to linker issues that I haven't yet resolved.

Running
-------

First make sure you've created `~/zcpm/` containing both `bdos.bin` and `bdos.lab` copied from the
source directory. Or, if you'd prefer you can manually specify the location of these files via command
line options.

Running the Debugger. I'm assuming that you've got some CP/M test binaries in `~/xcpm`

    ~/path/to/debugger ~/xcpm/drivea/CLS.COM
    ZCPM> go
    ZPPM> quit

It can be helpful to use symbol tables to add information to the log. We can use the symbol table
from the BDOS that we use as well as the one from the assembled binary that we're testing like this:

    ~/path/to/debugger --bdossym ../bdos/bdos.lab --usersym ~/Coding/z80/emutests/test05.lab ~/Coding/z80/emutests/test05.com blah.txt

Using the Runner.

    ~/path/to/runner ~/xcpm/drivea/HELLO.COM

Or with loading assembler-produced symbols:

    ~/path/to/runner --bdossym ../bdos/bdos.lab --usersym ~/Coding/z80/emutests/test06.lab ~/Coding/z80/emutests/test06.com

There's also an optional `USE_PROFILE` cmake option that enables profiling at build time. (With GCC only)

Handy commands
--------------

Setting up a build with static analysis enabled:

    cmake -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++ ../zcpm/ -DUSE_TIDY=ON

Running WordStar (after copying WS*.* into current directory):

    ~/path/to/runner --terminal=vt100 1 WS.COM

grepping the resultant log file for all BDOS calls (with some detail on each):

    grep -A 1 'BDOS: fn' zcpm.log

grepping the resultant log file to see a summary of what BDOS & BIOS calls were made:

    grep -E 'BIOS fn|BDOS: fn' zcpm.log

Using the gcc10 analyser
------------------------

As of gcc 10.x, gcc now has a static analysis option. In my tests I'm not having much success, I see
false (?) positives in STL code which I can't do anything about. So tread carefully.

It can be enabled by the cmake option `USE_ANALYSER`, assuming you're using gcc 10.

The main problem is that it is very slow to run, and needs LOTS of RAM to do its thing. So if it
aborts it has probably run out of memory. Sticking with a single-threaded build can help. And turn
off the ASAN stuff to make things work better. e.g.

    cd build
    rm -rf *
    cmake -DUSE_ANALYSER=ON -DUSE_SANITISERS=OFF ../zcpm
    make

Target Platforms:
----------------

  - [x] macOS
  - [ ] Windows
  - [x] Linux

License:
-------

  - BSD
