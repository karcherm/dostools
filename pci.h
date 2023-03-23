typedef unsigned int dev_addr;
#define ADDR(bus,dev,fn) (((bus) << 8) | ((dev) << 3) | (fn))

extern unsigned char last_bus;
extern unsigned int bios_version;

int pci_init(void);
int dev_by_id(unsigned int vendor, unsigned int device, int index, dev_addr *addr);
int dev_by_class(unsigned long classcode, int index, dev_addr *dev);
int pci_read_byte(dev_addr dev, unsigned int reg, unsigned char* data);
int pci_read_word(dev_addr dev, unsigned int reg, unsigned* data);
int pci_read_dword(dev_addr dev, unsigned int reg, unsigned long* data);
int pci_write_byte(dev_addr dev, unsigned int reg, unsigned char data);
int pci_write_word(dev_addr dev, unsigned int reg, unsigned data);
int pci_write_dword(dev_addr dev, unsigned int reg, unsigned long data);
