/* The itelx kernel/userspace interface: system call numbers, error codes,
 * flags and the structures that cross the boundary. Shared by the kernel,
 * the userspace library and the tools; only fixed-width types.
 *
 * System calls: int 0x80, number in RAX, arguments in RDI, RSI, RDX, R10,
 * R8, R9; the result comes back in RAX, a negative error code on failure.
 */
#ifndef ITX_ABI_H
#define ITX_ABI_H

#include <stdint.h>

/* ---- system calls */
#define SYS_EXIT        0
#define SYS_WRITE       1     /* fd, buf, len */
#define SYS_READ        2     /* fd, buf, len */
#define SYS_OPEN        3     /* path, flags */
#define SYS_CLOSE       4     /* fd */
#define SYS_SEEK        5     /* fd, offset, whence */
#define SYS_STAT        6     /* path, struct itx_stat* */
#define SYS_FSTAT       7     /* fd, struct itx_stat* */
#define SYS_READDIR     8     /* fd, struct itx_dirent* -> 1 entry, 0 end */
#define SYS_MKDIR       9     /* path */
#define SYS_REMOVE      10    /* path (file or empty directory) */
#define SYS_CHDIR       11    /* path */
#define SYS_GETCWD      12    /* buf, len */
#define SYS_SPAWN       13    /* path, argv, argc, flags -> pid */
#define SYS_WAIT        14    /* pid (-1 any), int *status -> pid */
#define SYS_GETPID      15
#define SYS_SLEEP       16    /* milliseconds */
#define SYS_UPTIME      17    /* -> milliseconds since boot */
#define SYS_BRK         18    /* new end (0: query) -> current end */
#define SYS_KILL        19    /* pid */
#define SYS_PROCLIST    20    /* struct itx_procinfo*, max -> count */
#define SYS_SYSINFO     21    /* struct itx_sysinfo* */
#define SYS_TTY_MODE    22    /* TTY_MODE_* */
#define SYS_TTY_SETFG   23    /* pid that Ctrl+C stops (0: none) */
#define SYS_TIME        24    /* -> seconds since 1970 (RTC) */
#define SYS_REBOOT      25
#define SYS_POWEROFF    26
#define SYS_YIELD       27
#define SYS_RENAME      28    /* from, to */
#define SYS_KLOG        29    /* buf, len -> bytes of kernel log */
#define SYS_MOUNTS      30    /* buf, len -> text */
#define SYS_TRUNCATE    31    /* path, size */
#define SYS_SYNC        32
#define SYS_TTY_INFO    33    /* struct itx_ttyinfo* */
#define SYS_LASTERR     34    /* buf, len: why the last spawn failed */
#define SYS_GETPPID     35
#define SYS_TTY_KEY     36    /* raw mode: struct itx_key* (blocks) */
#define SYS_TTY_DIR     37    /* direction for the next input field (TTY_DIR_*) */
#define SYS_DEVLIST     38    /* struct itx_devinfo*, max -> count (USB devices) */
#define SYS_EJECT       39    /* path of a volume in /media: written out and unmounted */
#define SYS_XVRCHECK    40    /* path, buf, len: 0 when it is a valid XVR executable, else why in buf */
/* itelx 1.4: what the machine is and what it is doing (the sys commands) */
#define SYS_CPUSTAT     41    /* struct itx_cpustat* */
#define SYS_HWINFO      42    /* struct itx_hwinfo* */
#define SYS_PCILIST     43    /* struct itx_pciinfo*, max -> count */
#define SYS_DISKLIST    44    /* struct itx_diskinfo*, max -> count (disks and their partitions) */
#define SYS_FSINFO      45    /* path, struct itx_fsinfo*: the filesystem the path is on */
#define SYS_TTY_KEYWAIT 46    /* struct itx_key*, ms: raw mode; -EAGAIN when no key came in time */
#define SYS_COUNT       47

/* spawn flags */
#define SPAWN_FG        1     /* the new process gets Ctrl+C */

/* ---- error codes */
#define EPERM      1
#define ENOENT     2
#define ESRCH      3
#define EINTR      4
#define EIO        5
#define E2BIG      7
#define ENOEXEC    8
#define EBADF      9
#define ECHILD    10
#define EAGAIN    11
#define ENOMEM    12
#define EACCES    13
#define EFAULT    14
#define EBUSY     16
#define EEXIST    17
#define EXDEV     18
#define ENODEV    19
#define ENOTDIR   20
#define EISDIR    21
#define EINVAL    22
#define ENFILE    23
#define EMFILE    24
#define EFBIG     27
#define ENOSPC    28
#define ESPIPE    29
#define EROFS     30
#define ERANGE    34
#define ENAMETOOLONG 36
#define ENOSYS    38
#define ENOTEMPTY 39
#define ELOOP     40
#define ECORRUPT  117
#define ECANCELED 125

