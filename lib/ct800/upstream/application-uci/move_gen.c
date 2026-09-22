/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2015-2023, Rasmus Althoff <info@ct800.net>
 *  Copyright (C) 2010-2014, George Georgopoulos
 *
 *  This file is part of CT800/NGPlay (move generator).
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

#include <stdint.h>
#include <stddef.h>
#include "ctdefs.h"
#include "move_gen.h"

/*---------- external variables ----------*/
/*-- READ-ONLY  --*/
extern PIECE Wpieces[16];
extern PIECE Bpieces[16];
extern PIECE *board[120];
extern const int8_t RowNum[120];
extern const int8_t boardXY[120];
extern const int8_t board64[64];
extern int en_passant_sq;
extern unsigned int gflags;
extern uint16_t W_history[6][ENDSQ], B_history[6][ENDSQ];
extern CMOVE W_Killers[2][MAX_DEPTH], B_Killers[2][MAX_DEPTH];

/*-- READ-WRITE --*/
extern int wking, bking;

/*---------- move compression data and functions ----------*/

enum
{
    MV_COMP_NULL,
    MV_COMP_PIECE,
    MV_COMP_WPAWN,
    MV_COMP_WKN_PROM,
    MV_COMP_WBP_PROM,
    MV_COMP_WRK_PROM,
    MV_COMP_WQN_PROM,
    MV_COMP_BPAWN,
    MV_COMP_BKN_PROM,
    MV_COMP_BBP_PROM,
    MV_COMP_BRK_PROM,
    MV_COMP_BQN_PROM
};

static const uint8_t Flag_Comp_To_Move[16] =
{
    0,  /*invalid*/
    1U, /*piece move*/
    WPAWN, WKNIGHT, WBISHOP, WROOK, WQUEEN, /*white pawn, maybe promotion*/
    BPAWN, BKNIGHT, BBISHOP, BROOK, BQUEEN, /*black pawn, maybe promotion*/
    0, 0, 0, 0 /*invalid*/
};

static const uint8_t Flag_Move_To_Comp[19] =
{
    MV_COMP_NULL,  /*invalid*/
    MV_COMP_PIECE, /*piece move*/
    /*white pawn move, maybe with promotion*/
    MV_COMP_WPAWN, MV_COMP_WKN_PROM, MV_COMP_WBP_PROM, MV_COMP_WRK_PROM, MV_COMP_WQN_PROM,
    /*invalid - WKING to BPAWN padding*/
    MV_COMP_NULL, MV_COMP_NULL, MV_COMP_NULL, MV_COMP_NULL, MV_COMP_NULL,
    /*black pawn move, maybe with promotion*/
    MV_COMP_BPAWN, MV_COMP_BKN_PROM, MV_COMP_BBP_PROM, MV_COMP_BRK_PROM, MV_COMP_BQN_PROM,
    /*invalid - BKING to PIECEMAX padding*/
    MV_COMP_NULL, MV_COMP_NULL
};

CMOVE Mvgen_Compress_Move(MOVE board_move)
{
    if (board_move.u != MV_NO_MOVE_MASK)
    {
        unsigned to64, from64, flag;

        from64 = (unsigned) boardXY[board_move.m.from];
        to64   = (unsigned) boardXY[board_move.m.to];
        flag   = Flag_Move_To_Comp[board_move.m.flag];

        return((CMOVE) (from64 | (to64 << 6) | (flag << 12)));
    } else
        return(0);
}

MOVE Mvgen_Decompress_Move(CMOVE comp_move)
{
    MOVE move;

    move.u = MV_NO_MOVE_MASK;

    if (comp_move != MV_NO_MOVE_CMASK)
    {
        move.m.from = board64[ comp_move       & 0x3FU];
        move.m.to   = board64[(comp_move >> 6) & 0x3FU];
        move.m.flag = Flag_Comp_To_Move[comp_move >> 12];
    }
    return(move);
}

/*---------- local functions ----------*/

#define Mvgen_Add_Capt_Mv(mfrom, mto, mflag, mlist, mnextfree, mvvlva) \
do { \
    MOVE mp; \
    mp.m.from = mfrom; \
    mp.m.to   = mto; \
    mp.m.flag = mflag; \
    mp.m.mvv_lva = mvvlva; \
    mlist[(*mnextfree)++].u = mp.u; \
} while (0)

static void Mvgen_Add_White_Mv(int xy0, int xy, int flag, MOVE *restrict movelist, int *restrict nextfree, int MvvLva, int level)
{
    MOVE mp;
    int nextfree_idx = *nextfree;

    if (UNLIKELY((nextfree_idx) >= MAXMV))
        return;

    mp.m.from = xy0;
    mp.m.to   = xy;
    mp.m.flag = flag;

    if (MvvLva == 0) /*quiet move, i.e. no capture and no promotion?*/
    {
        unsigned W_history_hit = 0;
        if (level >= 0)
        {
            CMOVE cmove = Mvgen_Compress_Move(mp);

            /*quiet depth killers go to 1 ... 2*/
            if (W_Killers[0][level] == cmove)
                W_history_hit = (MVV_LVA_KILLER_0 + HIST_MVV_MAX) << HIST_SHIFT_BITS;
            else if (W_Killers[1][level] == cmove)
                W_history_hit = (MVV_LVA_KILLER_1 + HIST_MVV_MAX) << HIST_SHIFT_BITS;
            else
                W_history_hit = W_history[board[xy0]->type - WPAWN][xy];
        } else
            W_history_hit = W_history[board[xy0]->type - WPAWN][xy];

        /*map quiet non-killer moves with positive history on -32 ... -1*/
        if (W_history_hit != 0)
            mp.m.mvv_lva = ((int)(W_history_hit >> HIST_SHIFT_BITS)) - ((int)HIST_MVV_MAX);
        else
        {
            int diff = xy - bking;

            /*other quiet moves go to -109 ... -33*/
            mp.m.mvv_lva = ((diff < 0) ? diff : -diff) - ((int)HIST_MVV_MAX);
        }
    } else /*captures / promotions*/
        mp.m.mvv_lva = MvvLva;

    movelist[nextfree_idx++].u = mp.u;
    *nextfree = nextfree_idx;
}

static int Mvgen_Square_Exists_In_Attack(int xy, const MOVE *restrict attack_movelist, int n_attack_moves)
{
    int j;
    for (j=0; j<n_attack_moves; j++) {
        if (attack_movelist[j].m.from == xy)
            return 1;
    }
    return 0;
}

static void Mvgen_Add_White_Bishop_Captures(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree)
{
    int xy0 = piece->xy, piece_type = piece->type;
    int xy=xy0, bmoves=0, test;
    for (;;) {
        xy+=9;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            bmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy-=9;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            bmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy+=11;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            bmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy-=11;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            bmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    piece->mobility += bmoves;
}

static void Mvgen_Add_White_Bishop_Moves(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level)
{
    int xy0 = piece->xy, piece_type = piece->type;
    int xy=xy0, bmoves=0, test;
    for (;;) {
        xy+=9;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            bmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type, level);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy-=9;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            bmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type, level);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy+=11;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            bmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type, level);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy-=11;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            bmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type, level);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    piece->mobility += bmoves;
}

static void Mvgen_Add_White_Bishop_Evasions(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, const MOVE *restrict attack_movelist, int n_attack_moves)
{
    int xy0 = piece->xy, piece_type = piece->type;
    int xy=xy0, bmoves=0, test;
    for (;;) {
        xy+=9;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            bmoves++;
        } else if (test > BLACK) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy-=9;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            bmoves++;
        } else if (test > BLACK) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy+=11;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            bmoves++;
        } else if (test > BLACK) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy-=11;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            bmoves++;
        } else if (test > BLACK) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            bmoves++;
            break;
        } else {
            break;
        }
    }
    piece->mobility += bmoves;
}

static void Mvgen_Add_White_Knight_Captures(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree)
{
    int xy0 = piece->xy, xy, nmoves=0, test;
    xy = xy0 + 21;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 - 21;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 + 19;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 - 19;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 + 12;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 - 12;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 + 8;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 - 8;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    piece->mobility += nmoves;
}

static void Mvgen_Add_White_Knight_Moves(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level)
{
    int xy0 = piece->xy, xy, nmoves=0, test;

    xy = xy0 + 21;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT, level);
        nmoves++;
    }
    xy = xy0 - 21;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT, level);
        nmoves++;
    }
    xy = xy0 + 19;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT, level);
        nmoves++;
    }
    xy = xy0 - 19;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT, level);
        nmoves++;
    }
    xy = xy0 + 12;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT, level);
        nmoves++;
    }
    xy = xy0 - 12;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT, level);
        nmoves++;
    }
    xy = xy0 + 8;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT, level);
        nmoves++;
    }
    xy = xy0 - 8;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT, level);
        nmoves++;
    }
    piece->mobility += nmoves;
}

static void Mvgen_Add_White_Knight_Evasions(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, const MOVE *restrict attack_movelist, int n_attack_moves)
{
    int xy0 = piece->xy, xy, nmoves=0, test;

    xy = xy0 + 21;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    } else if (test > BLACK) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        }
        nmoves++;
    }
    xy = xy0 - 21;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    } else if (test > BLACK) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        }
        nmoves++;
    }
    xy = xy0 + 19;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    } else if (test > BLACK) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        }
        nmoves++;
    }
    xy = xy0 - 19;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    } else if (test > BLACK) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        }
        nmoves++;
    }
    xy = xy0 + 12;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    } else if (test > BLACK) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        }
        nmoves++;
    }
    xy = xy0 - 12;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    } else if (test > BLACK) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        }
        nmoves++;
    }
    xy = xy0 + 8;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    } else if (test > BLACK) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        }
        nmoves++;
    }
    xy = xy0 - 8;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    } else if (test > BLACK) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKNIGHT);
        }
        nmoves++;
    }
    piece->mobility += nmoves;
}

