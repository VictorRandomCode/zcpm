zcpm
====

What The?
---------

Ever wanted to use a CP/M binary of [WordStar](https://en.wikipedia.org/wiki/WordStar) to edit your
resume on your Mac? Or felt the need to use a CP/M binary
of [pip](https://en.wikipedia.org/wiki/Peripheral_Interchange_Program)
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
this version, this now requires C++20.

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
simplistic display is simpler and faster, it just passes the escape sequences straight through
for the host terminal program to handle. Whereas terminal emulation (which currently supports
either VT100/ANSI or Televideo) will reinterpret escape sequences for those terminals into
generic `ncurses` commands to suit the host system.

Note that the VT100 emulation is more complete than the Televideo one; both of these are being
gradually improved as time allows, but if you have the option, use binaries that target VT100
(aka "ANSI") in preference to Televideo ones. Both emulations are a long way from being complete!

The `debugger` makes use of the [replxx](https://github.com/AmokHuginnsson/replxx) library
to make the debugger interface more usable.

Status
------

At this stage this all largely works, but there's a lot more that could/should be done. For
example I haven't worried about performance too much yet. `zcpm` does lots of checking of memory
accesses at run time, this is reasonably expensive and could be optimised. And the use of
`ncurses` could be optimised to reduce the number of API calls by coalescing sequences where
sensible.

Not all BDOS and BIOS functions are implemented, I'm gradually improving this but trying to focus
just on those which are commonly used first.

`zcpm` implements CP/M 2.2. `zcpm` did initially target CP/M 3.x, but that was considerably more
work, and very little CP/M software needs CP/M 3 anyway.

The terminal translation code allows a CP/M binary which targets either a VT100 "ANSI" terminal or
a Televideo terminal to work on any host system which is supported by ncurses, both of these are
a work in progress.

Longer term plans include a graphical debugger with integrated console using Qt. The earlier
`xcpm` implementation had this, I hope to revisit this with `zcpm`.

Building
--------

This needs [boost](https://www.boost.org/), [cmake](https://cmake.org/), and a modern C++ compiler.

For macOS, use [brew](https://brew.sh/) to provide boost, cmake and clang:

    brew install boost cmake llvm

For Debian-based systems, install these packages:

    sudo apt install build-essential clang libc++-dev libc++abi-dev cmake libboost-all-dev clang-tidy

Additionally, [replxx](https://github.com/AmokHuginnsson/replxx) needs to be
built from source. The simplest approach is to install it into `~/local` like this:

    cd
    git clone https://github.com/AmokHuginnsson/replxx.git
    cd replxx
    mkdir -p build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${HOME}/local .. && make
    make install

(If `replxx` is installed somewhere else, modify `debugger/CMakeLists.txt` as needed.)

Once that's all set up:

    mkdir build && cd build
    cmake -DCMAKE_PREFIX_PATH=~/local ../zcpm

Or if a particular non-default compiler is needed:

    mkdir build && cd build
    cmake -DCMAKE_PREFIX_PATH=~/local -DCMAKE_C_COMPILER=/opt/homebrew/Cellar/llvm/15.0.1/bin/clang -DCMAKE_CXX_COMPILER=/opt/homebrew/Cellar/llvm/15.0.1/bin/clang++ ../zcpm

Running
-------

First make sure `~/zcpm/` has been created containing both `bdos.bin` and `bdos.lab` copied from the
source directory. Or, if preferred the locations of those files can be manually specified via command
line options.

Running the Debugger. Assuming that some CP/M test binaries are in `~/xcpm`

    ~/path/to/debugger ~/xcpm/drivea/CLS.COM
    ZCPM> go
    ZPPM> quit

It can be helpful to use symbol tables to add information to the log. The symbol table
from the BDOS can be used as well as the one from the assembled binary that is being testing like this:

    ~/path/to/debugger --bdossym ../bdos/bdos.lab --usersym ~/Coding/z80/emutests/test05.lab ~/Coding/z80/emutests/test05.com blah.txt

Using the Runner:

    ~/path/to/runner ~/xcpm/drivea/HELLO.COM

Or with loading assembler-produced symbols:

    ~/path/to/runner --bdossym ../bdos/bdos.lab --usersym ~/Coding/z80/emutests/test06.lab ~/Coding/z80/emutests/test06.com

There's also an optional `USE_PROFILE` cmake option that enables profiling at build time. (With GCC only)

Keymaps
-------

Some CP/M programs such as WordStar or Turbo Pascal use keystrokes such as ^S to mean move one character left
(which was at the time referred to as WordStar cursor control). To make such programs work more nicely on a
modern keyboard, a keystroke mapping file is used which maps a left arrow to ^S and so on.

For example:

    KEY_UP    ^E
    KEY_RIGHT ^D
    KEY_LEFT  ^S
    KEY_DOWN  ^X

The first column is the ncurses key name, the second column is one or more keys that should be generated
in response. By default, Runner uses a predefined file called `wordstar.keys` which is loaded on startup,
this can be overridden by the `--keymap` command line argument.

Unfortunately this is an imperfect solution; CP/M is inconsistent with terminal management, it's hard to
cleanly abstract these aspects.

Command-line arguments
----------------------

| Option   | Default              | Meaning                                                                                |
|----------|----------------------|----------------------------------------------------------------------------------------|
| help     | (none)               | Lists the currently implemented options                                                |
| bdosfile | `zcpm/bdos.bin`      | Filename of a binary blob (assembled Z80 code) that implements BDOS                    |
| bdossym  | `zcpm/bdos.lab`      | Optional symbol (.lab) file for BDOS                                                   |
| usersym  | (none)               | Optional symbol (.lab) file for user executable; normally generated by a Z80 assembler |
| bdosbase | 0xDC00               | Base address for binary BDOS file                                                      |
| wboot    | 0xF203               | Address of WBOOT in loaded binary BDOS                                                 |
| fbase    | 0xE406               | Address of FBASE in loaded binary BDOS                                                 |
| terminal | (none)               | Terminal type to emulate; default is PLAIN, could also be VT100, TELEVIDEO             |
| keymap   | `zcpm/wordstar.keys` | Optional keymap file for terminal emulation                                            |
| columns  | 80                   | Terminal column count (ignored for 'plain' terminal)                                   |
| rows     | 24                   | Terminal row count (ignored for 'plain' terminal)                                      |
| memcheck | true                 | Enable memory access checks?                                                           |
| logbdos  | true                 | Enable logging of BDOS calls?                                                          |
| logfile  | `zcpm.log`           | Name of logfile                                                                        |
| binary   | (none)               | CP/M binary input file to execute                                                      |
| args     | (none))              | Parameters for binary                                                                  |

Handy commands
--------------

Running WordStar (after copying `WS*.*` into current directory):

    ~/path/to/runner --terminal=vt100 WS.COM

grepping the resultant log file for all BDOS calls (with some detail on each):

    grep -A 1 'BDOS: fn' zcpm.log

grepping the resultant log file to see a summary of what BDOS & BIOS calls were made:

    grep -E 'BIOS fn|BDOS: fn' zcpm.log

Build with static analysis (via `clang-tidy`) enabled:

    cmake -DCMAKE_PREFIX_PATH=~/local -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++ -DUSE_TIDY=1 ../zcpm/

or:

    cmake -DCMAKE_PREFIX_PATH=~/local -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++ -DUSE_TIDY=1 -DCLANG_TIDY_EXE=/opt/homebrew/Cellar/llvm/15.0.1/bin/clang-tidy ../zcpm

Using the gcc static analyser
-----------------------------

As of gcc 10.x, gcc has a static analysis facility. It can be enabled for `zcpm` by the cmake option `USE_ANALYSER`.

But note that it is very slow to run, and needs LOTS of RAM to do its thing. So if it
aborts it has probably run out of memory. Sticking with a single-threaded build can help. And turn
off the ASAN stuff to make things work better. e.g.

    cd build
    rm -rf *
    cmake -DUSE_ANALYSER=ON -DUSE_SANITISERS=OFF -DCMAKE_PREFIX_PATH=~/local -DCMAKE_C_COMPILER=gcc-12 -DCMAKE_CXX_COMPILER=g++-12 ../zcpm
    make

Target Platforms:
----------------

- [x] macOS
- [ ] Windows
- [x] Linux

License:
-------

- BSD
