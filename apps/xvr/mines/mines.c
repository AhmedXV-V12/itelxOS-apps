/* mines: minesweeper. Open every square that has no mine.
 *
 *   mines                  9 x 9, 10 mines
 *   mines 16               16 x 16, 40 mines
 *   mines <w> <h> <n>      your own size (up to 30 x 20) and number of mines
 *   mines ... --seed N     the same mines every time (for tests)
 *
 * Arrows (or w a s d) move, Space or Enter opens a square, f puts or takes
 * a flag, r starts again, q or Esc ends. A number tells how many of the 8
 * squares around have a mine. The first square you open is never a mine.
 */
#include <ui.h>

#define MAXW 30
#define MAXH 20

static int w = 9, h = 9, nmines = 10;
static bool mine[MAXH][MAXW], open_[MAXH][MAXW], flag[MAXH][MAXW];
static int cx, cy, opened, flags;
static bool placed, over, won;
static long t0, t1;

static int around(int x, int y) {
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if ((dx || dy) && nx >= 0 && ny >= 0 && nx < w && ny < h && mine[ny][nx]) n++;
        }
    return n;
}

static void new_game(void) {
    memset(mine, 0, sizeof mine);
    memset(open_, 0, sizeof open_);
    memset(flag, 0, sizeof flag);
    cx = w / 2;
    cy = h / 2;
    opened = flags = 0;
    placed = over = won = false;
    t0 = t1 = 0;
}

/* the mines go in after the first square is chosen, never on it or next to it */
static void place(int sx, int sy) {
    int free = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (x < sx - 1 || x > sx + 1 || y < sy - 1 || y > sy + 1) free++;
    int n = nmines < free ? nmines : free;
    while (n > 0) {
        int x = (int)rng_below((uint32_t)w), y = (int)rng_below((uint32_t)h);
        if (mine[y][x] || (x >= sx - 1 && x <= sx + 1 && y >= sy - 1 && y <= sy + 1)) continue;
        mine[y][x] = true;
        n--;
    }
    placed = true;
    t0 = uptime_ms();
}

/* opens a square and, when it has no mine around, everything around it */
static void reveal(int x, int y) {
    static int stack[MAXW * MAXH];
    static bool queued[MAXH][MAXW];
    memset(queued, 0, sizeof queued);
    int sp = 0;
    stack[sp++] = y * MAXW + x;
    queued[y][x] = true;
    while (sp) {
        int p = stack[--sp], px = p % MAXW, py = p / MAXW;
        if (open_[py][px] || flag[py][px]) continue;
        open_[py][px] = true;
        opened++;
        if (mine[py][px] || around(px, py)) continue;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int nx = px + dx, ny = py + dy;
                if (nx >= 0 && ny >= 0 && nx < w && ny < h && !open_[ny][nx] && !queued[ny][nx]) {
                    queued[ny][nx] = true;
                    stack[sp++] = ny * MAXW + nx;
                }
            }
    }
}

static const char *num_color(int n) {
    static const char *c[] = { C_RESET, C_CYAN, C_GREEN, C_RED, C_YELLOW, C_RED, C_CYAN, C_RESET, C_DIM };
    return c[n];
}

static void draw(const char *message) {
    long secs = placed ? ((over ? t1 : uptime_ms()) - t0) / 1000 : 0;
    ui_frame();
    ui_printf(C_CYAN "MINES" C_RESET "   %d x %d   mines %d   flags %d   time %ld s\n\n", w, h, nmines, flags, secs);
    for (int y = 0; y < h; y++) {
        ui_put("  ");
        for (int x = 0; x < w; x++) {
            bool cur = x == cx && y == cy;
            ui_put(cur ? C_YELLOW "[" C_RESET : " ");
            if (flag[y][x] && !(over && !mine[y][x])) ui_put(C_RED "F" C_RESET);
            else if (flag[y][x]) ui_put(C_YELLOW "x" C_RESET);         /* a wrong flag, shown at the end */
            else if (open_[y][x] || (over && mine[y][x])) {
                if (mine[y][x]) ui_put(C_RED "*" C_RESET);
                else {
                    int n = around(x, y);
                    if (n) ui_printf("%s%d" C_RESET, num_color(n), n);
                    else ui_put(" ");
                }
            } else ui_put(C_DIM "." C_RESET);
            ui_put(cur ? C_YELLOW "]" C_RESET : " ");
        }
        ui_put("\n");
    }
    ui_printf("\n%s\n", message);
    ui_put(C_DIM "arrows / w a s d: move   Space or Enter: open   f: flag   r: new game   q: quit" C_RESET "\n");
    ui_show();
}

int main(int argc, char **argv) {
    uint64_t seed = (uint64_t)uptime_ms() * 2654435761u ^ (uint64_t)unix_time();
    int nums[3], nn = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) { seed = strtoul(argv[++i], NULL, 10); continue; }
        if (nn < 3) nums[nn++] = atoi(argv[i]);
    }
    if (nn == 1 && nums[0] == 16) { w = h = 16; nmines = 40; }
    else if (nn == 3) { w = nums[0]; h = nums[1]; nmines = nums[2]; }
    else if (nn) {
        printf("use: mines [16 | <width> <height> <mines>]\n");
        return 2;
    }
    if (w < 5 || h < 5 || w > MAXW || h > MAXH || nmines < 1 || nmines > w * h - 9) {
        printf("mines: the size is 5 x 5 to %d x %d, with 1 to (squares - 9) mines\n", MAXW, MAXH);
        return 2;
    }
    rng_seed(seed);
    new_game();
    ui_begin();
    const char *message = "open a square: Space or Enter";
    char buf[128];
    for (;;) {
        draw(message);
        int k = ui_key();
        if (k < 0 || k == 'q' || k == 'Q' || k == 27) break;
        message = "";
        if (k == KEY_LEFT || k == 'a' || k == 'h') { if (cx > 0) cx--; }
        else if (k == KEY_RIGHT || k == 'd' || k == 'l') { if (cx < w - 1) cx++; }
        else if (k == KEY_UP || k == 'w' || k == 'k') { if (cy > 0) cy--; }
        else if (k == KEY_DOWN || k == 's' || k == 'j') { if (cy < h - 1) cy++; }
        else if (k == 'r' || k == 'R') { new_game(); message = "a new game"; }
        else if (over) { message = "the game is over: r for a new one, q to quit"; }
        else if (k == 'f' || k == 'F') {
            if (open_[cy][cx]) message = "this square is open";
            else { flag[cy][cx] = !flag[cy][cx]; flags += flag[cy][cx] ? 1 : -1; }
        } else if (k == ' ' || k == '\n') {
            if (flag[cy][cx]) { message = "there is a flag here (f takes it away)"; continue; }
            if (!placed) place(cx, cy);
            if (mine[cy][cx]) {
                over = true;
                t1 = uptime_ms();
                message = C_RED "BOOM - that was a mine" C_RESET "   (r: new game, q: quit)";
                continue;
            }
            reveal(cx, cy);
            if (opened == w * h - nmines) {
                over = won = true;
                t1 = uptime_ms();
                snprintf(buf, sizeof buf, C_GREEN "you cleared the field in %ld seconds!" C_RESET "   (r: new game, q: quit)",
                         (t1 - t0) / 1000);
                message = buf;
            }
        } else {
            message = "arrows to move, Space to open, f to flag";
        }
    }
    ui_end();
    printf("mines: %s\n", won ? "the field is cleared" : over ? "a mine went off" : "stopped");
    return 0;
}
