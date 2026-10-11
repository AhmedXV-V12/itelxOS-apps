/* 2048: slide the tiles; two equal tiles that meet become one. Reach 2048.
 *
 *   2048              play
 *   2048 --seed N     the same tiles every time (for tests)
 *
 * Arrows (or w a s d) slide, u takes the last move back, r starts again,
 * q or Esc ends. The best score is kept in /dex/data/2048.txt.
 */
#include <ui.h>

#define N 4

static int board[N][N], undo_board[N][N];
static long score, undo_score, best;
static bool can_undo, won, keep_going;

static void add_tile(void) {
    int free[N * N], n = 0;
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            if (!board[r][c]) free[n++] = r * N + c;
    if (!n) return;
    int at = free[rng_below((uint32_t)n)];
    board[at / N][at % N] = rng_below(10) == 0 ? 4 : 2;
}

static void new_game(void) {
    memset(board, 0, sizeof board);
    score = 0;
    can_undo = won = keep_going = false;
    add_tile();
    add_tile();
}

/* slides one line of N tiles towards index 0; returns whether anything moved */
static bool slide(int *line[N]) {
    int out[N] = { 0 }, n = 0;
    bool merged_last = false;
    for (int i = 0; i < N; i++) {
        int v = *line[i];
        if (!v) continue;
        if (n > 0 && out[n - 1] == v && !merged_last) {
            out[n - 1] = v * 2;
            score += v * 2;
            if (v * 2 == 2048) won = true;
            merged_last = true;
        } else {
            out[n++] = v;
            merged_last = false;
        }
    }
    bool moved = false;
    for (int i = 0; i < N; i++) {
        if (*line[i] != out[i]) moved = true;
        *line[i] = out[i];
    }
    return moved;
}

/* dir: 0 left, 1 right, 2 up, 3 down */
static bool move(int dir) {
    bool moved = false;
    for (int k = 0; k < N; k++) {
        int *line[N];
        for (int i = 0; i < N; i++) {
            int j = (dir == 1 || dir == 3) ? N - 1 - i : i;
            line[i] = dir < 2 ? &board[k][j] : &board[j][k];
        }
        if (slide(line)) moved = true;
    }
    return moved;
}

static bool can_move(void) {
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++) {
            if (!board[r][c]) return true;
            if (c + 1 < N && board[r][c] == board[r][c + 1]) return true;
            if (r + 1 < N && board[r][c] == board[r + 1][c]) return true;
        }
    return false;
}

static const char *tile_color(int v) {
    if (v <= 4) return C_RESET;
    if (v <= 64) return C_YELLOW;
    if (v <= 512) return C_GREEN;
    if (v <= 1024) return C_CYAN;
    return C_RED;
}

static void load_best(void) {
    char path[64];
    snprintf(path, sizeof path, "%s/2048.txt", ui_data_dir());
    char *t = read_file(path, NULL, 64);
    if (t) { best = atol(t); free(t); }
}

static void save_best(void) {
    if (score <= best) return;
    best = score;
    char path[64], text[32];
    snprintf(path, sizeof path, "%s/2048.txt", ui_data_dir());
    snprintf(text, sizeof text, "%ld\n", best);
    write_file(path, text, strlen(text), false);
}

static void draw(const char *message) {
    ui_frame();
    ui_printf(C_CYAN "2048" C_RESET "   score %ld   best %ld\n\n", score, best > score ? best : score);
    for (int r = 0; r < N; r++) {
        ui_put("  +------+------+------+------+\n  |");
        for (int c = 0; c < N; c++) {
            int v = board[r][c];
            if (v) ui_printf("%s%5d" C_RESET " |", tile_color(v), v);
            else ui_put(C_DIM "    . " C_RESET "|");
        }
        ui_put("\n");
    }
    ui_put("  +------+------+------+------+\n\n");
    ui_printf("%s\n", message);
    ui_put(C_DIM "arrows or w a s d: slide   u: undo   r: new game   q: quit" C_RESET "\n");
    ui_show();
}

int main(int argc, char **argv) {
    uint64_t seed = (uint64_t)uptime_ms() * 2654435761u ^ (uint64_t)unix_time();
    if (argc > 2 && !strcmp(argv[1], "--seed")) seed = strtoul(argv[2], NULL, 10);
    rng_seed(seed);
    load_best();
    new_game();
    ui_begin();
    const char *message = "slide the tiles; equal tiles join";
    for (;;) {
        draw(message);
        int k = ui_key();
        if (k < 0 || k == 'q' || k == 'Q' || k == 27) break;
        int dir = -1;
        if (k == KEY_LEFT || k == 'a' || k == 'h') dir = 0;
        else if (k == KEY_RIGHT || k == 'd' || k == 'l') dir = 1;
        else if (k == KEY_UP || k == 'w' || k == 'k') dir = 2;
        else if (k == KEY_DOWN || k == 's' || k == 'j') dir = 3;
        else if (k == 'u' || k == 'U') {
            if (can_undo) {
                memcpy(board, undo_board, sizeof board);
                score = undo_score;
                can_undo = false;
                message = "the last move is taken back";
            } else {
                message = "nothing to take back";
            }
            continue;
        } else if (k == 'r' || k == 'R') {
            save_best();
            new_game();
            message = "a new game";
            continue;
        } else {
            message = "use the arrows (or w a s d)";
            continue;
        }
        int saved[N][N];
        long saved_score = score;
        memcpy(saved, board, sizeof board);
        if (!move(dir)) {
            message = "nothing moves that way";
            continue;
        }
        memcpy(undo_board, saved, sizeof board);
        undo_score = saved_score;
        can_undo = true;
        add_tile();
        message = "";
        if (won && !keep_going) {
            message = C_GREEN "2048! you won - keep going for a higher score" C_RESET;
            keep_going = true;
        }
        if (!can_move()) {
            save_best();
            draw(C_RED "no more moves - game over" C_RESET "   (r: new game, q: quit)");
            for (;;) {
                k = ui_key();
                if (k == 'r' || k == 'R' || k == 'u' || k == 'U' || k == 'q' || k == 'Q' || k == 27 || k < 0) break;
            }
            if (k == 'r' || k == 'R') { new_game(); message = "a new game"; continue; }
            if (k == 'u' || k == 'U') {
                memcpy(board, undo_board, sizeof board);
                score = undo_score;
                can_undo = false;
                message = "the last move is taken back";
                continue;
            }
            break;
        }
    }
    save_best();
    ui_end();
    printf("2048: score %ld, best %ld\n", score, best);
    return 0;
}
