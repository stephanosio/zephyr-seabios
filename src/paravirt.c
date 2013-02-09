// Paravirtualization support.
//
// Copyright (C) 2009 Red Hat Inc.
//
// Authors:
//  Gleb Natapov <gnatapov@redhat.com>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h" // CONFIG_QEMU
#include "util.h" // dprintf
#include "byteorder.h" // be32_to_cpu
#include "ioport.h" // outw
#include "paravirt.h" // qemu_cfg_preinit
#include "smbios.h" // struct smbios_structure_header
#include "memmap.h" // add_e820
#include "cmos.h" // CMOS_*
#include "acpi.h" // acpi_setup
#include "mptable.h" // mptable_setup
#include "pci.h" // create_pirtable

struct e820_reservation {
    u64 address;
    u64 length;
    u32 type;
};

/* This CPUID returns the signature 'KVMKVMKVM' in ebx, ecx, and edx.  It
 * should be used to determine that a VM is running under KVM.
 */
#define KVM_CPUID_SIGNATURE     0x40000000

static void kvm_preinit(void)
{
    if (!CONFIG_QEMU)
        return;
    unsigned int eax, ebx, ecx, edx;
    char signature[13];

    cpuid(KVM_CPUID_SIGNATURE, &eax, &ebx, &ecx, &edx);
    memcpy(signature + 0, &ebx, 4);
    memcpy(signature + 4, &ecx, 4);
    memcpy(signature + 8, &edx, 4);
    signature[12] = 0;

    if (strcmp(signature, "KVMKVMKVM") == 0) {
        dprintf(1, "Running on KVM\n");
        PlatformRunningOn |= PF_KVM;
    }
}

void
qemu_ramsize_preinit(void)
{
    if (!CONFIG_QEMU)
        return;

    PlatformRunningOn = PF_QEMU;
    kvm_preinit();

    // On emulators, get memory size from nvram.
    u32 rs = ((inb_cmos(CMOS_MEM_EXTMEM2_LOW) << 16)
              | (inb_cmos(CMOS_MEM_EXTMEM2_HIGH) << 24));
    if (rs)
        rs += 16 * 1024 * 1024;
    else
        rs = (((inb_cmos(CMOS_MEM_EXTMEM_LOW) << 10)
               | (inb_cmos(CMOS_MEM_EXTMEM_HIGH) << 18))
              + 1 * 1024 * 1024);
    RamSize = rs;
    add_e820(0, rs, E820_RAM);

    // Check for memory over 4Gig
    u64 high = ((inb_cmos(CMOS_MEM_HIGHMEM_LOW) << 16)
                | ((u32)inb_cmos(CMOS_MEM_HIGHMEM_MID) << 24)
                | ((u64)inb_cmos(CMOS_MEM_HIGHMEM_HIGH) << 32));
    RamSizeOver4G = high;
    add_e820(0x100000000ull, high, E820_RAM);

    /* reserve 256KB BIOS area at the end of 4 GB */
    add_e820(0xfffc0000, 256*1024, E820_RESERVED);

    u32 count = qemu_cfg_e820_entries();
    if (count) {
        struct e820_reservation entry;
        int i;

        for (i = 0; i < count; i++) {
            qemu_cfg_e820_load_next(&entry);
            add_e820(entry.address, entry.length, entry.type);
        }
    } else if (runningOnKVM()) {
        // Backwards compatibility - provide hard coded range.
        // 4 pages before the bios, 3 pages for vmx tss pages, the
        // other page for EPT real mode pagetable
        add_e820(0xfffbc000, 4*4096, E820_RESERVED);
    }
}

void
qemu_biostable_setup(void)
{
    pirtable_setup();
    mptable_setup();
    smbios_setup();
    acpi_setup();
}


/****************************************************************
 * QEMU firmware config (fw_cfg) interface
 ****************************************************************/

int qemu_cfg_present;

#define QEMU_CFG_SIGNATURE              0x00
#define QEMU_CFG_ID                     0x01
#define QEMU_CFG_UUID                   0x02
#define QEMU_CFG_NUMA                   0x0d
#define QEMU_CFG_BOOT_MENU              0x0e
#define QEMU_CFG_MAX_CPUS               0x0f
#define QEMU_CFG_FILE_DIR               0x19
#define QEMU_CFG_ARCH_LOCAL             0x8000
#define QEMU_CFG_ACPI_TABLES            (QEMU_CFG_ARCH_LOCAL + 0)
#define QEMU_CFG_SMBIOS_ENTRIES         (QEMU_CFG_ARCH_LOCAL + 1)
#define QEMU_CFG_IRQ0_OVERRIDE          (QEMU_CFG_ARCH_LOCAL + 2)
#define QEMU_CFG_E820_TABLE             (QEMU_CFG_ARCH_LOCAL + 3)

