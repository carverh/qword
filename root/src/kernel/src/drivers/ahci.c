#include <ahci.h>
#include <ahci/hba.h>
#include <ahci/fis.h>
#include <pci.h>
#include <klib.h>

size_t ahci_base;
struct hba_port_t *hba_ports;

void start_cmd(volatile struct hba_port_t *);
void stop_cmd(volatile struct hba_port_t *);
void kmemset64(void *ptr, uint64_t c, size_t count);

static int check_type(volatile struct hba_port_t *port) {
    uint32_t ssts = port->ssts;

    uint8_t ipm = (ssts >> 8) & 0x0f;
    uint8_t det = ssts & 0x0f;

    /* Check for correct interface and active state */
    if ((ipm != 0x01) || (det != 0x03))
        return AHCI_DEV_NULL;

    switch (port->sig) {
        case SATA_SIG_ATAPI:
            return AHCI_DEV_SATAPI;
        case SATA_SIG_SEMB:
            return AHCI_DEV_SEMB;
        case SATA_SIG_PM:
            return AHCI_DEV_PM;
        default:
            return AHCI_DEV_SATA;
    }

    return AHCI_DEV_NULL;
}

void init_ahci(void) {
    struct pci_device_t device = {0};

    uint8_t class_mass_storage = 0x01;
    uint8_t subclass_serial_ata = 0x06;
    int ret = pci_get_device(&device, class_mass_storage, subclass_serial_ata);
    if (!ret) kprint(KPRN_INFO, "ahci: Found AHCI controller");

    ahci_base = (size_t)pci_read_device(&device, 0x24);
    kprint(KPRN_INFO, "ahci: ABAR at %x", ahci_base);
    volatile struct hba_mem_t *mem = (volatile struct hba_mem_t *)(ahci_base + MEM_PHYS_OFFSET);

    for (size_t i = 0; i < 32; i++) {
        int ret = probe_port(mem, i);
        switch (ret) {
            case AHCI_DEV_SATA:
                kprint(KPRN_INFO, "ahci: Found sata device at port index %u", i);
                ret = ahci_init_ata(&mem->ports[i], i);
                if (ret == -1)
                    kprint(KPRN_WARN, "failed to initialise sata device at index %u", i);
                continue;
            case AHCI_DEV_SATAPI:
                ret = ahci_init_atapi(&mem->ports[i], i);
                continue;
            default:
                continue;
        }
    }
}

int probe_port(volatile struct hba_mem_t *mem, size_t portno) {
    uint32_t pi = mem->pi;

    if (pi & 1) {
        return check_type(&mem->ports[portno]);
    }

    return -1;
}

int ahci_init_ata(volatile struct hba_port_t *port, size_t portno) {
    port_rebase(port, portno);
    int ret = ahci_identify(port, 0xec);
    if (ret == -1)
      return -1;

    return 0;
}

int ahci_init_atapi(volatile struct hba_port_t *port, size_t portno) {
    port_rebase(port, portno);

    return 0;
}

/* Allocate space for command lists, tables etc for a given port */
void port_rebase(volatile struct hba_port_t *port, size_t portno) {
    /* allocate an area for the command list */
    port->clb = (uint32_t)(size_t)pmm_alloc(1);
    port->clbu = 0;

    /* Reserve some memory for the hba fis receive area and setup values */
    struct hba_fis_t *hba_fis = pmm_alloc(1);

    /* set fis types in fis receive area */
    hba_fis->dsfis.fis_type = FIS_TYPE_DMA_SETUP;
    hba_fis->psfis.fis_type = FIS_TYPE_PIO_SETUP;
    hba_fis->rfis.fis_type = FIS_TYPE_REG_D2H;
    hba_fis->sdbfis[0] = FIS_TYPE_DEV_BITS;

    /* Set the address that received FISes will be copied to */
    port->fb  = (uint32_t)(size_t)hba_fis;
    port->fbu = 0;

    volatile struct hba_cmd_hdr_t *cmd_hdr = (volatile struct hba_cmd_hdr_t *)((size_t)port->clb);

    for (size_t i = 0; i < 32; i++) {
        cmd_hdr[i].prdtl = 8;

        /* command table base addr = 40K + 8K * portno + header index * 256 */
        cmd_hdr[i].ctba = (uint32_t)(size_t)pmm_alloc(1);
        cmd_hdr[i].ctbau = 0;
    }

    return;
}

