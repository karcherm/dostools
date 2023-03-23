#include <conio.h>
#include <string.h>
#include <stdio.h>
#include "pci.h"

#ifdef __WATCOMC__

#define asm _asm

#endif

void my_outpd(unsigned port, unsigned long value)
{
    asm {
        mov dx, [port]
        db 66h
        mov ax, [WORD PTR value]
        db 66h
        out dx,ax
    }
}

unsigned long my_inpd(unsigned port)
{
    asm {
        mov dx, [port]
        db 66h
        in ax,dx
        db 66h
        mov dx,ax
        mov cl,10h
        db 66h
        shr dx,cl
    }
}

typedef struct {
    void (*writeb)(unsigned char value);
    void (*writew)(unsigned int value);
    void (*writed)(unsigned long value);
    unsigned (*readb)();
    unsigned (*readw)();
    unsigned long (*readd)();
    int (*parse_address)(const char* addr);
} space_t;

static unsigned parsed_io_address;
int io_parse_address(const char* addr)
{
    char dummy;
    unsigned bus, dev, fn, bar;
    unsigned long offset;
    unsigned vendor;
    size_t addrlen = strlen(addr);
    if (addrlen > 10 && addrlen <= 12 &&
        addr[2] == ':' && addr[5] == '.' && addr[7] == '$' && addr[9] == '+' &&
        sscanf(addr, "%x:%x.%u$%u+%lx%c", &bus, &dev, &fn, &bar, &offset, &dummy) == 5)
    {
        dev_addr addr;
        unsigned long bar_val;
        if (pci_init() < 0)
        {
            fputs("No PCI BIOS found\n", stderr);
            return -1;
        }
        if (bus > last_bus)
        {
            fputs("Bad bus number\n", stderr);
            return -1;            
        }
        if (dev > 31)
        {
            fputs("Invalid device number\n", stderr);
            return -1;            
        }
        if (fn > 7)
        {
            fputs("Invalid function number\n", stderr);
            return -1;            
        }
        if (bar > 5)
        {
            fputs("Invalid BAR number\n", stderr);
            return -1;            
        }
        if (offset >= 0x100)
        {
            fputs("Invalid I/O offset (PCI I/O areas are 256 bytes maximum)\n", stderr);
            return -1;
        }
        addr = ADDR(bus, dev, fn);
        if (pci_read_word(addr, 0, &vendor) < 0 || vendor == 0xFFFF)
        {
            fputs("specified PCI device does not exist\n", stderr);
            return -1;
        }
        if (pci_read_dword(addr, 0x10 + 4*bar, &bar_val) < 0 || bar_val == 0x00000000)
        {
            fputs("specified base address register does not exist\n", stderr);
            return -1;
        }
        if (!(bar_val & 1))
        {
            fputs("specified base address register describes a memory mapped region\n", stderr);
            return -1;
        }
        parsed_io_address = (bar_val & ~1) + offset;
        return 0;
    }
    else if (addrlen <= 4 && sscanf(addr, "%x%c", &parsed_io_address, &dummy) == 1)
    {
        return 0;
    }

    return -1;
}

void io_writeb(unsigned char value)
{
    outp(parsed_io_address, value);
}

void io_writew(unsigned int value)
{
    outpw(parsed_io_address, value);
}

void io_writed(unsigned long value)
{
    my_outpd(parsed_io_address, value);
}

unsigned io_readb()
{
    return inp(parsed_io_address);
}

unsigned io_readw()
{
    return inpw(parsed_io_address);
}

unsigned long io_readd()
{
    return my_inpd(parsed_io_address);
}

static const space_t iospace = {
    io_writeb, io_writew, io_writed,
    io_readb, io_readw, io_readd,
    io_parse_address
};

int main(int argc, char** argv)
{
    char dummy;
    char sizechar;
    unsigned long value;
    enum { MODE_POKE, MODE_PEEK } mode;
    const space_t* space;

    if (argc < 2)
    {
        fputs("missing verb\n", stderr);
        return 1;
    }

    if (strncmp(argv[1], "out", 3) == 0)
    {
        sizechar = argv[1][3];
        space = &iospace;
        mode = MODE_POKE;
    }
    else if (strncmp(argv[1], "in", 2) == 0)
    {
        sizechar = argv[1][2];
        space = &iospace;
        mode = MODE_PEEK;
    }
/*    else if (strncmp(argv[1], "poke", 4) == 0)
    {
        sizechar = argv[1][4];
        space = memspace;
        mode = MODE_POKE;
    }*/
    else
    {
        fputs("Bad verb\n", stderr);
        return 1;
    }
    
    if (argc < 3)
    {
        fputs("missing address\n", stderr);
        return 1;
    }

    if (space->parse_address(argv[2]) < 0)
    {
        fputs("Bad address\n", stderr);
        return 1;
    }

    if (mode == MODE_POKE)
    {
        if (argc < 4)
        {
            fputs("missing data to write\n", stderr);
            return 1;
        }

        switch(sizechar)
        {
            case 'b':
                if (strlen(argv[3]) == 2 && sscanf(argv[3], "%lx%x", &value, &dummy) == 1)
                {
                    space->writeb(value & 0xFF);
                    return 0;
                }
                else
                {
                    fputs("Byte to write must be 2 hex digits\n", stderr);
                    return 1;
                }
            case 'w':
                if (strlen(argv[3]) == 4 && sscanf(argv[3], "%lx%x", &value, &dummy) == 1)
                {
                    space->writew(value & 0xFFFF);
                    return 0;
                }
                else
                {
                    fputs("Word to write must be 4 hex digits\n", stderr);
                    return 1;
                }
            case 'd':
            case 'l':
                if (strlen(argv[3]) == 8 && sscanf(argv[3], "%lx%x", &value, &dummy) == 1)
                {
                    space->writed(value);
                    return 0;
                }
                else
                {
                    fputs("Dword to write must be 8 hex digits\n", stderr);
                    return 1;
                }
            default:
                fputs("Bad size character\n", stderr);
                return 1;
        }
    }
    else if (mode == MODE_PEEK)
    {
        switch(sizechar)
        {
            case 'b':
                printf("%02x\n", space->readb());
                break;
            case 'w':
                printf("%04x\n", space->readw());
                break;
            case 'd':
            case 'l':
                printf("%08lx\n", space->readd());
                break;
            default:
                fputs("Bad size character\n", stderr);
                return 1;
        }
    }
    
    return 0;
}