static void
qemu_cfg_select(u16 f)
{
    outw(f, PORT_QEMU_CFG_CTL);
}

static void
qemu_cfg_read(void *buf, int len)
{
    insb(PORT_QEMU_CFG_DATA, buf, len);
}

static void
qemu_cfg_skip(int len)
{
    while (len--)
        inb(PORT_QEMU_CFG_DATA);
}

static void
qemu_cfg_read_entry(void *buf, int e, int len)
{
    qemu_cfg_select(e);
    qemu_cfg_read(buf, len);
}

void qemu_cfg_preinit(void)
{
    char *sig = "QEMU";
    int i;

    if (!CONFIG_QEMU)
        return;

    qemu_cfg_present = 1;

    qemu_cfg_select(QEMU_CFG_SIGNATURE);

    for (i = 0; i < 4; i++)
        if (inb(PORT_QEMU_CFG_DATA) != sig[i]) {
            qemu_cfg_present = 0;
            break;
        }
    dprintf(4, "qemu_cfg_present=%d\n", qemu_cfg_present);
}

void qemu_cfg_get_uuid(u8 *uuid)
{
    if (!qemu_cfg_present)
        return;

    qemu_cfg_read_entry(uuid, QEMU_CFG_UUID, 16);
}

int qemu_cfg_show_boot_menu(void)
{
    u16 v;
    if (!qemu_cfg_present)
        return 1;

    qemu_cfg_read_entry(&v, QEMU_CFG_BOOT_MENU, sizeof(v));

    return v;
}

int qemu_cfg_irq0_override(void)
{
    u8 v;

    if (!qemu_cfg_present)
        return 0;

    qemu_cfg_read_entry(&v, QEMU_CFG_IRQ0_OVERRIDE, sizeof(v));

    return v;
}

u16 qemu_cfg_smbios_entries(void)
{
    u16 cnt;

    if (!qemu_cfg_present)
        return 0;

    qemu_cfg_read_entry(&cnt, QEMU_CFG_SMBIOS_ENTRIES, sizeof(cnt));

    return cnt;
}

u32 qemu_cfg_e820_entries(void)
{
    u32 cnt;

    if (!qemu_cfg_present)
        return 0;

    qemu_cfg_read_entry(&cnt, QEMU_CFG_E820_TABLE, sizeof(cnt));
    return cnt;
}

void* qemu_cfg_e820_load_next(void *addr)
{
    qemu_cfg_read(addr, sizeof(struct e820_reservation));
    return addr;
}

struct smbios_header {
    u16 length;
    u8 type;
} PACKED;

struct smbios_field {
    struct smbios_header header;
    u8 type;
    u16 offset;
    u8 data[];
} PACKED;

struct smbios_table {
    struct smbios_header header;
    u8 data[];
} PACKED;

#define SMBIOS_FIELD_ENTRY 0
#define SMBIOS_TABLE_ENTRY 1

size_t qemu_cfg_smbios_load_field(int type, size_t offset, void *addr)
{
    int i;

    for (i = qemu_cfg_smbios_entries(); i > 0; i--) {
        struct smbios_field field;

        qemu_cfg_read((u8 *)&field, sizeof(struct smbios_header));
        field.header.length -= sizeof(struct smbios_header);

        if (field.header.type != SMBIOS_FIELD_ENTRY) {
            qemu_cfg_skip(field.header.length);
            continue;
        }

        qemu_cfg_read((u8 *)&field.type,
                      sizeof(field) - sizeof(struct smbios_header));
        field.header.length -= sizeof(field) - sizeof(struct smbios_header);

        if (field.type != type || field.offset != offset) {
            qemu_cfg_skip(field.header.length);
            continue;
        }

        qemu_cfg_read(addr, field.header.length);
        return (size_t)field.header.length;
    }
    return 0;
}

