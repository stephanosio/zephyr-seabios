// Variable layouts of bios.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __BIOSVAR_H
#define __BIOSVAR_H

#include "types.h" // u8
#include "farptr.h" // GET_FARVAR
#include "config.h" // CONFIG_*


/****************************************************************
 * Bios Data Area (BDA)
 ****************************************************************/

struct ivec {
    u16 offset;
    u16 seg;
};

struct bios_data_area_s {
    // 00:00
    struct ivec ivecs[256];
    // 30:00
//    u8 stack[256];
    // 40:00
    u16 port_com[4];
    u16 port_lpt[3];
    u16 ebda_seg;
    // 40:10
    u16 equipment_list_flags;
    u8 pad1;
    u16 mem_size_kb;
    u8 pad2;
    u8 ps2_ctrl_flag;
    u8 kbd_flag0;
    u8 kbd_flag1;
    u8 alt_keypad;
    u16 kbd_buf_head;
    u16 kbd_buf_tail;
    // 40:1e
    u8 kbd_buf[32];
    u8 floppy_recalibration_status;
    u8 floppy_motor_status;
    // 40:40
    u8 floppy_motor_counter;
    u8 floppy_last_status;
    u8 floppy_return_status[7];
    u8 video_mode;
    u16 video_cols;
    u16 video_pagesize;
    u16 video_pagestart;
    // 40:50
    u16 cursor_pos[8];
    // 40:60
    u16 cursor_type;
    u8 video_page;
    u16 crtc_address;
    u8 video_msr;
    u8 video_pal;
    u32 jump_cs_ip;
    u8 other_6b;
    u32 timer_counter;
    // 40:70
    u8 timer_rollover;
    u8 break_flag;
    u16 soft_reset_flag;
    u8 disk_last_status;
    u8 disk_count;
    u8 disk_control_byte;
    u8 port_disk;
    u8 lpt_timeout[4];
    u8 com_timeout[4];
    // 40:80
    u16 kbd_buf_start_offset;
    u16 kbd_buf_end_offset;
    u8 video_rows;
    u16 char_height;
    u8 video_ctl;
    u8 video_switches;
    u8 modeset_ctl;
    u8 dcc_index;
    u8 floppy_last_data_rate;
    u8 disk_status_controller;
    u8 disk_error_controller;
    u8 disk_interrupt_flag;
    u8 floppy_harddisk_info;
    // 40:90
    u8 floppy_media_state[4];
    u8 floppy_track0;
    u8 floppy_track1;
    u8 kbd_mode;
    u8 kbd_led;
    u32 ptr_user_wait_complete_flag;
    u32 user_wait_timeout;
    // 40:A0
    u8 rtc_wait_flag;
} PACKED;

// BDA floppy_recalibration_status bitdefs
#define FRS_TIMEOUT (1<<7)

// BDA rtc_wait_flag bitdefs
#define RWS_WAIT_PENDING (1<<0)
#define RWS_WAIT_ELAPSED (1<<7)

// BDA floppy_media_state bitdefs
#define FMS_DRIVE_STATE_MASK        (0x07)
#define FMS_MEDIA_DRIVE_ESTABLISHED (1<<4)
#define FMS_DOUBLE_STEPPING         (1<<5)
#define FMS_DATA_RATE_MASK          (0xc0)

// Accessor functions
#define GET_BDA(var) \
    GET_FARVAR(SEG_BDA, ((struct bios_data_area_s *)0)->var)
#define SET_BDA(var, val) \
    SET_FARVAR(SEG_BDA, ((struct bios_data_area_s *)0)->var, (val))
#define CLEARBITS_BDA(var, val) do {                                    \
        typeof(((struct bios_data_area_s *)0)->var) __val = GET_BDA(var); \
        SET_BDA(var, (__val & ~(val)));                                 \
    } while (0)
#define SETBITS_BDA(var, val) do {                                      \
        typeof(((struct bios_data_area_s *)0)->var) __val = GET_BDA(var); \
        SET_BDA(var, (__val | (val)));                                  \
    } while (0)


/****************************************************************
 * Hard drive info
 ****************************************************************/

struct chs_s {
    u16 heads;      // # heads
    u16 cylinders;  // # cylinders
    u16 spt;        // # sectors / track
};

// DPTE definition
struct dpte_s {
    u16 iobase1;
    u16 iobase2;
    u8  prefix;
    u8  unused;
    u8  irq;
    u8  blkcount;
    u8  dma;
    u8  pio;
    u16 options;
    u16 reserved;
    u8  revision;
    u8  checksum;
};

