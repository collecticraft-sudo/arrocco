/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Arrocco - CT800 port: where the pawn hash tables live.
 *
 * Force-included (-include ct800_memory.h) into EVERY CT800 translation unit,
 * upstream's and ours alike, before anything else. It exists for one reason:
 * the XIAO ESP32-S3 has about 320 KB of internal DRAM, and the frame buffer
 * (48 KB), the Game (25 KB), CT800's move stack (27 KB) and Arduino's own
 * statics already fill most of it. CT800's pawn hash tables are another 120 KB
 * of .bss, which does not fit - and this Arduino build has no .ext_ram.bss
 * section, so no attribute can push them into PSRAM.
 *
 * upstream/ is read-only, so the two arrays are turned into pointers to arrays
 * from the outside instead:
 *
 *     eval.c:  extern TT_PTT_ST P_T_T[PMAX_TT+1];
 *     becomes  extern TT_PTT_ST (*ct800_pawn_hash)[PMAX_TT+1];
 *
 *     eval.c:  &P_T_T[Indx]
 *     becomes  &(*ct800_pawn_hash)[Indx]
 *
 * Every use in the engine is an index, an address-of or a memset, and all three
 * mean exactly the same thing through the pointer. The array bound survives, so
 * sizeof still gives the table size. ct800_glue_init() then carves the tables
 * out of the caller's memory block together with the transposition tables, and
 * on the device that block comes from PSRAM.
 *
 * The host build does the same, so both have ONE code path and the simulator
 * exercises what the firmware runs.
 */
#ifndef ARROCCO_CT800_MEMORY_H
#define ARROCCO_CT800_MEMORY_H

#define P_T_T (*ct800_pawn_hash)
#define P_T_T_Rooks (*ct800_rook_hash)

#endif /* ARROCCO_CT800_MEMORY_H */