int qemu_cfg_smbios_load_external(int type, char **p, unsigned *nr_structs,
                                  unsigned *max_struct_size, char *end)
{
    static u64 used_bitmap[4] = { 0 };
    char *start = *p;
    int i;

    /* Check if we've already reported these tables */
    if (used_bitmap[(type >> 6) & 0x3] & (1ULL << (type & 0x3f)))
        return 1;

    /* Don't introduce spurious end markers */
    if (type == 127)
        return 0;

    for (i = qemu_cfg_smbios_entries(); i > 0; i--) {
        struct smbios_table table;
        struct smbios_structure_header *header = (void *)*p;
        int string;

        qemu_cfg_read((u8 *)&table, sizeof(struct smbios_header));
        table.header.length -= sizeof(struct smbios_header);

        if (table.header.type != SMBIOS_TABLE_ENTRY) {
            qemu_cfg_skip(table.header.length);
            continue;
        }

        if (end - *p < sizeof(struct smbios_structure_header)) {
            warn_noalloc();
            break;
        }

        qemu_cfg_read((u8 *)*p, sizeof(struct smbios_structure_header));
        table.header.length -= sizeof(struct smbios_structure_header);

        if (header->type != type) {
            qemu_cfg_skip(table.header.length);
            continue;
        }

        *p += sizeof(struct smbios_structure_header);

        /* Entries end with a double NULL char, if there's a string at
         * the end (length is greater than formatted length), the string
         * terminator provides the first NULL. */
        string = header->length < table.header.length +
                 sizeof(struct smbios_structure_header);

        /* Read the rest and terminate the entry */
        if (end - *p < table.header.length) {
            warn_noalloc();
            *p -= sizeof(struct smbios_structure_header);
            continue;
        }
        qemu_cfg_read((u8 *)*p, table.header.length);
        *p += table.header.length;
        *((u8*)*p) = 0;
        (*p)++;
        if (!string) {
            *((u8*)*p) = 0;
            (*p)++;
        }

        (*nr_structs)++;
        if (*p - (char *)header > *max_struct_size)
            *max_struct_size = *p - (char *)header;
    }

    if (start != *p) {
        /* Mark that we've reported on this type */
        used_bitmap[(type >> 6) & 0x3] |= (1ULL << (type & 0x3f));
        return 1;
    }

    return 0;
}

int qemu_cfg_get_numa_nodes(void)
{
    u64 cnt;

    qemu_cfg_read_entry(&cnt, QEMU_CFG_NUMA, sizeof(cnt));

    return (int)cnt;
}

void qemu_cfg_get_numa_data(u64 *data, int n)
{
    int i;

    for (i = 0; i < n; i++)
        qemu_cfg_read((u8*)(data + i), sizeof(u64));
}

u16 qemu_cfg_get_max_cpus(void)
{
    u16 cnt;

    if (!qemu_cfg_present)
        return 0;

    qemu_cfg_read_entry(&cnt, QEMU_CFG_MAX_CPUS, sizeof(cnt));

    return cnt;
}

static int
qemu_cfg_read_file(struct romfile_s *file, void *dst, u32 maxlen)
{
    if (file->size > maxlen)
        return -1;
    qemu_cfg_select(file->id);
    qemu_cfg_skip(file->rawsize);
    qemu_cfg_read(dst, file->size);
    return file->size;
}

static void
qemu_romfile_add(char *name, int select, int skip, int size)
{
    struct romfile_s *file = malloc_tmp(sizeof(*file));
    if (!file) {
        warn_noalloc();
        return;
    }
    memset(file, 0, sizeof(*file));
    strtcpy(file->name, name, sizeof(file->name));
    file->id = select;
    file->rawsize = skip; // Use rawsize to indicate skip length.
    file->size = size;
    file->copy = qemu_cfg_read_file;
    romfile_add(file);
}

// Populate romfile entries for legacy fw_cfg ports (that predate the
// "file" interface).
static void
qemu_cfg_legacy(void)
{
    // ACPI tables
    char name[128];
    u16 cnt;
    qemu_cfg_read_entry(&cnt, QEMU_CFG_ACPI_TABLES, sizeof(cnt));
    int i, offset = sizeof(cnt);
    for (i = 0; i < cnt; i++) {
        u16 len;
        qemu_cfg_read(&len, sizeof(len));
        offset += sizeof(len);
        snprintf(name, sizeof(name), "acpi/table%d", i);
        qemu_romfile_add(name, QEMU_CFG_ACPI_TABLES, offset, len);
        qemu_cfg_skip(len);
        offset += len;
    }
}

struct QemuCfgFile {
    u32  size;        /* file size */
    u16  select;      /* write this to 0x510 to read it */
    u16  reserved;
    char name[56];
};

void qemu_romfile_init(void)
{
    if (!CONFIG_QEMU || !qemu_cfg_present)
        return;

    // Populate romfiles for legacy fw_cfg entries
    qemu_cfg_legacy();

    // Load files found in the fw_cfg file directory
    u32 count;
    qemu_cfg_read_entry(&count, QEMU_CFG_FILE_DIR, sizeof(count));
    count = be32_to_cpu(count);
    u32 e;
    for (e = 0; e < count; e++) {
        struct QemuCfgFile qfile;
        qemu_cfg_read(&qfile, sizeof(qfile));
        qemu_romfile_add(qfile.name, be16_to_cpu(qfile.select)
                         , 0, be32_to_cpu(qfile.size));
    }
}
