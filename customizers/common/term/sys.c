/* sys: what the system is, what it is doing, and what it runs on (itelx 1.4).
 *
 *   sys              the system: version, how it started, its disks, memory,
 *                    processor, screen, owner, programs
 *   sys.info         what the processor, the memory and the graphics are
 *                    used for, measured over one second
 *   sys.info.device  the hardware: processor, memory modules, computer and
 *                    board, firmware, graphics, disks, USB and PCI devices
 *   sys.all          all three
 *   sys.top          every process and what it uses, live (/system/top.xvr)
 *
 * Everything shown is read from the kernel when the command runs: CPUID,
 * the firmware's memory map, SMBIOS and ACPI tables, PCI configuration
 * space, the time stamp counter. Nothing is assumed: what the machine does
 * not say is shown as "-". Serial numbers are never shown.
 *
 * On a kernel older than 1.4 (an update that is not finished yet) the
 * commands show what the old kernel can tell and say what is missing.
 */
#include <itx.h>
#include "sys.h"

#define CYAN   "\x1b[96m"
#define YELLOW "\x1b[93m"
#define GREEN  "\x1b[92m"
#define RED    "\x1b[91m"
#define DIM    "\x1b[90m"
#define RESET  "\x1b[0m"

/* ------------------------------------------------------------ helpers */

static void head(const char *title) { printf(CYAN "%s" RESET "\n", title); }

