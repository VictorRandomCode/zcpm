Testing with various CP/M binaries
==================================

Context
-------

This file records some useful data points that have been tested. Beyond these I also use a small set of handmade Z80
assembly programs to exercise specific BDOS calls or instruction combinations, but I haven't yet uploaded those.

All of these tests use the `runner` executable. Programs that are command-line only use the standard console output, but
for programs which use a more complicated interface, use the `--terminal=vt100` option. For example, run WordStar like
this:

    ~/path/to/runner --terminal=vt100 WS.COM

A great place to find more CP/M software is http://www.classiccmp.org/cpmarchives/

Keep in mind that `zcpm` is designed for CP/M 2.2 which covers the vast majority of available CP/M software. Trying to
run a CP/M 3.x program should fail cleanly, because most of them will detect the incorrect version, raise an error and
abort.

Currently, `zcpm` terminal output supports VT100 (aka ANSI) terminals and Televideo 925. VT100 support is more complete
than Televideo, both are a work in progress.

In order to run a binary that uses cursor addressing, you need a binary that either already supports your *host*
terminal emulation in which case the 'plain' emulation should be used, or a binary that targets VT100/ANSI or Televideo
925.

Some CP/M software (e.g. WordStar, Turbo Pascal, etc) included an "installer". Typically this installer needs to be run
first, and it will ask you for the target terminal type, and will then modify the main binary to support that terminal.

Standard CP/M 2.2 utilities
---------------------------

A standard CP/M system comes with various standard binaries such as `PIP.COM`. Unlike "real" applications such as
WordStar, these utilities have little value via ZCPM because the host platform offers much better tools for doing things
such as copying or editing files. Nevertheless they can be fun to play with. One place to grab them is downloading
`cpm22-b.zip` from http://www.cpm.z80.de/binary.html#utilities

Unzip that file and then you can, for example, use `PIP` to copy `READ.ME` to `READ.2` like this:

    ~/path/to/runner PIP.COM READ.2=READ.ME

WordStar
--------

WordStar 4.0 works properly. Download WS400.zip from http://www.retroarchive.org/cpm/text/wordstar-collection.html
and extract the ZIP contents to a new directory. Then run the installer using a command something like:

    ~/path/to/runner WINSTALL.COM

When prompted, select a VT100 terminal, and the installer will then modify the WS.COM file as needed. Then run WS.COM as
shown above, it should work as intended.

SuperCalc
---------

Download `Suprcalc.zip` from http://www.retroarchive.org/cpm/business/business.htm and extract the ZIP contents to a new
directory. Run the installer like this:

    ~/path/to/runner --terminal=vt100 INSTALL.COM

When prompted, use SC.COM as the name of the SuperCalc(tm) file and choose DEC VT-100 as the terminal type. Don't forget
to also download `sc.hlp`, SuperCalc won't run without it.

SuperCalc deliberately modifies memory locations 0000-0003 when it starts, overwriting the jump vector at that address.
ZCPM is written to disallow that in order to detect errant programs. Protection of that address can be disabled by
suitably modifying line 64 of hardware.cpp. If that is done, SuperCalc appears to correctly function. One day soon I'll
add a runtime flag to allow this when needed.

BDS-C
-----

Starting at http://www.cpm.z80.de/develop.htm download bdsc-all.zip and unzip it into a new directory. Within that:

    cd bdsc160
    cp work/* .

Create a source file there such as `blah.c` (using pre-ANSI C) and you can then use it compile, link, and run like this:

    ~/path/to/runner CC.COM blah.c
    ~/path/to/runner CLINK.COM blah
    ~/path/to/runner blah.com

VEDIT
-----

Starting at http://www.retroarchive.org/cpm/text/text.htm download `VEDIT_D1.ZIP` and `VEDIT_D2.ZIP`

Unzip both `vedit_d1.zip` and `vedit_d2.zip` into a new directory and then run the installer:

    ~/path/to/runner INSTALL.COM VPLUSZC.SET VPLUS.COM

Use option 12 to choose the CRT Terminal Type, and then choose "DEC VT-100". Exit the installer, and then you can run
VEDIT like this:

    ~/path/to/runner --terminal=vt100 VPLUS.COM

TODO: I'm still working out how to drive this program!

Turbo Pascal
------------

Starting at http://www.retroarchive.org/cpm/lang/lang.htm download `TPAS30.ZIP`

Unzip that file into a new directory and then run the installer:

    ~/path/to/runner tinst.com

At this stage you'll need to configure it for a Televideo terminal. Once this is done, Turbo Pascal can then be used
e.g.:

    ~/path/to/runner --terminal=televideo turbo.com

You'll then find that Turbo Pascal works as it should, but the tricky bit is that `zcpm` does not remap keyboard cursor
(arrow) keys to the emulated type, so you will need to use WordStar cursor controls to drive the editor.

Multiplan
---------

Starting at http://www.retroarchive.org/cpm/business/business.htm download `MULTPLAN.ZIP`

Unzip that file into a new directory and then run the installer:

    ~/path/to/runner --terminal=vt100 INSTALL.COM

Note that, for reasons not yet investigated, the installer needs to be run under a terminal emulator, not in
pass-through mode.

When prompted, choose either "DEC VT-100" or "Televideo 925". 

The installed binary can then be run like this (depending on the selected emulation):

    ~/path/to/runner --terminal=vt100 MP.COM

Currently, better results are to be had with VT100 rather than Televideo.

Nemesis
-------

A D&D style game, starting at http://www.retroarchive.org/cpm/games/games.htm download `nemesis.zip`

Unzip that file into a new directory, and from there start things like this:

    ~/path/to/runner --terminal=televideo termdef.com

Set up a new `term.def`, setting up the system for a Televideo terminal and a CPU speed of 4 MHz. Then set up a character
using this:

    ~/path/to/runner --terminal=televideo person.com

Assuming that you've named your new character as 'fred', start the game:

    ~/path/to/runner --terminal=televideo nemesis.com fred

This appears to work correctly. Refer to the PDF file in the zip contents for more information.

dBase II
--------

From http://www.retroarchive.org/cpm/dbase/dbase.htm download `ashtonpak.zip`

Unzip it into a new directory, and within that `cd` to `DBASE-II/CPM80/V2-41/D1` and then:

    ~/path/to/runner INSTALL.COM

Configure for a VT-100, and save the configuration.

    ~/path/to/runner --terminal=vt100 DBASE.COM

This appears to work as it should, based on my ancient memories of using dBase II!
