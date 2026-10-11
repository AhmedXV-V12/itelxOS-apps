/* life: John Conway's Game of Life, on the whole screen.
 *
 *   life [pattern] [generations] [ms]
 *
 * pattern: random (the default), glider, gun (Gosper's glider gun), pulsar,
 * rpent (the R-pentomino), acorn, spaceship. With no number of generations
 * it goes on until Ctrl+C; ms is the pause between generations (100).
 *
 * The rules: a live cell with 2 or 3 live neighbours lives on, a dead cell
 * with exactly 3 comes to life, every other cell dies or stays dead. The
 * edges wrap around.
 */
#include <ui.h>

#define MAXC 200
#define MAXR 80

static unsigned char cell[MAXR][MAXC], next_[MAXR][MAXC];
static int cols, rows;

static const struct { const char *name; const char *rows[14]; } PATTERNS[] = {
    { "glider", { ".O", "..O", "OOO" } },
    { "gun", { "........................O",
               "......................O.O",
               "............OO......OO............OO",
               "...........O...O....OO............OO",
               "OO........O.....O...OO",
               "OO........O...O.OO....O.O",
               "..........O.....O.......O",
               "...........O...O",
               "............OO" } },
    { "pulsar", { "..OOO...OOO", "", "O....O.O....O", "O....O.O....O", "O....O.O....O", "..OOO...OOO", "",
                  "..OOO...OOO", "O....O.O....O", "O....O.O....O", "O....O.O....O", "", "..OOO...OOO" } },
    { "rpent", { ".OO", "OO", ".O" } },
    { "acorn", { ".O", "...O", "OO..OOO" } },
    { "spaceship", { ".O..O", "O", "O...O", "OOOO" } },
};
#define NPAT ((int)(sizeof PATTERNS / sizeof PATTERNS[0]))

static void put_pattern(int p) {
    int ph = 0, pw = 0;
    for (int r = 0; r < 14 && PATTERNS[p].rows[r]; r++) {
        ph = r + 1;
        int l = (int)strlen(PATTERNS[p].rows[r]);
        if (l > pw) pw = l;
    }
    int top = (rows - ph) / 2, left = (cols - pw) / 2;
    if (!strcmp(PATTERNS[p].name, "glider") || !strcmp(PATTERNS[p].name, "gun")) { top = 1; left = 1; }
    for (int r = 0; r < ph; r++)
        for (int c = 0; PATTERNS[p].rows[r][c]; c++)
            if (PATTERNS[p].rows[r][c] == 'O' && top + r < rows && left + c < cols) cell[top + r][left + c] = 1;
}

static int step(void) {
    int alive = 0;
    for (int r = 0; r < rows; r++) {
        int up = (r + rows - 1) % rows, dn = (r + 1) % rows;
        for (int c = 0; c < cols; c++) {
            int lf = (c + cols - 1) % cols, rt = (c + 1) % cols;
            int n = cell[up][lf] + cell[up][c] + cell[up][rt] + cell[r][lf] + cell[r][rt] +
                    cell[dn][lf] + cell[dn][c] + cell[dn][rt];
            next_[r][c] = n == 3 || (n == 2 && cell[r][c]);
            alive += next_[r][c];
        }
    }
    memcpy(cell, next_, sizeof cell);
    return alive;
}

static int count(void) {
    int n = 0;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) n += cell[r][c];
    return n;
}

int main(int argc, char **argv) {
    const char *pattern = "random";
    long gens = -1, ms = 100;
    int nums = 0;
    for (int i = 1; i < argc; i++) {
        char *end;
        long v = strtol(argv[i], &end, 10);
        if (*end || end == argv[i]) pattern = argv[i];
        else if (nums++ == 0) gens = v;
        else ms = v;
    }
    int p = -1;
    for (int i = 0; i < NPAT; i++) if (!strcmp(pattern, PATTERNS[i].name)) p = i;
    if (p < 0 && strcmp(pattern, "random")) {
        printf("life: patterns: random");
        for (int i = 0; i < NPAT; i++) printf(", %s", PATTERNS[i].name);
        printf("\nuse: life [pattern] [generations] [ms between them]\n");
        return 2;
    }
    if (ms < 0) ms = 0;
    ui_size(&cols, &rows);
    rows -= 3;
    if (cols > MAXC) cols = MAXC;
    if (rows > MAXR) rows = MAXR;
    if (rows < 10) rows = 10;
    if (p < 0) {
        rng_seed((uint64_t)uptime_ms() * 2654435761u ^ (uint64_t)unix_time());
        for (int r = 0; r < rows; r++)
            for (int c = 0; c < cols; c++) cell[r][c] = rng_below(4) == 0;
    } else {
        put_pattern(p);
    }
    char *line = malloc((size_t)cols * 3 + 2);
    if (!line) return 1;
    int alive = count();
    for (long g = 0; gens < 0 || g <= gens; g++) {
        ui_frame();
        ui_printf(C_CYAN "LIFE" C_RESET "  %s   generation %ld   %d alive   " C_DIM "Ctrl+C stops" C_RESET "\n",
                  pattern, g, alive);
        for (int r = 0; r < rows; r++) {
            char *o = line;
            for (int c = 0; c < cols; c++) {
                if (cell[r][c]) { memcpy(o, "\xe2\x96\x88", 3); o += 3; }   /* a full block */
                else *o++ = ' ';
            }
            while (o > line && o[-1] == ' ') o--;
            *o++ = '\n';
            *o = 0;
            ui_put(C_GREEN);
            ui_put(line);
        }
        ui_put(C_RESET);
        ui_show();
        if (gens >= 0 && g == gens) break;
        if (ms) sleep_ms(ms);
        alive = step();
    }
    printf("life: %d cells alive at the end\n", alive);
    return 0;
}
