# dostools

## pci.exe

Without command line parameters, this tool dumps the address ranges, port ranges and IRQ assignments of all PCI devices.

The first parameter is taken as a filter for functions. It can have several forms, like
- `01:07.0` The primary function (.0) of device 7 on bus 1. bus and device numbers must be two hex digits each.
- `03/00/00` All functions with class 03, sub-class 00 and programming interface 00 (i.e. VGA cards).
- `03/00/00@0` The first VGA-compatible function.
- `03/00/00@1` The second VGA-compatible function. The instance number is *decimal* and may have as many digits as required
- `8086:7110` All devices with vendor ID 8086 and device ID 7110 (the primary function of Intel's PIIX4). Device and 
              vendor IDs are specified as hexadecimal and must be given using 4 digits.
- `8086:7110@0` The first PIIX4 device.

If the first parameter is the only parameter, all devices matching that parameter are dumped. Otherwise, the subsequen parameters
are a list of PCI registers ro be read/written. Again, several forms are supported. The number of digits in each form is fixed and
must be exactly as shown:
- `3C` Print the byte in configuration space at address 3C, i.e. the interrupt number as assigned by the BIOS or Operating system.
- `02.W` Print the word (16-bit) at configuration space address 02, i.e. the device ID. Lowercase "w" works, too.
- `10.L` Print the longword (32-bit) at configuration space address 10, i.e. the first BAR. Lowercase "l", as well
          as "D" or "d" (for double-word) work, too.
- `3C=0B` Overwrite the assigned interrupt number. Note: This does not actually re-route the interrupt, but makes drivers believe
          the function is connected to IRQ11 (0B hex).
- `04=0007` Update the command word to allow I/O response, memory response and bus-mastering
- `10=10000000` Move the range decoded in the first BAR to 0x10000000
- `04=0000:0004` Use 0004 as mask (only the busmaster enable bit) and write corresponding bits from 0000 to configuration word at
                 address 4, i.e.: disable busmastering. It is an error for the data to have bits set that are not enabled in the mask.
- `04=00:04` The same as above, but using byte-wide access to configuration space.
- `10=ABCD0000:FFFFF000` Update the top 20 bits of the first BAR.
- `04=04^00` Replacing the colon by the caret unlocks a special feature: bit flipping. Bits set in the first number (the "XOR mask"),
             but not set in the second number (the "AND mask") will be flipped. In this case, the bus master enable bit will be flipped
             every time this command is executed.

You may also use `-v` (verbose) to get a little more verbosity, or `-q` (quiet) to suppress most output, as well as `-h` for the built-in
help message. These switches must precede all other command line options.

## dumpmem.exe

Uses the BIOS extended memory copy function to access memory at arbitrary addresses and write it to a file. The invocation is like

```
dumpmem BIOS.BIN FFFE0000 0x20000
```

to dump the last 128K (0x20000 bytes) of the address space to a file called BIOS.BIN. On many 486 and Pentium-class computers,
this will create a dump of the contents pf the flash chip (which is different from the runtime BIOS you see at F000:0000). The
first parameter is the output filename, the second parameter is the start address (hex, not 0x in the beginning allowed) and the third
parameter is the size (as C integer, so use 0x for hex, a leading zero for octal or anything else for decimal).

Note that physical memory access may not produce the expected results in virtualized environments (like the Windows DOS box).