static void Mvgen_Add_White_Rook_Captures(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree)
{
    int xy0 = piece->xy, piece_type = piece->type;
    int xy=xy0, rmoves=0, test;
    for (;;) {
        xy++;
        test = board[xy]->type;
        if (test > BLACK) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            rmoves++;
            break;
        } else if (test == NO_PIECE) {
            rmoves++;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy--;
        test = board[xy]->type;
        if (test > BLACK) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            rmoves++;
            break;
        } else if (test == NO_PIECE) {
            rmoves++;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy+=10;
        test = board[xy]->type;
        if (test > BLACK) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            rmoves++;
            break;
        } else if (test == NO_PIECE) {
            rmoves++;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy-=10;
        test = board[xy]->type;
        if (test > BLACK) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            rmoves++;
            break;
        } else if (test == NO_PIECE) {
            rmoves++;
        } else {
            break;
        }
    }
    piece->mobility += rmoves;
}

static void Mvgen_Add_White_Rook_Moves(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level)
{
    int xy0 = piece->xy, piece_type = piece->type;
    int xy=xy0, rmoves=0, test;
    for (;;) {
        xy++;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            rmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type, level);
            rmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy--;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            rmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type, level);
            rmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy+=10;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            rmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type, level);
            rmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy-=10;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            rmoves++;
        } else if (test > BLACK) {
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type, level);
            rmoves++;
            break;
        } else {
            break;
        }
    }
    piece->mobility += rmoves;
}

static void Mvgen_Add_White_Rook_Evasions(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, const MOVE *restrict attack_movelist, int n_attack_moves)
{
    int xy0 = piece->xy, piece_type = piece->type;
    int xy=xy0, rmoves=0, test;
    for (;;) {
        xy++;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            rmoves++;
        } else if (test > BLACK) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            rmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy--;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            rmoves++;
        } else if (test > BLACK) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            rmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy+=10;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            rmoves++;
        } else if (test > BLACK) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            rmoves++;
            break;
        } else {
            break;
        }
    }
    xy=xy0;
    for (;;) {
        xy-=10;
        test = board[xy]->type;
        if (test == NO_PIECE) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            rmoves++;
        } else if (test > BLACK) {
            if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - piece_type);
            rmoves++;
            break;
        } else {
            break;
        }
    }
    piece->mobility += rmoves;
}

static void Mvgen_Add_White_Pawn_No_Caps_No_Prom_Moves(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level)
{
    int xy0 = piece->xy;
    int xy = xy0 + 10;
    if (board[xy]->type==0) {
        if (xy0<A7) { /*not in 7th rank */
            Mvgen_Add_White_Mv(xy0, xy, WPAWN, movelist, nextfree, (xy>=A6), level);
        }
        if (xy0<=H2) { /* Two step pawn move*/
            xy += 10;
            if (board[xy]->type==0) {
                Mvgen_Add_White_Mv(xy0, xy, WPAWN, movelist, nextfree, 0, level);
            }
        }
    }
}

static void Mvgen_Add_White_Pawn_Captures_And_Promotions(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int underprom)
{
    /* !! Attention. If promotion the promotion piece is saved in movelist[i]->flag*/
    int xy0 = piece->xy;
    int xy, test;
    if (xy0 >= A7) {  /* promotion */
        xy=xy0+9;
        test = board[xy]->type;
        if (test > BLACK) {
            /*PxQ plus promotion would come out at MVV_LVA=126, so subtract 2
              in the case to get the result below 125, i.e. the hash move value.*/
            Mvgen_Add_White_Mv(xy0, xy, WQUEEN, movelist, nextfree, ((test-14+WQUEEN) << 4) - WPAWN - 2 /*mvv lva*/, NO_LEVEL);
            if (underprom != QUEENING)
            {
                Mvgen_Add_White_Mv(xy0, xy, WROOK, movelist, nextfree, ((test-14+WROOK) << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_White_Mv(xy0, xy, WBISHOP, movelist, nextfree, ((test-14+WBISHOP) << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_White_Mv(xy0, xy, WKNIGHT, movelist, nextfree, ((test-14+WKNIGHT) << 4) - WPAWN, NO_LEVEL);
            }
        }
        xy++; /* = xy0+10;*/
        test = board[xy]->type;
        if (test == NO_PIECE) {
            Mvgen_Add_White_Mv(xy0, xy, WQUEEN, movelist, nextfree, (WQUEEN << 4) - WPAWN /*mvv lva*/, NO_LEVEL);
            if (underprom != QUEENING)
            {
                Mvgen_Add_White_Mv(xy0, xy, WROOK, movelist, nextfree, (WROOK << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_White_Mv(xy0, xy, WBISHOP, movelist, nextfree, (WBISHOP << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_White_Mv(xy0, xy, WKNIGHT, movelist, nextfree, (WKNIGHT << 4) - WPAWN, NO_LEVEL);
            }
        }
        xy++; /* = xy0+11;*/
        test = board[xy]->type;
        if (test > BLACK) {
            /*PxQ plus promotion would come out at MVV_LVA=126, so subtract 2
              in the case to get the result below 125, i.e. the hash move value.*/
            Mvgen_Add_White_Mv(xy0, xy, WQUEEN, movelist, nextfree, ((test-14+WQUEEN) << 4) - WPAWN - 2 /*mvv lva*/, NO_LEVEL);
            if (underprom != QUEENING)
            {
                Mvgen_Add_White_Mv(xy0, xy, WROOK, movelist, nextfree, ((test-14+WROOK) << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_White_Mv(xy0, xy, WBISHOP, movelist, nextfree, ((test-14+WBISHOP) << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_White_Mv(xy0, xy, WKNIGHT, movelist, nextfree, ((test-14+WKNIGHT) << 4) - WPAWN, NO_LEVEL);
            }
        }
    } else {
        xy=xy0+9;
        test = board[xy]->type;
        if (test > BLACK) {
            Mvgen_Add_White_Mv(xy0, xy, WPAWN, movelist, nextfree, ((test - BLACK) << 4) - WPAWN /*mvv lva*/, NO_LEVEL);
        } else if (xy==en_passant_sq) {
            Mvgen_Add_White_Mv(xy0, xy, WPAWN, movelist, nextfree, (WPAWN << 4) - WPAWN, NO_LEVEL);  /* en passant */
        }
        xy+=2; /* = xy0+11;*/
        test = board[xy]->type;
        if (test > BLACK) {
            Mvgen_Add_White_Mv(xy0, xy, WPAWN, movelist, nextfree, ((test - BLACK) << 4) - WPAWN /*mvv lva*/, NO_LEVEL);
        } else if (xy==en_passant_sq) {
            Mvgen_Add_White_Mv(xy0, xy, WPAWN, movelist, nextfree, (WPAWN << 4) - WPAWN, NO_LEVEL);  /* en passant */
        }
    }
}

static void Mvgen_Add_White_King_Captures(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree)
{
    int xy0 = piece->xy, xy, test;

    xy = xy0 + 1;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }
    xy = xy0 - 1;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }
    xy = xy0 + 9;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }
    xy++; /* = xy0 + 10;*/
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }
    xy++; /* = xy0 + 11;*/
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }
    xy = xy0 - 11;
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }
    xy++; /* = xy0 - 10;*/
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }
    xy++; /* = xy0 - 9;*/
    test = board[xy]->type;
    if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }
}

static void Mvgen_Add_White_King_Evasions(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, const MOVE *restrict attack_movelist, int n_attack_moves)
{
    int xy0 = piece->xy, xy, test;

    xy = xy0 - 1; /*West*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }

    xy = xy0 + 9; /*North-West*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }

    xy++; /*North*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }

    xy++; /*North-East*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }

    xy = xy0 + 1; /*East*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }

    xy = xy0 - 9; /*South-East*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }

    xy--; /*South*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }

    xy--; /*South-West*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if (test > BLACK) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/);
    }
}

static void Mvgen_Add_Black_Mv(int xy0, int xy, int flag, MOVE *restrict movelist, int *restrict nextfree, int MvvLva, int level)
{
    MOVE mp;
    int nextfree_idx = *nextfree;

    if (UNLIKELY((nextfree_idx) >= MAXMV))
        return;

    mp.m.from = xy0;
    mp.m.to   = xy;
    mp.m.flag = flag;

    if (MvvLva == 0) /*quiet move, i.e. no capture and no promotion?*/
    {
        unsigned B_history_hit = 0;

        if (level >= 0)
        {
            CMOVE cmove = Mvgen_Compress_Move(mp);

            /*quiet depth killers go to 1 ... 2*/
            if (B_Killers[0][level] == cmove)
                B_history_hit = (MVV_LVA_KILLER_0 + HIST_MVV_MAX) << HIST_SHIFT_BITS;
            else if (B_Killers[1][level] == cmove)
                B_history_hit = (MVV_LVA_KILLER_1 + HIST_MVV_MAX) << HIST_SHIFT_BITS;
            else
                B_history_hit = B_history[board[xy0]->type - BPAWN][xy];
        } else
            B_history_hit = B_history[board[xy0]->type - BPAWN][xy];

        /*map quiet non-killer moves with positive history on -32 ... -1*/
        if (B_history_hit != 0)
            mp.m.mvv_lva = ((int)(B_history_hit >> HIST_SHIFT_BITS)) - ((int)HIST_MVV_MAX);
        else
        {
            int diff = wking - xy;

            /*other quiet moves go to -109 ... -33*/
            mp.m.mvv_lva = ((diff < 0) ? diff : -diff) - ((int)HIST_MVV_MAX);
        }
    } else /*captures / promotions*/
        mp.m.mvv_lva = MvvLva;

    movelist[nextfree_idx++].u = mp.u;
    *nextfree = nextfree_idx;
}

static void Mvgen_Add_Black_Bishop_Captures(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree)
{
    int xy0 = piece->xy, piece_type = piece->type - BLACK;
    int xy=xy0, bmoves=0, test;
    for (;;) {
        xy+=11;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy-=11;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy+=9;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy-=9;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            }
        }
    }
    piece->mobility += bmoves;
}

static void Mvgen_Add_Black_Bishop_Moves(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level)
{
    int xy0 = piece->xy, piece_type = piece->type - BLACK;
    int xy=xy0, bmoves=0, test;
    for (;;) {
        xy+=11;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type, level);
                break;
            } else {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy-=11;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type, level);
                break;
            } else {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy+=9;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type, level);
                break;
            } else {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy-=9;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type, level);
                break;
            } else {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            }
        }
    }
    piece->mobility += bmoves;
}

