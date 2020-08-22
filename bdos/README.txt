We're using a disassembled BDOS that Clark A. Calkins constructed (which would have
been a massive effort!).  To assemble that file into a binary and listing, first
make sure that z80asm is in your path:

export PATH=$PATH:~/local/bin

And then do:

z80asm bdos.z80 -o bdos.bin --list=bdos.lis --label=bdos.lab
