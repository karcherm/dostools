#include <stdio.h>
#include <string.h>
#include "pci.h"

#define ARRAYSIZE(x) (sizeof(x) / sizeof(x[0]))

#define CMD_IO 1
#define CMD_MEM 2
#define CMD_MASTER 4

char *format_addr(dev_addr addr)
{
    static char addrbuf[20];
    sprintf(addrbuf, "%02x:%02x.%d", addr >> 8, (addr >> 3) & 0x1F, addr & 7);
    return addrbuf;
}

int read_barsize(dev_addr addr, unsigned int bar, unsigned long oldbar, unsigned int cmdbit, unsigned long *barsize)
{
    unsigned int oldcmd;
    unsigned long localsize;
    int status;
    if (pci_read_word(addr, 4, &oldcmd) < 0)
       return -1;
    if (pci_write_word(addr, 4, oldcmd & ~cmdbit) < 0)
       return -1;
    status = pci_write_dword(addr, bar, 0xFFFFFFFFUL);
    status |= pci_read_dword(addr, bar, &localsize);
    status |= pci_write_dword(addr, bar, oldbar);
    status |= pci_write_word(addr, 4, oldcmd);

    if (cmdbit == CMD_MEM)
    {
        // clear memory type flags
        localsize &= ~0xFUL;
    }
    else
    {
        // clear IO bit and reserved extra bit
        localsize &= ~3UL;
        // fake top bits as writeable even for devices that only accept 16 bit I/O addresses
        localsize |= 0xFFFF0000UL;
    }
    
    *barsize = -localsize;

    return status;
}

int cmdline_verbose = 1;

char *nice_size(unsigned long size)
{
    static char sizebuf[10];
    if (size < 1024)
        sprintf(sizebuf, "%u", (unsigned)size);
    else if (size < 1024L*1024)
        sprintf(sizebuf, "%uK", (unsigned)(size >> 10));
    else
        sprintf(sizebuf, "%uM", (unsigned)(size >> 20));
    return sizebuf;
}

int dump_bar(dev_addr addr, unsigned int bar)
{
    unsigned long barval;
    unsigned long barval_high;
    unsigned long barsize;
    unsigned long barsize_high;
    int bar_width;

    bar_width = 4;  // typical BARs are 32 bits (4 bytes) wide
    if (pci_read_dword(addr, bar, &barval) >= 0)
    {
        if (barval & 1)
        {
            if (read_barsize(addr, bar, barval, CMD_IO, &barsize) >= 0)
            {
                barval &= ~3L;
                printf("  %02x: PIO  at %04lx..%04lx\n", bar, barval, barval + barsize - 1);
            }
        }
        else
        {
            unsigned char barflags;
            const char *memkind;

            barflags =  barval & 0xF;
            memkind = (barflags & 8) ? "MEM " : "MMIO";
            if (read_barsize(addr, bar, barval, CMD_MEM, &barsize) >= 0 &&
                // "unimplemented BARs are hardwired to zero"
                // read_barsize returns 0 if a BAR is unimplemented, or if the alignment
                // requirement is 4G or higher (64-bit BARs). To differentiate, also look
                // at barflags, which is non-zero on 64-bit BARs.
                (barflags != 0 || barsize != 0))
            {
                barval &= ~0x0FL;
                switch(barflags & 0x6)
                {
                case 0:
                    // normal 32-bit memory space
                    printf("  %02x: %s at %08lx..%08lx (%s)\n",
                        bar, memkind, barval, (barval + barsize - 1),
                        nice_size(barsize));
                    break;
                case 2:
                    // Old PCI cards: 1MB memory space
                    printf("  %02x: %s at %05lx..%05lx (%s)\n",
                        bar, memkind, barval, (barval + barsize - 1),
                        nice_size(barsize));
                    break;
                case 4:
                    // 64-bit memory space
                    if (pci_read_dword(addr, bar + 4, &barval_high) >= 0 &&
                        read_barsize(addr, bar + 4, barval_high, CMD_MEM, &barsize_high) >= 0)
                    {
                        if (barsize == 0)  // this actually means 4G
                            barsize_high++;
                        printf("  %02x: %s at %08lx%08lx..%08lx%08lx (%s)\n",
                            bar, memkind, barval_high, barval, (barval_high + barsize_high), (barval + barsize - 1),
                            nice_size(barsize));
                    }
                    bar_width = 8;
                    break;
                default:
                    printf("  %02x: unhandled memory BAR type\n");
                    break;
                }
            }
        }
    }
    return bar_width;
}

