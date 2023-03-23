#include <dos.h>
#include "pci.h"

unsigned char last_bus = 0;
unsigned int bios_version;

int pci_init(void)
{
    union REGS r;
    r.x.ax = 0xB101;
    r.x.dx = 0;
    int86(0x1A, &r, &r);
    if (!r.x.cflag &&
         r.x.dx == 0x4350)  /* start of "PCI " in edx */
    {
        last_bus = r.h.cl;
        bios_version = r.x.bx;
        return 0;
    }
    return -1;
}