/* sokoban: push every box onto a target. You can push, never pull.
 *
 *   sokoban          the first level not solved yet
 *   sokoban <n>      level n
 *
 * Arrows (or w a s d) walk and push, u undoes, r restarts the level,
 * n / p go to the next / previous level, q or Esc ends. Solved levels are
 * kept in /dex/data/sokoban.txt. Every level here was checked with a
 * solver; the fewest moves are shown as the target.
 */
#include <ui.h>

/* # wall, . target, $ box, * box on a target, @ you, + you on a target */
static const struct { const char *map; int best; } LEVELS[] = {
    { "#######\n"
      "#     #\n"
      "# $ . #\n"
      "#  @  #\n"
      "#######\n", 5 },
    { "  ####\n"
      "###  #\n"
      "#  $ #\n"
      "# #. ##\n"
      "# .$@ #\n"
      "#     #\n"
      "#######\n", 7 },
    { "########\n"
      "#   .  #\n"
      "# $$   #\n"
      "#  @ . #\n"
      "########\n", 11 },
    { " #####\n"
      " #   ###\n"
      "##$#   #\n"
      "#  . $ #\n"
      "# .#@  #\n"
      "###    #\n"
      "  ######\n", 11 },
    { "########\n"
      "#  #   #\n"
      "# $  $ #\n"
      "#.##.# #\n"
      "#   @  #\n"
      "########\n", 13 },
    { "#######\n"
      "#. .  #\n"
      "# $$  #\n"
      "#  #  #\n"
      "# @   #\n"
      "#######\n", 15 },
    { "  #######\n"
      "  #  .  #\n"
      "### $#$ #\n"
      "#   .   #\n"
      "# #$#.  #\n"
      "#  @  ###\n"
      "#######\n", 32 },
    { "#########\n"
      "#   #   #\n"
      "# $ . $ #\n"
      "## .@. ##\n"
      "# $ . $ #\n"
      "#   #   #\n"
      "#########\n", 36 },
    { "#########\n"
      "#  .#.  #\n"
      "# $   $ #\n"
      "## # # ##\n"
      "#  $ $  #\n"
      "# .   . #\n"
      "#   @   #\n"
      "#########\n", 38 },
    { "  ######\n"
      "  #    #\n"
      "### ## #\n"
      "# $ $  #\n"
      "#.. # @#\n"
      "##     #\n"
      " #     #\n"
      " #######\n", 51 },
    { " ########\n"
      " #  #   #\n"
      " # $  $ #\n"
      "## #.## #\n"
      "#  . $ ##\n"
      "# @ .  #\n"
      "########\n", 51 },
};
#define NLEVELS ((int)(sizeof LEVELS / sizeof LEVELS[0]))

#define W 24
#define H 16
#define MAXUNDO 2000

static char wall[H][W], goal[H][W], box[H][W];
static int px, py, moves, pushes, level;
static int width, height;
static bool solved[NLEVELS];

/* the history: each step's direction and whether it pushed */
static struct { signed char dx, dy; bool push; } hist[MAXUNDO];
static int nhist;

static void load_level(int n) {
    memset(wall, 0, sizeof wall);
    memset(goal, 0, sizeof goal);
    memset(box, 0, sizeof box);
    width = height = 0;
    int x = 0, y = 0;
    for (const char *s = LEVELS[n].map; *s; s++) {
        if (*s == '\n') { y++; x = 0; continue; }
        if (x < W && y < H) {
            if (*s == '#') wall[y][x] = 1;
            if (*s == '.' || *s == '*' || *s == '+') goal[y][x] = 1;
            if (*s == '$' || *s == '*') box[y][x] = 1;
            if (*s == '@' || *s == '+') { px = x; py = y; }
        }
        x++;
        if (x > width) width = x;
        if (y + 1 > height) height = y + 1;
    }
    level = n;
    moves = pushes = nhist = 0;
}

static bool done(void) {
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            if (box[y][x] && !goal[y][x]) return false;
    return true;
}

static bool step(int dx, int dy) {
    int nx = px + dx, ny = py + dy;
    if (nx < 0 || ny < 0 || nx >= W || ny >= H || wall[ny][nx]) return false;
    bool push = false;
    if (box[ny][nx]) {
        int bx = nx + dx, by = ny + dy;
        if (bx < 0 || by < 0 || bx >= W || by >= H || wall[by][bx] || box[by][bx]) return false;
        box[ny][nx] = 0;
        box[by][bx] = 1;
        push = true;
        pushes++;
    }
    px = nx;
    py = ny;
    moves++;
    if (nhist == MAXUNDO) {
        memmove(hist, hist + 1, sizeof hist[0] * (MAXUNDO - 1));
        nhist--;
    }
    hist[nhist].dx = (signed char)dx;
    hist[nhist].dy = (signed char)dy;
    hist[nhist].push = push;
    nhist++;
    return true;
}

static bool undo(void) {
    if (!nhist) return false;
    nhist--;
    int dx = hist[nhist].dx, dy = hist[nhist].dy;
    if (hist[nhist].push) {
        box[py + dy][px + dx] = 0;
        box[py][px] = 1;
        pushes--;
    }
    px -= dx;
    py -= dy;
    moves--;
    return true;
}

