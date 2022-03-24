#include <dos.h>
#include <stddef.h>
#include <stdlib.h>

#include <stdio.h>

void extread(void far* dest, unsigned long src, size_t size)
{
    unsigned short gdt[8*4];    // 8 descriptors, 4 words each
    union REGS r;
    struct SREGS sr;
    unsigned long dest_lin;
    dest_lin = (((unsigned long)FP_SEG(dest)) << 4) + (FP_OFF(dest));
    gdt[8] = 0xFFFF;            // src limit
    gdt[9] = src & 0xFFFF;
    gdt[10] = ((src >> 16) & 0xFF) | 0x9300;
    gdt[11] = (src >> 16) & 0xFF00;

    gdt[12] = 0xFFFF;           // dst limit
    gdt[13] = dest_lin & 0xFFFF;
    gdt[14] = (dest_lin >> 16) | 0x9300;
    gdt[15] = 0;

    r.h.ah = 0x87;
    r.x.cx = size / 2;
    r.x.si = FP_OFF((void far*)gdt);
    segread(&sr);
    sr.es = sr.ss;
    int86x(0x15, &r, &r, &sr);
}

int main(int argc, char** argv)
{
    const size_t bufsize = 0x8000;
    void* buffer;
    FILE *outfile;
    unsigned long base;
    unsigned long size;
    unsigned char dummy;
    if (argc != 4)
    {
        puts("DUMPMEM - linear memory dumping utility, (C) 2022 Michael Karcher\n"
             "Distributable under the MIT license - no warranty included\n"
             "DUMPMEM <filename> <startaddress> <length>\n"
             "  filename - name of file to be written\n"
             "  address  - linear start address (hex)\n"
             "  length   - length (C like integer, start with 0x for hex)");
        return 0;
    }
    if (sscanf(argv[2], "%lx%c", &base, &dummy) != 1)
    {
        fprintf(stderr, "bad hex start address %s\n", argv[2]);
        return 1;
    }
    if (sscanf(argv[3], "%li%c", &size, &dummy) != 1)
    {
        fprintf(stderr, "bad size %s\n", argv[3]);
        return 1;
    }
    if (size != 0 && base + size - 1 < base)
    {
        fprintf(stderr, "address overflow: %08lx bytes starting at %08lx\n", size, base);
        return 1;
    }
    buffer = malloc(bufsize);
    if (!buffer)
    {
        fprintf(stderr, "out of memory");
        return 1;
    }
    outfile = fopen(argv[1], "wb");
    if (!outfile)
    {
        perror(argv[1]);
        return 1;
    }
    while (size > bufsize)
    {
        extread(buffer, base, bufsize);
        if (fwrite(buffer, 1, bufsize, outfile) != bufsize)
        {
            fprintf(stderr, "write error");
            fclose(outfile);
            return 1;
        }
        size -= bufsize;
        base += bufsize;
    }
    extread(buffer, base, size);
    if (fwrite(buffer, 1, size, outfile) != size)
    {
        fprintf(stderr, "write error");
        fclose(outfile);
        return 1;
    }
    if (fclose(outfile) < 0)
    {
        perror("closing output");
        return 1;
    }
    return 0;
}