static void Mvgen_Add_Black_Bishop_Evasions(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, const MOVE *restrict attack_movelist, int n_attack_moves)
{
    int xy0 = piece->xy, piece_type = piece->type - BLACK;
    int xy=xy0, bmoves=0, test;
    for (;;) {
        xy+=11;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            } else {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy-=11;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            } else {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy+=9;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            } else {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy-=9;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            bmoves++;
            if (test != NO_PIECE) {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            } else {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            }
        }
    }
    piece->mobility += bmoves;
}

static void Mvgen_Add_Black_Knight_Captures(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree)
{
    int xy0 = piece->xy, xy, nmoves=0, test;

    xy = xy0 + 21;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 - 21;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 + 19;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 - 19;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 + 12;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 - 12;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 + 8;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    xy = xy0 - 8;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        nmoves++;
    } else if (test == NO_PIECE) {
        nmoves++;
    }
    piece->mobility += nmoves;
}

static void Mvgen_Add_Black_Knight_Moves(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level)
{
    int xy0 = piece->xy;
    int xy, nmoves=0, test;
    xy = xy0 + 21;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT, level);
        nmoves++;
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    }
    xy = xy0 - 21;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT, level);
        nmoves++;
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    }
    xy = xy0 + 19;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT, level);
        nmoves++;
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    }
    xy = xy0 - 19;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT, level);
        nmoves++;
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    }
    xy = xy0 + 12;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT, level);
        nmoves++;
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    }
    xy = xy0 - 12;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT, level);
        nmoves++;
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    }
    xy = xy0 + 8;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT, level);
        nmoves++;
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    }
    xy = xy0 - 8;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT, level);
        nmoves++;
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
        nmoves++;
    }
    piece->mobility += nmoves;
}

static void Mvgen_Add_Black_Knight_Evasions(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, const MOVE *restrict attack_movelist, int n_attack_moves)
{
    int xy0 = piece->xy, xy, nmoves=0, test;

    xy = xy0 + 21;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        }
        nmoves++;
    } else if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    }
    xy = xy0 - 21;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        }
        nmoves++;
    } else if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    }
    xy = xy0 + 19;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        }
        nmoves++;
    } else if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    }
    xy = xy0 - 19;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        }
        nmoves++;
    } else if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    }
    xy = xy0 + 12;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        }
        nmoves++;
    } else if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    }
    xy = xy0 - 12;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        }
        nmoves++;
    } else if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    }
    xy = xy0 + 8;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        }
        nmoves++;
    } else if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    }
    xy = xy0 - 8;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKNIGHT);
        }
        nmoves++;
    } else if (test == NO_PIECE) {
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves)) {
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
        }
        nmoves++;
    }
    piece->mobility += nmoves;
}

static void Mvgen_Add_Black_Rook_Captures(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree)
{
    int xy0 = piece->xy, piece_type = piece->type - BLACK;
    int xy=xy0, rmoves=0, test;
    for (;;) {
        xy--;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy++;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy-=10;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy+=10;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            }
        }
    }
    piece->mobility += rmoves;
}

static void Mvgen_Add_Black_Rook_Moves(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level)
{
    int xy0 = piece->xy, piece_type = piece->type - BLACK;
    int xy=xy0, rmoves=0, test;
    for (;;) {
        xy--;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type, level);
                break;
            } else {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy++;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type, level);
                break;
            } else {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy-=10;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type, level);
                break;
            } else {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy+=10;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type, level);
                break;
            } else {
                Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
            }
        }
    }
    piece->mobility += rmoves;
}

static void Mvgen_Add_Black_Rook_Evasions(PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, const MOVE *restrict attack_movelist, int n_attack_moves)
{
    int xy0 = piece->xy, piece_type = piece->type - BLACK;
    int xy=xy0, rmoves=0, test;
    for (;;) {
        xy--;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            } else {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy++;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            } else {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy-=10;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            } else {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            }
        }
    }
    xy=xy0;
    for (;;) {
        xy+=10;
        test = board[xy]->type;
        if ((test > BLACK) || (test < NO_PIECE)) {
            break;
        } else {
            rmoves++;
            if (test != NO_PIECE) {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - piece_type);
                break;
            } else {
                if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves))
                    Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
            }
        }
    }
    piece->mobility += rmoves;
}

static void Mvgen_Add_Black_Pawn_No_Caps_No_Prom_Moves(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level)
{
    int xy0 = piece->xy;
    int xy = xy0 - 10;
    if (board[xy]->type==0) {
        if (xy0>H2) { /* not in 2nd rank */
            Mvgen_Add_Black_Mv(xy0, xy, BPAWN, movelist, nextfree, (xy<=H3), level);
        }
        if (xy0>=A7) { /*two step pawn move*/
            xy -= 10;
            if (board[xy]->type==0) {
                Mvgen_Add_Black_Mv(xy0, xy, BPAWN, movelist, nextfree, 0, level);
            }
        }
    }
}

static void Mvgen_Add_Black_Pawn_Captures_And_Promotions(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int underprom)
{
    /* !! Attention. If promotion the promotion piece is saved in movelist[i].flag */
    int xy0 = piece->xy;
    int xy, test;
    if (xy0 <= H2) {  /* promotion */
        xy=xy0-11;
        test = board[xy]->type;
        if ((test > NO_PIECE) && (test < BLACK)) {
            /*PxQ plus promotion would come out at MVV_LVA=126, so subtract 2
              in the case to get the result below 125, i.e. the hash move value.*/
            Mvgen_Add_Black_Mv(xy0, xy, BQUEEN, movelist, nextfree, ((test+WQUEEN-4) << 4) - WPAWN - 2 /*mvv lva*/, NO_LEVEL);
            if (underprom != QUEENING)
            {
                Mvgen_Add_Black_Mv(xy0, xy, BROOK, movelist,nextfree, ((test+WROOK-4) << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_Black_Mv(xy0, xy, BBISHOP, movelist,nextfree, ((test+WBISHOP-4) << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_Black_Mv(xy0, xy, BKNIGHT, movelist,nextfree, ((test+WKNIGHT-4) << 4) - WPAWN, NO_LEVEL);
            }
        }
        xy++; /* = xy0-10;*/
        test = board[xy]->type;
        if (test == NO_PIECE) {
            Mvgen_Add_Black_Mv(xy0, xy, BQUEEN, movelist, nextfree, (WQUEEN << 4) - WPAWN /*mvv lva*/, NO_LEVEL);
            if (underprom != QUEENING)
            {
                Mvgen_Add_Black_Mv(xy0, xy, BROOK, movelist, nextfree, (WROOK << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_Black_Mv(xy0, xy, BBISHOP, movelist, nextfree, (WBISHOP << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_Black_Mv(xy0, xy, BKNIGHT, movelist, nextfree, (WKNIGHT << 4) - WPAWN, NO_LEVEL);
            }
        }
        xy++; /* = xy0-9;*/
        test = board[xy]->type;
        if ((test > NO_PIECE) && (test < BLACK)) {
            /*PxQ plus promotion would come out at MVV_LVA=126, so subtract 2
              in the case to get the result below 125, i.e. the hash move value.*/
            Mvgen_Add_Black_Mv(xy0, xy, BQUEEN, movelist, nextfree, ((test+WQUEEN-4) << 4) - WPAWN - 2 /*mvv lva*/, NO_LEVEL);
            if (underprom != QUEENING)
            {
                Mvgen_Add_Black_Mv(xy0, xy, BROOK, movelist, nextfree, ((test+WROOK-4) << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_Black_Mv(xy0, xy, BBISHOP, movelist, nextfree, ((test+WBISHOP-4) << 4) - WPAWN, NO_LEVEL);
                Mvgen_Add_Black_Mv(xy0, xy, BKNIGHT, movelist, nextfree, ((test+WKNIGHT-4) << 4) - WPAWN, NO_LEVEL);
            }
        }
    } else {
        xy=xy0-11;
        test = board[xy]->type;
        if ((test > NO_PIECE) && (test < BLACK)) {
            Mvgen_Add_Black_Mv(xy0, xy, BPAWN, movelist, nextfree, (test << 4) - WPAWN /*mvv lva*/, NO_LEVEL);
        } else if (en_passant_sq==xy) {
            Mvgen_Add_Black_Mv(xy0, xy, BPAWN, movelist, nextfree, (WPAWN << 4) - WPAWN, NO_LEVEL);/*en passant*/
        }
        xy+=2; /* = xy0-9;*/
        test = board[xy]->type;
        if ((test > NO_PIECE) && (test < BLACK)) {
            Mvgen_Add_Black_Mv(xy0, xy, BPAWN, movelist, nextfree, (test << 4) - WPAWN /*mvv lva*/, NO_LEVEL);
        } else if (en_passant_sq==xy) {
            Mvgen_Add_Black_Mv(xy0, xy, BPAWN,movelist, nextfree, (WPAWN << 4) - WPAWN, NO_LEVEL); /*en passant*/
        }
    }
}

static void Mvgen_Add_Black_King_Captures(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree)
{
    int xy0 = piece->xy, xy, test;

    xy = xy0 + 1;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }
    xy = xy0 - 1;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }
    xy = xy0 - 11;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }
    xy++; /* = xy0 - 10;*/
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }
    xy++; /* = xy0 - 9;*/
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }
    xy = xy0 + 9;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }
    xy++; /* = xy0 + 10;*/
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }
    xy++; /* = xy0 + 11;*/
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }
}

static void Mvgen_Add_Black_King_Evasions(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, const MOVE *restrict attack_movelist, int n_attack_moves)
{
    int xy0 = piece->xy, xy, test;

    xy = xy0 - 1; /*West*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }

    xy = xy0 + 9; /*North-West*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }

    xy++; /*North*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }

    xy++; /*North-East*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }

    xy = xy0 + 1; /*East*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }

    xy = xy0 - 9; /*South-East*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }

    xy--; /*South*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }

    xy--; /*South-West*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        /*king cannot evade to those empty squares that are already in the attack list.*/
        if (Mvgen_Square_Exists_In_Attack(xy, attack_movelist, n_attack_moves) == 0)
            Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, NO_LEVEL);
    } else if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Capt_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/);
    }
}


