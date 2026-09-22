/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2015-2024, Rasmus Althoff <info@ct800.net>
 *  Copyright (C) 2010-2014, George Georgopoulos
 *
 *  This file is part of CT800/NGPlay (evaluation).
 *
 *  CT800/NGPlay is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  CT800/NGPlay is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with CT800/NGPlay. If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 *
 * general note for this module: some of the functions are pretty long,
 * several hundred lines. and this is already AFTER some refactoring..!
 *
 * however, they are pretty straight forward, so it's comprehensible.
 *
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "ctdefs.h"
#include "util.h"
#include "kpk.h"

/*---------- external variables ----------*/
/*-- READ-ONLY --*/
extern int wking, bking;
extern PIECE *board[120];
extern int game_started_from_0;
extern int32_t start_moves;
extern GAME_INFO game_info;
extern enum E_COLOUR computer_side;
extern unsigned int gflags;
extern int mv_stack_p;
extern MVST move_stack[MAX_STACK+1];
extern const int8_t boardXY[120];
extern const int8_t RowNum[120];
extern const int8_t ColNum[120];
extern const int8_t board64[64];

/*UCI option*/
extern int32_t eval_noise;

/*-- READ-WRITE--*/
extern PIECE Wpieces[16];
extern PIECE Bpieces[16];
extern TT_PTT_ST P_T_T[PMAX_TT+1];
extern TT_PTT_ROOK_ST P_T_T_Rooks[PMAX_TT+1];

/*---------- module global variables ----------*/

/*for the logic when and what to trade.
  they get set up in Eval_Setup_Initial_Material() which is called before the
  computer starts calculating its response move, so they reflect the material
  situation on the board.
  that will get evaluated in Eval_Static_Evaluation() during the search tree
  to examine what exchanges have been taking place within the search tree.
  every exchange that has occurred between the current board position and the
  leaves of the search tree will be considered.*/
static int start_material, start_qdiff, start_rdiff, start_mdiff, start_pdiff,
           start_piece_diff, start_pieces, start_pawns;

static const int8_t Central[120] = {
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,0,0,0,0,
    0,0,0,0,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0
};

static const int8_t PartCen[120] = {
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,1,1,1,1,0,0,0,
    0,0,0,1,0,0,1,0,0,0,
    0,0,0,1,0,0,1,0,0,0,
    0,0,0,1,1,1,1,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0
};

#ifndef AUTOTUNE
static
#endif
const int8_t WhiteSq[120] = {
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,0,
    0,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,0,
    0,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,0,
    0,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,0,
    0,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,0,
    0,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,0,
    0,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,0,
    0,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,LIGHT_SQ,DARK_SQ,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0
};

static const int8_t PartEdg[120] = {
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,1,1,1,1,1,1,0,0,
    0,0,1,0,0,0,0,1,0,0,
    0,0,1,0,0,0,0,1,0,0,
    0,0,1,0,0,0,0,1,0,0,
    0,0,1,0,0,0,0,1,0,0,
    0,0,1,1,1,1,1,1,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0
};

static const int8_t CentraliseE[120] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0,0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,0,
    0,-6,-4,-2,-2,-2,-2,-4,-6,0,
    0,-4,-2, 0, 0, 0, 0,-2,-4,0,
    0,-2, 0, 2, 2, 2, 2, 0,-2,0,
    0,-2, 0, 2, 4, 4, 2, 0,-2,0,
    0,-2, 0, 2, 4, 4, 2, 0,-2,0,
    0,-2, 0, 2, 2, 2, 2, 0,-2,0,
    0,-4,-2, 0, 0, 0, 0,-2,-4,0,
    0,-6,-4,-2,-2,-2,-2,-4,-6,0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,0
};

#ifndef AUTOTUNE
static
#endif
const int8_t FlipXY[120] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0, A8, B8, C8, D8, E8, F8, G8, H8, 0,
    0, A7, B7, C7, D7, E7, F7, G7, H7, 0,
    0, A6, B6, C6, D6, E6, F6, G6, H6, 0,
    0, A5, B5, C5, D5, E5, F5, G5, H5, 0,
    0, A4, B4, C4, D4, E4, F4, G4, H4, 0,
    0, A3, B3, C3, D3, E3, F3, G3, H3, 0,
    0, A2, B2, C2, D2, E2, F2, G2, H2, 0,
    0, A1, B1, C1, D1, E1, F1, G1, H1, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
};

/*auto tuned values: using the in-built auto tuner from zero.
  EPD: lichess-big3-resolved.book (Lichess)

  start with 100, 370, 380, 600, 1200.
  tunese 30 5 lichess-big3-resolved.book
  tunese 10 3 lichess-big3-resolved.book
  tunese 40 1 lichess-big3-resolved.book

  PSTs capped to -127 ... 127 automatically during tuning.
*/

#ifndef AUTOTUNE
static const
#endif
int material_pst[5][2] = /*MG/EG*/
#ifndef AUTOTUNE
{{85, 110}, {377, 385}, {391, 408}, {513, 733}, {1140, 1324}};
#else
{{TUNE_PAWN_V, TUNE_PAWN_V}, {TUNE_KNIGHT_V, TUNE_KNIGHT_V}, {TUNE_BISHOP_V, TUNE_BISHOP_V}, {TUNE_ROOK_V, TUNE_ROOK_V}, {TUNE_QUEEN_V, TUNE_QUEEN_V}};
#endif

#ifndef AUTOTUNE
#define PAWN_EG_V   110
#define KNIGHT_EG_V 385
#define BISHOP_EG_V 408
#define ROOK_EG_V   733
#define QUEEN_EG_V 1324
#else
#define PAWN_EG_V   material_pst[0][EG_PST]
#define KNIGHT_EG_V material_pst[1][EG_PST]
#define BISHOP_EG_V material_pst[2][EG_PST]
#define ROOK_EG_V   material_pst[3][EG_PST]
#define QUEEN_EG_V  material_pst[4][EG_PST]
#endif

/*bonus for bishop pair, with amount of pawns (0-16) as index*/
#ifndef AUTOTUNE
static const int8_t
#else
int
#endif
bishop_bonus[17] =
#ifndef AUTOTUNE
/*corrected 0, 69, 41 to 0, 8, 17.*/
{0, 8, 17, 34, 52, 65, 55, 65, 53, 53, 46, 43, 38, 35, 33, 26, 27};
#else
{0}; /*autotune from scratch*/
#endif

#ifndef AUTOTUNE
static const int8_t
#else
int
#endif
psqt[6][120][2] =
#ifndef AUTOTUNE
/*E1 for king-MG manually corrected from -2 to -10. The -2 is due to training
  positions that are won or drawn where the castling had not been done yet.
  The training works via correlation, but that is not causation.*/
{
    { /*pawns MG/EG*/
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, { -19,  -6}, { -18,   1}, { -18,   5}, {  -1,  21}, {  -3,  33}, {  -7,  10}, { -20,  -5}, { -18, -12}, {   0,   0},
        {   0,   0}, { -16,  -8}, { -16,  -2}, {  -9,  -2}, {   7,  11}, {  12,  13}, {  -2,  -2}, {  -8,  -7}, {  -1, -15}, {   0,   0},
        {   0,   0}, { -17,   0}, {  -8,   1}, {  -1,   1}, {  19,  -1}, {  13,   1}, {  15,  -6}, {  13, -10}, {  -1, -11}, {   0,   0},
        {   0,   0}, {  -6,  19}, {   7,   9}, {  15,   8}, {   9,  -5}, {  29,  -6}, {  39,  -9}, {  23,   1}, {  12,   2}, {   0,   0},
        {   0,   0}, {   1,  62}, {  17,  63}, {  48,  36}, {  49,   1}, {  61, -12}, {  74,   5}, {  46,  40}, {  11,  42}, {   0,   0},
        {   0,   0}, {  46,  84}, {  81,  56}, {  71,  78}, { 110,  15}, {  71,  10}, {  53,  21}, { -44,  69}, { -82,  92}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}
    },
    { /*knights MG/EG*/
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, { -95,  -4}, { -37, -26}, { -43,   2}, { -23,   6}, { -19,   5}, { -10,  -6}, { -38, -16}, { -65,  -2}, {   0,   0},
        {   0,   0}, { -46, -10}, { -34,   8}, { -18,  14}, {   0,  10}, {   0,  10}, {  -2,  12}, { -11,  -1}, { -16,   7}, {   0,   0},
        {   0,   0}, { -39,  -1}, { -14,  14}, {  -2,  16}, {   8,  38}, {  21,  33}, {   8,  12}, {  11,   4}, { -20,   2}, {   0,   0},
        {   0,   0}, { -19,  18}, {  -7,  27}, {  14,  42}, {   9,  40}, {  21,  45}, {  21,  33}, {  20,  26}, {  -3,   8}, {   0,   0},
        {   0,   0}, {  -3,  17}, {   6,  35}, {  31,  43}, {  44,  45}, {  25,  42}, {  66,  39}, {  19,  34}, {  34,  10}, {   0,   0},
        {   0,   0}, { -13,  11}, {  29,  19}, {  49,  29}, {  60,  29}, { 107,  12}, { 110,   2}, {  61,  10}, {  39, -11}, {   0,   0},
        {   0,   0}, { -34,   2}, {  -5,  15}, {  18,  16}, {  42,  15}, {  24,   6}, { 100, -12}, {  -5,  15}, {  21, -21}, {   0,   0},
        {   0,   0}, {-127, -98}, {-124,   1}, { -73,  14}, { -30,   2}, {   7,   8}, { -87,  -4}, {-103,   1}, {-123, -85}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}
    },
    { /*bishops MG/EG*/
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, { -15, -12}, {   9,   4}, {  -8, -10}, { -15,   7}, {  -6,   3}, { -19,  10}, {  -2,  -6}, {  -7, -25}, {   0,   0},
        {   0,   0}, {   2,   3}, {   7,  -3}, {  16,  -4}, {  -4,  12}, {   7,   8}, {  11,   8}, {  10,   4}, {   5, -14}, {   0,   0},
        {   0,   0}, {  -3,   5}, {   6,  18}, {  11,  24}, {   6,  23}, {   7,  30}, {   8,  18}, {  10,   9}, {   2,   0}, {   0,   0},
        {   0,   0}, { -15,  11}, {  -6,  24}, {   0,  29}, {  25,  27}, {  15,  23}, {   1,  24}, {  -4,  19}, {   0,  -2}, {   0,   0},
        {   0,   0}, { -18,  19}, {   0,  27}, {  23,  23}, {  25,  34}, {  24,  25}, {  25,  27}, {   3,  21}, { -12,  18}, {   0,   0},
        {   0,   0}, { -15,  27}, {  17,  17}, {  10,  23}, {  41,   9}, {  30,  13}, {  67,  17}, {  37,  13}, {  29,  13}, {   0,   0},
        {   0,   0}, { -18,  11}, {  -2,  16}, {  -2,  16}, { -25,  21}, {  17,   4}, {  12,  13}, {  -3,  18}, {   2,   1}, {   0,   0},
        {   0,   0}, { -47,  15}, { -63,  22}, { -60,  20}, {-100,  30}, { -77,  23}, { -80,  18}, { -21,  13}, { -66,   8}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}
    },
    { /*rooks MG/EG*/
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, { -17,  -3}, { -14,  -2}, {  -5,   6}, {   3,  -3}, {   9,  -8}, {   4,  -7}, {  13, -16}, { -13, -12}, {   0,   0},
        {   0,   0}, { -36,  -4}, { -30,   3}, { -12,   3}, { -15,   1}, {  -6,  -7}, {   6, -15}, {  35, -32}, {  -7, -16}, {   0,   0},
        {   0,   0}, { -36,   5}, { -31,   5}, { -23,   4}, { -23,   6}, { -12,   0}, {  -4, -11}, {  31, -32}, {   1, -23}, {   0,   0},
        {   0,   0}, { -33,  12}, { -40,  20}, { -23,  17}, { -24,  13}, { -16,   6}, { -17,   5}, {  12,  -7}, {  -5,  -8}, {   0,   0},
        {   0,   0}, { -26,  22}, { -14,  17}, { -12,  25}, { -16,  22}, { -14,   7}, {  17,  -2}, {  27,  -3}, {  24,  -9}, {   0,   0},
        {   0,   0}, { -13,  18}, {   3,  22}, {   6,  21}, {   7,  16}, {  50,   1}, {  47,  -2}, { 100, -11}, {  71, -17}, {   0,   0},
        {   0,   0}, {   7,  18}, {   9,  32}, {  29,  33}, {  44,  23}, {  30,  24}, {  76,   9}, {  73,   4}, {  94, -10}, {   0,   0},
        {   0,   0}, {  26,  15}, {  12,  24}, {  16,  31}, {  12,  29}, {  36,  19}, {  64,  11}, {  49,  15}, {  69,   6}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}
    },
    { /*queens MG/EG*/
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, { -16, -29}, { -12, -35}, {  -3, -32}, {   6, -25}, {   2, -34}, { -13, -32}, {  -2, -45}, {  -7, -45}, {   0,   0},
        {   0,   0}, {  -8, -29}, {  -5, -27}, {   4, -23}, {   3, -12}, {   4, -12}, {   9, -26}, {  21, -51}, {  25, -80}, {   0,   0},
        {   0,   0}, { -15, -16}, {  -8,   0}, { -12,  19}, { -17,  18}, { -13,  27}, {  -4,  22}, {  11,   2}, {   6,  -7}, {   0,   0},
        {   0,   0}, { -17,  -4}, { -27,  27}, { -22,  31}, { -24,  53}, { -22,  51}, { -15,  42}, {  -5,  31}, {   6,  18}, {   0,   0},
        {   0,   0}, { -29,   5}, { -25,  22}, { -22,  36}, { -36,  67}, { -30,  77}, {  -6,  65}, {  -3,  59}, {   8,  35}, {   0,   0},
        {   0,   0}, { -18,  -1}, { -23,  14}, { -23,  52}, {  -8,  59}, {   3,  76}, {  47,  57}, {  59,  16}, {  55,  13}, {   0,   0},
        {   0,   0}, { -29,   9}, { -40,  36}, { -39,  76}, { -47,  95}, { -38, 108}, {  18,  54}, {   3,  51}, {  53,  28}, {   0,   0},
        {   0,   0}, { -54,  31}, { -52,  43}, { -15,  53}, {  11,  48}, {  11,  48}, {  41,  35}, {  54,  -4}, {   0,  28}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}
    },
    { /*kings MG/EG*/
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   6, -42}, {  18, -12}, {  -5,   5}, { -77,  15}, { -10, -13}, { -41,  13}, {  17,  -9}, {  23, -45}, {   0,   0},
        {   0,   0}, {  36,  -4}, {   4,   2}, { -32,  18}, { -51,  26}, { -51,  29}, { -53,  26}, {  11,   1}, {  11,   4}, {   0,   0},
        {   0,   0}, { -44,  10}, { -31,  19}, { -85,  18}, { -99,  32}, { -92,  32}, { -97,  21}, { -59,  26}, { -83,  34}, {   0,   0},
        {   0,   0}, { -98,  20}, {-108,  42}, {-127,  42}, {-127,  38}, {-127,  36}, {-127,  44}, {-127,  50}, {-127,  46}, {   0,   0},
        {   0,   0}, {-101,  39}, {-119,  62}, {-127,  58}, {-127,  47}, {-127,  47}, {-127,  65}, {-127,  78}, {-127,  60}, {   0,   0},
        {   0,   0}, {-127,  53}, { -33,  62}, {-117,  58}, {-127,  70}, {-127,  76}, { -55,  72}, { -49,  89}, { -43,  57}, {   0,   0},
        {   0,   0}, { -80,  31}, { -49,  48}, { -66,  53}, {   0,  44}, { -10,  57}, { -24,  77}, { -12,  73}, {  -4,  50}, {   0,   0},
        {   0,   0}, {   0, -72}, {   1,  -9}, {  17,   3}, {  -5,  28}, {  -3,  21}, {   6,  31}, {  24,  32}, {  21, -62}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0},
        {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}, {   0,   0}
    }
};
#else
{0}; /*autotune from scratch*/
#endif

/*the index of the following variable is interpreted as 8-bit unsigned int
  where "bit set" means "pawn on this file", the a-file being the least
  significant bit.
  let's say we have a pawn on the a-file and one on the c-file, that is binary
  0b00000101, decimal 5. These are clearly two isolated pawns, that's why
  IsolaniTable[5] == 2.*/
static const int8_t IsolaniTable[256] = {
    0, 1, 1, 0, 1, 2, 0, 0,
    1, 2, 2, 1, 0, 1, 0, 0,
    1, 2, 2, 1, 2, 3, 1, 1,
    0, 1, 1, 0, 0, 1, 0, 0,
    1, 2, 2, 1, 2, 3, 1, 1,
    2, 3, 3, 2, 1, 2, 1, 1,
    0, 1, 1, 2, 1, 2, 0, 0,
    0, 1, 1, 0, 0, 1, 0, 0,
    1, 2, 2, 1, 2, 3, 1, 1,
    2, 3, 3, 2, 1, 2, 1, 1,
    2, 3, 3, 2, 3, 4, 2, 2,
    1, 2, 2, 1, 1, 2, 1, 1,
    0, 1, 1, 0, 1, 2, 0, 0,
    1, 2, 2, 1, 0, 1, 0, 0,
    0, 1, 1, 0, 1, 2, 0, 0,
    0, 1, 1, 0, 0, 1, 0, 0,
    1, 2, 2, 1, 2, 3, 1, 1,
    2, 3, 3, 2, 1, 2, 1, 1,
    2, 3, 3, 2, 3, 4, 2, 2,
    1, 2, 2, 1, 1, 2, 1, 1,
    2, 3, 3, 2, 3, 4, 2, 2,
    3, 4, 4, 3, 2, 3, 2, 2,
    1, 2, 2, 1, 2, 3, 1, 1,
    1, 2, 2, 1, 1, 2, 1, 1,
    0, 1, 1, 0, 1, 2, 0, 0,
    1, 2, 2, 1, 0, 1, 0, 0,
    1, 2, 2, 1, 2, 3, 1, 1,
    0, 1, 1, 0, 0, 1, 0, 0,
    0, 1, 1, 0, 1, 2, 0, 0,
    1, 2, 2, 1, 0, 1, 0, 0,
    0, 1, 1, 0, 1, 2, 0, 0,
    0, 1, 1, 0, 0, 1, 0, 0
};

/*for connected passed pawns. since that doesn't happen that often, and since
  a pre-check for more than two passed pawns is done anyway, better save some
  RAM and put the table to the ROM. that table counts how many
  connected/consecutive bite are in the index, maximum - so 0b11011 counts as
  2, not as 4. The important information is whether a passed pawn
  configuration has two or more neighbouring bits or not.*/
static const int8_t ConnectedTable[256] = {
    0, 0, 0, 2, 0, 0, 2, 3,
    0, 0, 0, 2, 2, 2, 3, 4,
    0, 0, 0, 2, 0, 0, 2, 3,
    2, 2, 2, 2, 3, 3, 4, 5,
    0, 0, 0, 2, 0, 0, 2, 3,
    0, 0, 0, 2, 2, 2, 3, 4,
    2, 2, 2, 2, 2, 2, 2, 3,
    3, 3, 3, 3, 4, 4, 5, 6,
    0, 0, 0, 2, 0, 0, 2, 3,
    0, 0, 0, 2, 2, 2, 3, 4,
    0, 0, 0, 2, 0, 0, 2, 3,
    2, 2, 2, 2, 3, 3, 4, 5,
    2, 2, 2, 2, 2, 2, 2, 3,
    2, 2, 2, 2, 2, 2, 3, 4,
    3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 6, 7,
    0, 0, 0, 2, 0, 0, 2, 3,
    0, 0, 0, 2, 2, 2, 3, 4,
    0, 0, 0, 2, 0, 0, 2, 3,
    2, 2, 2, 2, 3, 3, 4, 5,
    0, 0, 0, 2, 0, 0, 2, 3,
    0, 0, 0, 2, 2, 2, 3, 4,
    2, 2, 2, 2, 2, 2, 2, 3,
    3, 3, 3, 3, 4, 4, 5, 6,
    2, 2, 2, 2, 2, 2, 2, 3,
    2, 2, 2, 2, 2, 2, 3, 4,
    2, 2, 2, 2, 2, 2, 2, 3,
    2, 2, 2, 2, 3, 3, 4, 5,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 6, 6, 7, 8
};

/*now there are some tables which are moved into the slower ROM despite the
  waitstate slowdown. this is acceptable because these tables are only used
  rarely, so that it doesn't really matter.*/

/*value (for the OPPOSITE party!) of the king position in K+B vs. K+R if K+B
  has the dark square bishop attention, the a file is at the left, but the 1st
  rank is at the top of the table.*/
static const int LightBishopRook[120] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0,
    0, -60, -50, -40,   0,  50, 160, 240, 280, 0,
    0, -50, -70, -70, -30,   0,   0, 150, 240, 0,
    0, -40, -70, -80, -80, -40, -20,   0, 160, 0,
    0, - 0, -30, -80,-100,-100, -40,   0,  50, 0,
    0,  50,   0, -40,-100,-100, -80, -30,   0, 0,
    0, 160,   0, -20, -40, -80, -80, -70, -40, 0,
    0, 240, 150,   0,   0, -30, -70, -70, -50, 0,
    0, 280, 240, 160,  50,   0, -40, -50, -60, 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0
};

/*value (for the OPPOSITE party!) of the king position in K+B vs. K+R if K+B
  has the light square bishop attention, the a file is at the left, but the
  1st rank is at the top of the table.*/
