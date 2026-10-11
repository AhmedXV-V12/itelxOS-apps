/* sysmon: what the computer is doing, refreshed every second.
 *
 *   sysmon          live, until Ctrl+C
 *   sysmon <n>      n refreshes, then it ends (sysmon 1: one look)
 *
 * The system (version, firmware, screen, processor, uptime), the memory,
 * and every process with its share of the processor in the last second,
 * its processor time and its memory. Kernel threads are shown dim.
 */
#include <ui.h>

#define MAXP 128

static struct itx_procinfo now[MAXP], before[MAXP];
static int nnow, nbefore;

static const char *state_name(int s) {
    switch (s) {
    case PS_READY: return "ready";
    case PS_RUNNING: return "running";
    case PS_BLOCKED: return "waiting";
    case PS_ZOMBIE: return "ended";
    }
    return "?";
}

static uint64_t cpu_before(int pid) {
    for (int i = 0; i < nbefore; i++) if (before[i].pid == pid) return before[i].cpu_ms;
    return 0;
}

static void bar(uint64_t used, uint64_t total, int width) {
    int fill = total ? (int)(used * (uint64_t)width / total) : 0;
    ui_put("[");
    ui_put(fill * 10 > width * 8 ? C_RED : fill * 2 > width ? C_YELLOW : C_GREEN);
    for (int i = 0; i < width; i++) ui_put(i < fill ? "#" : C_DIM ".");
    ui_put(C_RESET "]");
}

int main(int argc, char **argv) {
    long rounds = argc > 1 ? atol(argv[1]) : -1;
    if (argc > 1 && rounds < 1) {
        printf("use: sysmon [number of refreshes]\n");
        return 2;
    }
    long t_before = uptime_ms();
    nbefore = proclist(before, MAXP);
    if (nbefore < 0) nbefore = 0;
    for (long round = 1; rounds < 0 || round <= rounds; round++) {
        sleep_ms(rounds == 1 ? 300 : 1000);
        struct itx_sysinfo si;
        if (sysinfo(&si)) { printf("sysmon: no system information\n"); return 1; }
        long t_now = uptime_ms();
        nnow = proclist(now, MAXP);
        if (nnow < 0) nnow = 0;
        long span = t_now - t_before;
        if (span < 1) span = 1;

        uint64_t up = si.uptime_ms / 1000;
        uint64_t used = si.total_kib - si.free_kib;
        ui_frame();
        ui_printf(C_CYAN "SYSMON" C_RESET "  %s   up %lu:%02lu:%02lu   %s   %ux%u\n", si.version,
                  (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60), (unsigned long)(up % 60),
                  si.firmware == 2 ? "UEFI" : si.firmware == 1 ? "BIOS" : "?", si.fb_width, si.fb_height);
        ui_printf("CPU     %s\n", si.cpu[0] ? si.cpu : "-");
        ui_printf("system  %s\n", si.root[0] ? si.root : "-");
        ui_put("memory  ");
        bar(used, si.total_kib, 30);
        ui_printf(" %lu of %lu MiB used, %lu MiB free (kernel heap %lu KiB)\n\n",
                  (unsigned long)(used / 1024), (unsigned long)(si.total_kib / 1024),
                  (unsigned long)(si.free_kib / 1024), (unsigned long)si.heap_kib);
        ui_printf("%d processes\n", nnow);
        ui_put(C_DIM "  PID  PARENT  STATE     CPU%   CPU TIME    MEMORY  NAME" C_RESET "\n");
        for (int i = 0; i < nnow; i++) {
            struct itx_procinfo *p = &now[i];
            uint64_t d = p->cpu_ms - cpu_before(p->pid);
            if (p->cpu_ms < cpu_before(p->pid)) d = 0;
            unsigned long pct10 = (unsigned long)(d * 1000 / (uint64_t)span);
            if (pct10 > 1000) pct10 = 1000;
            unsigned long cs = (unsigned long)(p->cpu_ms / 1000);
            ui_printf("%s%5d  %6d  %-8s %3lu.%lu  %3lu:%02lu.%02lu  %6lu K  %s" C_RESET "\n",
                      p->kernel ? C_DIM : p->state == PS_RUNNING ? C_GREEN : "",
                      p->pid, p->ppid, state_name(p->state), pct10 / 10, pct10 % 10,
                      cs / 60, cs % 60, (unsigned long)(p->cpu_ms % 1000 / 10),
                      (unsigned long)p->mem_kib, p->name);
        }
        if (rounds < 0 || round < rounds) ui_put(C_DIM "\nrefreshed every second - Ctrl+C stops" C_RESET "\n");
        ui_show();
        memcpy(before, now, sizeof now);
        nbefore = nnow;
        t_before = t_now;
    }
    return 0;
}