/* ---------- global functions ----------*/


void Mvgen_Add_White_King_Moves(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level)
{
    int xy0 = piece->xy, xy, test;

    xy = xy0 + 1;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/, NO_LEVEL);
    }
    xy = xy0 - 1;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/, NO_LEVEL);
    }
    xy = xy0 + 9;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/, NO_LEVEL);
    }
    xy++; /* = xy0 + 10;*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/, NO_LEVEL);
    }
    xy++; /* = xy0 + 11;*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/, NO_LEVEL);
    }
    xy = xy0 - 11;
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/, NO_LEVEL);
    }
    xy++; /* = xy0 - 10;*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/, NO_LEVEL);
    }
    xy++; /* = xy0 - 9;*/
    test = board[xy]->type;
    if (test == NO_PIECE) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    } else if (test > BLACK) {
        Mvgen_Add_White_Mv(xy0, xy, 1, movelist, nextfree, ((test - BLACK) << 4) - WKING /*mvv lva*/, NO_LEVEL);
    }

    /*the MVV-LVA values for castling should be high. Actually, lower
      than any winning capture, but that is not possible with MVV-LVA
      because winning captures are distributed too much.
      However, taking a queen with a pawn or minor piece is nearly
      always better than castling, and taking with a rook does not
      really happen in that game stage.
      So rank short castling above queen exchange, but below taking
      a queen with pawn or minor piece, i.e. on a level with RxQ.
      Long castling is more dangerous, but usually safer without queens,
      so rank it below QxQ.*/
    if (((gflags & WKMOVED) == 0) && (xy0 == E1))
    {
        if (((gflags & WRH1MOVED) == 0) && (board[H1]->type == WROOK) &&
            (board[F1]->type == NO_PIECE) && (board[G1]->type == NO_PIECE))
        {
            if (!Mvgen_White_King_In_Check())
            {
                wking = F1;
                if (!Mvgen_White_King_In_Check())
                    Mvgen_Add_White_Mv(E1, G1, 1, movelist, nextfree, MVV_LVA_CSTL_SHORT, NO_LEVEL);
                wking = E1;
            }
        }
        if (((gflags & WRA1MOVED) == 0) && (board[A1]->type == WROOK) &&
            (board[D1]->type == NO_PIECE) && (board[C1]->type == NO_PIECE) &&
            (board[B1]->type == NO_PIECE))
        {
            if (!Mvgen_White_King_In_Check())
            {
                wking = D1;
                if (!Mvgen_White_King_In_Check())
                    Mvgen_Add_White_Mv(E1, C1, 1, movelist, nextfree, MVV_LVA_CSTL_LONG, NO_LEVEL);
                wking = E1;
            }
        }
    }
}

void Mvgen_Add_Black_King_Moves(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level)
{
    int xy0 = piece->xy, xy, test;

    xy = xy0 - 11;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/, NO_LEVEL);
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    }
    xy++; /* = xy0 - 10;*/
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/, NO_LEVEL);
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    }
    xy++; /* = xy0 - 9;*/
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/, NO_LEVEL);
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    }
    xy = xy0 + 1;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/, NO_LEVEL);
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    }
    xy = xy0 - 1;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/, NO_LEVEL);
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    }
    xy = xy0 + 9;
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/, NO_LEVEL);
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    }
    xy++; /* = xy0 + 10;*/
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/, NO_LEVEL);
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    }
    xy++; /* = xy0 + 11;*/
    test = board[xy]->type;
    if ((test > NO_PIECE) && (test < BLACK)) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, (test << 4) - WKING /*mvv lva*/, NO_LEVEL);
    } else if (test == NO_PIECE) {
        Mvgen_Add_Black_Mv(xy0, xy, 1, movelist, nextfree, 0, level);
    }

    /*the MVV-LVA values for castling should be high. Actually, lower
      than any winning capture, but that is not possible with MVV-LVA
      because winning captures are distributed too much.
      However, taking a queen with a pawn or minor piece is nearly
      always better than castling, and taking with a rook does not
      really happen in that game stage.
      So rank short castling above queen exchange, but below taking
      a queen with pawn or minor piece, i.e. on a level with RxQ.
      Long castling is more dangerous, but usually safer without queens,
      so rank it below QxQ.*/
    if (((gflags & BKMOVED) == 0) && (xy0 == E8))
    {
        if (((gflags & BRH8MOVED) == 0) && (board[H8]->type == BROOK) &&
            (board[F8]->type == NO_PIECE) && (board[G8]->type == NO_PIECE))
        {
            if (!Mvgen_Black_King_In_Check())
            {
                bking = F8;
                if (!Mvgen_Black_King_In_Check())
                    Mvgen_Add_Black_Mv(E8, G8, 1, movelist, nextfree, MVV_LVA_CSTL_SHORT, NO_LEVEL);
                bking = E8;
            }
        }
        if (((gflags & BRA8MOVED) == 0) && (board[A8]->type == BROOK) &&
            (board[D8]->type == NO_PIECE) && (board[C8]->type == NO_PIECE) &&
            (board[B8]->type == NO_PIECE))
        {
            if (!Mvgen_Black_King_In_Check())
            {
                bking = D8;
                if (!Mvgen_Black_King_In_Check())
                    Mvgen_Add_Black_Mv(E8, C8, 1, movelist, nextfree, MVV_LVA_CSTL_LONG, NO_LEVEL);
                bking = E8;
            }
        }
    }
}

enum E_MOVESTATE Mvgen_Is_Move_Safe(MOVE move, int kingsq, enum E_COLOUR colour)
{
    int sq, from, to, type;
    int from64, to64, king64;
    int fromrank64, fromfile64, torank64, tofile64, kingrank64, kingfile64;
    int diff, fromrankdiff64, fromfilediff64, torankdiff64, tofilediff64;

    from = move.m.from;
    /*always full check on king moves*/
    if (from == kingsq)
        return(MOVE_UNKNOWN);
    to = move.m.to;

    /*check alignment of "from" square and king*/
    king64 = boardXY[kingsq];
    from64 = boardXY[from];
    to64   = boardXY[to];
    kingfile64 = king64 & 0x07;
    kingrank64 = king64 >> 3;
    fromfile64 = from64 & 0x07;
    fromrank64 = from64 >> 3;
    tofile64   = to64   & 0x07;
    torank64   = to64   >> 3;

    /*en passant, flag indicates pawn move.
      it might even be 0 if the move was already determined as illegal in
      scouting, whether it was a pawn move or not. However, that happens
      only at depth 6 or greater. It would cost more speed to check the
      flag==0 case than risking to do some useless in-check verifications
      in the search. therefore, this case is just ignored, and it doesn't
      matter whether this functions returns MOVE_UNSAFE or MOVE_UNKNOWN for
      flag==0. what does matter is checking for !=1 here and not >1 because
      if the flag==0 move happens to be an en passant move, we must not
      return MOVE_SAFE like at the end of this function, or else the engine
      would crash because a king would be captured.*/
    if ((move.m.flag != 1) && (fromfile64 != tofile64) && (board[to]->type == NO_PIECE))
        return(MOVE_UNKNOWN);

    /*vertical alignment*/
    if (kingfile64 == fromfile64)
    {
        if (kingfile64 == tofile64)
            return(MOVE_SAFE);
        diff = (from > kingsq) ? 10 : -10;
        for (sq = kingsq + diff; board[sq]->type == NO_PIECE; sq += diff) ;
        if (sq != from)
            return(MOVE_SAFE);
        for (sq += diff; board[sq]->type == NO_PIECE; sq += diff) ;
        type = board[sq]->type;
        if (colour == BLACK)
            return(((type == WQUEEN) || (type == WROOK)) ? MOVE_UNSAFE : MOVE_SAFE);
        else
            return(((type == BQUEEN) || (type == BROOK)) ? MOVE_UNSAFE : MOVE_SAFE);
    }

    /*horizontal alignment*/
    if (kingrank64 == fromrank64)
    {
        if (kingrank64 == torank64)
            return(MOVE_SAFE);
        diff = (from > kingsq) ? 1 : -1;
        for (sq = kingsq + diff; board[sq]->type == NO_PIECE; sq += diff) ;
        if (sq != from)
            return(MOVE_SAFE);
        for (sq += diff; board[sq]->type == NO_PIECE; sq += diff) ;
        type = board[sq]->type;
        if (colour == BLACK)
            return(((type == WQUEEN) || (type == WROOK)) ? MOVE_UNSAFE : MOVE_SAFE);
        else
            return(((type == BQUEEN) || (type == BROOK)) ? MOVE_UNSAFE : MOVE_SAFE);
    }