static const int DarkBishopRook[120] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0,
    0, 280, 240, 160,  50,   0, -40, -50, -60, 0,
    0, 240, 150,   0,   0, -30, -70, -70, -50, 0,
    0, 160,   0, -20, -40, -80, -80, -70, -40, 0,
    0,  50,   0, -40,-100,-100, -80, -30,   0, 0,
    0,   0, -30, -80,-100,-100, -40,   0,  50, 0,
    0, -40, -70, -80, -80, -40, -20,   0, 160, 0,
    0, -50, -70, -70, -30,   0,   0, 150, 240, 0,
    0, -60, -50, -40,   0,  50, 160, 240, 280, 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0
};

/*value (for the OPPOSITE party!) of king position in K+N va. K+R*/
static const int KnightRook[120] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0,
    0, 280, 260, 200, 200, 200, 200, 260, 280, 0,
    0, 260,   0,   0,   0,   0,   0,   0, 260, 0,
    0, 200,   0, -80, -80, -80, -80,   0, 200, 0,
    0, 200,   0, -80,-100,-100, -80,   0, 200, 0,
    0, 200,   0, -80,-100,-100, -80,   0, 200, 0,
    0, 200,   0, -80, -80, -80, -80,   0, 200, 0,
    0, 260,   0,   0,   0,   0,   0,   0, 260, 0,
    0, 280, 260, 200, 200, 200, 200, 260, 280, 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0, 0
};

/*for KNB-K*/
static const int8_t KingKnightDarkBishop[120] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0, 80, 70, 60, 50, 40, 30, 20, 10, 0,
    0, 70, 55, 45, 35, 25, 15,  5, 20, 0,
    0, 60, 45, 20, 10,  0,-10, 15, 30, 0,
    0, 50, 35, 10,-20,-30,  0, 25, 40, 0,
    0, 40, 25,  0,-30,-20, 10, 35, 50, 0,
    0, 30, 15,-10,  0, 10, 20, 45, 60, 0,
    0, 20,  5, 15, 25, 35, 45, 55, 70, 0,
    0, 10, 20, 30, 40, 50, 60, 70, 80, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0
};

/*
triangle sections:
    0, 50, 40, 30, 20, 10,  0,-10,-20, 0,
    0, 40, 30, 20, 10,  0,-10,-20,-10, 0,
    0, 30, 20, 10,  0,-10,-20,-10,  0, 0,
    0, 20, 10,  0,-10,-20,-10,  0, 10, 0,
    0, 10,  0,-10,-20,-10,  0, 10, 20, 0,
    0,  0,-10,-20,-10,  0, 10, 20, 30, 0,
    0,-10,-20,-10,  0, 10, 20, 30, 40, 0,
    0,-20,-10,  0, 10, 20, 30, 40, 50, 0,

centre overlay:
    0, 30, 30, 30, 30, 30, 30, 30, 30, 0,
    0, 30, 25, 25, 25, 25, 25, 25, 30, 0,
    0, 30, 25, 10, 10, 10, 10, 25, 30, 0,
    0, 30, 25, 10,-10,-10, 10, 25, 30, 0,
    0, 30, 25, 10,-10,-10, 10, 25, 30, 0,
    0, 30, 25, 10, 10, 10, 10, 25, 30, 0,
    0, 30, 25, 25, 25, 25, 25, 25, 30, 0,
    0, 30, 30, 30, 30, 30, 30, 30, 30, 0,
*/

/*for KNB-K*/
static const int8_t KingKnightLightBishop[120] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0, 10, 20, 30, 40, 50, 60, 70, 80, 0,
    0, 20,  5, 15, 25, 35, 45, 55, 70, 0,
    0, 30, 15,-10,  0, 10, 20, 45, 60, 0,
    0, 40, 25,  0,-30,-20, 10, 35, 50, 0,
    0, 50, 35, 10,-20,-30,  0, 25, 40, 0,
    0, 60, 45, 20, 10,  0,-10, 15, 30, 0,
    0, 70, 55, 45, 35, 25, 15,  5, 20, 0,
    0, 80, 70, 60, 50, 40, 30, 20, 10, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0
};

/*for KNB-K*/
static const int8_t KNBAttEdge[120] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,-50,-30,-30,-30,-30,-30,-30,-50, 0,
    0,-30,  0,  0,  0,  0,  0,  0,-30, 0,
    0,-30,  0,  0,  0,  0,  0,  0,-30, 0,
    0,-30,  0,  0,  0,  0,  0,  0,-30, 0,
    0,-30,  0,  0,  0,  0,  0,  0,-30, 0,
    0,-30,  0,  0,  0,  0,  0,  0,-30, 0,
    0,-30,  0,  0,  0,  0,  0,  0,-30, 0,
    0,-50,-30,-30,-30,-30,-30,-30,-50, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0
};

/*for KR-K*/
static const int8_t CentreManhattanDist[120] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0, 60, 50, 40, 30, 30, 40, 50, 60, 0,
    0, 50, 40, 30, 20, 20, 30, 40, 50, 0,
    0, 40, 30, 20, 10, 10, 20, 30, 40, 0,
    0, 30, 20, 10,  0,  0, 10, 20, 30, 0,
    0, 30, 20, 10,  0,  0, 10, 20, 30, 0,
    0, 40, 30, 20, 10, 10, 20, 30, 40, 0,
    0, 50, 40, 30, 20, 20, 30, 40, 50, 0,
    0, 60, 50, 40, 30, 30, 40, 50, 60, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0
};

/*for KQ-K and KQ-KR*/
static const int8_t CentreDist[120] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0, 50, 40, 30, 30, 30, 30, 40, 50, 0,
    0, 40, 30, 20, 20, 20, 20, 30, 40, 0,
    0, 30, 20, 10, 10, 10, 10, 20, 30, 0,
    0, 30, 20, 10,  0,  0, 10, 20, 30, 0,
    0, 30, 20, 10,  0,  0, 10, 20, 30, 0,
    0, 30, 20, 10, 10, 10, 10, 20, 30, 0,
    0, 40, 30, 20, 20, 20, 20, 30, 40, 0,
    0, 50, 40, 30, 30, 30, 30, 40, 50, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0
};

/*---------- local functions ----------*/

static int Eval_KingRook_King(int def_king, int att_king, int rook)
{
    int ret, _abs_diff, col_dist, row_dist;

    /*bring the attacking king close to the losing king*/
    _abs_diff = ColNum[def_king] - ColNum[att_king];
    ret = Abs(_abs_diff);
    _abs_diff = RowNum[def_king] - RowNum[att_king];
    ret += Abs(_abs_diff);

    /*distance between the kings is a malus for the attacking side*/
    ret *= (-2);

    /*put the rook far away either in terms of rows or of columns*/
    _abs_diff = ColNum[def_king] - ColNum[rook];
    col_dist = Abs(_abs_diff);
    _abs_diff = RowNum[def_king] - RowNum[rook];
    row_dist = Abs(_abs_diff);
    if (col_dist > row_dist)
        ret += col_dist;
    else
        ret += row_dist;

    /*force the losing king to the edge*/
    ret += CentreManhattanDist[def_king];

    ret += (ROOK_EG_V + 2*PAWN_EG_V);

    return(ret);
}

static int Eval_KingQueen_King(int def_king, int att_king)
{
    int ret, _abs_diff;

    /*stalemate against lone king is already checked in quiescence*/

    /*bring the attacking king close to the losing king*/
    _abs_diff = ColNum[def_king] - ColNum[att_king];
    ret = Abs(_abs_diff);
    _abs_diff = RowNum[def_king] - RowNum[att_king];
    ret += Abs(_abs_diff);

    /*distance between the kings is a malus for the attacking side*/
    ret *= (-2);

    /*force the losing king to the edge*/
    ret += CentreDist[def_king];

    ret += (QUEEN_EG_V + 2*PAWN_EG_V);

    return(ret);
}

static int Eval_KingKnightBishop_King(int def_king, int att_king, int bishop_colour, int knight)
{
    int ret, diff, dist;

    /*knight and attacking king away from the rim*/
    ret = KNBAttEdge[knight] + KNBAttEdge[att_king];

    /*attacking king to defending king*/
    dist = ColNum[att_king] - ColNum[def_king];
    dist = Abs(dist);
    diff = RowNum[att_king] - RowNum[def_king];
    diff = Abs(diff);
    ret -= 2*(dist + diff);

    /*defending king to the mate edge*/
    if (bishop_colour == DARK_SQ)
        ret += KingKnightDarkBishop[def_king];
    else
        ret += KingKnightLightBishop[def_king];

    return(ret);
}

static int Eval_White_King_Safety(int w_bishop_colour, int b_bishop_colour, int nof_queens)
{
    int xy = ColNum[wking];
    int res = 0;

    if (xy >= BOARD_F_FILE)
    {
        unsigned int hole1 = 1; /*escape hole on H2*/
        unsigned int hole2 = 1; /*escape hole on G2*/

        if (board[F2]->type == WPAWN)
            res += 8;
        if (board[G2]->type == WPAWN)
        {
            res += 12;
            hole2 = 0;
        } else
        {
            if (board[G3]->type == WPAWN)
                res += 4;
            if (board[G2]->type == WBISHOP)
                res += 8;
            else if (!(w_bishop_colour & LIGHT_SQ))
            {/* fianchetto without bishop*/
                res -= 15;
                if (b_bishop_colour & LIGHT_SQ)
                    res -= 15;
            }
            if (board[F3]->type == BPAWN)
            {
                res -= 30;
                hole2 = 0;
            } else if (board[H3]->type == BPAWN)
            {
                res -= 15;
                hole2 = 0;
            }
        }
        if (board[H2]->type == WPAWN)
        {
            res += 10;
            hole1 = 0;
        } else
        {
            if (board[H3]->type == WPAWN)
                res += 4;

            if (board[G3]->type == BPAWN)
            {
                hole1 = 0;
                res -= 20;
            }
        }
        /*reward only exactly one hole; two holes weaken the position.*/
        res += (int)( (hole1 ^ hole2) << 3 );
        if (nof_queens == 0)
            res /= 2; /* Without queens , King danger is half */
        if (((board[H1]->type == WROOK) || (board[H2]->type == WROOK)) && (RowNum[wking] == 1))
            res -= 40;
    } else if (xy <= BOARD_C_FILE)
    {
        unsigned int hole1 = 1; /*escape hole on A2*/
        unsigned int hole2 = 1; /*escape hole on B2*/

        if (board[C2]->type == WPAWN)
            res += 8;
        if (board[B2]->type == WPAWN)
        {
            res += 12;
            hole2 = 0;
        } else
        {
            if (board[B3]->type == WPAWN)
                res += 4;
            if (board[B2]->type == WBISHOP)
                res += 8;
            else if (!(w_bishop_colour & DARK_SQ))
            {/* fianchetto without bishop*/
                res -= 15;
                if (b_bishop_colour & DARK_SQ)
                    res -= 15;
            }
            if (board[C3]->type == BPAWN)
            {
                res -= 30;
                hole2 = 0;
            } else if (board[A3]->type == BPAWN)
            {
                res -= 15;
                hole2 = 0;
            }
        }
        if (board[A2]->type == WPAWN)
        {
            res += 10;
            hole1 = 0;
        } else
        {
            if (board[A3]->type == WPAWN)
                res += 4;
            if (board[B3]->type == BPAWN)
            {
                hole1 = 0;
                res -= 20;
            }
        }
        /*reward only exactly one hole; two holes weaken the position.*/
        res += (int)( (hole1 ^ hole2) << 3 );
        if (nof_queens == 0)
            res /= 2; /* Without queens , King danger is half */
        if (((board[A1]->type == WROOK) || (board[A2]->type == WROOK)) && (RowNum[wking] == 1))
            res -= 40;
    }
    return(res);
}

static int Eval_Black_King_Safety(int b_bishop_colour, int w_bishop_colour, int nof_queens)
{
    int xy = ColNum[bking];
    int res = 0;

    if (xy >= BOARD_F_FILE)
    {
        unsigned int hole1 = 1; /*escape hole on H7*/
        unsigned int hole2 = 1; /*escape hole on G7*/

        if (board[F7]->type == BPAWN)
            res -= 8;
        if (board[G7]->type == BPAWN)
        {
            res -= 12;
            hole2 = 0;
        } else
        {
            if (board[G6]->type == BPAWN)
                res -= 4;
            if (board[G7]->type == BBISHOP)
                res -= 8;
            else if (!(b_bishop_colour & DARK_SQ))
            {/* fianchetto without bishop*/
                res += 15;
                if (w_bishop_colour & DARK_SQ)
                    res += 15;
            }
            if (board[F6]->type == WPAWN)
            {
                res += 30;
                hole2 = 0;
            } else if (board[H6]->type == WPAWN)
            {
                res += 15;
                hole2 = 0;
            }
        }
        if (board[H7]->type == BPAWN)
        {
            res -= 10;
            hole1 = 0;
        } else
        {
            if (board[H6]->type == BPAWN)
                res -= 4;
            if (board[G6]->type == WPAWN)
            {
                hole1 = 0;
                res += 20;
            }
        }
        /*reward only exactly one hole; two holes weaken the position.*/
        res -= (int)( (hole1 ^ hole2) << 3 );
        if (nof_queens == 0)
            res /= 2; /* Without queens , King danger is half */
        if (((board[H8]->type == BROOK) || (board[H7]->type == BROOK)) && (RowNum[bking] == 8))
            res += 40;
    } else if (xy <= BOARD_C_FILE)
    {
        unsigned int hole1 = 1; /*escape hole on A7*/
        unsigned int hole2 = 1; /*escape hole on B7*/

        if (board[C7]->type == BPAWN)
            res -= 8;
        if (board[B7]->type == BPAWN)
        {
            res -= 12;
            hole2 = 0;
        } else
        {
            if (board[B6]->type == BPAWN)
                res -= 4;
            if (board[B7]->type == BBISHOP)
                res -= 8;
            else if (!(b_bishop_colour & LIGHT_SQ))
            {/* fianchetto without bishop*/
                res += 15;
                if (w_bishop_colour & LIGHT_SQ)
                    res += 15;
            }
            if (board[C6]->type == WPAWN)
            {
                res += 30;
                hole2 = 0;
            } else if (board[A6]->type == WPAWN)
            {
                res += 15;
                hole2 = 0;
            }
        }
        if (board[A7]->type == BPAWN)
        {
            res -= 10;
            hole1 = 0;
        } else
        {
            if (board[A6]->type == BPAWN)
                res -= 4;
            if (board[B6]->type == WPAWN)
            {
                hole1 = 0;
                res += 20;
            }
        }
        /*reward only exactly one hole; two holes weaken the position.*/
        res -= (int)( (hole1 ^ hole2) << 3 );
        if (nof_queens == 0)
            res /= 2; /* Without queens , King danger is half */
        if (((board[A8]->type == BROOK) || (board[A7]->type == BROOK)) && (RowNum[bking] == 8))
            res += 40;
    }
    return(res);
}