/* ---- files */
/* (host tools get the same values from the C library) */
#ifndef O_RDONLY
#define O_RDONLY  0x0000
#endif
#ifndef O_WRONLY
#define O_WRONLY  0x0001
#endif
#ifndef O_RDWR
#define O_RDWR    0x0002
#endif
#ifndef O_ACCMODE
#define O_ACCMODE 0x0003
#endif
#ifndef O_CREATE
#define O_CREATE  0x0040
#endif
#ifndef O_EXCL
#define O_EXCL    0x0080
#endif
#ifndef O_TRUNC
#define O_TRUNC   0x0200
#endif
#ifndef O_APPEND
#define O_APPEND  0x0400
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

#define VT_FILE 1
#define VT_DIR  2
#define VT_RAM  3             /* a RAM range in /ram */
#define VT_DEV  4             /* a device in /media (an empty file; its ino is the device id) */

#define ST_RDONLY 1           /* on a read-only filesystem */

#define ITX_NAME_MAX 255
#define ITX_PATH_MAX 512

struct itx_stat {
    uint64_t size;
    uint64_t ino;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t type;
    uint32_t flags;
    uint32_t links;
    uint32_t reserved;
    uint64_t phys;            /* VT_RAM: the physical address the file starts at */
};

struct itx_dirent {
    uint64_t ino;
    uint64_t size;
    uint32_t type;
    uint32_t name_len;
    char name[ITX_NAME_MAX + 1];
};

/* ---- processes and the system */
#define PS_READY   1
#define PS_RUNNING 2
#define PS_BLOCKED 3
#define PS_ZOMBIE  4

struct itx_procinfo {
    int32_t pid, ppid, state, kernel;
    uint64_t cpu_ms, mem_kib;
    char name[32];
};

struct itx_sysinfo {
    uint64_t total_kib, free_kib, heap_kib;
    uint64_t uptime_ms;
    uint32_t nprocs;
    uint32_t firmware;        /* 1 BIOS, 2 UEFI */
    uint32_t fb_width, fb_height;
    char cpu[48];
    char version[16];
    char root[48];            /* the device ITFS was mounted from */
};

/* processor time since start, measured with the time stamp counter at
 * every switch between processes (itelx 1.4) */
struct itx_cpustat {
    uint64_t now_us;          /* since the kernel started */
    uint64_t idle_us;         /* the processor had nothing to run */
    uint64_t user_us;         /* programs (user processes, their system calls included) */
    uint64_t kernel_us;       /* kernel threads (terminal input, USB, updates) */
    uint64_t interrupts;      /* device and timer interrupts */
    uint64_t syscalls;
    uint64_t switches;        /* switches from one process to another */
    uint64_t fb_bytes;        /* bytes the processor wrote to the screen */
    uint32_t tsc_mhz;
    uint32_t nprocs;
    uint32_t ready;           /* processes waiting for the processor */
    uint32_t blocked;         /* processes waiting for something else (a key, a disk, time) */
};

/* the machine (itelx 1.4). Text fields are NUL terminated, empty when the
 * machine does not say. Serial numbers and UUIDs are not taken over. */
#define HWF_SMBIOS  1         /* the firmware's SMBIOS tables were found */
#define HWF_ACPI    2         /* the ACPI tables were found */
#define HWF_FWINFO  4         /* the UEFI boot path handed over the firmware's own facts */

#define HW_MEMDEV_MAX 8

struct itx_memdev {           /* a memory module (SMBIOS type 17) */
    uint32_t size_mib;
    uint16_t speed_mts;       /* rated speed, MT/s (0 unknown) */
    uint16_t conf_mts;        /* configured speed */
    char type[12];            /* "DDR4", ... */
    char form[12];            /* "DIMM", "SODIMM", ... */
    char locator[24];
    char maker[24];
    char part[24];
};

