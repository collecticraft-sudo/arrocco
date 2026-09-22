/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Arrocco - the C face of the CT800 port.
 *
 * CT800 is a C99 engine whose whole board lives in global variables, and whose
 * headers use `restrict` and macro names (NONE, Max, INFINITY_, ...) that must
 * never reach the C++ side. So EVERYTHING that touches the engine stays in
 * ct800_glue.c, and the C++ wrapper (ct800_searcher.h) sees only this file:
 * plain C types, no CT800 header, no engine global.
 *
 * ONE instance only: the engine's board, move stack and hash tables are file
 * scope globals of the upstream sources. Two searchers would share them.
 * ct800_glue_* is not reentrant and not thread safe; call it from one thread.
 * The single exception is ct800_glue_abort(), which is a store to a volatile
 * flag and may be called from any thread (or an ISR) while think() runs.
 */
#ifndef ARROCCO_CT800_GLUE_H
#define ARROCCO_CT800_GLUE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Smallest usable engine memory, in bytes: the two transposition tables plus
   the pawn hash tables, which ct800_memory.h moves out of .bss and into this
   same block. The transposition tables cannot go below DEF_MAX_TT (0x8000)
   entries each, because the engine reports "hashfull" by scanning that many
   entries whatever the real size and would otherwise read past the end.
       2 * (0x8000 + 3) * 10   transposition tables   655420
       0x3000 * 8              pawn hash               98304
       0x3000 * 2              pawn/rook files         24576  */
#define CT800_HASH_MIN_BYTES ((size_t)778300u)

/* Clock and sleep, supplied by the platform. now_ms must be monotonic and
   must not wrap during a search; sleep_ms may return early. Set them once,
   before ct800_glue_init(). */
void ct800_glue_set_clock(int64_t (*now_ms)(void), void (*sleep_ms)(int32_t));

/* Hands the engine its two transposition tables, carved out of caller-owned
   memory: nothing is allocated or freed in here, ever. `bytes` must be at
   least CT800_HASH_MIN_BYTES; more is used only when it is at least twice as
   much (the table size is a power of two). Returns 0 on success, non-zero if
   the block is too small or the clock was not set. */
int ct800_glue_init(void *hash_memory, size_t bytes);

/* Entries per transposition table after init (0 before it), for reporting. */
size_t ct800_glue_hash_entries(void);

/* Seeds the engine's small random generator, which picks the opening book move and
   the evaluation noise of the weak levels. init() seeds it from the clock, which on a
   device reads about zero at that moment: without a call to this, every power-on
   replays the same "random" game. Tests that want a reproducible engine simply do not
   call it. */
void ct800_glue_seed(uint32_t seed);

/* Sets up the board. `fen` may be NULL or "" for the standard starting
   position; `uci_moves` may be NULL or "" for none, otherwise a space
   separated "e2e4 e7e5 g1f3" applied from that position. Returns 0 on
   success; on failure the board is left in a defined but unusable state and
   think() must not be called until a later setPosition succeeds. */
int ct800_glue_set_position(const char *fen, const char *uci_moves);

/* The side to move in the position set up last: 0 = White, 1 = Black. */
int ct800_glue_side_to_move(void);

/* Searches the position set up last and writes the best move as UCI text
   ("e2e4", "e7e8q") into best_uci, which must hold 6 bytes.
     time_ms    thinking time budget, milliseconds (>= 1)
     max_depth  hard depth cap in plies, 1..42 (use 42 for "no cap")
     cpu_speed  10..100, the engine's duty cycle in percent
     max_nps    node rate ceiling, nodes per second (0 = no ceiling)
     noise      static evaluation blurring, 0..100 percent
     use_book   non-zero to allow the built-in opening book
   Returns 1 when a move was found, 0 otherwise (mate, stalemate, aborted or
   no legal move); best_uci then holds "0000". nodes/spent_ms may be NULL. */
int ct800_glue_think(int32_t time_ms, int max_depth, int cpu_speed, uint64_t max_nps,
                     int32_t noise, int use_book, char best_uci[6], uint64_t *nodes,
                     int64_t *spent_ms);

/* Asks the running search to stop. Safe from another thread: it only stores a
   flag that the search polls (and that wakes its throttle sleeps). The flag is
   cleared at the start of every think(). */
void ct800_glue_abort(void);

/* Non-zero while a think() call is inside the engine. */
int ct800_glue_busy(void);

/* Drains the engine's text output (its UCI "info" lines) into `out`, at most
   `out_size` bytes including the NUL, and returns how many characters were
   written. The engine never prints on the device: Play_Print only fills a
   small ring buffer, and the oldest characters are dropped when it is full. */
int ct800_glue_log_drain(char *out, int out_size);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* ARROCCO_CT800_GLUE_H */