static void load_progress(void) {
    char path[64];
    snprintf(path, sizeof path, "%s/sokoban.txt", ui_data_dir());
    char *t = read_file(path, NULL, 4096);
    if (!t) return;
    for (char *p = t; *p;) {
        long n = strtol(p, &p, 10);
        if (n >= 1 && n <= NLEVELS) solved[n - 1] = true;
        while (*p && (*p < '0' || *p > '9')) p++;
    }
    free(t);
}

static void save_progress(void) {
    char path[64], text[256] = "";
    snprintf(path, sizeof path, "%s/sokoban.txt", ui_data_dir());
    for (int i = 0; i < NLEVELS; i++)
        if (solved[i]) {
            char n[8];
            snprintf(n, sizeof n, "%d\n", i + 1);
            strlcat(text, n, sizeof text);
        }
    write_file(path, text, strlen(text), false);
}

static void draw(const char *message) {
    int left = 0;
    for (int i = 0; i < NLEVELS; i++) if (!solved[i]) left++;
    ui_frame();
    ui_printf(C_CYAN "SOKOBAN" C_RESET "   level %d of %d%s   moves %d (fewest: %d)   pushes %d\n\n",
              level + 1, NLEVELS, solved[level] ? " " C_GREEN "(solved)" C_RESET : "", moves, LEVELS[level].best, pushes);
    for (int y = 0; y < height; y++) {
        ui_put("    ");
        for (int x = 0; x < width; x++) {
            if (x == px && y == py) ui_put(C_YELLOW "@" C_RESET);
            else if (wall[y][x]) ui_put(C_DIM "#" C_RESET);
            else if (box[y][x] && goal[y][x]) ui_put(C_GREEN "*" C_RESET);
            else if (box[y][x]) ui_put(C_CYAN "$" C_RESET);
            else if (goal[y][x]) ui_put(C_RED "." C_RESET);
            else ui_put(" ");
        }
        ui_put("\n");
    }
    ui_printf("\n%s\n", message);
    ui_put(C_DIM "you @  box $  target .  box on target *   " C_RESET "\n");
    ui_printf(C_DIM "arrows / w a s d: move   u: undo   r: restart   n/p: next/previous level   q: quit"
              C_RESET "   (%d level(s) left)\n", left);
    ui_show();
}

int main(int argc, char **argv) {
    load_progress();
    int start = 0;
    while (start < NLEVELS - 1 && solved[start]) start++;
    if (argc > 1) {
        int n = atoi(argv[1]);
        if (n < 1 || n > NLEVELS) {
            printf("sokoban: the levels are 1 to %d\n", NLEVELS);
            return 2;
        }
        start = n - 1;
    }
    load_level(start);
    ui_begin();
    const char *message = "push every box ($) onto a target (.)";
    for (;;) {
        draw(message);
        int k = ui_key();
        if (k < 0 || k == 'q' || k == 'Q' || k == 27) break;
        message = "";
        int dx = 0, dy = 0;
        if (k == KEY_LEFT || k == 'a' || k == 'h') dx = -1;
        else if (k == KEY_RIGHT || k == 'd' || k == 'l') dx = 1;
        else if (k == KEY_UP || k == 'w' || k == 'k') dy = -1;
        else if (k == KEY_DOWN || k == 's' || k == 'j') dy = 1;
        else if (k == 'u' || k == 'U' || k == '\b') { if (!undo()) message = "nothing to undo"; continue; }
        else if (k == 'r' || k == 'R') { load_level(level); message = "the level starts again"; continue; }
        else if (k == 'n' || k == 'N' || k == KEY_PGDN) {
            if (level + 1 < NLEVELS) load_level(level + 1); else message = "this is the last level";
            continue;
        } else if (k == 'p' || k == 'P' || k == KEY_PGUP) {
            if (level > 0) load_level(level - 1); else message = "this is the first level";
            continue;
        } else {
            message = "use the arrows (or w a s d)";
            continue;
        }
        if (!step(dx, dy) || !done()) continue;
        solved[level] = true;
        save_progress();
        bool all = true;
        for (int i = 0; i < NLEVELS; i++) if (!solved[i]) all = false;
        char msg[160];
        snprintf(msg, sizeof msg, C_GREEN "solved in %d moves%s" C_RESET " - %s", moves,
                 moves <= LEVELS[level].best ? ", the fewest possible!" : "",
                 level + 1 < NLEVELS ? "a key for the next level" : all ? "every level is solved!" : "a key to go on");
        draw(msg);
        k = ui_key();
        if (k < 0 || k == 'q' || k == 'Q' || k == 27) break;
        if (level + 1 < NLEVELS) load_level(level + 1);
        message = "";
    }
    ui_end();
    int count = 0;
    for (int i = 0; i < NLEVELS; i++) if (solved[i]) count++;
    printf("sokoban: %d of %d level(s) solved\n", count, NLEVELS);
    return 0;
}