struct ata_channel_s {
    u16 iobase1;      // IO Base 1
    u16 iobase2;      // IO Base 2
    u16 pci_bdf;
    u8  irq;          // IRQ
};

struct ata_device_s {
    u8  type;         // Detected type of ata (ata/atapi/none/unknown)
    u8  device;       // Detected type of attached devices (hd/cd/none)
    u8  removable;    // Removable device flag
    u8  lock;         // Locks for removable devices
    u8  mode;         // transfer mode : PIO 16/32 bits - IRQ - ISADMA - PCIDMA
    u16 blksize;      // block size

    u8  translation;  // type of translation
    struct chs_s  lchs;         // Logical CHS
    struct chs_s  pchs;         // Physical CHS

    u64 sectors;      // Total sectors count
};

struct ata_s {
    // ATA channels info
    struct ata_channel_s channels[CONFIG_MAX_ATA_INTERFACES];

    // ATA devices info
    struct ata_device_s  devices[CONFIG_MAX_ATA_DEVICES];
    //
    // map between bios hd/cd id and ata channels
    u8 hdcount, cdcount;
    u8 idmap[2][CONFIG_MAX_ATA_DEVICES];

    // Buffer for DPTE table
    struct dpte_s dpte;

    // Count of transferred sectors and bytes
    u16 trsfsectors;
};

// ElTorito Device Emulation data
struct cdemu_s {
    u8  active;
    u8  media;
    u8  emulated_drive;
    u8  controller_index;
    u16 device_spec;
    u32 ilba;
    u16 buffer_segment;
    u16 load_segment;
    u16 sector_count;

    // Virtual device
    struct chs_s  vdevice;
};


/****************************************************************
 * Initial Program Load (IPL)
 ****************************************************************/

struct ipl_entry_s {
    u16 type;
    u16 flags;
    u32 vector;
    u32 description;
};

struct ipl_s {
    struct ipl_entry_s table[8];
    u16 count;
    u16 sequence;
    u32 bootorder;
    u8 checkfloppysig;
};

#define IPL_TYPE_FLOPPY      0x01
#define IPL_TYPE_HARDDISK    0x02
#define IPL_TYPE_CDROM       0x03
#define IPL_TYPE_BEV         0x80


/****************************************************************
 * Extended Bios Data Area (EBDA)
 ****************************************************************/

struct fdpt_s {
    u16 cylinders;
    u8 heads;
    u8 a0h_signature;
    u8 phys_sectors;
    u16 precompensation;
    u8 reserved;
    u8 drive_control_byte;
    u16 phys_cylinders;
    u8 phys_heads;
    u16 landing_zone;
    u8 sectors;
    u8 checksum;
} PACKED;

struct extended_bios_data_area_s {
    u8 size;
    u8 reserved1[0x21];
    u32 far_call_pointer;
    u8 mouse_flag1;
    u8 mouse_flag2;
    u8 mouse_data[0x08];
    // 0x30
    u8 other1[0x0d];

    // 0x3d
    struct fdpt_s fdpt[2];

    // 0x5d
    u8 other2[0xC4];

    u8 ps2ctr;

    // Physical memory available.
    u32 ram_size;
    u16 e820_count;
    u32 e820_loc;
    u32 pir_loc;

    // ATA Driver data
    struct ata_s   ata;

    // El Torito Emulation data
    struct cdemu_s cdemu;

    // Initial program load
    struct ipl_s ipl;
} PACKED;

#define EBDA_SIZE DIV_ROUND_UP(sizeof(struct extended_bios_data_area_s), 1024)
#define BASE_MEM_IN_K (640 - EBDA_SIZE)

// Accessor functions
#define GET_EBDA(var) \
    GET_FARVAR(SEG_EBDA, ((struct extended_bios_data_area_s *)0)->var)
#define SET_EBDA(var, val) \
    SET_FARVAR(SEG_EBDA, ((struct extended_bios_data_area_s *)0)->var, (val))


/****************************************************************
 * Bios Config Table
 ****************************************************************/

struct bios_config_table_s {
    u16 size;
    u8 model;
    u8 submodel;
    u8 biosrev;
    u8 feature1, feature2, feature3, feature4, feature5;
} PACKED;

extern struct bios_config_table_s BIOS_CONFIG_TABLE;

#endif // __BIOSVAR_H