void dump_rombar(dev_addr addr, unsigned bar)
{
    unsigned long barval;
    if (pci_read_dword(addr, bar, &barval) >= 0)
    {
        if (barval & 1)
        {
            printf("  ROM enabled at %08lx\n", barval & ~1UL);
        }
        else
        {
            unsigned long barsize;
            if (read_barsize(addr, bar, barval, CMD_MEM, &barsize) >= 0 &&
                barsize != 0)
                printf("  ROM (disabled), area size %08lx (%s)\n", barsize, nice_size(barsize));
        }
    }
}

void dump_resources(dev_addr addr)
{
    unsigned int bar;
    for (bar = 0x10; bar <= 0x20;)
    {
        bar += dump_bar(addr, bar);
    }
    dump_rombar(addr, 0x30);
}

void dump_bridge(dev_addr addr)
{
    unsigned char primary_bus, secondary_bus, limit_bus;
    unsigned int memlow, memhigh;
    unsigned int bar;
    for (bar = 0x10; bar <= 0x14; bar += 4)
    {
        dump_bar(addr, bar);
    }
    if (pci_read_byte(addr, 0x18, &primary_bus) >= 0 &&
        pci_read_byte(addr, 0x19, &secondary_bus) >= 0 &&
        pci_read_byte(addr, 0x1A, &limit_bus) >= 0)
    {
        if (limit_bus == secondary_bus)
            printf("  bus %d -> %d\n", primary_bus, secondary_bus);
        else
            printf("  bus %d -> %d, downstream %d..%d\n", primary_bus, secondary_bus, secondary_bus + 1, limit_bus);
    }
    if (pci_read_word(addr, 0x20, &memlow) >= 0 &&
        pci_read_word(addr, 0x22, &memhigh) >= 0 &&
        memlow != 0 &&
        memhigh >= memlow)
    {
        printf("  forwarding MMIO %04x0000..%04xffff (%s)\n",
              memlow, memhigh | 0x000F,
              nice_size((unsigned long)(((memhigh-memlow) & 0xFFF0) + 0x10) << 16));
    }
    if (pci_read_word(addr, 0x24, &memlow) >= 0 &&
        pci_read_word(addr, 0x26, &memhigh) >= 0 &&
        memlow != 0 &&
        memhigh >= memlow)
    {
        unsigned int memtype;
        memtype = memlow & 0x000F;
        memlow &= 0xFFF0;
        memhigh |= 0x000F;
        if (memtype == 0)
        {
            printf("  forwarding MEM  %04x0000..%04xffff (%s)\n",
                  memlow, memhigh,
                  nice_size((unsigned long)(((memhigh-memlow) & 0xFFF0) + 0x10) << 16));            
        }
        else if (memtype == 1)
        {
            unsigned long memlow64, memhigh64;
            if (pci_read_dword(addr, 0x28, &memlow64) >= 0 &&
                pci_read_dword(addr, 0x2C, &memhigh64) >= 0)
            {
                // Range sizes above 4G result in wrong output.
                // This is a won't-fix-issue for now.
                printf("  forwarding MEM  %08lx%04x0000..%08lx%04xffff (%s)\n",
                      memlow64, memlow, memhigh64, memhigh,
                      nice_size((unsigned long)(((memhigh-memlow) & 0xFFF0) + 0x10) << 16));
            }
        }
        else
        {
            printf("  unsupported MEM type %d\n", memtype);
        }
    }
}