struct itx_hwinfo {
    uint32_t size;            /* sizeof(struct itx_hwinfo) */
    uint32_t flags;           /* HWF_* */
    /* processor */
    char cpu_vendor[16];
    char cpu_brand[48];
    uint32_t cpu_family, cpu_model, cpu_stepping;
    uint32_t cpu_count;       /* logical processors in the machine (ACPI), 0 unknown */
    uint32_t cpu_used;        /* those itelx runs on */
    uint32_t cpu_cores;       /* cores of one processor package, 0 unknown */
    uint32_t cpu_threads;     /* threads of one package, 0 unknown */
    uint32_t cpu_max_mhz;     /* SMBIOS, 0 unknown */
    uint32_t cpu_cur_mhz;
    uint32_t tsc_mhz;         /* measured by the kernel at start */
    uint32_t cache_kib[4];    /* L1 data, L1 instruction, L2, L3, 0 unknown */
    char cpu_socket[24];
    char hypervisor[16];      /* the virtual machine's signature; empty on real hardware */
    char cpu_flags[192];      /* features, separated by spaces */
    /* memory */
    uint64_t ram_kib;         /* RAM in the firmware's memory map */
    uint64_t usable_kib;      /* of it, what the kernel manages */
    uint64_t firmware_kib;    /* of it, what the firmware keeps (ACPI, runtime, reserved RAM) */
    uint64_t installed_mib;   /* the memory modules together (SMBIOS), 0 unknown */
    uint32_t mem_slots;       /* SMBIOS, 0 unknown */
    uint32_t mem_max_mib;     /* the most memory the board takes, SMBIOS */
    uint32_t n_memdev;
    uint32_t pad0;
    struct itx_memdev memdev[HW_MEMDEV_MAX];
    /* computer, board and firmware */
    char sys_maker[40], sys_product[48], sys_version[32], sys_family[32];
    char board_maker[40], board_product[40], board_version[32];
    char chassis[20];
    char bios_vendor[40], bios_version[40], bios_date[16];
    uint32_t bios_rom_kib;
    uint16_t bios_major, bios_minor;     /* the firmware's release, SMBIOS 2.4 and later */
    uint16_t smbios_major, smbios_minor;
    uint32_t firmware;        /* 1 BIOS, 2 UEFI */
    uint32_t secure_boot;     /* 0 unknown, 1 off, 2 on, 3 setup mode, 4 not supported */
    char uefi_vendor[40];
    uint32_t uefi_fw_rev;     /* the firmware's own revision number */
    uint32_t uefi_spec_rev;   /* the UEFI version it implements: major << 16 | minor */
    char acpi_oem[8], acpi_oem_table[12];
    uint32_t acpi_rev;        /* RSDP revision: 0 ACPI 1.0, 2 ACPI 2.0 or later */
    char acpi_tables[128];    /* table signatures, separated by spaces */
    /* screen */
    uint32_t fb_width, fb_height, fb_bpp, fb_pitch;
    uint64_t fb_phys, fb_bytes;
    /* the kernel */
    char version[16];
    char build[16];           /* build date, YYYY-MM-DD */
    uint64_t kernel_kib;      /* the kernel image in memory */
};

struct itx_pciinfo {
    uint8_t bus, dev, fn, rev;
    uint8_t cls, sub, progif, irq;
    uint16_t vendor, device;
    uint16_t sub_vendor, sub_device;
    uint64_t mem_bytes;       /* its largest memory window (BAR) */
    char kind[24];            /* what the class code says it is */
    char driver[12];          /* the itelx driver that uses it; empty: none */
};

struct itx_diskinfo {
    char name[16];            /* ata0, ahci0, nvme0, usb1; partitions: ata0p1, ... */
    char model[41];
    uint8_t removable;
    uint8_t partno;           /* 0: a whole disk */
    uint8_t mbr_type;         /* partitions: MBR type (0x7F ITFS, 0xEF EFI), or the closest one for GPT */
    uint32_t sector_size;
    uint32_t usb_id;          /* 0: not on USB */
    uint64_t bytes;
    char use[48];             /* what it is to itelx: "/ (ITFS)", "/media/STICK", ... */
};

struct itx_fsinfo {
    uint64_t total_bytes;
    uint64_t free_bytes;      /* ~0: not known */
    uint32_t block_bytes;
    uint32_t flags;           /* ST_RDONLY */
    char fsname[16];
    char source[48];
    char mount[64];
};

/* ---- devices (USB) */
#define DEVK_DEVICE   0       /* one itelx has no driver for */
#define DEVK_KEYBOARD 1
#define DEVK_MOUSE    2
#define DEVK_HID      3       /* another input device */
#define DEVK_HUB      4
#define DEVK_STORAGE  5

struct itx_devinfo {
    uint32_t id;              /* the number in its /media names */
    uint32_t kind;            /* DEVK_* */
    uint16_t vendor, product;
    uint8_t  speed;           /* 1 full, 2 low, 3 high (USB 2), 4 super (USB 3), 5 super+ */
    uint8_t  cls, sub, proto; /* USB class of its first interface */
    char port[24];            /* "1-3.2": controller-root port.hub port */
    char name[48];            /* what the device calls itself */
    char media[64];           /* its names in /media */
};

/* ---- terminal */
#define TTY_MODE_COOKED 0     /* line editing; read() returns whole lines */
#define TTY_MODE_RAW    1     /* read() returns keys as they come, no echo */

#define TTY_DIR_AUTO 0
#define TTY_DIR_LTR  1
#define TTY_DIR_RTL  2

struct itx_ttyinfo {
    uint32_t cols, rows;
    uint32_t lang;            /* 0 English, 1 Arabic (Alt+Shift toggles) */
    uint32_t dir;             /* TTY_DIR_* of the input field */
};

/* raw keys */
#define KEY_UP     0x110001
#define KEY_DOWN   0x110002
#define KEY_LEFT   0x110003
#define KEY_RIGHT  0x110004
#define KEY_HOME   0x110005
#define KEY_END    0x110006
#define KEY_DELETE 0x110007
#define KEY_PGUP   0x110008
#define KEY_PGDN   0x110009
#define KEY_ESC    0x11000A
#define KEY_F1     0x110011     /* F1..F12 = KEY_F1 + n */

struct itx_key {
    uint32_t code;            /* a Unicode character, or KEY_* */
    uint32_t mods;            /* 1 shift, 2 ctrl, 4 alt */
};

#endif