static void Eval_Pawn_Evaluation(PAWN_INFO *restrict pawn_info)
{
    unsigned int w_queen_pawns = 0, w_king_pawns = 0, b_queen_pawns = 0, b_king_pawns = 0;
    unsigned int w_min_rows[10], b_min_rows[10], w_max_rows[10], b_max_rows[10];
    unsigned int backward_files, xy_friend, bin_col;
    int i, xy, extra_pawn_val;

    extra_pawn_val = pawn_info->extra_pawn_val;

    /*basic idea with all the arrays: gather the data once, store them, and
      avoid various nested scan loops in later steps by reusing the data. that
      way, the pawn evaluation doesn't hamper the tactical abilities overly.
      plus that avoiding unnecessary  weaknesses gives stronger opponents
      fewer targets to aim at, which means fewer chances for a tactical shot.
      On the offensive side, enemy pawn weaknesses that can be exploited by
      rooks are marked as attractive targets (e.g. backward pawns on half open
      files).

      The idea is that attacking these weaknesses may not result in material
      gain, but it ties enemy pieces to the defence of weak pawns. That in
      turn means that the opponent has less pieces for attack available, which
      gives the computer more freedom to make the game. The other way round,
      that's why the computer tries to avoid having such weak spots itself, or
      at the very least trade in some form of compensation (like accepting a
      weak pawn in exchange for the enemy bishop pair).

      There isn't enough memory for storing everything, but the most important
      stuff is using the rooks somewhat cleverly. They are worth a lot of
      material points because they allow such attacks, so if we don't use them
      appropriately, this is a form of hidden material loss. So the data that
      are most important for the rook handling are stored in the pawn hash
      table. i.e. white pawn mask, black pawn mask, white preferred rook files
      and black preferred rook files.*/

    pawn_info->b_passed_rows[0] = 9;
    pawn_info->b_passed_rows[1] = 9;
    pawn_info->b_passed_rows[2] = 9;
    pawn_info->b_passed_rows[3] = 9;
    pawn_info->b_passed_rows[4] = 9;
    pawn_info->b_passed_rows[5] = 9;
    pawn_info->b_passed_rows[6] = 9;
    pawn_info->b_passed_rows[7] = 9;

    w_min_rows[0] = 9;
    w_min_rows[1] = 9;
    w_min_rows[2] = 9;
    w_min_rows[3] = 9;
    w_min_rows[4] = 9;
    w_min_rows[5] = 9;
    w_min_rows[6] = 9;
    w_min_rows[7] = 9;
    w_min_rows[8] = 9;
    w_min_rows[9] = 9;
    b_min_rows[0] = 0;
    b_min_rows[1] = 0;
    b_min_rows[2] = 0;
    b_min_rows[3] = 0;
    b_min_rows[4] = 0;
    b_min_rows[5] = 0;
    b_min_rows[6] = 0;
    b_min_rows[7] = 0;
    b_min_rows[8] = 0;
    b_min_rows[9] = 0;

    w_max_rows[0] = 0;
    w_max_rows[1] = 0;
    w_max_rows[2] = 0;
    w_max_rows[3] = 0;
    w_max_rows[4] = 0;
    w_max_rows[5] = 0;
    w_max_rows[6] = 0;
    w_max_rows[7] = 0;
    w_max_rows[8] = 0;
    w_max_rows[9] = 0;
    b_max_rows[0] = 9;
    b_max_rows[1] = 9;
    b_max_rows[2] = 9;
    b_max_rows[3] = 9;
    b_max_rows[4] = 9;
    b_max_rows[5] = 9;
    b_max_rows[6] = 9;
    b_max_rows[7] = 9;
    b_max_rows[8] = 9;
    b_max_rows[9] = 9;

    /*first the double pawns and isolani. note that this step also sets up
      the data for recognising devalued pawn majorities like in the Spanish
      Exchange Variation.*/

    for (i = 8; i < 16; i++)
    {
        /* evaluation for isolated white pawns, double white pawns and open files*/
        xy = Wpieces[i].xy;
        if ((xy != 0) && (Wpieces[i].type == WPAWN))
        {
            bin_col = ColNum[xy];
            xy_friend = RowNum[xy];
            if (w_min_rows[bin_col] > xy_friend) /*these data will be reused in the passed pawn and backward pawn search*/
                w_min_rows[bin_col] = xy_friend;
            if (w_max_rows[bin_col] < xy_friend) /*these data will be reused in the backward pawn search*/
                w_max_rows[bin_col] = xy_friend;

            bin_col = 1U << (bin_col - 1U);

            if (bin_col & QUEEN_SIDE)
                w_queen_pawns++;
            else
                w_king_pawns++;

            if (bin_col & pawn_info->w_pawn_mask)
                /*there is already a pawn on this file -> double pawn.
                 don't care about isolated double pawns as they will get
                 two times the isolani penalty anyway.*/
            {
                pawn_info->w_d_pawnmask |= bin_col;
                if (bin_col & FLANK_FILES)
                    extra_pawn_val -= 5;
                else if (bin_col & CENTRE_FILES)
                    extra_pawn_val -= 2;
                else /* edge files, that is ugly*/
                    extra_pawn_val -= 10;
            } else pawn_info->w_pawn_mask |= bin_col;
        }

        /* evaluation for isolated black pawns, double black pawns and open files*/
        xy = Bpieces[i].xy;
        if ((xy != 0) && (Bpieces[i].type == BPAWN))
        {
            bin_col = ColNum[xy];
            xy_friend = RowNum[xy];
            if (b_min_rows[bin_col] < xy_friend) /*these data will be reused in the passed pawn and backward pawn search*/
                b_min_rows[bin_col] = xy_friend;
            if (b_max_rows[bin_col] > xy_friend) /*these data will be reused in the backward pawn search*/
                b_max_rows[bin_col] = xy_friend;

            bin_col = 1U << (bin_col - 1U);

            if (bin_col & QUEEN_SIDE)
                b_queen_pawns++;
            else
                b_king_pawns++;

            if (bin_col & pawn_info->b_pawn_mask)
                /*there is already a pawn on this file -> double pawn.
                  don't care about isolated double pawns as they will get
                  two times the isolani penalty anyway.*/
            {
                pawn_info->b_d_pawnmask |= bin_col;
                if (bin_col & FLANK_FILES)
                    extra_pawn_val += 5;
                else if (bin_col & CENTRE_FILES)
                    extra_pawn_val += 2;
                else /* edge files, that is ugly*/
                    extra_pawn_val += 10;
            } else pawn_info->b_pawn_mask |= bin_col;
        }
    }

    pawn_info->w_isolani = IsolaniTable[pawn_info->w_pawn_mask];
    pawn_info->b_isolani = IsolaniTable[pawn_info->b_pawn_mask];

    /*what's left now are isolated double pawns - the table based isolani
      recognition has counted them only as one pawn until now. of course,
      that could be done in a loop, but that would incur some overhead.

      and double pawns in the centre files suck at least when the backward
      pawn is on the 2nd/7th rank since that hinders development and piece
      coordination. if that pawn cannot advance because the other pawn is
      right in front of it, that's even worse.*/

    if (pawn_info->w_d_pawnmask) /*are there white double pawns?*/
    {
        if (pawn_info->w_d_pawnmask & QUEEN_SIDE)
        {
            if (pawn_info->w_d_pawnmask & A_FILE) /*a file*/
                if ((pawn_info->w_pawn_mask & B_FILE)==0)
                {
                    pawn_info->w_isolani++;
                    extra_pawn_val -= 10;
                    if (A_FILE & (~pawn_info->b_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->b_rook_files |= A_FILE; /*mark it as target*/
                        extra_pawn_val -= 5; /*and give an extra penalty*/
                    }
                }
            if (pawn_info->w_d_pawnmask & B_FILE) /*b file*/
                if ((pawn_info->w_pawn_mask & (A_FILE | C_FILE))==0)
                {
                    pawn_info->w_isolani++;
                    extra_pawn_val -= 10;
                    if (B_FILE & (~pawn_info->b_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->b_rook_files |= B_FILE; /*mark it as target*/
                        extra_pawn_val -= 5; /*and give an extra penalty*/
                    }
                }
            if (pawn_info->w_d_pawnmask & C_FILE) /*c file*/
                if ((pawn_info->w_pawn_mask & (B_FILE | D_FILE))==0)
                {
                    pawn_info->w_isolani++;
                    extra_pawn_val -= 10;
                    if (C_FILE & (~pawn_info->b_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->b_rook_files |= C_FILE; /*mark it as target*/
                        extra_pawn_val -= 5; /*and give an extra penalty*/
                    }
                }
            if (pawn_info->w_d_pawnmask & D_FILE) /*d file*/
            {
                if ((pawn_info->w_pawn_mask & (C_FILE | E_FILE))==0)
                {
                    pawn_info->w_isolani++;
                    extra_pawn_val -= 10;
                    if (D_FILE & (~pawn_info->b_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->b_rook_files |= D_FILE; /*mark it as target*/
                        extra_pawn_val -= 5; /*and give an extra penalty*/
                    }
                }
                if (board[D2]->type == WPAWN)
                {
                    extra_pawn_val -= 5;
                    if (board[D3]->type == WPAWN)
                        extra_pawn_val -= 15;
                }
            }
        }
        if (pawn_info->w_d_pawnmask & KING_SIDE)
        {
            if (pawn_info->w_d_pawnmask & E_FILE) /*e file*/
            {
                if ((pawn_info->w_pawn_mask & (D_FILE | F_FILE))==0)
                {
                    pawn_info->w_isolani++;
                    extra_pawn_val -= 10;
                    if (E_FILE & (~pawn_info->b_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->b_rook_files |= E_FILE; /*mark it as target*/
                        extra_pawn_val -= 5; /*and give an extra penalty*/
                    }
                }
                if (board[E2]->type == WPAWN)
                {
                    extra_pawn_val -= 5;
                    if (board[E3]->type == WPAWN)
                        extra_pawn_val -= 15;
                }
            }
            if (pawn_info->w_d_pawnmask & F_FILE) /*f file*/
                if ((pawn_info->w_pawn_mask & (E_FILE | G_FILE))==0)
                {
                    pawn_info->w_isolani++;
                    extra_pawn_val -= 10;
                    if (F_FILE & (~pawn_info->b_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->b_rook_files |= F_FILE; /*mark it as target*/
                        extra_pawn_val -= 5; /*and give an extra penalty*/
                    }
                }
            if (pawn_info->w_d_pawnmask & G_FILE) /*g file*/
                if ((pawn_info->w_pawn_mask & (F_FILE | H_FILE))==0)
                {
                    pawn_info->w_isolani++;
                    extra_pawn_val -= 10;
                    if (G_FILE & (~pawn_info->b_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->b_rook_files |= G_FILE; /*mark it as target*/
                        extra_pawn_val -= 5; /*and give an extra penalty*/
                    }
                }
            if (pawn_info->w_d_pawnmask & H_FILE) /*h file*/
                if ((pawn_info->w_pawn_mask & G_FILE)==0)
                {
                    pawn_info->w_isolani++;
                    extra_pawn_val -= 10;
                    if (H_FILE & (~pawn_info->b_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->b_rook_files |= H_FILE; /*mark it as target*/
                        extra_pawn_val -= 5; /*and give an extra penalty*/
                    }
                }
        }
    }

    if (pawn_info->b_d_pawnmask) /*are there black double pawns?*/
    {
        if (pawn_info->b_d_pawnmask & QUEEN_SIDE)
        {
            if (pawn_info->b_d_pawnmask & A_FILE) /*a file*/
                if ((pawn_info->b_pawn_mask & B_FILE)==0)
                {
                    pawn_info->b_isolani++;
                    extra_pawn_val += 10;
                    if (A_FILE & (~pawn_info->w_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->w_rook_files |= A_FILE; /*mark it as target*/
                        extra_pawn_val += 5; /*and give an extra penalty*/
                    }
                }
            if (pawn_info->b_d_pawnmask & B_FILE) /*b file*/
                if ((pawn_info->b_pawn_mask & (A_FILE | C_FILE))==0)
                {
                    pawn_info->b_isolani++;
                    extra_pawn_val += 10;
                    if (B_FILE & (~pawn_info->w_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->w_rook_files |= B_FILE; /*mark it as target*/
                        extra_pawn_val += 5; /*and give an extra penalty*/
                    }
                }
            if (pawn_info->b_d_pawnmask & C_FILE) /*c file*/
                if ((pawn_info->b_pawn_mask & (B_FILE | D_FILE))==0)
                {
                    pawn_info->b_isolani++;
                    extra_pawn_val += 10;
                    if (C_FILE & (~pawn_info->w_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->w_rook_files |= C_FILE; /*mark it as target*/
                        extra_pawn_val += 5; /*and give an extra penalty*/
                    }
                }
            if (pawn_info->b_d_pawnmask & D_FILE) /*d file*/
            {
                if ((pawn_info->b_pawn_mask & (C_FILE | E_FILE))==0)
                {
                    pawn_info->b_isolani++;
                    extra_pawn_val += 10;
                    if (D_FILE & (~pawn_info->w_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->w_rook_files |= D_FILE; /*mark it as target*/
                        extra_pawn_val += 5; /*and give an extra penalty*/
                    }
                }
                if (board[D7]->type == BPAWN)
                {
                    extra_pawn_val += 5;
                    if (board[D6]->type == BPAWN)
                        extra_pawn_val += 15;
                }
            }
        }
        if (pawn_info->b_d_pawnmask & KING_SIDE)
        {
            if (pawn_info->b_d_pawnmask & E_FILE) /*e file*/
            {
                if ((pawn_info->b_pawn_mask & (D_FILE | F_FILE))==0)
                {
                    pawn_info->b_isolani++;
                    extra_pawn_val += 10;
                    if (E_FILE & (~pawn_info->w_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->w_rook_files |= E_FILE; /*mark it as target*/
                        extra_pawn_val += 5; /*and give an extra penalty*/
                    }
                }
                if (board[E7]->type == BPAWN)
                {
                    extra_pawn_val += 5;
                    if (board[E6]->type == BPAWN)
                        extra_pawn_val += 15;
                }
            }
            if (pawn_info->b_d_pawnmask & F_FILE) /*f file*/
                if ((pawn_info->b_pawn_mask & (E_FILE | G_FILE))==0)
                {
                    pawn_info->b_isolani++;
                    extra_pawn_val += 10;
                    if (F_FILE & (~pawn_info->w_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->w_rook_files |= F_FILE; /*mark it as target*/
                        extra_pawn_val += 5; /*and give an extra penalty*/
                    }
                }
            if (pawn_info->b_d_pawnmask & G_FILE) /*g file*/
                if ((pawn_info->b_pawn_mask & (F_FILE | H_FILE))==0)
                {
                    pawn_info->b_isolani++;
                    extra_pawn_val += 10;
                    if (G_FILE & (~pawn_info->w_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->w_rook_files |= G_FILE; /*mark it as target*/
                        extra_pawn_val += 5; /*and give an extra penalty*/
                    }
                }
            if (pawn_info->b_d_pawnmask & H_FILE) /*h file*/
                if ((pawn_info->b_pawn_mask & G_FILE)==0)
                {
                    pawn_info->b_isolani++;
                    extra_pawn_val += 10;
                    if (H_FILE & (~pawn_info->w_pawn_mask))
                        /*isolated double pawn on a half-open file - ooops.*/
                    {
                        pawn_info->w_rook_files |= H_FILE; /*mark it as target*/
                        extra_pawn_val += 5; /*and give an extra penalty*/
                    }
                }
        }
    }

    extra_pawn_val += 10*(pawn_info->b_isolani - pawn_info->w_isolani);

    /*devalued pawn majorities: e.g. abc against abcc like in the Spanish
      Exchange. that's also the reason for the weight of 18 - the bonus for
      having a bishop in that game phase. If we choose more, the machine will
      always favour that variation whith white. if we give less, the
      importance is flattened away. Choosing it the same enables that line of
      play, but if black gives up his bishop pair, then doing it in a way that
      puts a c pawn on the d file sets things straight.

      this kind of setup might arise anywhere, not only on the wings, but it
      is most common there, and it's most problematic there since it
      effectively leads to an outward passed pawn situation in the endgame
      although both side are equal in material.*/

    /*queenside first*/
    if (w_queen_pawns != b_queen_pawns)
    {
        if (w_queen_pawns > b_queen_pawns)
        {
            if ((QUEEN_SIDE & (pawn_info->w_pawn_mask ^ pawn_info->b_pawn_mask)) == 0)
            {
                extra_pawn_val -= 18;
                pawn_info->deval_pawn_majority++;
            }
        } else
        {
            if ((QUEEN_SIDE & (pawn_info->w_pawn_mask ^ pawn_info->b_pawn_mask)) == 0)
            {
                extra_pawn_val += 18;
                pawn_info->deval_pawn_majority--;
            }
        }
    }

    /*now the kingside*/
    if (w_king_pawns != b_king_pawns)
    {
        if (w_king_pawns > b_king_pawns)
        {
            if ((KING_SIDE & (pawn_info->w_pawn_mask ^ pawn_info->b_pawn_mask)) == 0)
            {
                extra_pawn_val -= 18;
                pawn_info->deval_pawn_majority++;
            }
        } else
        {
            if ((KING_SIDE & (pawn_info->w_pawn_mask ^ pawn_info->b_pawn_mask)) == 0)
            {
                extra_pawn_val += 18;
                pawn_info->deval_pawn_majority--;
            }
        }
    }

    /* evaluation for passed pawns*/
    for (i=8; i<16; i++) {
        xy = Wpieces[i].xy;
        if ((xy!=0) && (Wpieces[i].type==WPAWN))
        {
            unsigned int row_num;
            /*for the current pawn, get its row and column number. if the most
              backward black pawn row on the same column is greater (cannot be
              the same because two pieces can't share the same square), then
              the line itself is free. maybe because there is no black pawn,
              or it is behind our candidate pawn here (has hit and changed the
              line or so). To the left and right, if the most backward black
              pawns are on the same row or further down the board, they cannot
              hinder our candidate pawn from passing.*/
            bin_col = ColNum[xy];
            row_num = RowNum[xy];
            if ((b_min_rows[bin_col]<row_num) && (b_min_rows[bin_col-1]<=row_num) && (b_min_rows[bin_col+1]<=row_num))
            {   /*for pawns mobility means passed pawn*/

                /*check whether we have a doubled pawn. if so, only the frontrunner gets the bonus.*/
                if (w_max_rows[bin_col] == row_num)
                {
                    bin_col--;
                    if (pawn_info->w_passed_rows[bin_col] < row_num)
                        pawn_info->w_passed_rows[bin_col] = row_num;

                    bin_col = 1U << bin_col;

                    pawn_info->w_passed_pawns++;
                    pawn_info->w_passed_mask |= bin_col;
                    Wpieces[i].mobility = 2;
                    if (xy >= A4) Wpieces[i].mobility += 8;
                    if (xy >= A5) Wpieces[i].mobility += 12;
                    if (xy >= A6) Wpieces[i].mobility += 16;
                    if (board[xy-9]->type==WPAWN || board[xy-11]->type==WPAWN)
                        Wpieces[i].mobility += ((Wpieces[i].mobility)/2); /*supported passed pawn bonus*/
                }
            }
            if (xy >= A7)  Wpieces[i].mobility += 20;
            pawn_info->w_passed_mobility += Wpieces[i].mobility;
        }

        xy = Bpieces[i].xy;
        if ((xy!=0) && (Bpieces[i].type==BPAWN))
        {
            unsigned int row_num;
            /*for the current pawn, get its row and column number. if the most
              backward white pawn row on the same column is smaller (cannot be
              the same because two pieces can't share the same square), then
              the line itself is free. maybe because there is no white pawn,
              or it is behind our candidate pawn here (has hit and changed the
              line or so). To the left and right, if the most backward white
              pawns are on the same row or further up the board, they cannot
              hinder our candidate pawn from passing.*/
            bin_col = ColNum[xy];
            row_num = RowNum[xy];
            if ((w_min_rows[bin_col]>row_num) && (w_min_rows[bin_col-1]>=row_num) && (w_min_rows[bin_col+1]>=row_num))
            {
                /*check whether we have a doubled pawn. if so, only the frontrunner gets the bonus.*/
                if (b_max_rows[bin_col] == row_num)
                {
                    bin_col--;
                    if (pawn_info->b_passed_rows[bin_col] > row_num)
                        pawn_info->b_passed_rows[bin_col] = row_num;

                    bin_col = 1U << bin_col;

                    pawn_info->b_passed_pawns++;
                    pawn_info->b_passed_mask |= bin_col;
                    Bpieces[i].mobility = 2;
                    if (xy<=H5) Bpieces[i].mobility += 8;
                    if (xy<=H4) Bpieces[i].mobility += 12;
                    if (xy<=H3) Bpieces[i].mobility += 16;
                    if (board[xy+9]->type==BPAWN || board[xy+11]->type==BPAWN)
                        Bpieces[i].mobility += ((Bpieces[i].mobility)/2);/*supported passed pawn bonus*/
                }
            }
            if (xy<=H2)  Bpieces[i].mobility += 20;
            pawn_info->b_passed_mobility += Bpieces[i].mobility;
        }
    }

    /*files with passed pawns are good for rook support.*/
    pawn_info->w_rook_files |= pawn_info->w_passed_mask;
    pawn_info->b_rook_files |= pawn_info->b_passed_mask;

    extra_pawn_val += pawn_info->w_passed_mobility - pawn_info->b_passed_mobility;

    /*do we have an outward passed pawn?
      don't give too high an advantage because the pawn eval only takes pawns
      into consideration, not the rest of the pieces or the game phase. note
      that this is just an approximation, not an exact solution.

      On the other hand, it covers a number of standard cases without costing
      much processing time, mostly because the data used were cheap to aquire
      in the isolated pawn / passed pawn calculations, which would have been
      needed anyway.

      if the conditions for a passed pawn race on different wings are there,
      don't count the outside passed pawns. that would be too unreliable.

      note: further down in the endgame eval, an outside passed pawn gets another
      32 points bonus.*/

    if (pawn_info->w_passed_pawns)
    {
        /*queenside? so first check whether there are pawns left on the kingside, and no black passed pawns there.*/
        if ((pawn_info->w_pawn_mask & KING_SIDE) && ((pawn_info->b_passed_mask & KING_SIDE) == 0))
        {
            if ((pawn_info->w_passed_mask & (A_FILE)) &&
                    (!(pawn_info->b_pawn_mask & (A_FILE | B_FILE))))
            {
                pawn_info->w_outpassed++;
                extra_pawn_val += 20;
            }
            if ((pawn_info->w_passed_mask & (B_FILE)) &&
                    (!(pawn_info->b_pawn_mask & (A_FILE | B_FILE | C_FILE))))
            {
                pawn_info->w_outpassed++;
                extra_pawn_val += 20;
            }
        }
        /*kingside? so first check whether there are pawns left on the queenside, and no black passed pawns there.*/
        if ((pawn_info->w_pawn_mask & QUEEN_SIDE) && ((pawn_info->b_passed_mask & QUEEN_SIDE) == 0))
        {
            if ((pawn_info->w_passed_mask & (H_FILE)) &&
                    (!(pawn_info->b_pawn_mask & (H_FILE | G_FILE))))
            {
                pawn_info->w_outpassed++;
                extra_pawn_val += 20;
            }
            if ((pawn_info->w_passed_mask & (G_FILE)) &&
                    (!(pawn_info->b_pawn_mask & (H_FILE | G_FILE | F_FILE))))
            {
                pawn_info->w_outpassed++;
                extra_pawn_val += 20;
            }
        }
    }

    if (pawn_info->b_passed_pawns)
    {
        /*queenside? so first check whether there are pawns left on the kingside, and no white passed pawns there.*/
        if ((pawn_info->b_pawn_mask & KING_SIDE) && ((pawn_info->w_passed_mask & KING_SIDE) == 0))
        {
            if ((pawn_info->b_passed_mask & (A_FILE)) &&
                    (!(pawn_info->w_pawn_mask & (A_FILE | B_FILE))))
            {
                pawn_info->b_outpassed++;
                extra_pawn_val -= 20;
            }
            if ((pawn_info->b_passed_mask & (B_FILE)) &&
                    (!(pawn_info->w_pawn_mask & (A_FILE | B_FILE | C_FILE))))
            {
                pawn_info->b_outpassed++;
                extra_pawn_val -= 20;
            }
        }
        /*kingside? so first check whether there are pawns left on the queenside, and no white passed pawns there.*/
        if ((pawn_info->b_pawn_mask & QUEEN_SIDE) && ((pawn_info->w_passed_mask & QUEEN_SIDE) == 0))
        {
            if ((pawn_info->b_passed_mask & (H_FILE)) &&
                    (!(pawn_info->w_pawn_mask & (H_FILE | G_FILE))))
            {
                pawn_info->b_outpassed++;
                extra_pawn_val -= 20;
            }
            if ((pawn_info->b_passed_mask & (G_FILE)) &&
                    (!(pawn_info->w_pawn_mask & (H_FILE | G_FILE | F_FILE))))
            {
                pawn_info->b_outpassed++;
                extra_pawn_val -= 20;
            }
        }
    }

    /*evaluation for backward pawns. definition used is:
      a backward pawn is a pawn that
      - cannot advance without immediately being hit by an enemy pawn, AND
      - has no friendly pawns left or right at the same rank or behind, AND
      - stands on a half-open file.

     some points here:
     a) the half open file is debatable, but those are clearly the ugly cases.
        we have to take care not to punish centre pawns e.g. in stonewall
        formation as backwards because that formation at least gains space.
        Plus that the half-open file definition gets very well the case where
        a pawn majority cannot produce a passed pawn without assistance. That
        can get nasty in the endgame if the king has duties elsewhere.
     b) if there are pawns behind, but cannot advance to help our backward
        pawn because they are blocked by an enemy pawn on their own file, then
        this case is also caught.
     c) passed pawns obviously can never be backward, and they are by
        definition on a half-open file. so mask them out using the passed pawn
        mask.
     d) there may be more than one friendly pawn on the same file. if the
        front runner is a passed pawn, but the colleague behind a backward
        one, that situation will be ignored here. It's just a good-enough
        solution for normal cases.
     e) if there is a backward pawn on a half open file, but the hole is
        covered by a friendly double pawn on the same file, this will be
        caught since only the most advanced pawn on a file is considered here.
        So if the double pawn isn't an isolated one (in which case it already
        has been marked as rook target in the double pawn evaluation) and the
        front runner can get cover, it will not be marked as rook target here.

      OK, since a free file is marked with a zero bit and an occupied file
      with a one bit, what we are looking for is a 1 and a 0 in the same bit
      position: (x AND ( NOT Y)).*/

    /*get the candidate files for backward white pawns, the A file is in the
      least significant bit.*/
    backward_files = pawn_info->w_pawn_mask & (~pawn_info->b_pawn_mask) & (~pawn_info->w_passed_mask);

    bin_col = 1; /*start with the A file*/

    while (backward_files != 0)
    {
        if ((backward_files & 0x01) != 0) /*this file can be affected*/
        {
            unsigned int bw_found = 1, min_row, max_row, front_row;

            front_row = w_max_rows[bin_col]; /*on what row is the most advanced white pawn on our current file?*/

            /*look to the left*/
            min_row = w_min_rows[bin_col-1];
            max_row = b_max_rows[bin_col-1]; /*if our helper pawn cannot advance to cover, it isn't helpful.*/
            if ((min_row <= front_row) && ((max_row >= front_row) || (min_row > max_row)))
            {
                bw_found = 0;
                if (min_row == front_row-1)
                    extra_pawn_val += 10;
            }
            /*and to the right*/
            min_row = w_min_rows[bin_col+1];
            max_row = b_max_rows[bin_col+1];
            if ((min_row <= front_row) && ((max_row >= front_row) || (min_row > max_row)))
            {
                bw_found = 0;
                if (min_row == front_row-1)
                    extra_pawn_val += 10;
            }

            if (bw_found) /*backward white pawn found!*/
            {
                pawn_info->b_rook_files |= 1U << (bin_col -1); /*good file for black's (!) rooks*/
                extra_pawn_val -= 10;
                if ((bin_col == 4) || (bin_col == 5))
                    /*much worse in the centre because black will combine attacking that weak pawn with
                    centralising his pieces.*/
                    extra_pawn_val -= 30;
            }
        }
        backward_files >>= 1;
        bin_col++; /*scan forward to the next file*/
    }

    /*get the candidate files for backward black pawns, A file is in the least significant bit.*/
    backward_files = pawn_info->b_pawn_mask & (~pawn_info->w_pawn_mask) & (~pawn_info->b_passed_mask);

    bin_col = 1; /*start with the A file*/

    while (backward_files != 0)
    {
        if ((backward_files & 0x01) != 0) /*this file can be affected*/
        {
            unsigned int bw_found = 1, min_row, max_row, front_row;

            front_row = b_max_rows[bin_col]; /*on what row is the most advanced black pawn on our current file?*/

            /*look to the left*/
            min_row = b_min_rows[bin_col-1];
            max_row = w_max_rows[bin_col-1]; /*if our helper pawn cannot advance to cover, it isn't helpful.*/
            if ((min_row >= front_row) && ((max_row <= front_row) || (min_row < max_row)))
            {
                bw_found = 0;
                if (min_row == front_row+1)
                    extra_pawn_val -= 10;
            }
            /*and to the right*/
            min_row = b_min_rows[bin_col+1];
            max_row = w_max_rows[bin_col+1];
            if ((min_row >= front_row) && ((max_row <= front_row) || (min_row < max_row)))
            {
                bw_found = 0;
                if (min_row == front_row+1)
                    extra_pawn_val -= 10;
            }

            if (bw_found) /*backward black pawn found!*/
            {
                pawn_info->w_rook_files |= 1U << (bin_col -1); /*good file for white's (!) rooks*/
                extra_pawn_val += 10;
                if ((bin_col == 4) || (bin_col == 5))
                    /*much worse in the centre because white will combine attacking that weak pawn with
                    centralising his pieces.*/
                    extra_pawn_val += 30;
            }
        }
        backward_files >>= 1;
        bin_col++; /*scan forward to the next file*/
    }
    /*the "blocked position detection":
      - there have to be at least 14 pawns
      - blocking is detected in the centre files only.

      This is asymmetric evaluation because the software is better in open
      positions, so it makes sense to avoid blocked positions early on. Kind
      of an "anti-human-mode".*/
    if (pawn_info->all_pawns >= 14)
    {
        int blocked_files = 0;
        unsigned int both_pawn_mask = pawn_info->w_pawn_mask & pawn_info->b_pawn_mask;
        for (bin_col = 3; bin_col<=6; bin_col++)
        {
            int bin_col_minus = bin_col - 1;
            if (both_pawn_mask & (1U << bin_col_minus))
                /*a file can only be a blocked one if there are pawns of both colour on it*/
            {
                unsigned int w_front_row, b_front_row;

                w_front_row = w_max_rows[bin_col];
                b_front_row = b_max_rows[bin_col];

                if (w_front_row + 1 == b_front_row)
                    /*two pawns head to head*/
                    blocked_files++;
                else
                    /*not directly blocked, but is it the back pawn part of a stonewall formation?*/
                {
                    int bin_col_plus = bin_col + 1;
                    if (computer_side == WHITE)
                    {
                        if ((w_min_rows[bin_col_minus] > w_front_row) && (w_min_rows[bin_col_plus] > w_front_row) &&
                                (b_max_rows[bin_col_minus] == w_front_row+2) && (b_max_rows[bin_col_plus] == w_front_row+2))
                            blocked_files++;
                    } else
                    {
                        if ((b_min_rows[bin_col_minus] < b_front_row) && (b_min_rows[bin_col_plus] < b_front_row) &&
                                (w_max_rows[bin_col_minus] == b_front_row-2) && (w_max_rows[bin_col_plus] == b_front_row-2))
                            blocked_files++;
                    }
                }
            }
        }

        if (blocked_files > 1)
        {
            if (computer_side == WHITE)
                extra_pawn_val -= 10 * blocked_files;
            else
                extra_pawn_val += 10 * blocked_files;
        }

        /*general penalty for too many pawns => encourage a pawn trade*/
        if (pawn_info->all_pawns == 16)
        {
            if (computer_side == WHITE)
                extra_pawn_val -= 20;
            else
                extra_pawn_val += 20;
        } else
        {
            if (computer_side == WHITE)
                extra_pawn_val -= 10;
            else
                extra_pawn_val += 10;
        }

    }
    pawn_info->extra_pawn_val = extra_pawn_val;
} /*end of pawn evaluation (endgame pawn modification will follow further down in the endgame eval)*/

static int Eval_Middlegame_Evaluation(PAWN_INFO *restrict pawn_info, const PIECE_INFO *restrict piece_info)
{
    int i, ret = 0;

    ret += pawn_info->extra_pawn_val;

    /*rook handling: only from move 12 on. before that, better get out the minor pieces.*/
    if ((mv_stack_p + start_moves >= 24) || (game_started_from_0 == 0))
    {
        unsigned int wrook1_col = 0, brook1_col = 0;
        /*if white has rooks, white wants open files.
          Preferably open a centre file.*/
        if (piece_info->w_rooks > 0)
        {
            if (pawn_info->w_pawn_mask == NO_FILES_FREE)
                ret -= 10;
            else
                /*encourage opening some files, but not right where the king is.*/
            {
                if (ColNum[wking] >= BOARD_F_FILE) /*king is on the kingside*/
                {
                    if ((pawn_info->w_pawn_mask & (QUEENSIDE_FILES | CENTRE_FILES)) != (QUEENSIDE_FILES | CENTRE_FILES))
                        ret += 5;
                } else if (ColNum[wking] <= BOARD_C_FILE) /*king is on the queenside*/
                {
                    if ((pawn_info->w_pawn_mask & (KINGSIDE_FILES | CENTRE_FILES)) != (KINGSIDE_FILES | CENTRE_FILES))
                        ret += 5;
                } else /*king is in the middle. keep the centre closed (or get the king away).*/
                {
                    if ((pawn_info->w_pawn_mask & (NOT_CENTRE_FILES)) != (NOT_CENTRE_FILES))
                        ret += 5;
                }
            }
        }

        /*if black has rooks, black wants open files.
          Preferably open a centre file.*/
        if (piece_info->b_rooks > 0)
        {
            if (pawn_info->b_pawn_mask == NO_FILES_FREE)
                ret += 10;
            else
                /*encourage opening some files, but not right where the king is.*/
            {
                if (ColNum[bking] >= BOARD_F_FILE) /*king is on the kingside*/
                {
                    if ((pawn_info->b_pawn_mask & (QUEENSIDE_FILES | CENTRE_FILES)) != (QUEENSIDE_FILES | CENTRE_FILES))
                        ret -= 5;
                } else if (ColNum[bking] <= BOARD_C_FILE) /*king is on the queenside*/
                {
                    if ((pawn_info->b_pawn_mask & (KINGSIDE_FILES | CENTRE_FILES)) != (KINGSIDE_FILES | CENTRE_FILES))
                        ret -= 5;
                } else /*king is in the middle. keep the centre closed (or get the king away).*/
                {
                    if ((pawn_info->b_pawn_mask & (NOT_CENTRE_FILES)) != (NOT_CENTRE_FILES))
                        ret -= 5;
                }
            }
        }

        /* Rooks */
        for (i=2; i<=3; i++) /*let the compiler roll this loop out.*/
        {
            int xy = Wpieces[i].xy;

            if (xy != 0)
            {
                unsigned int bin_col = 1U << (ColNum[xy] - 1);
                if ((bin_col & pawn_info->w_pawn_mask) == 0)
                /*standing on a free file*/
                {
                    if (((bin_col & pawn_info->b_pawn_mask) == 0) || (bin_col & pawn_info->w_rook_files))
                    {
                        /*fully open or aiming at a backward pawn*/
                        ret += 12;

                        /*set up rook doubling eval*/

                        if (i == 2) /*first rook*/
                            wrook1_col = bin_col;
                        else /*second rook*/
                        {
                            /*if the first rook has not passed in the step above, then wrook1_col will
                              still be 0 initialised, and therefore, the comparison here will not match.
                              bin_col contains the binary column of the second rook at this point.*/
                            if (bin_col == wrook1_col)
                                /*rooks are nicely doubled on open file or aiming at a backward black pawn.*/
                                ret += 10;
                        }
                    }
                    else if ((bin_col & pawn_info->b_pawn_mask) && (bin_col & pawn_info->b_rook_files))
                    /*black passed pawn*/
                    {
                        int pawn_xy;
                        /*start our scan for the passed pawn from the 2nd rank on*/
                        pawn_xy = ColNum[xy]+(A2-1);
                        while (board[pawn_xy]->type != BPAWN)
                            pawn_xy += 10;

                        /*can we see our rook without further obstructions from that position on?*/
                        if (pawn_xy < xy)
                        /*then scan upwards*/
                        {
                            do {
                                pawn_xy += 10;
                            } while (((board[pawn_xy]->type == NO_PIECE) || (board[pawn_xy]->type == WROOK) || (board[pawn_xy]->type == WQUEEN)) && (pawn_xy != xy));
                            if (pawn_xy == xy)
                                /*then our rook stands behind that black passed pawn*/
                                ret += 15;
                        } else
                        {
                            /*then our rook stands in front of that black passed pawn. still important to stop it.*/
                            ret += 5;
                        }
                    } else
                    {
                        /* half open*/
                        if (ColNum[wking] >= BOARD_E_FILE) /*king is on the kingside*/
                        {
                            if (bin_col & (QUEEN_SIDE | CENTRE_FILES))
                                ret += 8;
                            else
                                ret += 5;
                        } else /*king on the queenside*/
                        {
                            if (bin_col & (KING_SIDE | CENTRE_FILES))
                                ret += 8;
                            else
                                ret += 5;
                        }
                    }
                } else if (bin_col & pawn_info->w_rook_files)
                /*white passed pawn*/
                {
                    int pawn_xy;
                    /*start our scan for the passed pawn from the 7th rank on*/
                    pawn_xy = ColNum[xy]+(A7-1);
                    while (board[pawn_xy]->type != WPAWN)
                        pawn_xy -= 10;

                    /*can we see our rook without further obstructions from that position on?*/
                    if (pawn_xy > xy)
                    /*then scan downwards*/
                    {
                        do {
                            pawn_xy -= 10;
                        } while (((board[pawn_xy]->type == NO_PIECE) || (board[pawn_xy]->type == WROOK) || (board[pawn_xy]->type == WQUEEN)) && (pawn_xy != xy));
                        if (pawn_xy == xy)
                            /*then our rook stands behind our passed pawn*/
                            ret += 15;
                    } else
                    {
                        /*then our rook stands in front of our passed pawn. not that good.*/
                        ret += 5;
                    }
                }
            }

            xy = Bpieces[i].xy;
            if (xy != 0)
            {
                unsigned int bin_col = 1U << (ColNum[xy] - 1);
                if ((bin_col & pawn_info->b_pawn_mask) == 0)
                /*standing on a free file*/
                {
                    if (((bin_col & pawn_info->w_pawn_mask) == 0) || (bin_col & pawn_info->b_rook_files))
                    {
                        /*fully open or aiming at a backward pawn*/
                        ret -= 12;

                        /*set up rook doubling eval*/

                        if (i == 2) /*first rook*/
                            brook1_col = bin_col;
                        else /*second rook*/
                        {
                            /*if the first rook has not passed in the step above, then brook1_col will
                              still be 0 initialised, and therefore, the comparison here will not match.
                              bin_col contains the binary column of the second rook at this point.*/
                            if (bin_col == brook1_col)
                                /*rooks are nicely doubled on open file or aiming at a backward white pawn.*/
                                ret -= 10;
                        }
                    }
                    else if ((bin_col & pawn_info->w_pawn_mask) && (bin_col & pawn_info->w_rook_files))
                    /*white passed pawn*/
                    {
                        int pawn_xy;
                        /*start our scan for the passed pawn from the 7th rank on*/
                        pawn_xy = ColNum[xy]+(A7-1);
                        while (board[pawn_xy]->type != WPAWN)
                            pawn_xy -= 10;

                        /*can we see our rook without further obstructions from that position on?*/
                        if (pawn_xy > xy)
                            /*then scan downwards*/
                        {
                            do {
                                pawn_xy -= 10;
                            } while (((board[pawn_xy]->type == NO_PIECE) || (board[pawn_xy]->type == BROOK) || (board[pawn_xy]->type == BQUEEN)) && (pawn_xy != xy));
                            if (pawn_xy == xy)
                                /*then our rook stands behind that white passed pawn*/
                                ret -= 15;
                        } else
                        {
                            /*then our rook stands in front of that white passed pawn. still important to stop it.*/
                            ret -= 5;
                        }
                    } else
                    {
                        /* half open*/
                        if (ColNum[bking] >= BOARD_E_FILE) /*king is on the kingside*/
                        {
                            if (bin_col & (QUEEN_SIDE | CENTRE_FILES))
                                ret -= 8;
                            else
                                ret -= 5;
                        } else /*king on the queenside*/
                        {
                            if (bin_col & (KING_SIDE | CENTRE_FILES))
                                ret -= 8;
                            else
                                ret -= 5;
                        }
                    }
                } else if (bin_col & pawn_info->b_rook_files)
                /*black passed pawn*/
                {
                    int pawn_xy;
                    /*start our scan for the passed pawn from the 2nd rank on*/
                    pawn_xy = ColNum[xy]+(A2-1);
                    while (board[pawn_xy]->type != BPAWN)
                        pawn_xy += 10;

                    /*can we see our rook without further obstructions from that position on?*/
                    if (pawn_xy < xy)
                    /*then scan upwards*/
                    {
                        do {
                            pawn_xy += 10;
                        } while (((board[pawn_xy]->type == NO_PIECE) || (board[pawn_xy]->type == BROOK) || (board[pawn_xy]->type == BQUEEN)) && (pawn_xy != xy));

                        if (pawn_xy == xy)
                            /*then our rook stands behind our passed pawn*/
                            ret -= 15;
                    } else
                    {
                        /*then our rook stands in front of our passed pawn. not that good.*/
                        ret -= 5;
                    }
                }
            }
        }
    }

    /* Central Squares Control */
    i=board[E4]->type;
    if (i != NO_PIECE)
    {
        if (i > BLACK)
            ret -= 7;
        else
            ret += 5;
    }

    i=board[D4]->type;
    if (i != NO_PIECE)
    {
        if (i > BLACK)
            ret -= 7;
        else
            ret += 5;
    }

    i=board[E5]->type;
    if (i != NO_PIECE)
    {
        if (i > BLACK)
            ret -= 5;
        else
            ret += 7;
    }

    i=board[D5]->type;
    if (i != NO_PIECE)
    {
        if (i > BLACK)
            ret -= 5;
        else
            ret += 7;
    }

    /* Kings safety */
    ret += Eval_White_King_Safety(piece_info->w_bishop_colour, piece_info->b_bishop_colour, piece_info->b_queens);
    ret += Eval_Black_King_Safety(piece_info->b_bishop_colour, piece_info->w_bishop_colour, piece_info->w_queens);
    /*additional king safety: missing pawns with the white king.*/
    if (piece_info->b_queens > 0)
    {
        int extra_king_safety = 0;
        if (ColNum[wking] >= BOARD_E_FILE) /*kingside or not yet castled*/
        {
            int pawns_up = 0;
            if (board[H2]->type != WPAWN)
            {
                if (board[H3]->type != WPAWN)
                    extra_king_safety -= 10;
                else
                    pawns_up++;
            }
            if (board[G2]->type != WPAWN)
            {
                if(board[G3]->type != WPAWN)
                    extra_king_safety -= 50;
                else
                    pawns_up++;
            }
            if (board[F2]->type != WPAWN)
            {
                if (board[F3]->type != WPAWN)
                    extra_king_safety -= 20;
                else
                    pawns_up++;
            }
            if (pawns_up > 1)
                extra_king_safety -= 10*pawns_up;
        } else /*queenside castling*/
        {
            int pawns_up = 0;
            if (board[A2]->type != WPAWN)
            {
                if (board[A3]->type != WPAWN)
                    extra_king_safety -= 10;
                else
                    pawns_up++;
            }
            if (board[B2]->type != WPAWN)
            {
                if (board[B3]->type != WPAWN)
                    extra_king_safety -= 50;
                else
                    pawns_up++;
            }
            if (board[C2]->type != WPAWN)
            {
                if (board[C3]->type != WPAWN)
                    extra_king_safety -= 30;
                else
                    pawns_up++;
            }
            if (pawns_up > 1)
                extra_king_safety -= 10*pawns_up;
        }
        ret += extra_king_safety;
    }
    /*additional king safety: missing pawns with the black king*/
    if (piece_info->w_queens > 0)
    {
        int extra_king_safety = 0;
        if (ColNum[bking] >= BOARD_E_FILE) /*kingside or not yet castled*/
        {
            int pawns_up = 0;
            if (board[H7]->type != BPAWN)
            {
                if (board[H6]->type != BPAWN)
                    extra_king_safety += 10;
                else
                    pawns_up++;
            }
            if (board[G7]->type != BPAWN)
            {
                if (board[G6]->type != BPAWN)
                    extra_king_safety += 50;
                else
                    pawns_up++;
            }
            if (board[F7]->type != BPAWN)
            {
                if (board[F6]->type != BPAWN)
                    extra_king_safety += 20;
                else
                    pawns_up++;
            }
            if (pawns_up > 1)
                extra_king_safety += 10*pawns_up;
        } else /*queenside castling*/
        {
            int pawns_up = 0;
            if (board[A7]->type != BPAWN)
            {
                if (board[A6]->type != BPAWN)
                    extra_king_safety += 10;
                else
                    pawns_up++;
            }
            if (board[B7]->type != BPAWN)
            {
                if (board[B6]->type != BPAWN)
                    extra_king_safety += 50;
                else
                    pawns_up++;
            }
            if (board[C7]->type != BPAWN)
            {
                if (board[C6]->type != BPAWN)
                    extra_king_safety += 30;
                else
                    pawns_up++;
            }
            if (pawns_up > 1)
                extra_king_safety += 10*pawns_up;
        }
        ret += extra_king_safety;
    }
    return(ret);
}

/*unlike the other partial eval functions, this is a void return type;
  instead, it takes the existing eval (from Eval_Static_Evaluation) as 2nd
  argument by reference. The reason is that unlike the other functions, this
  is not only addition/subtraction on the existing value, but sometimes also
  a plain overwrite, e.g. in KNB-K.*/
static void Eval_Endgame_Evaluation(PAWN_INFO *restrict pawn_info, int *current_eval,
                                    const PIECE_INFO *restrict piece_info, int pawn_hash_hit,
                                    enum E_COLOUR side_to_move, int pure_material)
{
    int xy, i, ret;
    unsigned int bin_col;
    int three_or_four_pieces_no_pawns = (piece_info->all_pieces < 5) && (pawn_info->all_pawns==0);
    int king_halts_passed, basic_endgames, disregard_centre, wk_centre, bk_centre;

    ret = *current_eval;

    if (!pawn_hash_hit)
    {
        int max_passed_connected;
        int extra_pawn_val = pawn_info->extra_pawn_val;
        /*Add a penalty for no pawns at endgame*/
        if (pawn_info->w_pawns == 0)
            extra_pawn_val -= 50;
        if (pawn_info->b_pawns == 0)
            extra_pawn_val += 50;
        /*Give some extra malus for endgame isolated pawns.
          pay attention that the overall sum for making black pawns "f6, g6, h6"
          (best constellation) to "f6, h6, h5" (worst constellation) is less
          than 100 points difference - don't sacrifice a pawn for that change!*/
        if (pawn_info->w_isolani) {
            if (pawn_info->w_isolani>2)
                extra_pawn_val -= (pawn_info->w_isolani<<2);
            else
                extra_pawn_val -= (pawn_info->w_isolani<<1);
        }
        if (pawn_info->b_isolani) {
            if (pawn_info->b_isolani>2)
                extra_pawn_val += (pawn_info->b_isolani<<2);
            else
                extra_pawn_val += (pawn_info->b_isolani<<1);
        }
        /*choice of the factor:
          - the base worth of an outside passer is already 20 points.
          - bishop is 5 points more worth than a knight.
          - with spread pawns (necessary for an outward passer) even 5 more, makes 10.
          => for encouraging trading the bishop versus the knight for making an
          outside passer, we must add more than 10 points.*/
        if (pawn_info->w_outpassed)
            extra_pawn_val += (pawn_info->w_outpassed << 4);
        if (pawn_info->b_outpassed)
            extra_pawn_val -= (pawn_info->b_outpassed << 4);

        /*similar for devalued pawn majority: base penalty is 18, the bishop
          pair's worth in the opening. in the endgame, we add another 10 points
          penalty, making it 28 points, less then the bishop pair bonus. DON'T
          bitshift because that variable is negative in case of a black devalued
          majority.*/
        extra_pawn_val -= pawn_info->deval_pawn_majority*10;

        /* endgame adjustments for passed pawns: double their base value in the endgame.*/
        extra_pawn_val += pawn_info->w_passed_mobility - pawn_info->b_passed_mobility;

        /*now check for connected white passed pawns. we're only checking for
          the most advanced connected pair. it doesn't make sense to check for
          three connected passed pawns, that doesn't happen in real games - or
          the game is decided anyway.*/
        if (pawn_info->w_passed_pawns > 1) /*connected pawns require at least two of them*/
        {
            if (ConnectedTable[pawn_info->w_passed_mask]) /*and in adjacent files*/
            {
                max_passed_connected = 0;
                /*scan the adjacent files*/
                for (i = 0, bin_col = (A_FILE | B_FILE); i < 7; bin_col <<= 1, i++)
                {
                    if ((pawn_info->w_passed_mask & bin_col) == bin_col) /*found two connected passed pawns!*/
                    {
                        int Passed;
                        int _abs_diff;
                        _abs_diff = pawn_info->w_passed_rows[i] - pawn_info->w_passed_rows[i+1];
                        _abs_diff = Abs(_abs_diff);
                        if (_abs_diff <= 1) /*adjacent and connected*/
                            Passed = (pawn_info->w_passed_rows[i] + pawn_info->w_passed_rows[i+1]) << 2;
                        else
                            Passed = (pawn_info->w_passed_rows[i] + pawn_info->w_passed_rows[i+1]) << 1;
                        if (Passed > max_passed_connected)
                            max_passed_connected = Passed;
                    }
                }
                extra_pawn_val += max_passed_connected;
            }
        }

        /*now check for connected black passed pawns.*/
        if (pawn_info->b_passed_pawns > 1) /*connected pawns require at least two of them*/
        {
            if (ConnectedTable[pawn_info->b_passed_mask]) /*and in adjacent files*/
            {
                max_passed_connected = 0;
                /*scan the adjacent files*/
                for (i = 0, bin_col = (A_FILE | B_FILE); i < 7; bin_col <<= 1, i++)
                {
                    if ((pawn_info->b_passed_mask & bin_col) == bin_col) /*found two connected passed pawns!*/
                    {
                        int Passed;
                        int _abs_diff;
                        _abs_diff = pawn_info->b_passed_rows[i] - pawn_info->b_passed_rows[i+1];
                        _abs_diff = Abs(_abs_diff);
                        if (_abs_diff <= 1) /*adjacent and connected*/
                            Passed = (18-(pawn_info->b_passed_rows[i] + pawn_info->b_passed_rows[i+1])) << 2;
                        else
                            Passed = (18-(pawn_info->b_passed_rows[i] + pawn_info->b_passed_rows[i+1])) << 1;
                        if (Passed > max_passed_connected)
                            max_passed_connected = Passed;
                    }
                }
                extra_pawn_val -= max_passed_connected;
            }
        }
        /*limit the range. 511 is already for totally unrealistic positions.*/
        if (UNLIKELY(extra_pawn_val > 511))
            extra_pawn_val = 511;
        else if (UNLIKELY(extra_pawn_val < -511))
            extra_pawn_val = -511;

        pawn_info->extra_pawn_val = extra_pawn_val;
    }

    ret += pawn_info->extra_pawn_val;

    /*extra pawns with kings only should mostly win*/
    if (piece_info->all_pieces - pawn_info->all_pawns == 2 /*the kings*/)
        ret += (pawn_info->w_pawns - pawn_info->b_pawns) * 25;

    /* White King special endgame evaluation */
    xy = wking;
    if ((piece_info->all_pieces == 3) && (piece_info->b_rooks == 1))
        basic_endgames = 0; /*gets special eval*/
    else
        basic_endgames = (three_or_four_pieces_no_pawns && (piece_info->white_pieces==1));
    king_halts_passed = 0;
    if (three_or_four_pieces_no_pawns && piece_info->black_pieces==1) {
        int _abs_diff;
        _abs_diff = RowNum[wking] - RowNum[bking];
        ret -= (Abs(_abs_diff)) * 5;
        _abs_diff = ColNum[wking] - ColNum[bking];
        ret -= (Abs(_abs_diff)) * 5;
    }

    /*White has only king and pawns?*/
    if (pawn_info->w_pawns + 1 == piece_info->white_pieces)
    {
        /*check for rule of the square with a black passed pawn.*/
        unsigned int b_passed_mask = ((pawn_info->b_pawn_mask) & (pawn_info->b_rook_files));
        int prom_square = A1;
        while (b_passed_mask) /*black passed pawns there*/
        {
            if (b_passed_mask & 1U) /*file found*/
            {
                int king_dist, pawn_dist, king_col_dist, king_row_dist, sq = prom_square + 10;

                while ((board[sq]->type != BPAWN) && (sq < A6)) /*double step*/
                    sq += 10;

                pawn_dist = RowNum[sq] - 1;
                /*pawn_dist holds how many steps the pawn has until promotion.
                  now check the distance of the white king.*/
                king_col_dist = ColNum[xy] - ColNum[prom_square];
                king_col_dist = Abs(king_col_dist);
                king_row_dist = RowNum[xy] - RowNum[prom_square];
                king_row_dist = Abs(king_row_dist);
                king_dist = Max(king_col_dist, king_row_dist);
                if (side_to_move == WHITE)
                    king_dist--;
                if (king_dist > pawn_dist) /*unstoppable*/
                    ret -= ROOK_EG_V;
            }
            b_passed_mask >>= 1U;
            prom_square++; /*next file*/
        }
    }

    /*rook + pawn vs. rook: defending king holds the promotion square*/
    if (piece_info->all_pieces == 5 && piece_info->w_rooks == 1 &&
        piece_info->b_rooks == 1 && pawn_info->w_pawns == 0 && pawn_info->b_pawns == 1)
    {
        /*get the promotion file of the black pawn, that's where the white king should be.*/
        int prom_square, col_pawn, pawn_xy, wrook_col;
        PIECE *p = Bpieces[0].next;

        if (p->type != BPAWN) /*can either be the rook or the pawn*/
            p = p->next;

        pawn_xy = p->xy;
        col_pawn = ColNum[pawn_xy];
        prom_square = col_pawn - 1 + A1;
        wrook_col = ColNum[Wpieces[0].next->xy];

        king_halts_passed = 1;

        /*white king position*/
        if ((xy == prom_square) || (xy == prom_square + 10))
            ret /= 4;
        else
        {
            /*if the king cannot keep the promotion square, then go
              to the shorter side.*/
            if (col_pawn >= BOARD_E_FILE) /*kingside*/
            {
                if ((xy == prom_square + 1) || (xy == prom_square + 11))
                    ret /= 2;
                else if ((xy == prom_square + 21) || (xy == prom_square + 22)|| (xy == prom_square + 12))
                    ret = (ret * 3) / 4; /*no int overflow because eval is small enough*/
            } else
            {
                if ((xy == prom_square - 1) || (xy == prom_square + 9))
                    ret /= 2;
                else if ((xy == prom_square + 19) || (xy == prom_square + 18) || (xy == prom_square + 8))
                    ret = (ret * 3) / 4; /*no int overflow because eval is small enough*/
            }
        }

        /*white rook position*/
        if (col_pawn >= BOARD_E_FILE) /*kingside*/
        {
            if (wrook_col == BOARD_A_FILE)
                ret += 15;
        } else
        {
            if (wrook_col == BOARD_H_FILE)
                ret += 15;
        }

        /*black king cut off from pawn?*/
        if (RowNum[bking] - RowNum[pawn_xy] >= 2)
            ret += 20;
        else
        {
            int _abs_diff;
            _abs_diff = ColNum[bking] - col_pawn;
            _abs_diff = Abs(_abs_diff);
            if (_abs_diff >= 3)
                ret += 20;
        }
    }

    /*if two pawns plus pieces: kings close to the pawns*/
    if ((pawn_info->all_pawns == 2) && (pawn_info->w_pawns == 2 || pawn_info->b_pawns == 2))
    {
        PIECE *p;
        int pawn_xy, col_pawn, row_pawn, col_dist, row_dist, dist, search_type;

        if (pawn_info->w_pawns == 2)
        {
            search_type = WPAWN;
            p = Wpieces[0].next;
        } else
        {
            search_type = BPAWN;
            p = Bpieces[0].next;
        }

        while (p->type != search_type)
            p = p->next;

        pawn_xy = p->xy;
        col_pawn = ColNum[pawn_xy];
        row_pawn = RowNum[pawn_xy];

        col_dist = ColNum[wking] - col_pawn;
        col_dist = Abs(col_dist);
        row_dist = RowNum[wking] - row_pawn;
        row_dist = Abs(row_dist);
        dist  = (col_dist > row_dist) ? col_dist : row_dist;

        col_dist = ColNum[bking] - col_pawn;
        col_dist = Abs(col_dist);
        row_dist = RowNum[bking] - row_pawn;
        row_dist = Abs(row_dist);
        dist -= (col_dist > row_dist) ? col_dist : row_dist;

        while (p->type != search_type)
            p = p->next;

        pawn_xy = p->xy;
        col_pawn = ColNum[pawn_xy];
        row_pawn = RowNum[pawn_xy];

        col_dist = ColNum[wking] - col_pawn;
        col_dist = Abs(col_dist);
        row_dist = RowNum[wking] - row_pawn;
        row_dist = Abs(row_dist);
        dist += (col_dist > row_dist) ? col_dist : row_dist;

        col_dist = ColNum[bking] - col_pawn;
        col_dist = Abs(col_dist);
        row_dist = RowNum[bking] - row_pawn;
        row_dist = Abs(row_dist);
        dist -= (col_dist > row_dist) ? col_dist : row_dist;

        /*both kings have a summary distance of at least 2 to the pawns,
          but that will cancel each other out.*/
        ret -= dist * 10;

        disregard_centre = 1;
    } else
        disregard_centre = 0;

    wk_centre = 0;
    if (disregard_centre == 0)
    {
        if (Central[xy]) { /* Centralised King */
            if (!king_halts_passed) wk_centre += 30;
            if (basic_endgames) wk_centre -= 15;
        } else if (PartCen[xy]) { /* Partially Centralised King */
            if (!king_halts_passed) wk_centre += 15;
            if (basic_endgames) wk_centre -= 25;
        } else if (PartEdg[xy]) { /* King almost on edge */
            if (!king_halts_passed) wk_centre -= 7;
            if (basic_endgames) {
                if (xy==B2 || xy==G2 || xy==B7 || xy==G7) { /* Penalty for almost edge corners */
                    wk_centre -= 150;
                } else if (xy==C2 || xy==F2 || xy==B3 || xy==G3 || xy==B6 || xy==C7 || xy==F7 || xy==G6) {
                    /* smaller penalty for closer to the centre*/
                    wk_centre -=80;
                } else
                    wk_centre -= 50;
            }
        } else { /*if (IsBoardEdge)*/
            /* king on Edge */
            if (king_halts_passed) {
                wk_centre -= 10;
            } else {
                wk_centre -= 25;
            }
            if (basic_endgames) {
                wk_centre -= 200;
                if ((piece_info->all_queens==0) && (piece_info->all_rooks==0)) {
                    if (piece_info->b_bishop_colour == TWO_COLOUR) { /* K+B+B vs K */
                        if (xy==D1 || xy==E1 || xy==H4 || xy==H5 || xy==A4 || xy==A5 || xy==D8 || xy==E8 )
                        {   /* Away from corner */
                            wk_centre += 120;
                        } else if (xy==C1 || xy==F1 || xy==H3 || xy==H6 || xy==A3 || xy==A6 || xy==C8 || xy==F8 )
                        {   /* Partially cornered*/
                            wk_centre -= 10;
                        } else if (xy==A1 || xy==H1 || xy==A8 || xy==H8 || xy==B1 || xy==G1 ||
                                   xy==H2 || xy==H7 || xy==A2 || xy==A7 || xy==B8 || xy==G8)
                        {   /* cornered */
                            wk_centre -= 120;
                        }
                    }
                }
            }
        }
        ret += wk_centre;
    }

    /* Black King special endgame evaluation */
    xy = bking;
    king_halts_passed = 0;
    if ((piece_info->all_pieces == 3) && (piece_info->w_rooks == 1))
        basic_endgames = 0; /*gets special eval*/
    else
        basic_endgames = (three_or_four_pieces_no_pawns && (piece_info->black_pieces==1));
    if (three_or_four_pieces_no_pawns && piece_info->white_pieces==1) {
        int _abs_diff;
        _abs_diff = RowNum[wking] - RowNum[bking];
        ret += (Abs(_abs_diff)) * 5;
        _abs_diff = ColNum[wking] - ColNum[bking];
        ret += (Abs(_abs_diff)) * 5;
    }

    /*Black has only king and pawns?*/
    if (pawn_info->b_pawns + 1 == piece_info->black_pieces)
    {
        /*check for rule of the square with a white passed pawn.*/
        unsigned int w_passed_mask = ((pawn_info->w_pawn_mask) & (pawn_info->w_rook_files));
        int prom_square = A8;
        while (w_passed_mask) /*white passed pawns there*/
        {
            if (w_passed_mask & 1U) /*file found*/
            {
                int king_dist, pawn_dist, king_col_dist, king_row_dist, sq = prom_square - 10;

                while ((board[sq]->type != WPAWN) && (sq > A3)) /*double step*/
                    sq -= 10;

                pawn_dist = 8 - RowNum[sq];
                /*pawn_dist holds how many steps the pawn has until promotion.
                  now check the distance of the black king.*/
                king_col_dist = ColNum[xy] - ColNum[prom_square];
                king_col_dist = Abs(king_col_dist);
                king_row_dist = RowNum[xy] - RowNum[prom_square];
                king_row_dist = Abs(king_row_dist);
                king_dist = Max(king_col_dist, king_row_dist);
                if (side_to_move == BLACK)
                    king_dist--;
                if (king_dist > pawn_dist) /*unstoppable*/
                    ret += ROOK_EG_V;
            }
            w_passed_mask >>= 1U;
            prom_square++; /*next file*/
        }
    }

    /*rook + pawn vs. rook: defending king holds the promotion square*/
    if (piece_info->all_pieces == 5 && piece_info->w_rooks == 1 &&
        piece_info->b_rooks == 1 && pawn_info->w_pawns == 1 && pawn_info->b_pawns == 0)
    {
        /*get the promotion file of the white pawn, that's where the black king should be.*/
        int prom_square, col_pawn, pawn_xy, brook_col;
        PIECE *p = Wpieces[0].next;

        if (p->type != WPAWN) /*can either be the rook or the pawn*/
            p = p->next;

        pawn_xy = p->xy;
        col_pawn = ColNum[pawn_xy];
        prom_square = col_pawn - 1 + A8;
        brook_col = ColNum[Bpieces[0].next->xy];

        king_halts_passed = 1;

        /*black king position*/
        if ((xy == prom_square) || (xy == prom_square - 10))
            ret /= 4;
        else
        {
            /*if the king cannot keep the promotion square, then go
              to the shorter side.*/
            if (col_pawn >= BOARD_E_FILE) /*kingside*/
            {
                if ((xy == prom_square + 1) || (xy == prom_square - 9))
                    ret /= 2;
                else if ((xy == prom_square - 19) || (xy == prom_square - 18)|| (xy == prom_square - 8))
                    ret = (ret * 3) / 4; /*no int overflow because eval is small enough*/
            } else
            {
                if ((xy == prom_square - 1) || (xy == prom_square - 11))
                    ret /= 2;
                else if ((xy == prom_square - 21) || (xy == prom_square - 22) || (xy == prom_square - 12))
                    ret = (ret * 3) / 4; /*no int overflow because eval is small enough*/
            }
        }

        /*black rook position*/
        if (col_pawn >= BOARD_E_FILE) /*kingside*/
        {
            if (brook_col == BOARD_A_FILE)
                ret -= 15;
        } else
        {
            if (brook_col == BOARD_H_FILE)
                ret -= 15;
        }

        /*white king cut off from pawn?*/
        if (RowNum[pawn_xy] - RowNum[wking] >= 2)
            ret -= 20;
        else
        {
            int _abs_diff;
            _abs_diff = ColNum[wking] - col_pawn;
            _abs_diff = Abs(_abs_diff);
            if (_abs_diff >= 3)
                ret -= 20;
        }
    }

    bk_centre = 0;
    if (disregard_centre == 0)
    {
        if (Central[xy]) {
            if (!king_halts_passed) bk_centre -= 30;
            if (basic_endgames) bk_centre += 15;
        } else if (PartCen[xy]) {
            if (!king_halts_passed) bk_centre -= 15;
            if (basic_endgames) bk_centre += 25;
        } else if (PartEdg[xy]) { /* King almost on edge */
            if (!king_halts_passed) bk_centre += 7;
            if (basic_endgames) {
                if (xy==B2 || xy==G2 || xy==B7 || xy==G7) { /* Penalty for almost edge corners */
                    bk_centre += 150;
                } else if (xy==C2 || xy==F2 || xy==B3 || xy==G3 || xy==B6 || xy==C7 || xy==F7 || xy==G6) {
                    /* smaller penalty for closer to the centre*/
                    bk_centre +=80;
                } else
                    bk_centre += 50;
            }
        } else { /*if (IsBoardEdge)*/
            /* king on Edge */
            if (king_halts_passed) {
                bk_centre += 10;
            } else {
                bk_centre += 25;
            }
            if (basic_endgames) {
                bk_centre += 200;
                if ((piece_info->all_queens==0) && (piece_info->all_rooks==0)) {
                    if (piece_info->w_bishop_colour == TWO_COLOUR) { /* K+B+B vs K*/
                        if (xy==D1 || xy==E1 || xy==H4 || xy==H5 || xy==A4 || xy==A5 || xy==D8 || xy==E8 )
                        {   /* Away from corner */
                            bk_centre -= 120;
                        } else if (xy==C1 || xy==F1 || xy==H3 || xy==H6 || xy==A3 || xy==A6 || xy==C8 || xy==F8 )
                        {   /* Partially cornered*/
                            bk_centre += 10;
                        } else if (xy==A1 || xy==H1 || xy==A8 || xy==H8 || xy==B1 || xy==G1 ||
                                   xy==H2 || xy==H7 || xy==A2 || xy==A7 || xy==B8 || xy==G8)
                        {   /* cornered */
                            bk_centre += 120;
                        }
                    }
                }
            }
        }
        ret += bk_centre;
    }

    /*still endgame evaluation.*/
    /* Rooks. only evaluate their passed pawn support. */
    for (i = 2; i <= 3; i++) /*let the compiler roll this loop out.*/
    {
        xy = Wpieces[i].xy;
        if (xy != 0)
        {
            bin_col = 1U << (ColNum[xy] - 1);
            if ((bin_col & pawn_info->w_pawn_mask) == 0)
                /*standing on a free file*/
            {
                if ((bin_col & pawn_info->b_pawn_mask) && (bin_col & pawn_info->b_rook_files))
                    /*black passed pawn*/
                {
                    int pawn_xy;
                    /*start our scan for the passed pawn from the 2nd rank on*/
                    pawn_xy = ColNum[xy]+(A2-1);
                    while (board[pawn_xy]->type != BPAWN)
                        pawn_xy += 10;

                    /*can we see our rook without further obstructions from that position on?*/
                    if (pawn_xy < xy)
                        /*then scan upwards*/
                    {
                        do {
                            pawn_xy += 10;
                        } while (((board[pawn_xy]->type == NO_PIECE) || (board[pawn_xy]->type == WROOK) || (board[pawn_xy]->type == WQUEEN)) && (pawn_xy != xy));

                        if (pawn_xy == xy)
                            /*then our rook stands behind that black passed pawn*/
                            ret += 30;
                    } else
                    {
                        /*then our rook stands in front of that black passed pawn. still important to stop it.*/
                        ret += 5;
                    }
                }
            } else if (bin_col & pawn_info->w_rook_files)
                /*white passed pawn*/
            {
                int pawn_xy;
                /*start our scan for the passed pawn from the 7th rank on*/
                pawn_xy = ColNum[xy]+(A7-1);
                while (board[pawn_xy]->type != WPAWN)
                    pawn_xy -= 10;

                /*can we see our rook without further obstructions from that position on?*/
                if (pawn_xy > xy)
                    /*then scan downwards*/
                {
                    do {
                        pawn_xy -= 10;
                    } while (((board[pawn_xy]->type == NO_PIECE) || (board[pawn_xy]->type == WROOK) || (board[pawn_xy]->type == WQUEEN)) && (pawn_xy != xy));

                    if (pawn_xy == xy)
                        /*then our rook stands behind our passed pawn*/
                        ret += 30;
                } else
                {
                    /*then our rook stands in front of our passed pawn. not that good.*/
                    ret += 5;
                }
            }
        }

        xy = Bpieces[i].xy;
        if (xy != 0)
        {
            bin_col = 1U << (ColNum[xy] - 1);
            if ((bin_col & pawn_info->b_pawn_mask) == 0)
                /*standing on a free file*/
            {
                if ((bin_col & pawn_info->w_pawn_mask) && (bin_col & pawn_info->w_rook_files))
                    /*white passed pawn*/
                {
                    int pawn_xy;
                    /*start our scan for the passed pawn from the 7th rank on*/
                    pawn_xy = ColNum[xy]+(A7-1);
                    while (board[pawn_xy]->type != WPAWN)
                        pawn_xy -= 10;

                    /*can we see our rook without further obstructions from that position on?*/
                    if (pawn_xy > xy)
                        /*then scan downwards*/
                    {
                        do {
                            pawn_xy -= 10;
                        } while (((board[pawn_xy]->type == NO_PIECE) || (board[pawn_xy]->type == BROOK) || (board[pawn_xy]->type == BQUEEN)) && (pawn_xy != xy));

                        if (pawn_xy == xy)
                            /*then our rook stands behind that black passed pawn*/
                            ret -= 30;
                    } else
                    {
                        /*then our rook stands in front of that black passed pawn. still important to stop it.*/
                        ret -= 5;
                    }
                }
            } else if (bin_col & pawn_info->b_rook_files)
                /*black passed pawn*/
            {
                int pawn_xy;
                /*start our scan for the passed pawn from the 2nd rank on*/
                pawn_xy = ColNum[xy]+(A2-1);
                while (board[pawn_xy]->type != BPAWN)
                    pawn_xy += 10;

                /*can we see our rook without further obstructions from that position on?*/
                if (pawn_xy < xy)
                    /*then scan upwards*/
                {
                    do {
                        pawn_xy += 10;
                    } while (((board[pawn_xy]->type == NO_PIECE) || (board[pawn_xy]->type == BROOK) || (board[pawn_xy]->type == BQUEEN)) && (pawn_xy != xy));

                    if (pawn_xy == xy)
                        /*then our rook stands behind our passed pawn*/
                        ret -= 30;
                } else
                {
                    /*then our rook stands in front of our passed pawn. not that good.*/
                    ret -= 5;
                }
            }
        }
    }

    /*general note on draw endgames: the return value sometimes isn't zero,
      but something small like +/- 5 centipawns as to ensure that the weaker
      side favours draw by repetition or taking an opportunity to hit and
      make draw by insufficient material.*/

    /*one minor piece ahead, but no pawns is drawish*/
    if ((pawn_info->all_pawns != 0) &&
        (((pawn_info->w_pawns == 0) && (ret > 0) &&
          (piece_info->w_knights + piece_info->w_bishops == piece_info->b_knights + piece_info->b_bishops + 1)) ||
         ((pawn_info->b_pawns == 0) && (ret < 0) &&
          (piece_info->w_knights + piece_info->w_bishops + 1 == piece_info->b_knights + piece_info->b_bishops))) &&
        (piece_info->w_queens == piece_info->b_queens) &&
        (piece_info->w_rooks == piece_info->b_rooks))
    {
        /*no mate threats without queens and rooks.
          note that w_queens==b_queens and w_rooks==b_rooks at this point.*/
        if ((piece_info->w_queens == 0) && (piece_info->w_rooks == 0))
            ret = (ret > 0) ? -1 : 1;
        else
            ret /= 4;
    }

    if (UNLIKELY(pawn_info->all_pawns == 0))
    {
        /*if there are no pawns, being less than a rook up (e.g. just a minor
          piece) is usually a draw.*/
        if (Abs(pure_material) < EG_WINNING_MARGIN)
            ret /= 4;
        else
        {
            if (piece_info->black_pieces == 1) /*mate against lone black king*/
            {
                if ((piece_info->white_pieces > 3) || /*not elementary like KBN:K*/
                    ((piece_info->white_pieces == 3) && (piece_info->all_minor_pieces < 2)))
                {
                    int _abs_diff, king_col_dist, king_row_dist;

                    _abs_diff = ColNum[wking] - ColNum[bking];
                    king_col_dist = Abs(_abs_diff);
                    _abs_diff = RowNum[wking] - RowNum[bking];
                    king_row_dist = Abs(_abs_diff);

                    ret -=  (king_col_dist + king_row_dist - 2) * 30;

                    ret += 2 * CentreManhattanDist[bking];

                }
            } else if (piece_info->white_pieces == 1) /*mate against lone white king*/
            {
                if ((piece_info->black_pieces > 3) || /*not elementary like KBN:K*/
                    ((piece_info->black_pieces == 3) && (piece_info->all_minor_pieces < 2)))
                {
                    int _abs_diff, king_col_dist, king_row_dist;

                    _abs_diff = ColNum[wking] - ColNum[bking];
                    king_col_dist = Abs(_abs_diff);
                    _abs_diff = RowNum[wking] - RowNum[bking];
                    king_row_dist = Abs(_abs_diff);

                    ret +=  (king_col_dist + king_row_dist - 2) * 30;

                    ret -= 2 * CentreManhattanDist[wking];
                }
            }
        }
    }

    if ((piece_info->all_minor_pieces == 1) && (pawn_info->all_pawns != 0) && (piece_info->all_pieces - pawn_info->all_pawns == 3))
    {
        /*check KBp vs. K. That is draw with A or H pawn and the "wrong"
          bishop if the enemy king controls the promotion square. This
          implementation works also with more than one pawn on the A or H file
          since the pawn mask is checked.

          Also tricky positions like these work:
          8/8/1b5p/8/6P1/8/5k1K/8 w - - 0 1 (1. Kh1 draws; 1. Kh3 loses)
          8/8/1b5p/8/6P1/7K/5k2/8 b - - 0 1 (1. ... Bc7 wins; 1. ... Kg1 also, but takes longer)

          The stalemate recognition is partly in the main search, partly in
          quiescence.*/
        if ((pawn_info->w_pawns > 0) && (pawn_info->b_pawns <= 2) &&
            (piece_info->white_pieces - pawn_info->w_pawns > 1))
        {
            if (piece_info->w_bishops == 1) /*white has bishop and pawns*/
            {
                if ((pawn_info->w_pawn_mask == A_FILE) && (piece_info->w_bishop_colour == DARK_SQ)) /*a-pawns and dark Bishop*/
                {
                    if ((bking == A8) || (bking == A7) || (bking == B8) || (bking == B7))
                        ret = 5;
                    else
                    {
                        int row_dist, col_dist, def_king_dist, att_king_dist, pawn_dist;

                        row_dist = 8 - RowNum[bking]; /*A8 is 8th row*/
                        col_dist = ColNum[bking] - 1; /*A8 is 1st col*/
                        def_king_dist = (row_dist > col_dist) ? row_dist : col_dist;
                        if (side_to_move == BLACK)
                            def_king_dist--;

                        row_dist = 8 - RowNum[wking]; /*A8 is 8th row*/
                        col_dist = ColNum[wking] - 1; /*A8 is 1st col*/
                        att_king_dist = (row_dist > col_dist) ? row_dist : col_dist;

                        /*defender may reach the edge?*/
                        if (def_king_dist < att_king_dist)
                        {
                            for (i = A7; i > A3; i -= 10)
                                if (board[i]->type == WPAWN)
                                    break;
                            pawn_dist = 8 - RowNum[i];

                            if (def_king_dist <= pawn_dist)
                            {
                                if (pawn_info->w_pawns == 1)
                                    ret = 0;
                                else
                                    ret -= bk_centre + 150;
                                ret += def_king_dist * 5;
                            }
                        }
                        ret += def_king_dist * 5;
                    }
                } else if ((pawn_info->w_pawn_mask == H_FILE) && (piece_info->w_bishop_colour == LIGHT_SQ)) /*h-pawns and light Bishop*/
                {
                    if ((bking == H8) || (bking == H7) || (bking == G8) || (bking == G7))
                        ret = 5;
                    else
                    {
                        int row_dist, col_dist, def_king_dist, att_king_dist, pawn_dist;

                        row_dist = 8 - RowNum[bking]; /*H8 is 8th row*/
                        col_dist = 8 - ColNum[bking]; /*H8 is 8th col*/
                        def_king_dist = (row_dist > col_dist) ? row_dist : col_dist;
                        if (side_to_move == BLACK)
                            def_king_dist--;

                        row_dist = 8 - RowNum[wking]; /*H8 is 8th row*/
                        col_dist = 8 - ColNum[wking]; /*H8 is 8th col*/
                        att_king_dist = (row_dist > col_dist) ? row_dist : col_dist;

                        /*defender may reach the edge?*/
                        if (def_king_dist < att_king_dist)
                        {
                            for (i = H7; i > H3; i -= 10)
                                if (board[i]->type == WPAWN)
                                    break;
                            pawn_dist = 8 - RowNum[i];

                            if (def_king_dist <= pawn_dist)
                            {
                                if (pawn_info->w_pawns == 1)
                                    ret = 0;
                                else
                                    ret -= bk_centre + 150;
                                ret += def_king_dist * 5;
                            }
                        }
                        ret += def_king_dist * 5;
                    }
                }
            } else if ((piece_info->w_knights == 1) && (pawn_info->w_pawns == 1) &&
                       (pawn_info->b_pawns == 0)) /*white has knight and pawn*/
            {
                if (board[A7]->type == WPAWN)
                {
                    if ((bking == A8) || (bking == B8) || (bking == B7))
                        ret = 5;
                } else if (board[H7]->type == WPAWN)
                {
                    if ((bking == H8) || (bking == G8) || (bking == G7))
                        ret = 5;
                }
            }
        } else if ((pawn_info->b_pawns > 0) && (pawn_info->w_pawns <= 2) &&
                   (piece_info->black_pieces - pawn_info->b_pawns > 1))
        {
            if (piece_info->b_bishops == 1) /*black has bishop and pawns*/
            {
                if ((pawn_info->b_pawn_mask == A_FILE) && (piece_info->b_bishop_colour == LIGHT_SQ)) /*a-pawns and light Bishop*/
                {
                    if ((wking == A1) || (wking == A2) || (wking == B1) || (wking == B2))
                        ret = -5;
                    else
                    {
                        int row_dist, col_dist, def_king_dist, att_king_dist, pawn_dist;

                        row_dist = RowNum[wking] - 1; /*A1 is 1st row*/
                        col_dist = ColNum[wking] - 1; /*A1 is 1st col*/
                        def_king_dist = (row_dist > col_dist) ? row_dist : col_dist;
                        if (side_to_move == WHITE)
                            def_king_dist--;

                        row_dist = RowNum[bking] - 1; /*A1 is 1st row*/
                        col_dist = ColNum[bking] - 1; /*A1 is 1st col*/
                        att_king_dist = (row_dist > col_dist) ? row_dist : col_dist;

                        /*defender may reach the edge?*/
                        if (def_king_dist < att_king_dist)
                        {
                            for (i = A2; i < A6; i += 10)
                                if (board[i]->type == BPAWN)
                                    break;
                            pawn_dist = RowNum[i] - 1;

                            if (def_king_dist <= pawn_dist)
                            {
                                if (pawn_info->b_pawns == 1)
                                    ret = 0;
                                else
                                    ret += 150 - wk_centre;
                                ret -= def_king_dist * 5;
                            }
                        }
                        ret -= def_king_dist * 5;
                    }
                } else if ((pawn_info->b_pawn_mask == H_FILE) && (piece_info->b_bishop_colour == DARK_SQ)) /*h-pawns and dark Bishop*/
                {
                    if ((wking == H1) || (wking == H2) || (wking == G1) || (wking == G2))
                        ret = -5;
                    else
                    {
                        int row_dist, col_dist, def_king_dist, att_king_dist, pawn_dist;

                        row_dist = RowNum[wking] - 1; /*H1 is 1st row*/
                        col_dist = 8 - ColNum[wking]; /*H1 is 8th col*/
                        def_king_dist = (row_dist > col_dist) ? row_dist : col_dist;
                        if (side_to_move == WHITE)
                            def_king_dist--;

                        row_dist = RowNum[bking] - 1; /*H1 is 1st row*/
                        col_dist = 8 - ColNum[bking]; /*H1 is 8th col*/
                        att_king_dist = (row_dist > col_dist) ? row_dist : col_dist;

                        /*defender may reach the edge?*/
                        if (def_king_dist < att_king_dist)
                        {
                            for (i = H2; i < H6; i += 10)
                                if (board[i]->type == BPAWN)
                                    break;
                            pawn_dist = RowNum[i] - 1;

                            if (def_king_dist <= pawn_dist)
                            {
                                if (pawn_info->b_pawns == 1)
                                    ret = 0;
                                else
                                    ret += 150 - wk_centre;
                                ret -= def_king_dist * 5;
                            }
                        }
                        ret -= def_king_dist * 5;
                    }
                }
            } else if ((piece_info->b_knights == 1) && (pawn_info->b_pawns == 1) &&
                       (pawn_info->w_pawns == 0)) /*black has knight and pawn*/
            {
                if (board[A2]->type == BPAWN)
                {
                    if ((wking == A1) || (wking == B1) || (wking == B2))
                        ret = -5;
                } else if (board[H2]->type == BPAWN)
                {
                    if ((wking == H1) || (wking == G1) || (wking == G2))
                        ret = -5;
                }
            }
        }
    } else if ((piece_info->all_pieces - pawn_info->all_pawns == 2 /*only pawns - 2 is the kings*/) && (pawn_info->all_pawns > 1) && ((pawn_info->w_pawns == 0) || (pawn_info->b_pawns == 0)))
    {
        /*check for the configuration with a doubled, tripled or whatever'ed
          pawn on the A or H file against the lone king. that is draw if the
          defender controls the promotion square. the check for a single pawn
          on the A or H file is not being done here, but several cases further
          down, using the KPK lookup table.
          that's why "pawn_info->all_pawns > 1" is checked here.*/
        if (pawn_info->w_pawns == 0) /*no white pawns*/
        {
            if (pawn_info->b_pawn_mask == A_FILE)
            {
                if ((wking == A1) || (wking == A2) || (wking == B1) || (wking == B2))
                    ret = -5;
            } else if (pawn_info->b_pawn_mask == H_FILE)
            {
                if ((wking == H1) || (wking == H2) || (wking == G1) || (wking == G2))
                    ret = -5;
            }
        } else /*no black pawns*/
        {
            if (pawn_info->w_pawn_mask == A_FILE)
            {
                if ((bking == A8) || (bking == A7) || (bking == B8) || (bking == B7))
                    ret = 5;
            } else if (pawn_info->w_pawn_mask == H_FILE)
            {
                if ((bking == H8) || (bking == H7) || (bking == G8) || (bking == G7))
                    ret = 5;
            }
        }
    }
    else if (piece_info->all_pieces == 4)
    {
        if (piece_info->all_minor_pieces != 0)
        {
            if ((piece_info->w_rooks == 1) && (piece_info->b_bishops == 1)) /*K+R vs. K+B*/
                /*K+R vs. K+B is draw if the king with the bishop keeps away
                 from the corner that has the colour of his bishop.*/
            {
                if (piece_info->b_bishop_colour == LIGHT_SQ)
                    ret = (ROOK_EG_V-BISHOP_EG_V) + LightBishopRook[bking];
                else
                    ret = (ROOK_EG_V-BISHOP_EG_V) + DarkBishopRook[bking];
                /*make the difference clearly smaller than the material difference
                  to indicate it is tending towards draw. Note that if the king
                  with the bishop is in the "wrong" corner, this will still be
                  greater than the difference in material.*/
                ret /= 2;
            } else if ((piece_info->b_rooks == 1) && (piece_info->w_bishops == 1)) /*K+B vs. K+R*/
            {
                if (piece_info->w_bishop_colour == LIGHT_SQ)
                    ret = - ((ROOK_EG_V-BISHOP_EG_V) + LightBishopRook[wking]);
                else
                    ret = - ((ROOK_EG_V-BISHOP_EG_V) + DarkBishopRook[wking]);
                /*make the difference clearly smaller than the material difference
                  to indicate it is tending towards draw. Note that if the king
                  with the bishop is in the "wrong" corner, this will still be
                  greater than the difference in material.*/
                ret /= 2;
            } else if ((piece_info->w_rooks == 1) && (piece_info->b_knights == 1))
                /*K+R vs. K+N: the weaker party has to keep the king as
                  centralised as possible and must keep the knight close
                  to the king.*/
            {
                int _abs_diff, j;
                /*find the black knights's position*/
                xy = Bpieces[0].next->xy;

                /*get the distance between knight and king, counted in king moves*/
                _abs_diff = ColNum[bking] - ColNum[xy];
                i = Abs(_abs_diff);
                _abs_diff = RowNum[bking] - RowNum[xy];
                j = Abs(_abs_diff);
                xy = (i > j) ? i : j;

                xy--; /*neighbouring squares count as "no distance"*/

                /*keeping the knight close.*/
                if (xy > 0)
                {
                    xy *= 80;
                    xy -= 20;
                } else
                    xy = 0;

                ret = ((ROOK_EG_V-KNIGHT_EG_V) + KnightRook[bking] + xy)/2;
            } else if ((piece_info->b_rooks == 1) && (piece_info->w_knights == 1))
                /*K+R vs. K+N: the weaker party has to keep the king as
                  centralised as possible and must keep the knight close
                  to the king.*/
            {
                int _abs_diff, j;
                /*find the white knights's position*/
                xy = Wpieces[0].next->xy;

                /*get the distance between knight and king, counted in king moves*/
                _abs_diff = ColNum[wking] - ColNum[xy];
                i = Abs(_abs_diff);
                _abs_diff = RowNum[wking] - RowNum[xy];
                j = Abs(_abs_diff);
                xy = (i > j) ? i : j;

                xy--; /*neighbouring squares count as "no distance"*/

                /*keeping the knight close.*/
                if (xy > 0)
                {
                    xy *= 80;
                    xy -= 20;
                } else
                    xy = 0;

                ret = -((ROOK_EG_V-KNIGHT_EG_V) + KnightRook[wking] + xy)/2;
            } else if ((piece_info->w_bishops == 1) && (piece_info->w_knights == 1))
            {
                /*find the knight*/
                PIECE *p = Wpieces[0].next;
                i = (p->type == WKNIGHT) ? p->xy : p->next->xy;
                ret = BISHOP_EG_V + KNIGHT_EG_V + PAWN_EG_V + Eval_KingKnightBishop_King(bking, wking, piece_info->w_bishop_colour, i);
            }
            else if ((piece_info->b_bishops == 1) && (piece_info->b_knights == 1))
            {
                /*find the knight*/
                PIECE *p = Bpieces[0].next;
                i = (p->type == BKNIGHT) ? p->xy : p->next->xy;
                ret = -(BISHOP_EG_V + KNIGHT_EG_V + PAWN_EG_V + Eval_KingKnightBishop_King(wking, bking, piece_info->b_bishop_colour, i));
            } else if (piece_info->w_knights == 2)
            {
                int _abs_diff, j;
                PIECE *p;
                /*get the distance between the kings, counted in king moves*/
                _abs_diff = ColNum[wking] - ColNum[bking];
                i = Abs(_abs_diff);
                _abs_diff = RowNum[wking] - RowNum[bking];
                j = Abs(_abs_diff);
                xy = (i > j) ? i : j;

                /*still draw, but reward forcing the black king to the border
                  and getting the white king close. maybe the opponent commits
                  an error?*/
                ret = (30 - CentraliseE[bking] - xy);

                /*get the knights close*/

                p = Wpieces[0].next;
                _abs_diff = ColNum[wking] - ColNum[p->xy];
                i = Abs(_abs_diff);
                _abs_diff = RowNum[wking] - RowNum[p->xy];
                j = Abs(_abs_diff);
                ret -= (i > j) ? i : j;

                p = p->next;
                _abs_diff = ColNum[wking] - ColNum[p->xy];
                i = Abs(_abs_diff);
                _abs_diff = RowNum[wking] - RowNum[p->xy];
                j = Abs(_abs_diff);
                ret -= (i > j) ? i : j;

                /*worst case scenario: eval is still positive*/
            } else if (piece_info->b_knights == 2)
            {
                int _abs_diff, j;
                PIECE *p;
                /*get the distance between the kings, counted in king moves*/
                _abs_diff = ColNum[wking] - ColNum[bking];
                i = Abs(_abs_diff);
                _abs_diff = RowNum[wking] - RowNum[bking];
                j = Abs(_abs_diff);
                xy = (i > j) ? i : j;

                /*still draw, but reward forcing the white king to the border
                  and getting the black king close. maybe the opponent commits
                  an error?*/
                ret = -(30 - CentraliseE[wking] - xy);

                /*get the knights close*/

                p = Bpieces[0].next;
                _abs_diff = ColNum[bking] - ColNum[p->xy];
                i = Abs(_abs_diff);
                _abs_diff = RowNum[bking] - RowNum[p->xy];
                j = Abs(_abs_diff);
                ret += (i > j) ? i : j;

                p = p->next;
                _abs_diff = ColNum[bking] - ColNum[p->xy];
                i = Abs(_abs_diff);
                _abs_diff = RowNum[bking] - RowNum[p->xy];
                j = Abs(_abs_diff);
                ret += (i > j) ? i : j;
                /*worst case scenario: eval is still negative*/
            }
        } else /*4 pieces without minor pieces*/
        {
            if ((piece_info->w_queens == 1) && (pawn_info->b_pawns == 1))
            {
                /*queen draws against a/c/f/h pawn on 2nd rank with close king*/
                int pawn_xy = Bpieces[0].next->xy;
                if (RowNum[pawn_xy] == 2)
                {
                    if ((pawn_xy == A2) || (pawn_xy == C2) || (pawn_xy == F2) || (pawn_xy == H2))
                    {
                        int _abs_diff, king_dist, king_col_dist, king_row_dist, def_to_move;

                        _abs_diff = ColNum[wking] - ColNum[pawn_xy];
                        king_col_dist = Abs(_abs_diff);
                        _abs_diff = RowNum[wking] - RowNum[pawn_xy];
                        king_row_dist = Abs(_abs_diff);

                        if (king_col_dist > king_row_dist)
                            king_dist = king_col_dist;
                        else
                            king_dist = king_row_dist;

                        if (side_to_move == BLACK) def_to_move = 1; else def_to_move = 0;
                        /*if the defender, i.e. the side with the pawn, is to move, the attacking
                          king may be closer by one square without changing the result.*/

                        if (pawn_xy == C2)
                        {
                            if (king_dist >= 3-def_to_move)
                            {
                                if ((bking == B1) || (bking == B2))
                                    ret = 10;
                                else if (bking == A1) /*only go to a1 if b2 or b1 are not possible*/
                                    ret = 15;
                            } else ret += (16 - 8 * king_dist);
                        } else if (pawn_xy == F2)
                        {
                            if (king_dist >= 3-def_to_move)
                            {
                                if ((bking == G1) || (bking == G2))
                                    ret = 10;
                                else if (bking == H1) /*only go to h1 if g2 or g1 are not possible*/
                                    ret = 15;
                            } else ret += (16 - 8 * king_dist);
                        } else if (pawn_xy == A2)
                        {
                            if ((bking == A1) && (def_to_move) &&
                                (((ColNum[Wpieces[0].next->xy] == BOARD_B_FILE) && (RowNum[Wpieces[0].next->xy] >= 3)) ||
                                (Wpieces[0].next->xy == C2) || (king_dist >= 5)))
                            /*stalemate conditions or attacking king far enough away to get out of the corner*/
                            {
                                ret = 10;
                            } else if (king_dist >= 5-def_to_move)
                            {
                                if ((bking == B1) || (bking == B2) ||
                                    ((bking == A1) && (ColNum[Wpieces[0].next->xy] == BOARD_B_FILE)))
                                {
                                    ret = 10;
                                }
                            } else ret += (40 - 8 * king_dist);
                        } else if (pawn_xy == H2)
                        {
                            if ((bking == H1) && (def_to_move) &&
                                (((ColNum[Wpieces[0].next->xy] == BOARD_G_FILE) && (RowNum[Wpieces[0].next->xy] >= 3)) ||
                                (Wpieces[0].next->xy == F2) || (king_dist >= 5)))
                            /*stalemate conditions or attacking king far enough away to get out of the corner*/
                            {
                                ret = 10;
                            } else if (king_dist >= 5-def_to_move)
                            {
                                if ((bking == G1) || (bking == G2) ||
                                    ((bking == H1) && (ColNum[Wpieces[0].next->xy] == BOARD_G_FILE)))
                                {
                                    ret = 10;
                                }
                            } else ret += (40 - 8 * king_dist);
                        }
                    }
                }
            } else if ((piece_info->b_queens == 1) && (pawn_info->w_pawns == 1))
            {
                /*queen draws against a/c/f/h pawn on 7th rank with close king*/
                int pawn_xy = Wpieces[0].next->xy;
                if (RowNum[pawn_xy] == 7)
                {
                    if ((pawn_xy == A7) || (pawn_xy == C7) || (pawn_xy == F7) || (pawn_xy == H7))
                    {
                        int _abs_diff, king_dist, king_col_dist, king_row_dist, def_to_move;

                        _abs_diff = ColNum[bking] - ColNum[pawn_xy];
                        king_col_dist = Abs(_abs_diff);
                        _abs_diff = RowNum[bking] - RowNum[pawn_xy];
                        king_row_dist = Abs(_abs_diff);

                        if (king_col_dist > king_row_dist)
                            king_dist = king_col_dist;
                        else
                            king_dist = king_row_dist;

                        if (side_to_move == WHITE) def_to_move = 1; else def_to_move = 0;
                        /*if the defender, i.e. the side with the pawn, is to move, the attacking
                          king may be closer by one square without changing the result.*/

                        if (pawn_xy == C7)
                        {
                            if (king_dist >= 3-def_to_move)
                            {
                                if ((wking == B8) || (wking == B7))
                                    ret = -10;
                                else if (wking == A8) /*only go to a8 if b7 or b8 are not possible*/
                                    ret = -15;
                            } else ret -= (16 - 8 * king_dist);
                        } else if (pawn_xy == F7)
                        {
                            if (king_dist >= 3-def_to_move)
                            {
                                if ((wking == G8) || (wking == G7))
                                    ret = -10;
                                else if (wking == H8) /*only go to h8 if g7 or g8 are not possible*/
                                    ret = -15;
                            } else ret -= (16 - 8 * king_dist);
                        } else if (pawn_xy == A7)
                        {
                            if ((wking == A8) && (def_to_move) &&
                                (((ColNum[Bpieces[0].next->xy] == BOARD_B_FILE) && (RowNum[Bpieces[0].next->xy] <= 6)) ||
                                (Bpieces[0].next->xy == C7) || (king_dist >= 5)))
                            /*stalemate conditions or attacking king far enough away to get out of the corner*/
                            {
                                ret = -10;
                            } else if (king_dist >= 5-def_to_move)
                            {
                                if ((wking == B8) || (wking == B7) ||
                                    ((wking == A8) && (ColNum[Bpieces[0].next->xy] == BOARD_B_FILE)))
                                {
                                    ret = -10;
                                }
                            } else ret -= (40 - 8 * king_dist);
                        } else if (pawn_xy == H7)
                        {
                            if ((wking == H8) && (def_to_move) &&
                                (((ColNum[Bpieces[0].next->xy] == BOARD_G_FILE) && (RowNum[Bpieces[0].next->xy] <= 6)) ||
                                (Bpieces[0].next->xy == F7) || (king_dist >= 5)))
                            /*stalemate conditions or attacking king far enough away to get out of the corner*/
                            {
                                ret = -10;
                            } else if (king_dist >= 5-def_to_move)
                            {
                                if ((wking == G8) || (wking == G7) ||
                                    ((wking == H8) && (ColNum[Bpieces[0].next->xy] == BOARD_G_FILE)))
                                {
                                    ret = -10;
                                }
                            } else ret -= (40 - 8 * king_dist);
                        }
                    }
                }
            } else if ((piece_info->w_rooks == 1) && (pawn_info->b_pawns == 1))
            {
                /*since draw with queen against a/c/f/h pawn is implemented,
                  rook against pawn on 2nd/7th rank also has to be there because otherwise,
                  the engine would underpromote to a rook.*/
                int pawn_xy = Bpieces[0].next->xy;
                if (RowNum[pawn_xy] == 2)
                {
                    int _abs_diff, def_king_pawn_dist, king_col_dist, king_row_dist;

                    /*distance of defending king to promotion square*/
                    _abs_diff = ColNum[bking] - ColNum[pawn_xy];
                    king_col_dist = Abs(_abs_diff);
                    _abs_diff = RowNum[bking] - RowNum[pawn_xy-10];
                    king_row_dist = Abs(_abs_diff);

                    if (king_col_dist > king_row_dist)
                        def_king_pawn_dist = king_col_dist;
                    else
                        def_king_pawn_dist = king_row_dist;

                    /*if the rook side could take the pawn while not losing the rook,
                      then quiescence search would not have lead to KRKP.*/
                    if (def_king_pawn_dist <= 2)
                    {
                        int att_king_pawn_dist;
                        /*distance from attacking king to promotion square*/
                        _abs_diff = ColNum[wking] - ColNum[pawn_xy];
                        king_col_dist = Abs(_abs_diff);
                        _abs_diff = RowNum[wking] - RowNum[pawn_xy-10];
                        king_row_dist = Abs(_abs_diff);

                        if (king_col_dist > king_row_dist)
                            att_king_pawn_dist = king_col_dist;
                        else
                            att_king_pawn_dist = king_row_dist;

                        if (bking == pawn_xy - 10) att_king_pawn_dist--;
                        if (side_to_move == WHITE) att_king_pawn_dist--;

                        if (att_king_pawn_dist >= def_king_pawn_dist + 1)
                            ret = -50;
                    }
                }
            }  else if ((piece_info->b_rooks == 1) && (pawn_info->w_pawns == 1))
            {
                /*since draw with queen against a/c/f/h pawn is implemented,
                  rook against pawn on 2nd/7th rank also has to be there because otherwise,
                  the engine would underpromote to a rook.*/
                int pawn_xy = Wpieces[0].next->xy;
                if (RowNum[pawn_xy] == 7)
                {
                    int _abs_diff, def_king_pawn_dist, king_col_dist, king_row_dist;

                    /*distance of defending king to promotion square*/
                    _abs_diff = ColNum[wking] - ColNum[pawn_xy];
                    king_col_dist = Abs(_abs_diff);
                    _abs_diff = RowNum[wking] - RowNum[pawn_xy-10];
                    king_row_dist = Abs(_abs_diff);

                    if (king_col_dist > king_row_dist)
                        def_king_pawn_dist = king_col_dist;
                    else
                        def_king_pawn_dist = king_row_dist;

                    /*if the rook side could take the pawn while not losing the rook,
                      then quiescence search would not have lead to KRKP.*/
                    if (def_king_pawn_dist <= 2)
                    {
                        int att_king_pawn_dist;
                        /*distance from attacking king to promotion square*/
                        _abs_diff = ColNum[bking] - ColNum[pawn_xy];
                        king_col_dist = Abs(_abs_diff);
                        _abs_diff = RowNum[bking] - RowNum[pawn_xy-10];
                        king_row_dist = Abs(_abs_diff);

                        if (king_col_dist > king_row_dist)
                            att_king_pawn_dist = king_col_dist;
                        else
                            att_king_pawn_dist = king_row_dist;

                        if (wking == pawn_xy + 10) att_king_pawn_dist--;
                        if (side_to_move == BLACK) att_king_pawn_dist--;

                        if (att_king_pawn_dist >= def_king_pawn_dist + 1)
                            ret = 50;
                    }
                }
            } else if ((piece_info->w_queens == 1) && (piece_info->b_rooks == 1))
            {
                /*KQ vs KR*/
                int cdiff, rdiff, king_dist, rook_dist, rook_xy;

                cdiff = ColNum[wking] - ColNum[bking];
                cdiff = Abs(cdiff);
                rdiff = RowNum[wking] - RowNum[bking];
                rdiff = Abs(rdiff);
                king_dist = Max(cdiff, rdiff) - 2;

                rook_xy = Bpieces[0].next->xy;
                cdiff = ColNum[rook_xy] - ColNum[bking];
                cdiff = Abs(cdiff);
                rdiff = RowNum[rook_xy] - RowNum[bking];
                rdiff = Abs(rdiff);
                rook_dist = Max(cdiff, rdiff) - 1;

                ret = (PAWN_EG_V + QUEEN_EG_V - ROOK_EG_V) + (CentreDist[bking] << 1) - (king_dist << 4) + (rook_dist << 2);
            }  else if ((piece_info->b_queens == 1) && (piece_info->w_rooks == 1))
            {
                /*KR vs KQ*/
                int cdiff, rdiff, king_dist, rook_dist, rook_xy;

                cdiff = ColNum[wking] - ColNum[bking];
                cdiff = Abs(cdiff);
                rdiff = RowNum[wking] - RowNum[bking];
                rdiff = Abs(rdiff);
                king_dist = Max(cdiff, rdiff) - 2;

                rook_xy = Wpieces[0].next->xy;
                cdiff = ColNum[rook_xy] - ColNum[wking];
                cdiff = Abs(cdiff);
                rdiff = RowNum[rook_xy] - RowNum[wking];
                rdiff = Abs(rdiff);
                rook_dist = Max(cdiff, rdiff) - 1;

                ret = -((PAWN_EG_V + QUEEN_EG_V - ROOK_EG_V) + (CentreDist[wking] << 1) - (king_dist << 4) + (rook_dist << 2));
            }
        } /*end of 4 pieces without minor pieces*/
    } else if (piece_info->all_pieces == 3)
    {
        if (pawn_info->all_pawns == 1) /*Kp vs K or K vs Kp*/
        {
            int is_won;

            if (pawn_info->w_pawns == 1) /*Kp vs K*/
            {
                /*find the white pawn*/
                xy = Wpieces[0].next->xy;

                if (side_to_move == WHITE) /*white to move*/
                    is_won = Kpk_Probe(KPK_WHITE, boardXY[wking], boardXY[xy], boardXY[bking]);
                else
                    is_won = Kpk_Probe(KPK_BLACK, boardXY[wking], boardXY[xy], boardXY[bking]);

                if (is_won == 0)
                {
                    /*if the computer has the pawn advantage, it is still draw,
                      but push the pawn forward because that will force the
                      game to the end much faster, and maybe the opponent goes
                      wrong.*/
                    ret = (2+(RowNum[xy]-2)*2);
                }
                else
                {
                    /*this is won, and a won position is better if the pawn is
                      more advanced, but it must be worth less than a queen to
                      reward promotion.*/
                    ret = (ROOK_EG_V + (RowNum[xy]-2)*20);
                }
            } else /*K vs. Kp*/
            {
                /*find the black pawn*/
                xy = Bpieces[0].next->xy;

                if (side_to_move == WHITE) /*white to move, reversed*/
                    is_won = Kpk_Probe_Reverse(KPK_BLACK, boardXY[wking], boardXY[xy], boardXY[bking]);
                else
                    is_won = Kpk_Probe_Reverse(KPK_WHITE, boardXY[wking], boardXY[xy], boardXY[bking]);

                if (is_won == 0)
                {
                    /*if the computer has the pawn advantage, it is still draw,
                      but push the pawn forward because that will force the
                      game to the end much faster, and maybe the opponent goes
                      wrong.*/
                    ret = -(2+(7-RowNum[xy])*2);
                }
                else
                {
                    /*this is won, and a won position is better if the pawn is
                      more advanced, but it must be worth less than a queen to
                      reward promotion.*/
                    ret = -(ROOK_EG_V + (7-RowNum[xy])*20);
                }
            }
        } else if (piece_info->w_rooks == 1) /*K+R vs. K*/
            ret = Eval_KingRook_King(bking, wking, Wpieces[0].next->xy);
        else if (piece_info->b_rooks == 1) /*K vs. K+R*/
            ret = -Eval_KingRook_King(wking, bking, Bpieces[0].next->xy);
        else if (piece_info->w_queens == 1)
            ret = Eval_KingQueen_King(bking, wking);
        else if (piece_info->b_queens == 1)
            ret = -Eval_KingQueen_King(wking, bking);
    } else if ((piece_info->all_pieces == 5) && (piece_info->all_minor_pieces == 3))
    {
        if ((piece_info->w_knights+piece_info->w_bishops > 0) && (piece_info->b_knights+piece_info->b_bishops > 0))
            /*technically, KBBKN is a win, but only with tablebases. Even
              grandmasters have overlooked this until the arrival of
              tablebases. If an opponent has tablebases, that means it is a
              PC program, and with the computing power and the big hash tables
              of a PC, the opponent would have won long before this endgame
              situation could arise anyway.*/
        {
            ret /= 10;
        }
    }
    *current_eval = ret;
}

/*both middle- and endgame:
  implement "when up in material, trade pieces; when down in material, trade
  pawns". only small amounts of points are given, just enough to steer things
  into the right direction if nothing else is more important.

  the reference start values have been calculated in SetupInitialMaterial()
  which was called before the computer started to enter the search tree. any
  difference in the material must have been due to trades or captures during
  the search tree.

  note that for really drastic changes within the search tree relative to the
  board position (> +/- 250 centipawns), lazy eval will kick in if it's during
  the middle game so that this part here will not be reached. Most probably,
  this will belong to an irrelevant line anyway.*/
#ifndef AUTOTUNE
static int Eval_Trade_Logic(const PAWN_INFO *restrict pawn_info, const PIECE_INFO *restrict piece_info, int pure_material)
{
    int ret = 0;
    /*first case: one side is down in pieces. no matter whether that is
      compensated by being up in pawns, further trade of pieces is a bad idea
      while pawn trade is fine.*/
    if (start_piece_diff != 0)
    {
        if (start_piece_diff > 0) /*white has more pieces*/
        {
            if ((pawn_info->w_pawns-pawn_info->b_pawns == start_pdiff) && (piece_info->w_queens-piece_info->b_queens == start_qdiff) && (piece_info->w_rooks-piece_info->b_rooks == start_rdiff)
                    && (piece_info->w_knights+piece_info->w_bishops-piece_info->b_knights-piece_info->b_bishops == start_mdiff)) /*only if 1:1 trades have been taking place*/
            {
                int current_pieces = piece_info->w_queens+piece_info->w_rooks+piece_info->b_queens+piece_info->b_rooks+piece_info->all_minor_pieces;
                if (current_pieces <= start_pieces) /*exclude queening situations*/
                    /*if one is up in pieces, one doesn't want to trade pawns
                      no matter whether up in pawns or down in pawns because
                      that lessens the chance for promotion and victory.*/
                    ret += (((start_pieces - current_pieces) << 2) - ((start_pawns - pawn_info->all_pawns) << 1));
            }
        } else /*black has more pieces*/
        {
            if ((pawn_info->w_pawns-pawn_info->b_pawns == start_pdiff) && (piece_info->w_queens-piece_info->b_queens == start_qdiff) && (piece_info->w_rooks-piece_info->b_rooks == start_rdiff)
                    && (piece_info->w_knights+piece_info->w_bishops-piece_info->b_knights-piece_info->b_bishops == start_mdiff)) /*only if 1:1 trades have been taking place*/
            {
                int current_pieces = piece_info->w_queens+piece_info->w_rooks+piece_info->b_queens+piece_info->b_rooks+piece_info->all_minor_pieces;
                if (current_pieces <= start_pieces) /*exclude queening situations*/
                    /*if one is up in pieces, one doesn't want to trade pawns
                      no matter whether up in pawns or down in pawns because
                      that lessens the chance for promotion and victory.*/
                    ret -= (((start_pieces - current_pieces) << 2) - ((start_pawns - pawn_info->all_pawns) << 1));
            }
        }
    }
    else if ((start_material > 80) || (start_material < -80))
        /*one side has more material on the board (not just in the search tree).*/
    {
        int _abs_diff;
        _abs_diff = pure_material - start_material;
        _abs_diff = Abs(_abs_diff);
        if (_abs_diff < 30)
            /*only if there is a material imbalance, but don't count
              knight/bishop difference, and don't count heavy material changes
              like capturing or queening. just make sure that if any trade
              has taken place, it is really of equal quality.

              note: the penalty e.g. for a piece trade is 8 points: in case of
              an equal exchange, the piece difference before/after will be 2,
              not 1. Shifted by 2 (i.e. multiplied by 4), this makes 8 points
              penalty.*/
        {
            int current_pieces = piece_info->w_queens+piece_info->w_rooks+piece_info->b_queens+piece_info->b_rooks+piece_info->all_minor_pieces;
            if ((current_pieces != start_pieces) || (pawn_info->all_pawns != start_pawns)) /*something must have been traded (or captured)!*/
            {
                if ((pawn_info->w_pawns-pawn_info->b_pawns == start_pdiff) && (piece_info->w_queens-piece_info->b_queens == start_qdiff) && (piece_info->w_rooks-piece_info->b_rooks == start_rdiff)
                        && (piece_info->w_knights+piece_info->w_bishops-piece_info->b_knights-piece_info->b_bishops == start_mdiff)) /*only if 1:1 trades have been taking place*/
                {
                    if (current_pieces <= start_pieces) /*make sure to exclude any queening situation*/
                    {
                        if (start_material > 0) /*white has the advantage. a piece trade shall be worth 2 pawn trades.*/
                            ret += (((start_pieces - current_pieces) << 2) - ((start_pawns - pawn_info->all_pawns) << 1));
                        else /*black has the advantage.*/
                            ret -= (((start_pieces - current_pieces) << 2) - ((start_pawns - pawn_info->all_pawns) << 1));
                    }
                }
            }
        }
    } else
        /*asymmetric eval: don't trade pieces without reason. pieces are for
          fighting, not for trading! though 8 points per traded piece are
          small enough to be overridden in order to gain material, or to
          defend against an ongoing attack (which involves more mobility points).*/
    {
        int _abs_diff;
        _abs_diff = pure_material - start_material;
        _abs_diff = Abs(_abs_diff);
        if (_abs_diff < 40)
        {
            /*penalise an unmotivated exchange by 8 points.
              (8 points because the piece difference is 2 and multiplied by 4
              through the shift)*/
            int current_pieces = piece_info->w_queens+piece_info->w_rooks+piece_info->b_queens+piece_info->b_rooks+piece_info->all_minor_pieces;
            if (current_pieces < start_pieces)
            {
                if (computer_side == WHITE)
                    ret -= (start_pieces - current_pieces) << 2;
                else
                    ret += (start_pieces - current_pieces) << 2;
            }
        }
    }
    return(ret);
}
#endif

static void Eval_Do_Noise(int *eval)
{
    if (eval_noise != 0)
    {
        int32_t tmp_eval, noise;

        /*noise between +/- 50 cps*/
        noise = Util_Rand(PAWN_V + 1U);
        noise -= PAWN_V / 2;
        /*weighted with eval_noise, 0-100*/
        noise *= eval_noise;

        /*original eval*/
        tmp_eval = *eval;
        /*weighted with 100-eval_noise, 100-0*/
        tmp_eval *= (100 - eval_noise);

        *eval = (tmp_eval + noise) / 100;
    }
}


/*---------- global functions ----------*/


/*for the display of the piece list (bishops) in the HMI module.*/
int Eval_Is_Light_Square(int square)
{
    return (WhiteSq[square] == LIGHT_SQ);
}

/*The static evaluation function.

  Parameters:
    *enough_material is for returning whether it's a material draw.
    side_to_move is needed for:
        - fixing eval oscillation during the opening (the right to move is
          precious)
        - the KP-K evaluation.

  Returns: The static evaluation value of the board position.

  Implicit parameters: board position, ply number, pawn hash table,
                       computer side, ...

  Basic outline of what it does:
  - get the raw material evaluation
  - add up the mobility
  - perform trapped bishop detection on a2/a7/h2/h7
  - determine material draw due to insufficient material (if so, return)
  - middle game lazy eval: if the eval changed too drastically from the last
    confirmed eval (the turn before), this is either a really good move and
    the opponent commited a blunder, or it is in a line that will be cut away
    in the search tree anyway => return
  - opposite bishops check: levels out material imbalance to a certain degree
  - for the early opening: more weight for minor pieces' mobility,
    less for the major pieces
  - extra pawn value: pawn hash table lookup, considering whether we are in
    middle game or endgame (different pawn weights)
  - if that fails: pawn evaluation (passed pawns, isolated pawns,
    double pawns, backward pawns, devalued pawn majorities on the wings,
    outward passed pawns, blocked center detection). this gets the positional
    pawn value and the pawn mask. the latter one is needed e.g. for the rook
    evaluation, to put them on open files or behind passed pawns.
  - exchange logics: if up in material, trade pieces; if down in material,
    trade pawns; if even, the computer shall be reluctant to exchange pieces.
    The trades are determined by listing up the material at the root of the
    search tree, so any changes must be due to captures/exchanges within the
    search tree.
  - if middle game:
    - add in the eval blurring, if configured (must be here and not in the
      endgame because various endgame handling would cease to work with
      blurring)
    - modify the pawn value (centre pawns are worth more than rim pawns)
    - rook handling (only from move 12 onwards): put them on open files,
      semi-open files, behind passed pawns, or attack the opponent's
      backward pawns.
    - knights / bishops: evaluate their position on the board
    - give some bonus for pawns occupying the centre
    - king safety evaluation: castling, holes, queens present, missing
      cover pawns
  - if endgame:
    - modify pawn weighting (isolani and passed pawns become more important)
    - check for various special endgames
      (KQ-K, KR-K, KNB-K, KP-K, KR-KN, KR-KB)
    - reward king centralisation
    - detect de facto draws like KNN-K
    - correct eval for positions like KP-KB where the side with the pawn is
      down in terms of adding up the material value, but the other side can
      never mate
  - write back the pawn evaluation including the middle/endgame modifications.
    There is also a bit for storing whether the pawn eval is a middle or
    endgame one. This is important during the transition from middle game to
    endgame.*/
int Eval_Static_Evaluation(int *restrict enough_material, enum E_COLOUR side_to_move,
                           unsigned *is_endgame)
{
    /*use a common array to init the structs in one shot. aliasing isn't
      a problem because these structs are made up of uint32_t and int32_t,
      which are allowed to alias.
      defined as static because putting them on the stack (which would be fine)
      costs 3% performance on ARM.*/
    static uint32_t eval_info[(sizeof(PAWN_INFO) + sizeof(PIECE_INFO))/sizeof(uint32_t)];
    static PAWN_INFO  *pawn_info  = (PAWN_INFO *)   eval_info;
    static PIECE_INFO *piece_info = (PIECE_INFO *) (eval_info + (sizeof(PAWN_INFO)/sizeof(uint32_t)));
    int ret, pure_material, pawn_hash_hit;
    int mg_mat_value = 0, eg_mat_value = 0, mg_pos_value, eg_pos_value;
    unsigned middle_game;
    int total_mobility, minor_mobility = 0, rook_mobility = 0, queen_mobility = 0;
    int w_minors, b_minors;
    PIECE *p;
    /*speed hack to avoid reloading the array addresses in loops*/
#ifndef AUTOTUNE
    const int8_t
#else
    int
#endif
    (*p_psqt)[120][2] = psqt;
#ifndef AUTOTUNE
    const
#endif
    int (*p_material_pst)[2] = material_pst;

    memset(eval_info, 0, sizeof(eval_info));
    pure_material = move_stack[mv_stack_p].material;
    /*white king PSTs*/
    {
#ifndef AUTOTUNE
        const int8_t
#else
        int
#endif
        *p_pqst_val = &p_psqt[WKING - WPAWN][wking][MG_PST];

        mg_pos_value = (int) *p_pqst_val++;
        eg_pos_value = (int) *p_pqst_val;
    }
    /*black king PSTs*/
    {
#ifndef AUTOTUNE
        const int8_t
#else
        int
#endif
        *p_pqst_val = &p_psqt[BKING - BPAWN][FlipXY[bking]][MG_PST];

        mg_pos_value -= (int) *p_pqst_val++;
        eg_pos_value -= (int) *p_pqst_val;
    }
    /*reward the right to move*/
    ret = (side_to_move == WHITE) ? SIDE_TO_MOVE_V : -SIDE_TO_MOVE_V;

    for (p = Wpieces[0].next; p != NULL; p = p->next)
    {
        uint64_t sq_bit_mask;
        int p_xy = p->xy, ptype_index = p->type - WPAWN;

        {
#ifndef AUTOTUNE
            const
#endif
            int *p_mat_val = &p_material_pst[ptype_index][MG_PST];

            mg_mat_value += *p_mat_val++;
            eg_mat_value += *p_mat_val;
        }
        {
#ifndef AUTOTUNE
            const int8_t
#else
            int
#endif
            *p_pqst_val = &p_psqt[ptype_index][p_xy][MG_PST];

            mg_pos_value += (int) *p_pqst_val++;
            eg_pos_value += (int) *p_pqst_val;
        }

        switch (ptype_index)
        {
        case WPAWN - WPAWN:
            pawn_info->w_pawns++;
            break;
        case WROOK - WPAWN:
            rook_mobility += p->mobility;
            piece_info->w_rooks++;
            break;
        case WKNIGHT - WPAWN:
            minor_mobility += p->mobility;
            piece_info->w_knights++;
            break;
        case WBISHOP - WPAWN:
            minor_mobility += p->mobility;
            piece_info->w_bishops++;
            piece_info->w_bishop_colour |= WhiteSq[p_xy];
            sq_bit_mask = 1ULL << boardXY[p_xy];
            sq_bit_mask &= ((1ULL << BP_H7) | (1ULL << BP_A7) | (1ULL << BP_H1) | (1ULL << BP_A1));
            /*performance boost of 3%: check the special squares only if needed*/
            if (!sq_bit_mask)
                break;
            switch (p_xy)
            {
            case A1:
                if (board[C3]->type == BPAWN)
                {
                    /*walled in by enemy pawn*/
                    ret -= 40;
                    if ((board[B4]->type == BPAWN) || (board[D4]->type == BPAWN))
                        ret -= 40;
                }
                break;
            case H1:
                if (board[F3]->type == BPAWN)
                {
                    /*walled in by enemy pawn*/
                    ret -= 40;
                    if ((board[G4]->type == BPAWN) || (board[E4]->type == BPAWN))
                        ret -= 40;
                }
                break;
            case A7:
                /*poisoned pawn detection - if the bishop can get out, the search will show it*/
                if (board[B6]->type == BPAWN)
                {
                    ret -= 120;
                    if (board[C7]->type == BPAWN) /*that bishop is almost certainly lost*/
                        ret -= 60;
                }
                break;
            case H7:
                /*poisoned pawn detection - if the bishop can get out, the search will show it*/
                if(board[G6]->type == BPAWN)
                {
                    ret -= 120;
                    if (board[F7]->type == BPAWN) /*that bishop is almost certainly lost*/
                        ret -= 60;
                }
                break;
            }
            break;
        case WQUEEN - WPAWN:
            queen_mobility += p->mobility;
            piece_info->w_queens++;
            break;
        default:
            break;
        }
    }

    for (p = Bpieces[0].next; p != NULL; p = p->next)
    {
        uint64_t sq_bit_mask;
        int p_xy = p->xy, ptype_index = p->type - BPAWN;

        {
#ifndef AUTOTUNE
            const
#endif
            int *p_mat_val = &p_material_pst[ptype_index][MG_PST];

            mg_mat_value -= *p_mat_val++;
            eg_mat_value -= *p_mat_val;
        }
        {
#ifndef AUTOTUNE
            const int8_t
#else
            int
#endif
            *p_pqst_val = &p_psqt[ptype_index][FlipXY[p_xy]][MG_PST];

            mg_pos_value -= (int) *p_pqst_val++;
            eg_pos_value -= (int) *p_pqst_val;
        }
        switch (ptype_index)
        {
        case BPAWN - BPAWN:
            pawn_info->b_pawns++;
            break;
        case BROOK - BPAWN:
            rook_mobility -= p->mobility;
            piece_info->b_rooks++;
            break;
        case BKNIGHT - BPAWN:
            minor_mobility -= p->mobility;
            piece_info->b_knights++;
            break;
        case BBISHOP - BPAWN:
            minor_mobility -= p->mobility;
            piece_info->b_bishops++;
            piece_info->b_bishop_colour |= WhiteSq[p_xy];
            sq_bit_mask = 1ULL << boardXY[p_xy];
            sq_bit_mask &= ((1ULL << BP_H8) | (1ULL << BP_A8) | (1ULL << BP_H2) | (1ULL << BP_A2));
            /*performance boost of 3%: check the special squares only if needed*/
            if (!sq_bit_mask)
                break;
            switch (p_xy)
            {
            case A2:
                /*poisoned pawn detection - if the bishop can get out, the search will show it*/
                if (board[B3]->type == WPAWN)
                {
                    ret += 120;
                    if (board[C2]->type == WPAWN) /*that bishop is almost certainly lost*/
                        ret += 60;
                }
                break;
            case H2:
                /*poisoned pawn detection - if the bishop can get out, the search will show it*/
                if (board[G3]->type == WPAWN)
                {
                    ret += 120;
                    if (board[F2]->type == WPAWN) /*that bishop is almost certainly lost*/
                        ret += 60;
                }
                break;
            case A8:
                if (board[C6]->type == WPAWN)
                {
                    /*walled in by enemy pawn*/
                    ret += 40;
                    if ((board[B5]->type == WPAWN) || (board[D5]->type == WPAWN))
                        ret += 40;
                }
                break;
            case H8:
                if (board[F6]->type == WPAWN)
                {
                    /*walled in by enemy pawn*/
                    ret += 40;
                    if ((board[G5]->type == WPAWN) || (board[E5]->type == WPAWN))
                        ret += 40;
                }
                break;
            }
            break;
        case BQUEEN - BPAWN:
            queen_mobility -= p->mobility;
            piece_info->b_queens++;
            break;
        default:
            break;
        }
    }

    piece_info->all_rooks = piece_info->w_rooks + piece_info->b_rooks;
    piece_info->all_queens = piece_info->w_queens + piece_info->b_queens;
    pawn_info->all_pawns = pawn_info->w_pawns + pawn_info->b_pawns;
    w_minors = piece_info->w_bishops + piece_info->w_knights;
    b_minors = piece_info->b_bishops + piece_info->b_knights;
    piece_info->all_minor_pieces = w_minors + b_minors;

    /* include kings in piece number */
    piece_info->white_pieces = 1 + pawn_info->w_pawns + w_minors + piece_info->w_rooks + piece_info->w_queens;
    piece_info->black_pieces = 1 + pawn_info->b_pawns + b_minors + piece_info->b_rooks + piece_info->b_queens;
    piece_info->all_pieces = piece_info->white_pieces + piece_info->black_pieces;
    if (UNLIKELY( ((pawn_info->all_pawns == 0) && (piece_info->all_rooks == 0) && (piece_info->all_queens == 0)) &&
                  ((piece_info->all_pieces < 4) ||
                   ((piece_info->w_knights == 0) && (piece_info->b_knights == 0) &&
                    (piece_info->w_bishop_colour != TWO_COLOUR) && (piece_info->b_bishop_colour != TWO_COLOUR) &&
                    ((piece_info->w_bishops == 0) || (piece_info->b_bishops == 0) || (piece_info->w_bishop_colour == piece_info->b_bishop_colour))))
                ))
    {
        *enough_material = 0;
        return(0);
    }

    *enough_material = piece_info->all_pieces - pawn_info->all_pawns;

    {
        /*if all pieces are on the board, that sums up to 24.*/
        int32_t game_phase = piece_info->all_minor_pieces + 2*piece_info->all_rooks + 4*piece_info->all_queens;

        if (game_phase > 24) game_phase = 24; /*promotion cap*/
        ret += ((mg_mat_value + mg_pos_value)*game_phase + (eg_mat_value + eg_pos_value)*(24 - game_phase))/24;
    }

    /* End of 1st pass */
    middle_game = (!(piece_info->all_pieces < 20 && (piece_info->all_rooks < 4 || piece_info->all_pieces < 13) && (piece_info->all_queens < 2 || piece_info->all_pieces < 13 || (piece_info->all_pieces-pawn_info->all_pawns < 7 ))) );

    /*the mobility values shift during the game:
      - below move 10, queen and rook mobility count with 33% of their actual
        value while minor piece mobility counts double (get the minor pieces
        out first).
      - between move 10 and 18, queen and rook mobility count with 66% of
        their actual value while minor piece mobility counts with 150%
        (transition to middle game).
      - from move 18 on, all pieces count with their actual mobility.*/
    {
        int ply_no = mv_stack_p + start_moves;

        if ((ply_no >= 36) || (game_started_from_0 == 0)) /*mobility from move 18*/
            total_mobility = queen_mobility + rook_mobility + minor_mobility;
        else
        {
            if (ply_no <= 20) /*mobility below move 10*/
            {
                total_mobility = (queen_mobility + rook_mobility)/3 +  minor_mobility*2;
                /*draw score shifting during the opening*/
                if (computer_side == WHITE)
                    ret += 30;
                else
                    ret -= 30;
                /*developing central bishops in front of 2nd/7th rank pawns usually
                  isn't good. only for the opening since that's a development issue.*/
                if ((board[E2]->type == WPAWN) && (board[E3]->type == WBISHOP)) ret-= 20;
                if ((board[D2]->type == WPAWN) && (board[D3]->type == WBISHOP)) ret-= 20;
                if ((board[E7]->type == BPAWN) && (board[E6]->type == BBISHOP)) ret += 20;
                if ((board[D7]->type == BPAWN) && (board[D6]->type == BBISHOP)) ret += 20;
                /*blocking the king side bishop with the f pawn happens.*/
                if ((board[F3]->type == WPAWN) && (board[G2]->type == WBISHOP)) ret-= 20;
                if ((board[F6]->type == BPAWN) && (board[G7]->type == BBISHOP)) ret += 20;
            } else /*mobility between move 10 and move 18, transition to middle game*/
            {
                total_mobility = ((queen_mobility + rook_mobility)*2)/3 + (minor_mobility*3)/2;
                /*draw score shifting during the opening*/
                if (computer_side == WHITE)
                    ret += 15;
                else
                    ret -= 15;
                /*developing central bishops in front of 2nd/7th rank pawns usually
                  isn't good. only for the opening since that's a development issue.*/
                if ((board[E2]->type == WPAWN) && (board[E3]->type == WBISHOP)) ret-= 20;
                if ((board[D2]->type == WPAWN) && (board[D3]->type == WBISHOP)) ret-= 20;
                if ((board[E7]->type == BPAWN) && (board[E6]->type == BBISHOP)) ret += 20;
                if ((board[D7]->type == BPAWN) && (board[D6]->type == BBISHOP)) ret += 20;
                /*blocking the king side bishop with the f pawn happens.*/
                if ((board[F3]->type == WPAWN) && (board[G2]->type == WBISHOP)) ret-= 20;
                if ((board[F6]->type == BPAWN) && (board[G7]->type == BBISHOP)) ret += 20;
            }
        }
    }

    /*heighten the mobility importance*/
    ret += total_mobility * 3;

    /* Depending on number of pawns, add bonus for 2 bishops*/
    if (piece_info->w_bishop_colour == TWO_COLOUR) ret += bishop_bonus[pawn_info->all_pawns];
    if (piece_info->b_bishop_colour == TWO_COLOUR) ret -= bishop_bonus[pawn_info->all_pawns];

    /*now for the pawn evaluation, which maybe is in the pawn hash table.*/
    uint64_t pawnkey64 = move_stack[mv_stack_p].mv_pawn_hash;
    uint32_t pawnhashupper = pawnkey64 >> 32u;
    uint32_t pawnhashadditional = (((uint32_t)pawnkey64) >> 16u) & PTT_HASH_BITS;

    /*the endgame pawn eval has some modifications. so if we are at the
      transition from middlegame to endgame, the pawn eval is not independent
      from the piece situation. since that might be anywhere in the search
      tree, we must avoid taking a middle game pawn eval in an endgame
      situation. that's why the middlegame bit gets fiddled into the
      additional bits.*/
    if (middle_game)
        pawnhashadditional |= PTT_MG_BIT;
    size_t Indx = (((size_t) pawnkey64) & PMAX_TT) ^ middle_game;
    TT_PTT_ST *ptt_ptr = &P_T_T[Indx];
    TT_PTT_ROOK_ST *ptt_rook_ptr = &P_T_T_Rooks[Indx];
    uint32_t ptt_val = ptt_ptr->value;

    if ((ptt_ptr->pawn_hash_upper == pawnhashupper) && ((ptt_val & (PTT_HASH_BITS | PTT_MG_BIT)) == pawnhashadditional))
    {
        if (ptt_val & PTT_SIGN_BIT)
            pawn_info->extra_pawn_val = -((int)(ptt_val & PTT_VALUE_BITS));
        else
            pawn_info->extra_pawn_val = (int)(ptt_val & PTT_VALUE_BITS);
        pawn_info->w_pawn_mask = ptt_ptr->w_pawn_mask;
        pawn_info->b_pawn_mask = ptt_ptr->b_pawn_mask;
        pawn_info->w_rook_files = ptt_rook_ptr->w_rook_files;
        pawn_info->b_rook_files = ptt_rook_ptr->b_rook_files;
        pawn_hash_hit = 1;
    } else {
        /*not found in the hash table, so calculate.*/
        pawn_hash_hit = 0;
        Eval_Pawn_Evaluation(pawn_info);
    }

#ifndef AUTOTUNE
    /*evaluate piece and pawn trades within the search tree*/
    ret += Eval_Trade_Logic(pawn_info, piece_info, pure_material);
#endif

    if (middle_game)
    {
        /*middle game specific evaluation*/
        *is_endgame = IS_NOT_ENDGAME;
        ret += Eval_Middlegame_Evaluation(pawn_info, piece_info);
    } else
    {   /*Endgame Eval.
          unlike the other partial eval functions, this is a void return type;
          instead, it takes the existing eval ("ret") as 2nd argument by
          reference. The reason is that unlike the other functions, this is
          not only addition/subtraction on the existing value, but sometimes
          also a plain overwrite, e.g. in KNB-K.*/
        {
            unsigned eg_config = IS_ENDGAME;

            if ((piece_info->w_queens + piece_info->w_rooks + piece_info->w_bishops == 0) &&
                (piece_info->w_knights < 2))
            {
                eg_config |= IS_ENDGAME_WHITE;
            }
            if ((piece_info->b_queens + piece_info->b_rooks + piece_info->b_bishops == 0) &&
                (piece_info->b_knights < 2))
            {
                eg_config |= IS_ENDGAME_BLACK;
            }

            *is_endgame = eg_config;
        }
        Eval_Endgame_Evaluation(pawn_info, &ret, piece_info, pawn_hash_hit, side_to_move, pure_material);
    }

    if (!pawn_hash_hit)
    {
        /*no cache hit, so the values were recalculated. store them in the pawn cache.*/
        ptt_ptr->pawn_hash_upper = pawnhashupper;

        /*fiddle 6 more pawn hash bits into the value*/
        if (pawn_info->extra_pawn_val >= 0)
            ptt_ptr->value = (uint16_t)(((uint32_t)(pawn_info->extra_pawn_val)) | pawnhashadditional);
        else
            ptt_ptr->value = (uint16_t)(((uint32_t)(-pawn_info->extra_pawn_val)) | pawnhashadditional | PTT_SIGN_BIT);
        ptt_ptr->w_pawn_mask = (uint8_t) pawn_info->w_pawn_mask;
        ptt_ptr->b_pawn_mask = (uint8_t) pawn_info->b_pawn_mask;
        ptt_rook_ptr->w_rook_files = (uint8_t) pawn_info->w_rook_files;
        ptt_rook_ptr->b_rook_files = (uint8_t) pawn_info->b_rook_files;
    }

    /*attention to differently coloured bishops - that tends to be drawish.*/
    if (UNLIKELY((piece_info->w_bishop_colour != piece_info->b_bishop_colour) && (piece_info->w_bishops == 1) && (piece_info->b_bishops== 1)))
    {
        int32_t bishop_discount = ret;

        if ((piece_info->w_knights != 0) || (piece_info->b_knights != 0)) /*that enables still a knight-bishop-exchange*/
            /*flatten the difference by 15% for the square control possibility in defence*/
            bishop_discount *= 15;
        else /*different bishops, no knights*/
        {
            if ((piece_info->all_queens !=0) || (piece_info->all_rooks != 0))
                /*with major pieces still on, things are more dynamic, 25% discount*/
                bishop_discount *= 25;
            else
            {
                /*discount maximum.*/
                bishop_discount *= 40;
            }
        }
        bishop_discount /= 100;

        /*clip to +/- 75 centipawns for preventing to throw away even more material.*/
        if (bishop_discount > 75)
            bishop_discount = 75;
        else if (bishop_discount < -75)
            bishop_discount = -75;

        ret -= bishop_discount;
    }

    Eval_Do_Noise(&ret);
    return (ret);
}

/*set up the initial material affairs before the computer starts calculating.
  calls Eval_Static_Evaluation() once so that also the pawn evaluation runs
  through and we can extract the value from the hash table.
  the determined values will be considered in the leaves of the search tree,
  in Eval_Static_Evaluation(), to judge the influence of exchanges within the
  search tree.*/
int Eval_Setup_Initial_Material(void)
{
    PIECE *p;
    int enough_material;
    int start_wqueens = 0, start_wrooks = 0, start_wminors = 0, start_wpawns = 0;
    int start_bqueens = 0, start_brooks = 0, start_bminors = 0, start_bpawns = 0;
    unsigned dummy;

    for (p = Wpieces[0].next; p != NULL; p = p->next)
    {
        switch (p->type)
        {
        case WQUEEN:
            start_wqueens++;
            break;
        case WROOK:
            start_wrooks++;
            break;
        case WKNIGHT:
        case WBISHOP:
            start_wminors++;
            break;
        case WPAWN:
            start_wpawns++;
            break;
        default:
            break;
        }
    }
    for (p = Bpieces[0].next; p != NULL; p = p->next)
    {
        switch (p->type)
        {
        case BQUEEN:
            start_bqueens++;
            break;
        case BROOK:
            start_brooks++;
            break;
        case BKNIGHT:
        case BBISHOP:
            start_bminors++;
            break;
        case BPAWN:
            start_bpawns++;
            break;
        default:
            break;
        }
    }

    /*set up the globals for Eval_Static_Evaluation() in the search tree*/
    start_qdiff = start_wqueens - start_bqueens;
    start_rdiff = start_wrooks - start_brooks;
    start_mdiff = start_wminors - start_bminors;
    start_pdiff = start_wpawns - start_bpawns;

    start_piece_diff = start_qdiff + start_rdiff + start_mdiff;

    start_pawns = start_wpawns + start_bpawns;
    start_pieces = start_wqueens+start_bqueens+start_wrooks+start_brooks+start_wminors+start_bminors;

    /*we're just interested in the draw material evaluation.*/
    (void) Eval_Static_Evaluation(&enough_material, computer_side, &dummy);

    start_material = move_stack[mv_stack_p].material;
    return(enough_material);
}

/*zero out the over the board material balance*/
void Eval_Zero_Initial_Material(void)
{
    start_material = 0;
    start_qdiff = 0;
    start_rdiff = 0;
    start_mdiff = 0;
    start_pdiff = 0;
    start_piece_diff = 0;
    start_pieces = 0;
    start_pawns = 0;
}