void dump_device(dev_addr addr)
{
    unsigned vendor, device;
    unsigned char cls, subcls, progif;
    unsigned char intpin, irqnum;
    unsigned char hdrtype;
    if (pci_read_byte(addr, 0xE, &hdrtype) >= 0 &&
        pci_read_word(addr, 0, &vendor) >= 0 &&
        pci_read_word(addr, 2, &device) >= 0 &&
        pci_read_byte(addr, 0x0b, &cls) >= 0 &&
        pci_read_byte(addr, 0x0a, &subcls) >= 0 &&
        pci_read_byte(addr, 0x09, &progif) >= 0)
    {
        printf("%s: id %04x:%04x, class %02x/%02x/%02x\n",
               format_addr(addr),
               vendor, device,
               cls, subcls, progif);
        if (cmdline_verbose)
        {
            if (hdrtype == 0)
                dump_resources(addr);
            if (hdrtype == 1)
                dump_bridge(addr);
            if (pci_read_byte(addr, 0x3D, &intpin) >= 0 &&
                intpin != 0 &&
                pci_read_byte(addr, 0x3C, &irqnum) >= 0)
            {
                printf("  INT%c -> IRQ%d\n", 'A' + intpin - 1, irqnum);
            }
        }
    }
    else
        printf("%s: <error>\n", format_addr(addr));
}

typedef void iterate_fn(dev_addr addr);

void iterate_class(unsigned long classcode, iterate_fn *handler)
{
    int idx;
    dev_addr addr;
    for (idx = 0; dev_by_class(classcode, idx, &addr) >= 0; idx++)
    {
        handler(addr);
    }
}

void iterate_devid(unsigned int vendor, unsigned int device, iterate_fn *handler)
{
    int idx;
    dev_addr addr;
    for (idx = 0; dev_by_id(vendor, device, idx, &addr) >= 0; idx++)
    {
        handler(addr);
    }
}

void iterate_all(iterate_fn *handler)
{
    int bus, dev, fn;
    for (bus = 0; bus <= last_bus; bus++)
    {
        for (dev = 0; dev < 32; dev++)
        {
            dev_addr addr = ADDR(bus, dev, 0);
            unsigned char hdrtype;
            pci_read_byte(addr, 0xE, &hdrtype);
            if (hdrtype != 0xff)
            {
                int maxfncount = 1;
                if (hdrtype & 0x80)
                    maxfncount = 8;
                for (fn = 0; fn < maxfncount; fn++)
                {
                    pci_read_byte(addr + fn, 0xE, &hdrtype);
                    if (hdrtype != 0xff)
                        handler(addr + fn);
                    else
                        break;
                }
            }
        }
    }
}

unsigned long cmdline_class;

void iterate_cmdline_class(iterate_fn *handler)
{
    iterate_class(cmdline_class, handler);
}

unsigned int cmdline_vendor, cmdline_device;

void iterate_cmdline_devid(iterate_fn *handler)
{
    iterate_devid(cmdline_vendor, cmdline_device, handler);
}

dev_addr cmdline_addr;

void iterate_cmdline_addr(iterate_fn *handler)
{
    handler(cmdline_addr);
}
typedef void iterator_fn(iterate_fn *handler);

#define NOP         0
#define READ_BYTE   1
#define READ_WORD   2
#define READ_DWORD  3
#define WRITE_BYTE  4
#define WRITE_WORD  5
#define WRITE_DWORD 6
#define PATCH_BYTE  7
#define PATCH_WORD  8
#define PATCH_DWORD 9

struct patch_info {
       unsigned char regnr;
       unsigned char mode;
       unsigned long xormask;
       unsigned long andmask;
};

// Under DOS, we have 128 bytes command line, so we can't reach 40 reads
struct patch_info cmdline_patches[40] = {0};