static void row(const char *label, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void row(const char *label, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("  %-12s %s\n", label, buf);
}

static const char *or_dash(const char *s) { return s && s[0] ? s : "-"; }

/* a long list of words, broken between words to the terminal's width */
static void row_words(const char *label, const char *text) {
    struct itx_ttyinfo ti;
    int width = tty_info(&ti) == 0 && ti.cols > 40 ? (int)ti.cols - 16 : 64;
    char line[512];
    const char *p = text;
    bool first = true;
    while (*p) {
        int n = (int)strlen(p);
        if (n > width) {
            n = width;
            while (n > 0 && p[n] != ' ') n--;
            if (!n) n = width;                    /* one word longer than the line */
        }
        if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
        memcpy(line, p, (size_t)n);
        line[n] = 0;
        row(first ? label : "", "%s", line);
        first = false;
        p += n;
        while (*p == ' ') p++;
    }
    if (first) row(label, "-");
}

/* a copy without the spaces firmware pads its names with */
static const char *trim(const char *s, char *out, size_t len) {
    while (*s == ' ') s++;
    strlcpy(out, s, len);
    size_t l = strlen(out);
    while (l && out[l - 1] == ' ') out[--l] = 0;
    return out;
}

static const char *fs_name(const char *fs, char *out, size_t len) {
    if (!strncmp(fs, "fat", 3)) snprintf(out, len, "FAT%s", fs + 3);
    else if (!strcmp(fs, "itfs")) strlcpy(out, "ITFS", len);
    else strlcpy(out, fs, len);
    return out;
}

/* 512 MiB, 1.5 GiB, 64 KiB */
static const char *size_str(uint64_t bytes, char *out, size_t len) {
    if (bytes >= (1ULL << 30)) {
        uint64_t t = bytes * 10 / (1ULL << 30);
        if (t % 10 && t < 1000) snprintf(out, len, "%lu.%lu GiB", (unsigned long)(t / 10), (unsigned long)(t % 10));
        else snprintf(out, len, "%lu GiB", (unsigned long)(t / 10));
    } else if (bytes >= (1ULL << 20)) {
        uint64_t t = bytes * 10 / (1ULL << 20);
        if (t % 10 && t < 1000) snprintf(out, len, "%lu.%lu MiB", (unsigned long)(t / 10), (unsigned long)(t % 10));
        else snprintf(out, len, "%lu MiB", (unsigned long)(t / 10));
    } else if (bytes >= 1024) {
        snprintf(out, len, "%lu KiB", (unsigned long)(bytes >> 10));
    } else {
        snprintf(out, len, "%lu B", (unsigned long)bytes);
    }
    return out;
}

/* tenths of a percent */
static unsigned long pct10(uint64_t part, uint64_t whole) {
    if (!whole) return 0;
    uint64_t p = part * 1000 / whole;
    return (unsigned long)(p > 1000 ? 1000 : p);
}

static const char *bar(char *out, int width, unsigned long p10) {
    int fill = (int)((p10 * (unsigned long)width + 500) / 1000);
    if (fill > width) fill = width;
    int o = 0;
    out[o++] = '[';
    const char *c = p10 >= 800 ? RED : p10 >= 500 ? YELLOW : GREEN;
    o += snprintf(out + o, 16, "%s", c);
    for (int i = 0; i < width; i++) {
        if (i == fill) o += snprintf(out + o, 16, "%s", DIM);
        out[o++] = i < fill ? '#' : '.';
    }
    o += snprintf(out + o, 16, "%s]", RESET);
    out[o] = 0;
    return out;
}

static void civil(long days, int *y, unsigned *m, unsigned *d) {
    days += 719468;
    long era = (days >= 0 ? days : days - 146096) / 146097;
    unsigned doe = (unsigned)(days - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)((long)yoe + era * 400 + (*m <= 2));
}

static void duration(uint64_t s, char *out, size_t len) {
    if (s >= 86400) snprintf(out, len, "%lu d %lu h %02lu min", (unsigned long)(s / 86400), (unsigned long)(s / 3600 % 24),
                             (unsigned long)(s / 60 % 60));
    else snprintf(out, len, "%lu h %02lu min %02lu s", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                  (unsigned long)(s % 60));
}

static const char *vendor_name(uint16_t v) {
    switch (v) {
    case 0x8086: case 0x8087: return "Intel";
    case 0x1022: return "AMD";
    case 0x1002: return "AMD (ATI)";
    case 0x10DE: return "NVIDIA";
    case 0x1234: return "QEMU / Bochs";
    case 0x1B36: return "Red Hat (QEMU)";
    case 0x1AF4: return "Red Hat (virtio)";
    case 0x15AD: return "VMware";
    case 0x80EE: return "VirtualBox";
    case 0x1414: return "Microsoft";
    case 0x10EC: return "Realtek";
    case 0x14E4: return "Broadcom";
    case 0x1000: return "Broadcom (LSI)";
    case 0x168C: return "Qualcomm Atheros";
    case 0x17CB: return "Qualcomm";
    case 0x1B21: return "ASMedia";
    case 0x1106: return "VIA";
    case 0x1039: return "SiS";
    case 0x1180: return "Ricoh";
    case 0x104C: return "Texas Instruments";
    case 0x1217: return "O2 Micro";
    case 0x144D: return "Samsung";
    case 0x1C5C: return "SK hynix";
    case 0x15B7: return "SanDisk (WD)";
    case 0x1987: return "Phison";
    case 0x126F: return "Silicon Motion";
    case 0x1E0F: return "KIOXIA";
    case 0x1179: return "Toshiba";
    case 0x2646: return "Kingston";
    case 0x1CC1: return "ADATA";
    case 0x1344: return "Micron";
    case 0x197B: return "JMicron";
    case 0x1912: return "Renesas";
    case 0x1033: return "NEC";
    case 0x5333: return "S3";
    case 0x102B: return "Matrox";
    case 0x1013: return "Cirrus Logic";
    case 0x18D1: return "Google";
    case 0x106B: return "Apple";
    case 0x1D17: return "Zhaoxin";
    case 0x1E49: return "YMTC";
    case 0x1D6A: return "Aquantia";
    }
    return "";
}

/* the graphics devices virtual machines show; real ones are named by maker */
static const char *gpu_name(uint16_t v, uint16_t d) {
    if (v == 0x1234 && d == 0x1111) return "standard VGA";
    if (v == 0x1B36 && d == 0x0100) return "QXL";
    if (v == 0x1AF4 && d == 0x1050) return "GPU";
    if (v == 0x15AD && d == 0x0405) return "SVGA II";
    if (v == 0x80EE && d == 0xBEEF) return "graphics adapter";
    if (v == 0x1013 && d == 0x00B8) return "GD 5446";
    if (v == 0x1414 && d == 0x5353) return "Hyper-V video";
    return "";
}

static const char *secure_boot_name(uint32_t s) {
    switch (s) {
    case 1: return "off";
    case 2: return "on";
    case 3: return "off (setup mode: no keys enrolled)";
    case 4: return "not supported by this firmware";
    }
    return "-";
}

/* what the running kernel can tell (cached) */
static struct itx_sysinfo g_si;
static struct itx_hwinfo g_hw;
static bool g_have_hw;

static void load(void) {
    sysinfo(&g_si);
    g_have_hw = hwinfo(&g_hw) == 0 && g_hw.size == sizeof g_hw;
}

static void old_kernel_note(void) {
    printf(YELLOW "  The kernel is %s: these programs are itelx 1.4, the kernel was not updated yet.\n"
           "  Start the computer once from the update stick to finish the update (see the README on it)." RESET "\n",
           g_si.version);
}

static int count_ext(const char *dir, const char *ext) {
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return 0;
    struct itx_dirent de;
    int n = 0;
    while (readdir(fd, &de) == 1) if (de.type == VT_FILE && ends_with(de.name, ext)) n++;
    close(fd);
    return n;
}

/* "WHAT: FS, size, free" (fs left out when WHAT names it) */
static void fs_line(const char *label, const char *path, const char *what, bool name_fs) {
    struct itx_fsinfo fi;
    if (fsinfo(path, &fi)) { row(label, "%s", what); return; }
    char a[24], b[24], n[24];
    char fs[32] = "";
    if (name_fs) snprintf(fs, sizeof fs, "%s, ", fs_name(fi.fsname, n, sizeof n));
    if (fi.free_bytes == ~0ULL)
        row(label, "%s: %s%s", what, fs, size_str(fi.total_bytes, a, sizeof a));
    else
        row(label, "%s: %s%s, %s free%s", what, fs, size_str(fi.total_bytes, a, sizeof a),
            size_str(fi.free_bytes, b, sizeof b), fi.flags & ST_RDONLY ? " (read only to programs)" : "");
}

/* ----------------------------------------------------------------- sys */

static void overview(void) {
    head("sys - the system");
    if (g_have_hw)
        row("system", "%s, kernel %s built %s, x86-64 (64-bit)", ITELX_USERSPACE, g_hw.version, g_hw.build);
    else
        row("system", "%s programs on the %s kernel, x86-64 (64-bit)", ITELX_USERSPACE, g_si.version);
    if (g_si.firmware == 2)
        row("start", "UEFI: IFelxBoot started the kernel from the EFI partition (\\boot\\vmlinuz)");
    else if (g_si.firmware == 1)
        row("start", "BIOS: the itelx MBR and stage2 started the kernel from the EFI partition (\\boot\\vmlinuz)");
    char t[48];
    duration(g_si.uptime_ms / 1000, t, sizeof t);
    row("running for", "%s", t);
    long now = unix_time();
    if (now > 0) {
        int y;
        unsigned m, d;
        civil(now / 86400, &y, &m, &d);
        long s = now % 86400;
        row("clock", "%04d-%02u-%02u %02ld:%02ld:%02ld (the computer's clock)", y, m, d, s / 3600, s / 60 % 60, s % 60);
    }
    size_t olen = 0;
    char *owner = read_file("/system/owner", &olen, 256);
    if (owner) {
        while (olen && (owner[olen - 1] == '\n' || owner[olen - 1] == '\r' || owner[olen - 1] == ' ')) owner[--olen] = 0;
        row("owner", "%s", or_dash(owner));
        free(owner);
    } else {
        row("owner", "- (no /system/owner: itelx was not installed with ODE)");
    }

    char what[80];
    fs_line("system disk", "/", or_dash(g_si.root), false);
    if (g_have_hw) {
        struct itx_diskinfo dk[32];
        int n = disklist(dk, 32);
        for (int i = 0; i < n; i++) {
            if (!strstr(dk[i].use, "(boot files)")) continue;
            char path[64];
            strlcpy(path, dk[i].use, sizeof path);
            char *sp = strchr(path, ' ');
            if (sp) *sp = 0;
            snprintf(what, sizeof what, "%s on %s", path, dk[i].name);
            fs_line("boot files", path, what, true);
        }
    }

    char a[24], b[24];
    row("memory", "%s for the system, %s free", size_str(g_si.total_kib * 1024, a, sizeof a),
        size_str(g_si.free_kib * 1024, b, sizeof b));
    if (g_have_hw && g_hw.cpu_count > 1)
        row("processor", "%s (itelx runs on 1 of %u)", or_dash(g_si.cpu), g_hw.cpu_count);
    else
        row("processor", "%s", or_dash(g_si.cpu));
    struct itx_ttyinfo ti;
    tty_info(&ti);
    row("screen", "%u x %u pixels, %u x %u characters", g_si.fb_width, g_si.fb_height, ti.cols, ti.rows);
    row("keyboard", "%s (Alt+Shift switches between English and Arabic)", ti.lang ? "Arabic" : "English");

    struct itx_procinfo p[80];
    int np = proclist(p, 80), user = 0, kern = 0;
    for (int i = 0; i < np; i++) {
        if (p[i].pid == 0 || p[i].state == PS_ZOMBIE) continue;
        if (p[i].kernel) kern++; else user++;
    }
    row("processes", "%d: %d program(s), %d kernel thread(s)", user + kern, user, kern);
    row("programs", "/dex: %d executable(s) (.xvr), %d XVC program(s) (.xvc)", count_ext("/dex", ".xvr"),
        count_ext("/dex", ".xvc"));
    struct itx_stat st;
    row("startup", "%s", stat("/system/startup.xvc", &st) == 0 ? "/system/startup.xvc runs at every start" : "none (/system/startup.xvc)");
    row("licences", "kernel: itelx Kernel License 1.0; userspace: MPL-2.0 (/system/licenses)");
    if (!g_have_hw) old_kernel_note();
}

void sys_overview(void) { load(); overview(); }

/* ------------------------------------------------------------ sys.info */

struct sample {
    uint64_t ms;
    struct itx_cpustat cs;
    bool have_cs;
    struct itx_procinfo p[80];
    int np;
};

static void take(struct sample *s) {
    s->have_cs = cpustat(&s->cs) == 0;
    s->ms = s->have_cs ? s->cs.now_us / 1000 : (uint64_t)uptime_ms();
    s->np = proclist(s->p, 80);
    if (s->np < 0) s->np = 0;
}

static uint64_t busy_ms_of(const struct sample *s) {
    uint64_t t = 0;
    for (int i = 0; i < s->np; i++) if (s->p[i].pid != 0) t += s->p[i].cpu_ms;
    return t;
}

static int first_display(struct itx_pciinfo *out) {
    struct itx_pciinfo pc[64];
    int n = pcilist(pc, 64);
    for (int i = 0; i < n; i++) if (pc[i].cls == 0x03) { *out = pc[i]; return 1; }
    return 0;
}

static void usage(void) {
    head("sys.info - processor, memory and graphics use, measured over one second");
    flush();
    struct sample *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    if (!a || !b) { free(a); free(b); printf("sys.info: out of memory\n"); return; }
    take(a);
    sleep_ms(1000);
    take(b);
    uint64_t span = b->ms > a->ms ? b->ms - a->ms : 1;
    char br[96], x[24], y[24];

    /* processor */
    if (a->have_cs && b->have_cs) {
        uint64_t total = b->cs.now_us - a->cs.now_us;
        uint64_t idle = b->cs.idle_us - a->cs.idle_us;
        uint64_t user = b->cs.user_us - a->cs.user_us;
        uint64_t kern = b->cs.kernel_us - a->cs.kernel_us;
        unsigned long busy = 1000 - pct10(idle, total);
        unsigned long pu = pct10(user, total), pk = pct10(kern, total);
        row("processor", "%s %lu.%lu %%  %s", bar(br, 30, busy), busy / 10, busy % 10, or_dash(g_si.cpu));
        row("", "programs %lu.%lu %%, kernel %lu.%lu %%, idle %lu.%lu %%", pu / 10, pu % 10, pk / 10, pk % 10,
            (1000 - busy) / 10, (1000 - busy) % 10);
        uint64_t s = total ? total : 1;
        row("", "%u process(es): %u waiting for the processor, %u waiting for input, a disk or time",
            b->cs.nprocs, b->cs.ready, b->cs.blocked);
        row("", "%lu interrupts/s, %lu system calls/s, %lu switches between processes/s",
            (unsigned long)((b->cs.interrupts - a->cs.interrupts) * 1000000 / s),
            (unsigned long)((b->cs.syscalls - a->cs.syscalls) * 1000000 / s),
            (unsigned long)((b->cs.switches - a->cs.switches) * 1000000 / s));
        if (g_have_hw && g_hw.cpu_count == 2)
            row("", "itelx runs on one processor; the other one stays unused");
        else if (g_have_hw && g_hw.cpu_count > 2)
            row("", "itelx runs on one processor; the other %u stay unused", g_hw.cpu_count - 1);
    } else {
        uint64_t d = busy_ms_of(b) - busy_ms_of(a);
        unsigned long busy = pct10(d, span);
        row("processor", "%s %lu.%lu %%  %s", bar(br, 30, busy), busy / 10, busy % 10, or_dash(g_si.cpu));
        row("", "(measured from the processes' time; the 1.3 kernel does not count idle time)");
    }

    /* memory */
    struct itx_sysinfo si;
    sysinfo(&si);
    uint64_t used = si.total_kib - si.free_kib, progs = 0;
    for (int i = 0; i < b->np; i++) if (!b->p[i].kernel && b->p[i].state != PS_ZOMBIE) progs += b->p[i].mem_kib;
    unsigned long mp = pct10(used, si.total_kib);
    row("memory", "%s %s of %s used (%lu.%lu %%)", bar(br, 30, mp), size_str(used * 1024, x, sizeof x),
        size_str(si.total_kib * 1024, y, sizeof y), mp / 10, mp % 10);
    char h[24], k[24], f[24];
    uint64_t other = used > progs + si.heap_kib ? used - progs - si.heap_kib : 0;
    row("", "programs %s, kernel heap %s, kernel, page tables and buffers %s; %s free",
        size_str(progs * 1024, x, sizeof x), size_str(si.heap_kib * 1024, h, sizeof h),
        size_str(other * 1024, k, sizeof k), size_str(si.free_kib * 1024, f, sizeof f));

    /* graphics */
    struct itx_pciinfo g;
    if (first_display(&g)) {
        const char *n = gpu_name(g.vendor, g.device);
        row("graphics", "%s%s%s [%04x:%04x]", vendor_name(g.vendor), n[0] ? " " : "", n, g.vendor, g.device);
    } else {
        row("graphics", "%s", g_have_hw ? "no display controller on the PCI bus" : "- (needs the 1.4 kernel)");
    }
    row("", "graphics processor: 0 %% - itelx has no driver for it; the processor draws the screen");
    if (a->have_cs && b->have_cs) {
        uint64_t s = b->cs.now_us - a->cs.now_us;
        uint64_t bytes = (b->cs.fb_bytes - a->cs.fb_bytes) * 1000000 / (s ? s : 1);
        row("", "screen %ux%u, %u bits: %s/s drawn by the processor", si.fb_width, si.fb_height,
            g_have_hw ? g_hw.fb_bpp : 32, size_str(bytes, x, sizeof x));
    } else {
        row("", "screen %ux%u (the firmware's framebuffer)", si.fb_width, si.fb_height);
    }
    if (!g_have_hw) old_kernel_note();
    free(a);
    free(b);
}

void sys_usage(void) { load(); usage(); }

/* ----------------------------------------------------- sys.info.device */

static void device(void) {
    head("sys.info.device - the hardware");
    if (!g_have_hw) {
        row("processor", "%s", or_dash(g_si.cpu));
        char a[24];
        row("memory", "%s for the system", size_str(g_si.total_kib * 1024, a, sizeof a));
        row("firmware", "%s", g_si.firmware == 2 ? "UEFI" : g_si.firmware == 1 ? "BIOS" : "-");
        row("screen", "%u x %u", g_si.fb_width, g_si.fb_height);
        printf("  The board, the firmware's details, the memory modules, the graphics and PCI devices\n"
               "  are read by the 1.4 kernel.\n");
        old_kernel_note();
        return;
    }
    char a[24], b[24], c[24], d[24];
    const struct itx_hwinfo *h = &g_hw;

    printf(CYAN "processor" RESET "\n");
    row("name", "%s", or_dash(h->cpu_brand));
    row("maker", "%s, family %u, model %u, stepping %u", or_dash(h->cpu_vendor), h->cpu_family, h->cpu_model,
        h->cpu_stepping);
    char cores[96] = "";
    if (h->cpu_cores && h->cpu_threads) snprintf(cores, sizeof cores, " (%u cores, %u threads in a package)", h->cpu_cores, h->cpu_threads);
    if (h->cpu_count) row("processors", "%u in the machine%s; itelx runs on %u", h->cpu_count, cores, h->cpu_used);
    else row("processors", "-%s; itelx runs on %u", cores, h->cpu_used);
    char spd[96];
    int o = snprintf(spd, sizeof spd, "%u MHz (time stamp counter, measured)", h->tsc_mhz);
    if (h->cpu_max_mhz) snprintf(spd + o, sizeof spd - o, ", up to %u MHz (firmware)", h->cpu_max_mhz);
    row("speed", "%s", spd);
    if (h->cache_kib[0] || h->cache_kib[2])
        row("cache", "L1 %s data + %s instructions, L2 %s, L3 %s",
            size_str((uint64_t)h->cache_kib[0] * 1024, a, sizeof a), size_str((uint64_t)h->cache_kib[1] * 1024, b, sizeof b),
            size_str((uint64_t)h->cache_kib[2] * 1024, c, sizeof c),
            h->cache_kib[3] ? size_str((uint64_t)h->cache_kib[3] * 1024, d, sizeof d) : "-");
    if (h->cpu_socket[0]) row("socket", "%s", h->cpu_socket);
    row_words("features", h->cpu_flags);
    if (h->hypervisor[0]) row("machine", "a virtual machine (%s)", h->hypervisor);

    printf(CYAN "memory" RESET "\n");
    if (h->installed_mib) {
        char slots[64] = "";
        if (h->mem_slots) snprintf(slots, sizeof slots, " in %u of %u slot(s)", h->n_memdev, h->mem_slots);
        char max[48] = "";
        if (h->mem_max_mib) snprintf(max, sizeof max, "; the board takes up to %s", size_str((uint64_t)h->mem_max_mib << 20, c, sizeof c));
        row("installed", "%s%s%s", size_str(h->installed_mib << 20, a, sizeof a), slots, max);
    } else {
        row("installed", "- (the firmware does not list the modules)");
    }
    for (uint32_t i = 0; i < h->n_memdev && i < HW_MEMDEV_MAX; i++) {
        const struct itx_memdev *m = &h->memdev[i];
        char sp[48] = "";
        if (m->speed_mts && m->conf_mts && m->conf_mts != m->speed_mts)
            snprintf(sp, sizeof sp, " %u MT/s (running at %u)", m->speed_mts, m->conf_mts);
        else if (m->speed_mts) snprintf(sp, sizeof sp, " %u MT/s", m->speed_mts);
        char who[64] = "";
        if (m->maker[0] || m->part[0]) snprintf(who, sizeof who, ", %s%s%s", m->maker, m->maker[0] && m->part[0] ? " " : "", m->part);
        row(m->locator[0] ? m->locator : "module", "%s %s%s%s%s%s", size_str((uint64_t)m->size_mib << 20, a, sizeof a),
            or_dash(m->type), m->form[0] ? " " : "", m->form, sp, who);
    }
    row("RAM", "%s in the memory map: %s for itelx, %s kept by the firmware", size_str(h->ram_kib * 1024, a, sizeof a),
        size_str(h->usable_kib * 1024, b, sizeof b), size_str(h->firmware_kib * 1024, c, sizeof c));

    printf(CYAN "computer and board" RESET "\n");
    if (h->flags & HWF_SMBIOS) {
        row("computer", "%s %s%s%s", or_dash(h->sys_maker), h->sys_product, h->sys_version[0] ? ", version " : "",
            h->sys_version);
        if (h->sys_family[0]) row("family", "%s", h->sys_family);
        if (h->board_maker[0] || h->board_product[0])
            row("board", "%s %s%s%s", or_dash(h->board_maker), h->board_product, h->board_version[0] ? ", version " : "",
                h->board_version);
        else
            row("board", "- (the firmware does not describe it)");
        if (h->chassis[0]) row("case", "%s", h->chassis);
    } else {
        row("computer", "- (no SMBIOS tables: the firmware does not describe the computer)");
    }

    printf(CYAN "firmware" RESET "\n");
    if (h->firmware == 2) {
        if (h->flags & HWF_FWINFO) {
            /* the UEFI revision: major << 16 | minor * 10 (+ errata): 2.70 is 2.7, 2.31 is 2.3.1 */
            unsigned mj = h->uefi_spec_rev >> 16, mn = h->uefi_spec_rev & 0xFFFF;
            char ver[16];
            if (mn % 10) snprintf(ver, sizeof ver, "%u.%u.%u", mj, mn / 10, mn % 10);
            else snprintf(ver, sizeof ver, "%u.%u", mj, mn / 10);
            row("type", "UEFI %s, %s (firmware revision 0x%x)", ver, or_dash(h->uefi_vendor), h->uefi_fw_rev);
        } else {
            row("type", "UEFI");
        }
        row("secure boot", "%s", secure_boot_name(h->secure_boot));
    } else {
        row("type", "BIOS (legacy start)");
    }
    if (h->flags & HWF_SMBIOS) {
        char rel[24] = "";
        if (h->bios_major || h->bios_minor) snprintf(rel, sizeof rel, ", release %u.%u", h->bios_major, h->bios_minor);
        row("firmware", "%s, version %s, %s%s", or_dash(h->bios_vendor), or_dash(h->bios_version), or_dash(h->bios_date), rel);
        if (h->bios_rom_kib) row("ROM", "%s", size_str((uint64_t)h->bios_rom_kib * 1024, a, sizeof a));
        row("SMBIOS", "%u.%u", h->smbios_major, h->smbios_minor);
    }
    char t1[16], t2[16];
    if (h->flags & HWF_ACPI)
        row("ACPI", "%s, %s %s; tables %s", h->acpi_rev >= 2 ? "2.0 or later" : "1.0",
            or_dash(trim(h->acpi_oem, t1, sizeof t1)), trim(h->acpi_oem_table, t2, sizeof t2), or_dash(h->acpi_tables));
    else row("ACPI", "-");

    printf(CYAN "graphics" RESET "\n");
    struct itx_pciinfo pc[64];
    int npc = pcilist(pc, 64), ngpu = 0;
    for (int i = 0; i < npc; i++) {
        if (pc[i].cls != 0x03) continue;
        ngpu++;
        const char *n = gpu_name(pc[i].vendor, pc[i].device);
        char win[48] = "";
        if (pc[i].mem_bytes) snprintf(win, sizeof win, ", memory window %s", size_str(pc[i].mem_bytes, a, sizeof a));
        row("device", "%02x:%02x.%u %s%s%s [%04x:%04x] rev %u%s", pc[i].bus, pc[i].dev, pc[i].fn,
            or_dash(vendor_name(pc[i].vendor)), n[0] ? " " : "", n, pc[i].vendor, pc[i].device, pc[i].rev, win);
    }
    if (!ngpu) row("device", "- (no display controller on the PCI bus)");
    row("screen", "%u x %u, %u bits per pixel, framebuffer at 0x%lx (%s)", h->fb_width, h->fb_height, h->fb_bpp,
        (unsigned long)h->fb_phys, size_str(h->fb_bytes, a, sizeof a));
    row("driver", "none: itelx draws on the framebuffer the firmware set up, with the processor");

    printf(CYAN "storage" RESET "\n");
    struct itx_diskinfo dk[32];
    int nd = disklist(dk, 32);
    for (int i = 0; i < nd; i++) {
        const struct itx_diskinfo *k = &dk[i];
        if (!k->partno) {
            char sec[24] = "";
            if (k->sector_size && k->sector_size != 512) snprintf(sec, sizeof sec, ", %u-byte sectors", k->sector_size);
            row(k->name, "%s, %s%s%s%s%s", or_dash(k->model), size_str(k->bytes, a, sizeof a), sec,
                k->removable || k->usb_id ? ", removable (USB)" : "", k->use[0] ? ": " : "", k->use);
        } else {
            char lab[20];
            snprintf(lab, sizeof lab, "  %s", k->name);
            row(lab, "%s, type 0x%02x%s%s", size_str(k->bytes, a, sizeof a), k->mbr_type, k->use[0] ? ": " : "", k->use);
        }
    }
    if (!nd) row("disks", "-");

    printf(CYAN "USB" RESET "\n");
    struct itx_devinfo u[32];
    int nu = devlist(u, 32);
    static const char *kinds[] = { "device", "keyboard", "mouse", "input", "hub", "storage" };
    for (int i = 0; i < nu; i++)
        row(u[i].port, "%s [%04x:%04x] %s", u[i].kind < 6 ? kinds[u[i].kind] : "device", u[i].vendor, u[i].product,
            or_dash(u[i].name));
    if (nu <= 0) row("devices", "none");

    printf(CYAN "PCI devices" RESET "\n");
    for (int i = 0; i < npc; i++) {
        char lab[16];
        snprintf(lab, sizeof lab, "%02x:%02x.%u", pc[i].bus, pc[i].dev, pc[i].fn);
        char drv[32] = "";
        if (pc[i].driver[0]) snprintf(drv, sizeof drv, ", itelx driver %s", pc[i].driver);
        row(lab, "[%04x:%04x] %-26s %s%s", pc[i].vendor, pc[i].device, pc[i].kind, or_dash(vendor_name(pc[i].vendor)), drv);
    }
}

void sys_device(void) { load(); device(); }

void sys_all(void) {
    load();
    overview();
    printf("\n");
    usage();
    printf("\n");
    device();
}

/* ------------------------------------------------------------- sys.top */

struct frame { char *buf; int len, cap; };

static void fput(struct frame *f, const char *s) {
    int l = (int)strlen(s);
    if (f->len + l + 1 > f->cap) {
        int cap = (f->len + l + 1) * 2;
        char *n = realloc(f->buf, cap);
        if (!n) return;
        f->buf = n;
        f->cap = cap;
    }
    memcpy(f->buf + f->len, s, l);
    f->len += l;
    f->buf[f->len] = 0;
}

static void fprintf_(struct frame *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void fprintf_(struct frame *f, const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fput(f, line);
}

static const char *state_name(int s) {
    switch (s) {
    case PS_READY: return "ready";
    case PS_RUNNING: return "running";
    case PS_BLOCKED: return "waiting";
    case PS_ZOMBIE: return "ended";
    }
    return "?";
}

struct trow { struct itx_procinfo p; unsigned long pct; };

static uint64_t before_ms(const struct sample *s, int pid) {
    for (int i = 0; i < s->np; i++) if (s->p[i].pid == pid) return s->p[i].cpu_ms;
    return 0;
}

static void draw_top(struct frame *f, const struct sample *a, const struct sample *b, long interval, bool live_keys) {
    struct itx_sysinfo si;
    sysinfo(&si);
    struct itx_ttyinfo ti;
    tty_info(&ti);
    uint64_t span = b->ms > a->ms ? b->ms - a->ms : 1;
    char br[96], x[24], y[24], up[48];
    f->len = 0;
    if (f->buf) f->buf[0] = 0;
    duration(si.uptime_ms / 1000, up, sizeof up);
    fprintf_(f, CYAN "sys.top" RESET "  %s   up %s   every %ld s   %s\n", ITELX_USERSPACE, up, interval,
             live_keys ? "q: quit" : "Ctrl+C: quit");
    unsigned long busy;
    if (a->have_cs && b->have_cs) {
        uint64_t total = b->cs.now_us - a->cs.now_us;
        busy = 1000 - pct10(b->cs.idle_us - a->cs.idle_us, total);
        unsigned long pu = pct10(b->cs.user_us - a->cs.user_us, total), pk = pct10(b->cs.kernel_us - a->cs.kernel_us, total);
        fprintf_(f, "processor %s %3lu.%lu %%   programs %lu.%lu %%, kernel %lu.%lu %%\n", bar(br, 30, busy), busy / 10,
                 busy % 10, pu / 10, pu % 10, pk / 10, pk % 10);
    } else {
        busy = pct10(busy_ms_of(b) - busy_ms_of(a), span);
        fprintf_(f, "processor %s %3lu.%lu %%\n", bar(br, 30, busy), busy / 10, busy % 10);
    }
    uint64_t used = si.total_kib - si.free_kib, progs = 0;
    for (int i = 0; i < b->np; i++) if (!b->p[i].kernel && b->p[i].state != PS_ZOMBIE) progs += b->p[i].mem_kib;
    unsigned long mp = pct10(used, si.total_kib);
    char z[24];
    fprintf_(f, "memory    %s %3lu.%lu %%   %s of %s, programs %s\n", bar(br, 30, mp), mp / 10, mp % 10,
             size_str(used * 1024, x, sizeof x), size_str(si.total_kib * 1024, y, sizeof y), size_str(progs * 1024, z, sizeof z));
    if (a->have_cs && b->have_cs) {
        uint64_t s = b->cs.now_us - a->cs.now_us;
        if (!s) s = 1;
        fprintf_(f, "tasks     %u: %u waiting for the processor, %u waiting   %lu interrupts/s, %lu calls/s, %lu switches/s\n",
                 b->cs.nprocs, b->cs.ready, b->cs.blocked,
                 (unsigned long)((b->cs.interrupts - a->cs.interrupts) * 1000000 / s),
                 (unsigned long)((b->cs.syscalls - a->cs.syscalls) * 1000000 / s),
                 (unsigned long)((b->cs.switches - a->cs.switches) * 1000000 / s));
        fprintf_(f, "graphics  0 %% (no driver): the processor drew %s/s on the screen\n",
                 size_str((b->cs.fb_bytes - a->cs.fb_bytes) * 1000000 / s, x, sizeof x));
    }
    fput(f, "\n" DIM "  PID  PARENT  NAME                STATE      CPU %     TIME     MEMORY  KIND" RESET "\n");

    struct trow rows[80];
    int n = 0;
    for (int i = 0; i < b->np && n < 80; i++) {
        const struct itx_procinfo *p = &b->p[i];
        if (p->pid == 0) continue;                        /* idle: the header's idle share */
        uint64_t prev = before_ms(a, p->pid);
        uint64_t d = p->cpu_ms >= prev ? p->cpu_ms - prev : 0;
        rows[n].p = *p;
        rows[n].pct = pct10(d, span);
        n++;
    }
    for (int i = 1; i < n; i++) {                         /* most processor time first */
        struct trow k = rows[i];
        int j = i - 1;
        while (j >= 0 && (rows[j].pct < k.pct || (rows[j].pct == k.pct && rows[j].p.pid > k.p.pid))) {
            rows[j + 1] = rows[j];
            j--;
        }
        rows[j + 1] = k;
    }
    int room = (int)ti.rows - 9;
    if (room < 3) room = 3;
    for (int i = 0; i < n && i < room; i++) {
        const struct itx_procinfo *p = &rows[i].p;
        unsigned long cs = (unsigned long)(p->cpu_ms / 10);
        char mem[24] = "-";
        if (!p->kernel) snprintf(mem, sizeof mem, "%lu KiB", (unsigned long)p->mem_kib);
        fprintf_(f, "%s%5d  %6d  %-18.18s  %-8s  %3lu.%lu  %3lu:%02lu.%02lu  %9s  %s" RESET "\n",
                 p->kernel ? DIM : p->state == PS_RUNNING ? GREEN : "", p->pid, p->ppid, p->name, state_name(p->state),
                 rows[i].pct / 10, rows[i].pct % 10, cs / 6000, cs / 100 % 60, cs % 100, mem,
                 p->kernel ? "kernel" : "program");
    }
    if (n > room) fprintf_(f, DIM "  ... %d more" RESET "\n", n - room);
}

int sys_top_main(int argc, char **argv) {
    long interval = 1;
    long rounds = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) rounds = atol(argv[++i]);
        else if (isdigit((unsigned char)argv[i][0])) interval = atol(argv[i]);
        else {
            printf("use: sys.top [seconds between refreshes, 1-60] [-n number of refreshes]\n");
            return 2;
        }
    }
    if (interval < 1 || interval > 60) interval = 1;
    load();
    struct itx_key k;
    /* raw keys with a timeout need the 1.4 kernel; else Ctrl+C stops it */
    bool live = tty_mode(TTY_MODE_RAW) == 0 && tty_keywait(&k, 0) != -ENOSYS;
    if (!live) tty_mode(TTY_MODE_COOKED);
    struct sample *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    struct frame f = { 0, 0, 0 };
    if (!a || !b) { printf("sys.top: out of memory\n"); return 1; }
    take(a);
    int status = 0;
    for (long round = 1; rounds < 0 || round <= rounds; round++) {
        if (live) {
            long until = uptime_ms() + interval * 1000;
            bool quit = false;
            for (;;) {
                long left = until - uptime_ms();
                if (left <= 0) break;
                int r = tty_keywait(&k, left);
                if (r == -EAGAIN) break;
                if (r < 0) { quit = true; break; }
                if (k.code == 'q' || k.code == 'Q' || k.code == KEY_ESC || k.code == 27 || k.code == 3) { quit = true; break; }
            }
            if (quit) break;
        } else {
            sleep_ms(interval * 1000);
        }
        take(b);
        draw_top(&f, a, b, interval, live);
        flush();
        write(1, "\x1b[2J", 4);
        if (f.buf) write(1, f.buf, f.len);
        struct sample *t = a;
        a = b;
        b = t;
    }
    if (live) tty_mode(TTY_MODE_COOKED);
    free(f.buf);
    free(a);
    free(b);
    return status;
}