    /*diagonal alignment*/
    fromfilediff64 = fromfile64 - kingfile64;
    fromrankdiff64 = fromrank64 - kingrank64;
    if (Abs(fromfilediff64) == Abs(fromrankdiff64))
    {
        tofilediff64 = tofile64 - kingfile64;
        torankdiff64 = torank64 - kingrank64;
        if ((Abs(tofilediff64) != Abs(torankdiff64)) ||
            (fromfilediff64 * tofilediff64 < 0) ||
            (fromrankdiff64 * torankdiff64 < 0))
        {
            diff  = (fromrankdiff64 > 0) ? 10 : -10;
            diff += (fromfilediff64 > 0) ? 1  : -1;
            for (sq = kingsq + diff; board[sq]->type == NO_PIECE; sq += diff) ;
            if (sq != from)
                return(MOVE_SAFE);
            for (sq += diff; board[sq]->type == NO_PIECE; sq += diff) ;
            type = board[sq]->type;
            if (colour == BLACK)
                return(((type == WQUEEN) || (type == WBISHOP)) ? MOVE_UNSAFE : MOVE_SAFE);
            else
                return(((type == BQUEEN) || (type == BBISHOP)) ? MOVE_UNSAFE : MOVE_SAFE);
        }
    }
    /*no particular alignment*/
    return(MOVE_SAFE);
}