void apply_patch(dev_addr addr, const struct patch_info *p)
{
    unsigned char b;
    unsigned int  w;
    unsigned long d;
    switch(p->mode)
    {
        case NOP:
            break;
        case READ_BYTE:
            if (pci_read_byte(addr, p->regnr, &b) >= 0)
                printf("%s - %02x is %02x\n",
                       format_addr(addr), p->regnr, b);
            break;
        case READ_WORD:
            if (pci_read_word(addr, p->regnr, &w) >= 0)
                printf("%s - %02x.W is %04x\n",
                       format_addr(addr), p->regnr, w);
            break;
        case READ_DWORD:
            if (pci_read_dword(addr, p->regnr, &d) >= 0)
                printf("%s - %02x.L is %08lx\n",
                       format_addr(addr), p->regnr, d);
            break;
        case WRITE_BYTE:
            if (pci_write_byte(addr, p->regnr, (unsigned char)p->xormask) >= 0 && cmdline_verbose)
                printf("%s - %02x <- %02x\n",
                       format_addr(addr), p->regnr, (unsigned char)p->xormask);
            break;
        case WRITE_WORD:
            if (pci_write_word(addr, p->regnr, (unsigned int)p->xormask) >= 0 && cmdline_verbose)
                printf("%s - %02x.W <- %04x\n",
                       format_addr(addr), p->regnr, (unsigned int)p->xormask);
            break;
        case WRITE_DWORD:
            if (pci_write_dword(addr, p->regnr, p->xormask) >= 0 && cmdline_verbose)
                printf("%s - %02x.L <- %08lx\n",
                       format_addr(addr), p->regnr, p->xormask);
            break;
        case PATCH_BYTE:
            if (pci_read_byte(addr, p->regnr, &b) >= 0)
            {
                unsigned char new_b = (b & p->andmask) ^ p->xormask;
                if (pci_write_byte(addr, p->regnr, new_b) >= 0 && cmdline_verbose)
                    printf("%s - %02x <- %02x (was %02x)\n",
                          format_addr(addr), p->regnr, new_b, b);
            }
            break;
        case PATCH_WORD:
            if (pci_read_word(addr, p->regnr, &w) >= 0)
            {
                unsigned new_w = (w & p->andmask) ^ p->xormask;
                if (pci_write_word(addr, p->regnr, new_w) >= 0 && cmdline_verbose)
                    printf("%s - %02x <- %04x (was %04x)\n",
                          format_addr(addr), p->regnr, new_w, w);
            }
            break;
        case PATCH_DWORD:
            if (pci_read_dword(addr, p->regnr, &d) >= 0)
            {
                unsigned long new_d = (d & p->andmask) ^ p->xormask;
                if (pci_write_dword(addr, p->regnr, new_d) >= 0 && cmdline_verbose)
                    printf("%s - %02x <- %08lx (was %08lx)\n",
                          format_addr(addr), p->regnr, new_d, d);
            }
            break;
    }
}

void apply_cmdline_patches(dev_addr addr)
{
    int idx;
    for (idx = 0; idx < ARRAYSIZE(cmdline_patches); idx++)
    {
        apply_patch(addr, &cmdline_patches[idx]);
    }
}

