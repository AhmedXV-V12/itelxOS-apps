/* ui: what the full-screen apps share.
 *
 * The itelx terminal keeps lines of text and has no cursor movement, so a
 * full-screen app draws a whole frame each time: the frame is built in
 * memory, then written in one piece after "clear the screen". The terminal
 * draws only the cells that changed, so this does not flicker.
 *
 * Colours are the terminal's six: reset (white), dim, yellow, red, green,
 * cyan. Keys come one by one in raw mode (no echo, no line editing);
 * Ctrl+C still stops the program.
 */
#ifndef UI_H
#define UI_H

#include <itx.h>

#define C_RESET  "\x1b[0m"
#define C_DIM    "\x1b[90m"
#define C_YELLOW "\x1b[93m"
#define C_RED    "\x1b[91m"
#define C_GREEN  "\x1b[92m"
#define C_CYAN   "\x1b[96m"

/* the screen in character cells (the bottom status row not counted) */
void ui_size(int *cols, int *rows);

/* raw keys on (ui_begin) and off (ui_end); a key: a Unicode character,
 * '\n' for Enter, '\b' for Backspace, 27 for Esc, or KEY_UP ... KEY_F1+n;
 * -1 when the terminal cannot be read. In raw mode the terminal passes
 * Ctrl+C on as a key: ui_key ends the program then (exit status 130). */
void ui_begin(void);
void ui_end(void);
int  ui_key(void);

/* the frame */
void ui_frame(void);                         /* start a new, empty frame */
void ui_put(const char *s);
void ui_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ui_show(void);                          /* clear the screen and write the frame */

/* a line of text typed by the user, with the terminal's line editing
 * (raw keys are switched off while it is read); false on Esc/end of input */
bool ui_ask(const char *prompt, char *buf, size_t len);

/* a small random number generator (xorshift64*) */
void     rng_seed(uint64_t seed);
uint32_t rng_below(uint32_t n);               /* 0 .. n-1 */

/* the folder apps keep their data in: /dex/data (made when missing) */
const char *ui_data_dir(void);

#endif