int Mvgen_White_King_In_Check_Info(MOVE *restrict attack_movelist, int *restrict attackers)
{
    int xyk=wking, xy, i, nLine, test, nextfree=0, n_attackers=0;
    MOVE attack_move;
    uint8_t Linexy[10];

    attack_move.m.to   = xyk;
    attack_move.m.mvv_lva = 0;
    /*black pawn checks and some knights square*/
    xy=xyk+8;
    if (board[xy]->type==BKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = BKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy++; /*+9*/
    if (board[xy]->type==BPAWN) {
        attack_move.m.from = xy;
        attack_move.m.flag = BPAWN;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=2; /*+11*/
    if (board[xy]->type==BPAWN) {
        attack_move.m.from = xy;
        attack_move.m.flag = BPAWN;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy++; /*+12*/
    if (board[xy]->type==BKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = BKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=7; /*+19*/
    if (board[xy]->type==BKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = BKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=2; /*+21*/
    if (board[xy]->type==BKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = BKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    /*black bishop or queen checks*/
    nLine=0;
    xy=xyk;
    for (;;) {
        xy+=9;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                Linexy[nLine] = xy;
                nLine++;
                continue;
            }
            break;
        } else {
            if(test == BBISHOP || test == BQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    nLine=0;
    xy=xyk;
    for (;;) {
        xy+=11;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                Linexy[nLine] = xy;
                nLine++;
                continue;
            }
            break;
        } else {
            if(test == BBISHOP || test == BQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    /*black rook or queen checks*/
    nLine=0;
    xy=xyk;
    for (;;) {
        xy++;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                Linexy[nLine] = xy;
                nLine++;
                continue;
            }
            break;
        } else {
            if(test == BROOK || test == BQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    nLine=0;
    xy=xyk;
    for (;;) {
        xy--;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                Linexy[nLine] = xy;
                nLine++;
                continue;
            }
            break;
        } else {
            if(test == BROOK || test == BQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    nLine=0;
    xy=xyk;
    for (;;) {
        xy+=10;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                Linexy[nLine] = xy;
                nLine++;
                continue;
            }
            break;
        } else  {
            if(test == BROOK || test == BQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }

    /*middle game optimisation: if the white king is on the 1st rank,
      there cannot be checks from behind.*/
    if (xyk <= H1)
    {
        *attackers = n_attackers;
        return nextfree;
    }

    /*remaining black knight checks*/
    xy=xyk-21;
    if (board[xy]->type==BKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = BKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=2; /*-19*/
    if (board[xy]->type==BKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = BKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=7; /*-12*/
    if (board[xy]->type==BKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = BKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=4; /*-8*/
    if (board[xy]->type==BKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = BKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    /*black bishop or queen checks*/
    nLine=0;
    xy=xyk;
    for (;;) {
        xy-=9;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                Linexy[nLine] = xy;
                nLine++;
                continue;
            }
            break;
        } else {
            if(test == BBISHOP || test == BQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    nLine=0;
    xy=xyk;
    for (;;) {
        xy-=11;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                Linexy[nLine] = xy;
                nLine++;
                continue;
            }
            break;
        } else {
            if(test == BBISHOP || test == BQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    /*black rook or queen checks*/
    nLine=0;
    xy=xyk;
    for (;;) {
        xy-=10;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                Linexy[nLine] = xy;
                nLine++;
                continue;
            }
            break;
        } else {
            if(test == BROOK || test == BQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    *attackers = n_attackers;
    return nextfree;
}

int Mvgen_Black_King_In_Check_Info(MOVE *restrict attack_movelist, int *restrict attackers)
{
    int xyk=bking, xy, i, nLine, test, nextfree=0, n_attackers=0;
    MOVE attack_move;
    uint8_t Linexy[10];

    attack_move.m.to   = xyk;
    attack_move.m.mvv_lva = 0;
    *attackers   = 0;
    /*white pawn and knight checks*/
    xy=xyk-21;
    if (board[xy]->type==WKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = WKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=2; /*-19*/
    if (board[xy]->type==WKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = WKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=7; /*-12*/
    if (board[xy]->type==WKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = WKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy++; /*-11*/
    if (board[xy]->type==WPAWN) {
        attack_move.m.from = xy;
        attack_move.m.flag = WPAWN;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=2; /*-9*/
    if (board[xy]->type==WPAWN) {
        attack_move.m.from = xy;
        attack_move.m.flag = WPAWN;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy++; /*-8*/
    if (board[xy]->type==WKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = WKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    /*white bishop or queen checks*/
    nLine=0;
    xy=xyk;
    for (;;) {
        xy -= 11;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            Linexy[nLine] = xy;
            nLine++;
            continue;
        } else {
            if (test == WBISHOP || test == WQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    nLine=0;
    xy=xyk;
    for (;;) {
        xy -= 9;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            Linexy[nLine] = xy;
            nLine++;
            continue;
        } else {
            if (test == WBISHOP || test == WQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    /*white rook or queen checks*/
    nLine=0;
    xy=xyk;
    for (;;) {
        xy++;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            Linexy[nLine] = xy;
            nLine++;
            continue;
        } else {
            if (test == WROOK || test == WQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    nLine=0;
    xy=xyk;
    for (;;) {
        xy--;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            Linexy[nLine] = xy;
            nLine++;
            continue;
        } else {
            if (test == WROOK || test == WQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    nLine=0;
    xy=xyk;
    for (;;) {
        xy -= 10;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            Linexy[nLine] = xy;
            nLine++;
            continue;
        } else {
            if (test == WROOK || test == WQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }

    /*middle game optimisation: if the black king is on the 8th rank,
      there cannot be checks from behind.*/
    if (xyk >= A8)
    {
        *attackers = n_attackers;
        return nextfree;
    }

    /*remaining white knight checks*/
    xy=xyk+8;
    if (board[xy]->type==WKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = WKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=4; /*+12*/
    if (board[xy]->type==WKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = WKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=7; /*+19*/
    if (board[xy]->type==WKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = WKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    xy+=2; /*+21*/
    if (board[xy]->type==WKNIGHT) {
        attack_move.m.from = xy;
        attack_move.m.flag = WKNIGHT;
        attack_movelist[nextfree].u = attack_move.u;
        nextfree++;
        n_attackers++;
    }
    /*white bishop or queen checks*/
    nLine=0;
    xy=xyk;
    for (;;) {
        xy += 9;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            Linexy[nLine] = xy;
            nLine++;
            continue;
        } else {
            if (test == WBISHOP || test == WQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    nLine=0;
    xy=xyk;
    for (;;) {
        xy += 11;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            Linexy[nLine] = xy;
            nLine++;
            continue;
        } else {
            if (test == WBISHOP || test == WQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    /*white rook or queen checks*/
    nLine=0;
    xy=xyk;
    for (;;) {
        xy += 10;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            Linexy[nLine] = xy;
            nLine++;
            continue;
        } else {
            if (test == WROOK || test == WQUEEN) {
                for (i=0; i<nLine; i++) {
                    attack_move.m.from = Linexy[i];
                    attack_move.m.flag = test;
                    attack_movelist[nextfree].u = attack_move.u;
                    nextfree++;
                }
                attack_move.m.from = xy;
                attack_move.m.flag = test;
                attack_movelist[nextfree].u = attack_move.u;
                nextfree++;
                if (++n_attackers > 1) {
                    *attackers = n_attackers;
                    return nextfree;
                }
            }
            break;
        }
    }
    *attackers = n_attackers;
    return nextfree;
}

int Mvgen_White_King_In_Check(void)
{
    int xyk=wking, xy;
    int test;

    /*black pawn checks & kings side by side and some knights squares*/
    if (board[xyk-1]->type==BKING || board[xyk+1]->type==BKING || board[xyk+8]->type==BKNIGHT)
        return 1;
    test = board[xyk+9]->type;
    if (test == BPAWN || test == BKING)
        return 1;
    if (board[xyk+10]->type==BKING)
        return 1;
    test = board[xyk+11]->type;
    if (test == BPAWN || test == BKING)
        return 1;
    if (board[xyk+12]->type==BKNIGHT || board[xyk+19]->type==BKNIGHT || board[xyk+21]->type==BKNIGHT)
        return 1;
    /*black bishop or queen checks*/
    xy=xyk;
    for (;;) {
        xy+=9;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                continue;
            }
            break;
        } else {
            if(test == BBISHOP || test == BQUEEN) {
                return 1;
            }
            break;
        }
    }
    xy=xyk;
    for (;;) {
        xy+=11;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                continue;
            }
            break;
        } else {
            if(test == BBISHOP || test == BQUEEN) {
                return 1;
            }
            break;
        }
    }
    /*black rook or queen checks*/
    xy=xyk;
    for (;;) {
        xy++;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                continue;
            }
            break;
        } else {
            if(test == BROOK || test == BQUEEN) {
                return 1;
            }
            break;
        }
    }
    xy=xyk;
    for (;;) {
        xy--;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                continue;
            }
            break;
        } else {
            if(test == BROOK || test == BQUEEN) {
                return 1;
            }
            break;
        }
    }
    xy=xyk;
    for (;;) {
        xy+=10;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                continue;
            }
            break;
        } else  {
            if(test == BROOK || test == BQUEEN) {
                return 1;
            }
            break;
        }
    }

    /*middle game optimisation: if the white king is on the 1st rank,
      there cannot be checks from behind.*/
    if (xyk <= H1)
        return 0;

    /*kings side by side & some knight squares*/
    if (board[xyk-21]->type==BKNIGHT || board[xyk-19]->type==BKNIGHT ||
        board[xyk-12]->type==BKNIGHT || board[xyk-11]->type==BKING   ||
        board[xyk-10]->type==BKING   || board[xyk -9]->type==BKING   ||
        board[xyk -8]->type==BKNIGHT)
    {
        return 1;
    }
    /*black bishop or queen checks*/
    xy=xyk;
    for (;;) {
        xy-=9;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                continue;
            }
            break;
        } else {
            if(test == BBISHOP || test == BQUEEN) {
                return 1;
            }
            break;
        }
    }
    xy=xyk;
    for (;;) {
        xy-=11;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                continue;
            }
            break;
        } else {
            if(test == BBISHOP || test == BQUEEN) {
                return 1;
            }
            break;
        }
    }
    /*black rook or queen checks*/
    xy=xyk;
    for (;;) {
        xy-=10;
        test = board[xy]->type;
        if (test < BLACK) {
            if (test == NO_PIECE) {
                continue;
            }
            break;
        } else {
            if(test == BROOK || test == BQUEEN) {
                return 1;
            }
            break;
        }
    }
    return 0;
}

int Mvgen_Black_King_In_Check(void)
{
    int xyk=bking, xy;
    int test;

    /*white pawn checks & kings side by side & some knight squares*/
    if (board[xyk-21]->type==WKNIGHT || board[xyk-19]->type==WKNIGHT || board[xyk-12]->type==WKNIGHT)
        return 1;
    test=board[xyk-11]->type;
    if (test == WPAWN || test == WKING)
        return 1;
    if (board[xyk-10]->type==WKING)
        return 1;
    test=board[xyk-9]->type;
    if (test == WPAWN || test == WKING)
        return 1;
    if (board[xyk-8]->type==WKNIGHT || board[xyk-1]->type==WKING || board[xyk+1]->type==WKING)
        return 1;
    /*white bishop or queen checks*/
    xy=xyk;
    for (;;) {
        xy -= 11;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            continue;
        } else {
            if (test == WBISHOP || test == WQUEEN) {
                return 1;
            }
            break;
        }
    }
    xy=xyk;
    for (;;) {
        xy -= 9;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            continue;
        } else {
            if (test == WBISHOP || test == WQUEEN) {
                return 1;
            }
            break;
        }
    }
    /*white rook or queen checks*/
    xy=xyk;
    for (;;) {
        xy++;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            continue;
        } else {
            if (test == WROOK || test == WQUEEN) {
                return 1;
            }
            break;
        }
    }
    xy=xyk;
    for (;;) {
        xy--;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            continue;
        } else {
            if (test == WROOK || test == WQUEEN) {
                return 1;
            }
            break;
        }
    }
    xy=xyk;
    for (;;) {
        xy -= 10;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            continue;
        } else {
            if (test == WROOK || test == WQUEEN) {
                return 1;
            }
            break;
        }
    }

    /*middle game optimisation: if the black king is on the 8th rank,
      there cannot be checks from behind.*/
    if (xyk >= A8)
        return 0;

    /*kings side by side & some knight squares*/
    if (board[xyk +8]->type==WKNIGHT || board[xyk +9]->type==WKING   ||
        board[xyk+10]->type==WKING   || board[xyk+11]->type==WKING   ||
        board[xyk+12]->type==WKNIGHT || board[xyk+19]->type==WKNIGHT ||
        board[xyk+21]->type==WKNIGHT)
    {
        return 1;
    }
    /*white rook or queen checks*/
    xy=xyk;
    for (;;) {
        xy += 10;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            continue;
        } else {
            if (test == WROOK || test == WQUEEN) {
                return 1;
            }
            break;
        }
    }
    /*white bishop or queen checks*/
    xy=xyk;
    for (;;) {
        xy += 9;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            continue;
        } else {
            if (test == WBISHOP || test == WQUEEN) {
                return 1;
            }
            break;
        }
    }
    xy=xyk;
    for (;;) {
        xy += 11;
        test=board[xy]->type;
        if (test > BLACK) {
            break;
        } else if (test == NO_PIECE) {
            continue;
        } else {
            if (test == WBISHOP || test == WQUEEN) {
                return 1;
            }
            break;
        }
    }
    return 0;
}

int Mvgen_Find_All_White_Evasions(MOVE *restrict movelist, const MOVE *restrict attack_movelist, int n_attack_moves, int n_attacking_pieces, int underprom)
{
    int test, j;
    int nextfree=0;
    int _abs_diff;
    PIECE *p;

    Mvgen_Add_White_King_Evasions(&Wpieces[0], movelist, &nextfree, attack_movelist, n_attack_moves);
    if (n_attacking_pieces>1) { /* If double check we are done */
        return nextfree;
    }
    for (p=Wpieces[0].next; p!=NULL; p=p->next) {
        int p_xy;

        p->mobility = 0;
        switch (p->type) {
        case WPAWN  :
            p_xy = p->xy;
            for (j=0; j<n_attack_moves; j++) {
                test = attack_movelist[j].m.from - p_xy;
                if ((test == 11) || (test == 9) || (test == 10) || ((test == 20) && (RowNum[p_xy] == 2)) ||
                    (((test == 1) || (test == -1)) && (RowNum[p_xy] == 5)) /*e.p.*/) {
                    Mvgen_Add_White_Pawn_Captures_And_Promotions(p, movelist, &nextfree, underprom);
                    Mvgen_Add_White_Pawn_No_Caps_No_Prom_Moves(p, movelist, &nextfree, NO_LEVEL);
                    break;
                }
            }
            break;
        case WKNIGHT:
            p_xy = p->xy;
            for (j=0; j<n_attack_moves; j++) {
                _abs_diff = attack_movelist[j].m.from - p_xy;
                test = Abs(_abs_diff);
                if ((test == 12) || (test == 21) || (test == 19) || (test == 8)) {
                    Mvgen_Add_White_Knight_Evasions(p, movelist, &nextfree, attack_movelist, n_attack_moves);
                    p->mobility -= 4;
                    break;
                }
            }
            break;
        case WBISHOP:
            p_xy = p->xy;
            for (j=0; j<n_attack_moves; j++) {
                _abs_diff = attack_movelist[j].m.from - p_xy;
                test = Abs(_abs_diff);
                if (((((unsigned) test) % 11U)==0) || ((((unsigned) test) % 9U)==0)) {
                    Mvgen_Add_White_Bishop_Evasions(p, movelist, &nextfree, attack_movelist, n_attack_moves);
                    p->mobility -= 6;
                    break;
                }
            }
            break;
        case WROOK  :
            Mvgen_Add_White_Rook_Evasions(p, movelist, &nextfree, attack_movelist, n_attack_moves);
            p->mobility -= 7;
            break;
        case WQUEEN :
            Mvgen_Add_White_Rook_Evasions(p, movelist, &nextfree, attack_movelist, n_attack_moves);
            Mvgen_Add_White_Bishop_Evasions(p, movelist, &nextfree, attack_movelist, n_attack_moves);
            p->mobility -= 13;
            break;
        default:
            break;
        }
    }
    return nextfree;
}

int Mvgen_Find_All_Black_Evasions(MOVE *restrict movelist, const MOVE *restrict attack_movelist, int n_attack_moves, int n_attacking_pieces, int underprom)
{
    int test, j;
    int nextfree=0;
    int _abs_diff;
    PIECE *p;

    Mvgen_Add_Black_King_Evasions(&Bpieces[0], movelist, &nextfree, attack_movelist, n_attack_moves);
    if (n_attacking_pieces>1) { /* If double check we are done */
        return nextfree;
    }
    for (p=Bpieces[0].next; p!=NULL; p=p->next) {
        int p_xy;

        p->mobility = 0;
        switch (p->type) {
        case BPAWN  :
            p_xy = p->xy;
            for (j=0; j<n_attack_moves; j++) {
                test = attack_movelist[j].m.from - p_xy;
                if ((test == -11) || (test == -9) || (test == -10) || ((test == -20) && (RowNum[p_xy] == 7)) ||
                    (((test == 1) || (test == -1)) && (RowNum[p_xy] == 4)) /*e.p.*/) {
                    Mvgen_Add_Black_Pawn_Captures_And_Promotions(p, movelist, &nextfree, underprom);
                    Mvgen_Add_Black_Pawn_No_Caps_No_Prom_Moves(p, movelist, &nextfree,NO_LEVEL);
                    break;
                }
            }
            break;
        case BKNIGHT:
            p_xy = p->xy;
            for (j=0; j<n_attack_moves; j++) {
                _abs_diff = attack_movelist[j].m.from - p_xy;
                test = Abs(_abs_diff);
                if ((test == 12) || (test == 21) || (test == 19) || (test == 8)) {
                    Mvgen_Add_Black_Knight_Evasions(p, movelist, &nextfree, attack_movelist, n_attack_moves);
                    p->mobility -= 4;
                    break;
                }
            }
            break;
        case BBISHOP:
            p_xy = p->xy;
            for (j=0; j<n_attack_moves; j++) {
                _abs_diff = attack_movelist[j].m.from - p_xy;
                test = Abs(_abs_diff);
                if (((((unsigned) test) % 11U)==0) || ((((unsigned) test) % 9U)==0)) {
                    Mvgen_Add_Black_Bishop_Evasions(p, movelist, &nextfree, attack_movelist, n_attack_moves);
                    p->mobility -= 6;
                    break;
                }
            }
            break;
        case BROOK  :
            Mvgen_Add_Black_Rook_Evasions(p, movelist, &nextfree, attack_movelist, n_attack_moves);
            p->mobility -= 7;
            break;
        case BQUEEN :
            Mvgen_Add_Black_Rook_Evasions(p, movelist, &nextfree, attack_movelist, n_attack_moves);
            Mvgen_Add_Black_Bishop_Evasions(p, movelist, &nextfree, attack_movelist, n_attack_moves);
            p->mobility -= 13;
            break;
        default:
            break;
        }
    }
    return nextfree;
}

int Mvgen_Find_All_White_Moves(MOVE *restrict movelist, int level, int underprom)
{
    int nextfree=0;
    PIECE *p;
    /* Find Moves plus mobility value for Rooks,Queens,Bishops,Knights. */
    /* Also subtract mobility normalisation value */
    for (p=Wpieces[0].next; p!=NULL; p=p->next) {
        p->mobility = 0;
        switch (p->type) {
        case WPAWN  :
            Mvgen_Add_White_Pawn_Captures_And_Promotions(p, movelist, &nextfree, underprom);
            Mvgen_Add_White_Pawn_No_Caps_No_Prom_Moves(p, movelist, &nextfree, level);
            break;
        case WKNIGHT:
            Mvgen_Add_White_Knight_Moves(p, movelist, &nextfree, level);
            p->mobility -= 4;
            break;
        case WBISHOP:
            Mvgen_Add_White_Bishop_Moves(p, movelist, &nextfree, level);
            p->mobility -= 6;
            break;
        case WROOK  :
            Mvgen_Add_White_Rook_Moves(p, movelist, &nextfree, level);
            p->mobility -= 7;
            break;
        case WQUEEN :
            Mvgen_Add_White_Rook_Moves(p, movelist, &nextfree, level);
            Mvgen_Add_White_Bishop_Moves(p, movelist, &nextfree, level);
            p->mobility -= 13;
            break;
        default:
            break;
        }
    }
    Mvgen_Add_White_King_Moves(&Wpieces[0], movelist, &nextfree, level);
    /* Sorting left for later during search */

    return nextfree;
}

int Mvgen_Find_All_Black_Moves(MOVE *restrict movelist, int level, int underprom)
{
    int nextfree=0;
    PIECE *p;
    /* Find Moves plus mobility value for Rooks,Queens,Bishops,Knights. */
    /* Also subtract mobility normalisation value */
    for (p=Bpieces[0].next; p!=NULL; p=p->next) {
        p->mobility = 0;
        switch (p->type) {
        case BPAWN  :
            Mvgen_Add_Black_Pawn_Captures_And_Promotions(p, movelist, &nextfree, underprom);
            Mvgen_Add_Black_Pawn_No_Caps_No_Prom_Moves(p, movelist, &nextfree, level);
            break;
        case BKNIGHT:
            Mvgen_Add_Black_Knight_Moves(p, movelist, &nextfree, level);
            p->mobility -= 4;
            break;
        case BBISHOP:
            Mvgen_Add_Black_Bishop_Moves(p, movelist, &nextfree, level);
            p->mobility -= 6;
            break;
        case BROOK  :
            Mvgen_Add_Black_Rook_Moves(p, movelist, &nextfree, level);
            p->mobility -= 7;
            break;
        case BQUEEN :
            Mvgen_Add_Black_Rook_Moves(p, movelist, &nextfree, level);
            Mvgen_Add_Black_Bishop_Moves(p, movelist, &nextfree, level);
            p->mobility -= 13;
            break;
        default:
            break;
        }
    }
    Mvgen_Add_Black_King_Moves(&Bpieces[0], movelist, &nextfree, level);
    /* Sorting left for later during search */

    return nextfree;
}

int Mvgen_Find_All_White_Captures_And_Promotions(MOVE *restrict movelist, int underprom) /* This is used in quiescence search */
{
    int nextfree=0;
    PIECE *p;
    /* Find Moves plus mobility value for Rooks,Queens,Bishops,Knights. */
    /* Also subtract mobility normalisation value */
    for (p=Wpieces[0].next; p!=NULL; p=p->next) {
        p->mobility = 0;
        switch (p->type) {
        case WPAWN  :
            Mvgen_Add_White_Pawn_Captures_And_Promotions(p, movelist, &nextfree, underprom);
            break;
        case WKNIGHT:
            Mvgen_Add_White_Knight_Captures(p, movelist, &nextfree);
            p->mobility -= 4;
            break;
        case WBISHOP:
            Mvgen_Add_White_Bishop_Captures(p, movelist, &nextfree);
            p->mobility -= 6;
            break;
        case WROOK  :
            Mvgen_Add_White_Rook_Captures(p, movelist, &nextfree);
            p->mobility -= 7;
            break;
        case WQUEEN :
            Mvgen_Add_White_Rook_Captures(p, movelist, &nextfree);
            Mvgen_Add_White_Bishop_Captures(p, movelist, &nextfree);
            p->mobility -= 13;
            break;
        default:
            break;
        }
    }
    Mvgen_Add_White_King_Captures(&Wpieces[0], movelist, &nextfree);

    return nextfree;
}

int Mvgen_Find_All_Black_Captures_And_Promotions(MOVE *restrict movelist, int underprom)
{
    int nextfree=0;
    PIECE *p;
    /* Find Moves plus mobility value for Rooks,Queens,Bishops,Knights. */
    /* Also subtract mobility normalisation value */
    for (p=Bpieces[0].next; p!=NULL; p=p->next) {
        p->mobility = 0;
        switch (p->type) {
        case BPAWN  :
            Mvgen_Add_Black_Pawn_Captures_And_Promotions(p, movelist, &nextfree, underprom);
            break;
        case BKNIGHT:
            Mvgen_Add_Black_Knight_Captures(p, movelist, &nextfree);
            p->mobility -= 4;
            break;
        case BBISHOP:
            Mvgen_Add_Black_Bishop_Captures(p, movelist, &nextfree);
            p->mobility -= 6;
            break;
        case BROOK  :
            Mvgen_Add_Black_Rook_Captures(p, movelist, &nextfree);
            p->mobility -= 7;
            break;
        case BQUEEN :
            Mvgen_Add_Black_Rook_Captures(p, movelist, &nextfree);
            Mvgen_Add_Black_Bishop_Captures(p, movelist, &nextfree);
            p->mobility -= 13;
            break;
        default:
            break;
        }
    }
    Mvgen_Add_Black_King_Captures(&Bpieces[0], movelist, &nextfree);

    return nextfree;
}

/*checks whether the squares between from and to are free,
  useful for sliding pieces (queen, rook, bishop).*/
static int Mvgen_Check_Slider_Free(int from, int to, int step)
{
    for (int sq = from + step; sq != to; sq += step)
        if (board[sq]->type != NO_PIECE)
            return 0;
    return 1;
}

/*checks a move for pseudo legality.*/
int Mvgen_Check_Move_Legality(MOVE move, enum E_COLOUR colour)
{
    int from, to, from_type, to_type, diff;
    unsigned abs_diff;

    /*check for pseudolegal move*/

    /*we do separate switch-cases for each colour; while it would look more
      nice with one switch structure for both sides (with double cases), that
      might be much slower, depending on how the compiler decides to implement
      it. if it is an if-then-else-chain, we would end up with some type of
      "from_type == WPIECE || from_type==BPIECE" chain while such a chain will
      be without the or-checks with this duplicated code way. Same idea with
      the scattered "return 0" which save a branch out of the switch structure
      and out of the if-clause - which would just end up at the function end
      with "return 0". Maybe on some platforms, the compiler would get it, but
      that isn't a safe bet.

      Sometimes, going for speed just makes the code somewhat ugly, especially
      if speed matters and size doesn't.

      the pieces are ordered by probability of movements - kings and pawns
      move less than pieces, and the queen with her great mobility is very
      likely to move in the search tree, so that comes first.*/

    to = move.m.to;
    to_type = board[to]->type;
    from = move.m.from;
    from_type = board[from]->type;

    if (colour == WHITE)
    {
        if (UNLIKELY((to_type >= WPAWN) && (to_type <= WKING))) /*white cannot capture white pieces*/
            return 0;
        if (UNLIKELY(to_type == BKING)) /*kings cannot be captured*/
            return 0;

        switch (from_type)
        {
        case WQUEEN:
            diff = to - from;
            abs_diff = Abs(diff);
            if ((abs_diff % 10U) == 0) /*vertical move*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 10)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -10)) return 1;
                }
            } else if ((abs_diff % 11U) == 0) /*diagonal move up-right*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 11)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -11)) return 1;
                }
            } else if ((abs_diff % 9U) == 0) /*diagonal move up-left*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 9)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -9)) return 1;
                }
            } else if (RowNum[from] == RowNum[to]) /*horizontal move*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 1)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -1)) return 1;
                }
            }
            return 0;
        case WROOK:
            diff = to - from;
            abs_diff = Abs(diff);
            if ((abs_diff % 10U) == 0) /*vertical move*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 10)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -10)) return 1;
                }
            } else if (RowNum[from] == RowNum[to]) /*horizontal move*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 1)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -1)) return 1;
                }
            }
            return 0;
        case WBISHOP:
            diff = to - from;
            abs_diff = Abs(diff);
            if ((abs_diff % 11U) == 0) /*diagonal move up-right*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 11)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -11)) return 1;
                }
            } else if ((abs_diff % 9U) == 0) /*diagonal move up-left*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 9)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -9)) return 1;
                }
            }
            return 0;
        case WKNIGHT:
            diff = to - from;
            abs_diff = Abs(diff);
            if (LIKELY((abs_diff == 8U) || (abs_diff == 12U) || (abs_diff == 19U) || (abs_diff == 21U)))
                return 1;
            return 0;
        case WPAWN:
            if (to >= A8) /*check promotion flag*/
            {
                int flag = move.m.flag;

                if ((flag < WKNIGHT) || (flag > WQUEEN))
                    return 0;
            }
            diff = to - from;
            if (diff == 10) /*one step forward?*/
            {
                if (to_type == NO_PIECE)
                    return 1;
            } else if ((diff == 9) || (diff == 11)) /*capture, either directly or en passant?*/
            {
                if ((to_type != NO_PIECE) || (to == en_passant_sq))
                    return 1;
            } else if (diff == 20) /*two steps forward from the second rank?*/
            {
                if ((RowNum[from] == 2) && (to_type == NO_PIECE) && (board[from + 10]->type == NO_PIECE))
                    return 1;
            }
            return 0;
        case WKING:
            diff = to - from;
            abs_diff = Abs(diff);
            if ((abs_diff == 1U) || (abs_diff == 10U) || (abs_diff == 11U) || (abs_diff == 9U))
                return 1;
            /*check castling*/
            if ((abs_diff == 2U) && ((gflags & WKMOVED) == 0) && (wking == E1) && (to_type == NO_PIECE))
            {
                if (to == G1) /*castling short*/
                {
                    if (((gflags & WRH1MOVED) == 0) && (board[F1]->type == NO_PIECE) &&
                        (board[H1]->type == WROOK))
                    {
                        if (Mvgen_White_King_In_Check())
                            return 0;
                        wking = F1;
                        if (Mvgen_White_King_In_Check())
                        {
                            wking = E1;
                            return 0;
                        }
                        wking = E1;
                        return 1;
                    }
                } else if (to == C1) /*castling long*/
                {
                    if (((gflags & WRA1MOVED) == 0) &&
                        (board[D1]->type == NO_PIECE) && (board[B1]->type == NO_PIECE) &&
                        (board[A1]->type == WROOK))
                    {
                        if (Mvgen_White_King_In_Check())
                            return 0;
                        wking = D1;
                        if (Mvgen_White_King_In_Check())
                        {
                            wking = E1;
                            return 0;
                        }
                        wking = E1;
                        return 1;
                    }
                }
            }
            return 0;
        }
    } else /*black piece moving*/
    {
        if (UNLIKELY(to_type >= BPAWN)) /*black cannot capture black pieces*/
            return 0;
        if (UNLIKELY(to_type == WKING)) /*kings cannot be captured*/
            return 0;

        switch (from_type)
        {
        case BQUEEN:
            diff = to - from;
            abs_diff = Abs(diff);
            if ((abs_diff % 10U) == 0) /*vertical move*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 10)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -10)) return 1;
                }
            } else if ((abs_diff % 11U) == 0) /*diagonal move up-right*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 11)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -11)) return 1;
                }
            } else if ((abs_diff % 9U) == 0) /*diagonal move up-left*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 9)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -9)) return 1;
                }
            } else if (RowNum[from] == RowNum[to]) /*horizontal move*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 1)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -1)) return 1;
                }
            }
            return 0;
        case BROOK:
            diff = to - from;
            abs_diff = Abs(diff);
            if ((abs_diff % 10U) == 0) /*vertical move*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 10)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -10)) return 1;
                }
            } else if (RowNum[from] == RowNum[to]) /*horizontal move*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 1)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -1)) return 1;
                }
            }
            return 0;
        case BBISHOP:
            diff = to - from;
            abs_diff = Abs(diff);
            if ((abs_diff % 11U) == 0) /*diagonal move up-right*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 11)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -11)) return 1;
                }
            } else if ((abs_diff % 9U) == 0) /*diagonal move up-left*/
            {
                if (diff > 0)
                {
                    if (Mvgen_Check_Slider_Free(from, to, 9)) return 1;
                } else
                {
                    if (Mvgen_Check_Slider_Free(from, to, -9)) return 1;
                }
            }
            return 0;
        case BKNIGHT:
            diff = to - from;
            abs_diff = Abs(diff);
            if (LIKELY((abs_diff == 8U) || (abs_diff == 12U) || (abs_diff == 19U) || (abs_diff == 21U)))
                return 1;
            return 0;
        case BPAWN:
            if (to <= H1) /*check promotion flag*/
            {
                int flag = move.m.flag;

                if ((flag < BKNIGHT) || (flag > BQUEEN))
                    return 0;
            }
            diff = to - from;
            if (diff == -10) /*one step forward?*/
            {
                if (to_type == NO_PIECE)
                    return 1;
            } else if ((diff == -9) || (diff == -11)) /*capture, either directly or en passant?*/
            {
                if ((to_type != NO_PIECE) || (to == en_passant_sq))
                    return 1;
            } else if (diff == -20) /*two steps forward from the seventh rank?*/
            {
                if ((RowNum[from] == 7) && (to_type == NO_PIECE) && (board[from - 10]->type == NO_PIECE))
                    return 1;
            }
            return 0;
        case BKING:
            diff = to - from;
            abs_diff = Abs(diff);
            if ((abs_diff == 1U) || (abs_diff == 10U) || (abs_diff == 11U) || (abs_diff == 9U))
                return 1;
            /*check castling*/
            if ((abs_diff == 2U) && ((gflags & BKMOVED) == 0) && (bking == E8) && (to_type == NO_PIECE))
            {
                if (to == G8) /*castling short*/
                {
                    if (((gflags & BRH8MOVED) == 0) && (board[F8]->type == NO_PIECE) &&
                        (board[H8]->type == BROOK))
                    {
                        if (Mvgen_Black_King_In_Check())
                            return 0;
                        bking = F8;
                        if (Mvgen_Black_King_In_Check())
                        {
                            bking = E8;
                            return 0;
                        }
                        bking = E8;
                        return 1;
                    }
                } else if (to == C8) /*castling long*/
                {
                    if (((gflags & BRA8MOVED) == 0) &&
                        (board[D8]->type == NO_PIECE) && (board[B8]->type == NO_PIECE) &&
                        (board[A8]->type == BROOK))
                    {
                        if (Mvgen_Black_King_In_Check())
                            return 0;
                        bking = D8;
                        if (Mvgen_Black_King_In_Check())
                        {
                            bking = E8;
                            return 0;
                        }
                        bking = E8;
                        return 1;
                    }
                }
            }
            return 0;
        }
    }
    return 0; /*if we even get here, something is wrong anyway, so better reject that move.*/
}

/*find only moves for a certain piece. needed for the optimised UCI parser
  with legality checking.*/
int Mvgen_Find_All_White_Moves_Piece(MOVE *restrict movelist, int from_sq)
{
    int nextfree=0;
    PIECE *p = board[from_sq];

    switch (p->type)
    {
        case WPAWN  :
            Mvgen_Add_White_Pawn_Captures_And_Promotions(p,movelist,&nextfree, UNDERPROM);
            Mvgen_Add_White_Pawn_No_Caps_No_Prom_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        case WKNIGHT:
            Mvgen_Add_White_Knight_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        case WBISHOP:
            Mvgen_Add_White_Bishop_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        case WROOK  :
            Mvgen_Add_White_Rook_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        case WQUEEN :
            Mvgen_Add_White_Rook_Moves(p, movelist, &nextfree, NO_LEVEL);
            Mvgen_Add_White_Bishop_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        case WKING  :
            Mvgen_Add_White_King_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        default:
            break;
    }
    return nextfree;
}

/*find only moves for a certain piece. needed for the optimised UCI parser
  with legality checking.*/
int Mvgen_Find_All_Black_Moves_Piece(MOVE *restrict movelist, int from_sq)
{
    int nextfree=0;
    PIECE *p = board[from_sq];

    switch (p->type)
    {
        case BPAWN  :
            Mvgen_Add_Black_Pawn_Captures_And_Promotions(p,movelist,&nextfree, UNDERPROM);
            Mvgen_Add_Black_Pawn_No_Caps_No_Prom_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        case BKNIGHT:
            Mvgen_Add_Black_Knight_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        case BBISHOP:
            Mvgen_Add_Black_Bishop_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        case BROOK  :
            Mvgen_Add_Black_Rook_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        case BQUEEN :
            Mvgen_Add_Black_Rook_Moves(p, movelist, &nextfree, NO_LEVEL);
            Mvgen_Add_Black_Bishop_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        case BKING  :
            Mvgen_Add_Black_King_Moves(p, movelist, &nextfree, NO_LEVEL);
            break;
        default:
            break;
    }
    return nextfree;
}
