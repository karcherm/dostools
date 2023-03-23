#include <dos.h>
#include "pci.h"

#ifdef __WATCOMC__

#define asm _asm

#define RETURNING_AX_PREFIX \
    int result;
#define RETURNING_AX_ASM_SUFFIX \
        mov [result],ax
#define RETURNING_AX_C_SUFFIX \
   return result;

#else

#define RETURNING_AX_PREFIX
#define RETURNING_AX_ASM_SUFFIX
#define RETURNING_AX_C_SUFFIX \
   return _AX;

#endif

int dev_by_id(unsigned int vendor, unsigned int device, int index, dev_addr *addr)
{
    union REGS r;
    r.x.ax = 0xB102;
    r.x.dx = vendor;
    r.x.cx = device;
    r.x.si = index;
    int86(0x1A, &r, &r);
    if (r.x.cflag)
       return -1;
    *addr = r.x.bx;
    return 0;
}

int dev_by_class(unsigned long classcode, int index, dev_addr *dev)
{
    RETURNING_AX_PREFIX
    asm {
        mov ax,0B103h
        db 66h
        mov cx,[WORD PTR classcode]
        mov si,[index]
        int 1Ah
        sbb ax,ax
        mov si, dev
        mov [si],bx
        RETURNING_AX_ASM_SUFFIX
    }
    RETURNING_AX_C_SUFFIX
}

int pci_read_byte(dev_addr dev, unsigned int reg, unsigned char* data)
{
    union REGS r;
    r.x.ax = 0xB108;
    r.x.bx = dev;
    r.x.di = reg;
    int86(0x1A, &r, &r);
    if (r.x.cflag)
       return -1;
    *data = r.h.cl;
    return 0;
}

int pci_read_word(dev_addr dev, unsigned int reg, unsigned* data)
{
    union REGS r;
    r.x.ax = 0xB109;
    r.x.bx = dev;
    r.x.di = reg;
    int86(0x1A, &r, &r);
    if (r.x.cflag)
       return -1;
    *data = r.x.cx;
    return 0;
}

int pci_read_dword(dev_addr dev, unsigned int reg, unsigned long* data)
{
    RETURNING_AX_PREFIX
    asm {
        mov ax,0B10Ah
        mov bx,[dev]
        mov di,[reg]
        int 1Ah
        sbb ax,ax
        mov si, [data]
        db 66h
        mov [si], cx
        RETURNING_AX_ASM_SUFFIX
    }
    RETURNING_AX_C_SUFFIX
}

int pci_write_byte(dev_addr dev, unsigned int reg, unsigned char data)
{
    union REGS r;
    r.x.ax = 0xB10B;
    r.x.bx = dev;
    r.h.cl = data;
    r.x.di = reg;
    int86(0x1A, &r, &r);
    if (r.x.cflag)
       return -1;
    return 0;
}

int pci_write_word(dev_addr dev, unsigned int reg, unsigned data)
{
    union REGS r;
    r.x.ax = 0xB10c;
    r.x.bx = dev;
    r.x.cx = data;
    r.x.di = reg;
    int86(0x1A, &r, &r);
    if (r.x.cflag)
       return -1;
    return 0;
}

int pci_write_dword(dev_addr dev, unsigned int reg, unsigned long data)
{
    RETURNING_AX_PREFIX
    asm {
        mov ax,0B10Dh
        mov bx,[dev]
        db 66h
        mov cx,[word ptr data]
        mov di,[reg]
        int 1Ah
        sbb ax,ax
        RETURNING_AX_ASM_SUFFIX
    }
    RETURNING_AX_C_SUFFIX
}