int main(int argc, char** argv)
{
    char dummy;
    iterator_fn *iter = iterate_all;
    iterate_fn *handler = dump_device;

    if (argc > 1 && strcmp(argv[1], "-q") == 0)
    {
        argc--;
        argv++;
        cmdline_verbose = 0;
    }

    if (argc > 1 && strcmp(argv[1], "-v") == 0)
    {
        argc--;
        argv++;
        cmdline_verbose = 2;
    }

    if (argc == 2 && strcmp(argv[1], "-h") == 0)
    {
        puts("PCI dump/patch utility for DOS, (C) 2022 Michael Karcher\n"
             "Distributable under the MIT license - no warranty included\n"
             "PCI [<devspec> [<patchspec>*]]\n"
             "  <devspec> specifies one or multiple devices, like this:\n"
             "    vvvv:dddd   - all cards with vendor id vvvv and device id dddd\n"
             "    cc/ss/ii    - all cards with class cc, subclass ss and progif ii\n"
             "    vvvv:dddd@n - n'th card matching vvvv:dddd\n"
             "    cc/ss/ii@n  - n'th card matching cc/ss/ii\n"
             "    bb:dd.f     - function f on device dd on bus bb\n"
             "     n - a zero-based decimal number (multiple digits allowed)\n"
             "     all other letters represent hexadecimal digits. The number of digits\n"
             "     is required to be exactly the repetition count of that letter.\n"
             "  <patchspec> specifies a register read or patch operation, like this\n"
             "    rr / rr.B    - read a single byte\n"
             "    rr.W         - read a 16-bit word\n"
             "    rr.L / rr.D  - read a 32-bit long (aka dword)\n"
             "    rr=xx        - write a single byte\n"
             "    rr=xxxx      - write a 16-bit word\n"
             "    rr=xxxxxxxx  - write a 32-bit dword\n"
             "    rr=xx:mm     - byte read/mod/write: new value xx in bits given by mm\n"
             "    rr=xxxx:mmmm - word r/m/w\n"
             "    rr=xxxxxxxx:mmmmmmmm - long r/m/w\n"
             "     In r/m/w operations, the value must not have any bits set that are not set\n"
             "     in the mask. The ':' character can be replaced by '^', to enable the bit\n"
             "     flip mode. Bits set in xx that are not the in the mask are allowed, and\n"
             "     will be toggled. Example: 04=03^01 will set bit 0 and toggle bit 1\n"
             "  If no patchspec is given, the selected devices are dumped");
        return 0;
    }

    if (pci_init() < 0)
    {
        fputs("No PCI BIOS found\n", stderr);
        return 1;
    }
    if (cmdline_verbose > 1)
        printf("PCI BIOS v%x.%02x found, managing busses 0..%d\n",
                    bios_version >> 8, bios_version & 0xFF, last_bus);

    if (argc > 1)
    {
        size_t speclen = strlen(argv[1]);
        if (speclen == 9 && argv[1][4] == ':')
        {
            if (sscanf(argv[1], "%x:%x%c", &cmdline_vendor, &cmdline_device, &dummy) != 2)
            {
                fprintf(stderr, "bad device id specification %s\n", argv[1]);
                return 1;
            }
            iter = iterate_cmdline_devid;
        }
        else if(speclen == 8 && argv[1][2] == '/' && argv[1][5] == '/')
        {
            unsigned int cls, subcls, progif;
            if (sscanf(argv[1], "%x/%x/%x%c", &cls, &subcls, &progif, &dummy) != 3)
            {
                fprintf(stderr, "bad class specification %s\n", argv[1]);
                return 1;
            }
            cmdline_class = ((unsigned long)cls << 16) | (subcls << 8) | progif;
            iter = iterate_cmdline_class;
        }
        else if(speclen == 7 && argv[1][2] == ':' && argv[1][5] == '.')
        {
            unsigned int bus, dev, fn;
            if (sscanf(argv[1], "%x:%x.%x%c", &bus, &dev, &fn, &dummy) != 3)
            {
                fprintf(stderr, "bad address specification %s\n", argv[1]);
                return 1;
            }
            cmdline_addr = ADDR(bus, dev, fn);
            iter = iterate_cmdline_addr;
        }
        else if (speclen > 10 && argv[1][4] == ':' && argv[1][9] == '@')
        {
            int cmdline_idx;
            // vendor/device id: vvvv:dddd
            if (sscanf(argv[1], "%x:%x@%d%c", &cmdline_vendor, &cmdline_device, &cmdline_idx, &dummy) != 3)
            {
                fprintf(stderr, "bad device id instance specification %s\n", argv[1]);
                return 1;
            }
            if (dev_by_id(cmdline_vendor, cmdline_device, cmdline_idx, &cmdline_addr) < 0)
            {
                fprintf(stderr, "device %s not found\n", argv[1]);
                return 1;
            }
            iter = iterate_cmdline_addr;
        }
        else if(speclen > 9 && argv[1][2] == '/' && argv[1][5] == '/' && argv[1][8] == '@')
        {
            unsigned int cls, subcls, progif;
            int cmdline_idx;
            if (sscanf(argv[1], "%x/%x/%x@%d%c", &cls, &subcls, &progif, &cmdline_idx, &dummy) != 4)
            {
                fprintf(stderr, "bad class instance specification %s\n", argv[1]);
                return 1;
            }
            cmdline_class = ((unsigned long)cls << 16) | (subcls << 8) | progif;
            if (dev_by_class(cmdline_class, cmdline_idx, &cmdline_addr) < 0)
            {
                fprintf(stderr, "device %s not found\n", argv[1]);
                return 1;
            }
            iter = iterate_cmdline_addr;
        }
        else
        {
            fprintf(stderr, "unsupported parameter %s\n", argv[1]);
            return 1;
        }
    }

    if (argc > 2)
    {
        int i;
        int tempint, tempint2;
        char patchkind;
        // switch from "lspci" to "setpci" mode
        handler = apply_cmdline_patches;
        for (i = 2; i < argc; i++)
        {
            int badarg = 0;
            char *arg = argv[i];
            struct patch_info *p = &cmdline_patches[i-2];
            size_t arglen = strlen(arg);
            if (arglen == 2)
            {
                if (sscanf(arg, "%x%c", &p->regnr) != 1)
                    badarg = 1;
                else
                    p->mode = READ_BYTE;
            }
            else if(arglen == 4 && arg[2] == '.')
            {
                char widthbyte;
                if (sscanf(arg, "%x.%c%c", &p->regnr, &widthbyte, &dummy) != 2)
                    badarg = 1;
                else
                {
                    switch(widthbyte)
                    {
                        case 'b':
                        case 'B':
                            p->mode = READ_BYTE;
                            break;
                        case 'w':
                        case 'W':
                            p->mode = READ_WORD;
                            if (p->regnr & 1)
                            {
                                fprintf(stderr, "misaligned word %02x\n", p->regnr);
                                badarg = 1;
                            }
                            break;
                        case 'd':
                        case 'D':
                        case 'l':
                        case 'L':
                            p->mode = READ_DWORD;
                            if (p->regnr & 3)
                            {
                                fprintf(stderr, "misaligned dword %02x\n", p->regnr);
                                badarg = 1;
                            }
                            break;
                        default:
                            badarg = 1;
                            break;
                    }
                }
            }
            else if(arglen == 5 && arg[2] == '=')
            {
                if (sscanf(arg, "%x=%x%c", &p->regnr, &tempint, &dummy) != 2)
                    badarg = 1;
                p->mode = WRITE_BYTE;
                p->xormask = tempint;
            }
            else if(arglen == 7 && arg[2] == '=')
            {
                if (sscanf(arg, "%x=%x%c", &p->regnr, &tempint, &dummy) != 2)
                    badarg = 1;
                else
                {
                    p->mode = WRITE_WORD;
                    if (p->regnr & 1)
                    {
                        fprintf(stderr, "misaligned word %02x\n", p->regnr);
                        badarg = 1;
                    }
                    p->xormask = tempint;
                }
            }
            else if(arglen == 11 && arg[2] == '=')
            {
                if (sscanf(arg, "%x=%lx%c", &p->regnr, &p->xormask, &dummy) != 2)
                    badarg = 1;
                else
                {
                    p->mode = WRITE_DWORD;
                    if (p->regnr & 3)
                    {
                        fprintf(stderr, "misaligned dword %02x\n", p->regnr);
                        badarg = 1;
                    }
                }
            }
            else if(arglen == 8 && arg[2] == '=' && (arg[5] == ':' || arg[5] == '^'))
            {
                if (sscanf(arg, "%x=%x%c%x%c", &p->regnr, &tempint, &patchkind, &tempint2, &dummy) != 4)
                    badarg = 1;
                else
                {
                    if (patchkind == ':' && (tempint & ~tempint2) != 0)
                    {
                        fprintf(stderr, "value %02x not inside mask %02x\n", tempint, tempint2);
                        badarg = 1;
                    }
                    else
                    {
                        p->mode = PATCH_BYTE;
                        p->xormask = tempint;
                        p->andmask = ~tempint2;
                    }
                }
            }
            else if(arglen == 12 && arg[2] == '=' && (arg[7] == ':' || arg[7] == '^'))
            {
                if (sscanf(arg, "%x=%x%c%x%c", &p->regnr, &tempint, &patchkind, &tempint2, &dummy) != 4)
                    badarg = 1;
                else
                {
                    if (patchkind == ':' && (tempint & ~tempint2) != 0)
                    {
                        fprintf(stderr, "value %04x not inside mask %04x\n", tempint, tempint2);
                        badarg = 1;
                    }
                    else
                    {
                        p->mode = PATCH_WORD;
                        p->xormask = tempint;
                        p->andmask = ~tempint2;
                    }
                }
            }
            else if(arglen == 20 && arg[2] == '=' && (arg[11] == ':' || arg[11] == '^'))
            {
                if (sscanf(arg, "%x=%lx%c%lx%c", &p->regnr, &p->xormask, &patchkind, &p->andmask, &dummy) != 4)
                    badarg = 1;
                else
                {
                    p->andmask ^= 0xFFFFFFFFUL;
                    if (patchkind == ':' && (p->xormask & p->andmask) != 0)
                    {
                        fprintf(stderr, "value %08lx not inside mask %08lx\n", p->xormask, ~p->andmask);
                        badarg = 1;
                    }
                    else
                    {
                        p->mode = PATCH_DWORD;
                    }
                }
            }
            else
            {
                badarg = 1;
            }
            if (badarg)
            {
                fprintf(stderr, "bad patch specification %s\n", arg);
                return 1;
            }
        }
    }

    iter(handler);
    return 0;
}