int ahci_identify(volatile struct hba_port_t *port, uint8_t cmd) {
    uint16_t *dest = pmm_alloc(1);
    kmemset(dest, 0, 512);

    int spin = 0;

    port->is = port->is;
    port->serr = port->serr;

    int slot = find_cmdslot(port);
    if (slot == -1) {
        kprint(KPRN_WARN, "ahci: failed to find command slot for identify command");
        return -1;
    }

    volatile struct hba_cmd_hdr_t *cmd_hdr = (volatile struct hba_cmd_hdr_t *)((size_t)port->clb);

    /* Setup command header */
    cmd_hdr += slot;
    cmd_hdr->cfl = sizeof(volatile struct fis_regh2d_t) / sizeof(uint32_t);
    cmd_hdr->w = 0;
    cmd_hdr->prdtl = 1;

    /* construct a command table and populate it */
    volatile struct hba_cmd_tbl_t *cmdtbl = (volatile struct hba_cmd_tbl_t *)(
            ((size_t)cmd_hdr->ctba));
    kmemset64((void *)((size_t)cmd_hdr->ctba + MEM_PHYS_OFFSET), 0,
            sizeof(volatile struct hba_cmd_tbl_t) / 8);

    cmdtbl->prdt_entry[0].dba = (uint32_t)(size_t)dest;
    cmdtbl->prdt_entry[0].dbc = 511;
    cmdtbl->prdt_entry[0].i = 1;

    struct fis_regh2d_t *cmdfis = (struct fis_regh2d_t *)(((size_t)cmdtbl->cfis));
    kmemset64((void *)(((size_t)(void *)cmdtbl->cfis)), 0, sizeof(struct fis_regh2d_t) / 8);

    cmdfis->command = cmd;
    cmdfis->c = 1;
    cmdfis->device = 0;
    cmdfis->pmport = 0;
    cmdfis->fis_type = FIS_TYPE_REG_H2D;

    while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
        spin++;
    }

    if (spin > 1000000) {
        kprint(KPRN_WARN, "ahci: Port hung");
    }

    start_cmd(port);
    port->ci = 1 << slot;

    for (;;) {
        if (!(port->ci & (1 << slot))) {
            break;
        }
        /* check for task file error */
        if (port->is & (1 << 30)) {
            kprint(KPRN_WARN, "ahci: Disk read error in ahci_identify()");
            return -1;
        }
    }

    if (port->is & (1 << 30)) {
        kprint(KPRN_WARN, "ahci: Disk read error in ahci_identify()");
        return -1;
    }

    stop_cmd(port);

    /* parse the identify data */
    char *serial = kalloc(20);
    size_t i = 0;

    for (size_t j = 10; j < 20; j++) {
        uint16_t d = dest[j];
        char a = (char)((uint8_t)(d >> 8));
        if (a != '\0') {
            serial[i++] = a;
        }

        char b  = (char)(uint8_t)d;
        if (b != '\0') {
            serial[i++] = a;
        }
    }

    char *model = kalloc(20);
    i = 0;

    for (size_t j = 27; j < 47; j++) {
        uint16_t d = dest[j];
        char a = (char)((uint8_t)(d >> 8));
        if (a != '\0') {
            model[i] = a;
            i++;
        }

        char b  = (char)(uint8_t)d;
        if (b != '\0') {
            model[i] = b;
            i++;
        }
    }

    kprint(KPRN_INFO, "ahci: Disk serial no. is %s", serial);
    kprint(KPRN_INFO, "ahci: Sector count = %u", *((uint64_t *)&dest[100]));
    kprint(KPRN_INFO, "ahci: Disk model is %s", model);
    kprint(KPRN_INFO, "ahci: Identify successful");

    return 0;
}

void start_cmd(volatile struct hba_port_t *port) {
    port->cmd &= ~HBA_PxCMD_ST;

    while (port->cmd & HBA_PxCMD_CR);

    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;

    return;
}

int find_cmdslot(volatile struct hba_port_t *port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if (!(slots & (1 << i))) {
            return i;
        }
    }

    return -1;
}

void stop_cmd(volatile struct hba_port_t *port) {
    /* Clear bit 0 */
    port->cmd &= ~HBA_PxCMD_ST;

    while (port->cmd & HBA_PxCMD_CR);

    /* Clear bit 4 */
    port->cmd &= ~HBA_PxCMD_FRE;

    return;
}

void kmemset64(void *ptr, uint64_t c, size_t count) {
    uint64_t *p = ptr;

    for (size_t i = 0; i < count; i++) {
        p[i] = c;
    }

    return;
}
