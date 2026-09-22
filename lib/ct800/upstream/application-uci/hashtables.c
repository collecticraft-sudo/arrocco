/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2015-2021, Rasmus Althoff <info@ct800.net>
 *  Copyright (C) 2010-2014, George Georgopoulos
 *
 *  This file is part of CT800/NGPlay (hash tables).
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
#include <string.h>
#include "ctdefs.h"
#include "util.h"
#include "move_gen.h"
#include "search.h"

/*---------- external variables ----------*/
/*-- READ-ONLY  --*/
extern PIECE *board[120];
extern PIECE empty_p;
extern PIECE Wpieces[16];
extern PIECE Bpieces[16];
extern int mv_stack_p;
extern int Starting_Mv;
extern uint16_t cstack[MAX_STACK+1];
extern int cst_p;
extern int fifty_moves;
extern int en_passant_sq;
extern unsigned int gflags;
extern const int8_t RowNum[120];
extern const int8_t ColNum[120];
extern const int8_t boardXY[120];
extern size_t MAX_TT;
extern unsigned int hash_clear_counter;

/*-- READ-WRITE --*/
extern MVST move_stack[MAX_STACK+1];

extern TT_ST *T_T;
extern TT_ST *Opp_T_T;

/*---------- module global variables ----------*/

#define MTLENGTH  624U
#define BITMASK32 0xFFFFFFFFU
#define BITPOW31  ((uint32_t)(1UL << 31))
static unsigned int mt[MTLENGTH];
static unsigned int idx;

static uint64_t hash_board[PIECEMAX][ENDSQ];
static uint64_t hash_ep[64];

static int signed_material_table[PIECEMAX] =
           {0, 0,  PAWN_V,  KNIGHT_V,  BISHOP_V,  ROOK_V,  QUEEN_V, 0, 0, 0,
            0, 0, -PAWN_V, -KNIGHT_V, -BISHOP_V, -ROOK_V, -QUEEN_V, 0
           };


/*---------- local functions ----------*/

static void Hash_Init_Rand32_MT(uint32_t seed)
{
    uint32_t i;
    idx = 0;
    mt[0] = seed;
    for(i = 1; i < MTLENGTH; i++) mt[i] = (1812433253U*(mt[i-1]^(mt[i-1]>>30))+i) & BITMASK32;
}

static uint32_t Hash_Get_Random32_MT(void)
{
    uint32_t y;
    if(idx == 0) {
        uint32_t i;
        for(i = 0; i < MTLENGTH; i++) {
            y = (mt[i]&BITPOW31) + (mt[(i+1)%MTLENGTH] & (BITPOW31-1));
            mt[i] = mt[(i+397U)%MTLENGTH] ^ (y >> 1);
            if (y%2) mt[i] ^= 2567483615U;
        }
    }
    y = mt[idx];
    y ^= y >> 11;
    y ^= (y << 7) & 2636928640U;
    y ^= (y << 15) & 4022730752U;
    y ^= y >> 18;
    idx = (idx + 1) % MTLENGTH;
    return y;
}

static uint64_t Hash_Get_Random64_MT(void)
{
    uint64_t ret;
    uint32_t high, low;

    low  = Hash_Get_Random32_MT();
    high = Hash_Get_Random32_MT();

    ret = (((uint64_t) high) << 32) | low;

    return ret;
}


/*---------- global functions ----------*/


void Hash_Clear_Tables(void)
{
    memset(T_T, 0,     (MAX_TT + CLUSTER_SIZE) * sizeof(TT_ST));
    memset(Opp_T_T, 0, (MAX_TT + CLUSTER_SIZE) * sizeof(TT_ST));
}

/*clear the oldest entries in the hash tables to deal with aging.*/
void Hash_Cut_Tables(unsigned int clear_counter)
{
    size_t i;

    /*shift that over the depth length*/
    clear_counter <<= 6;

    for (i = 0; i < MAX_TT+CLUSTER_SIZE; i++)
    {
        TT_ST *ttentry;

        ttentry = T_T + i;
        if (((ttentry->depth) & 0xC0) == clear_counter)
            memset(ttentry, 0, sizeof(TT_ST));

        ttentry = Opp_T_T + i;
        if (((ttentry->depth) & 0xC0) == clear_counter)
            memset(ttentry, 0, sizeof(TT_ST));
    }
}


/*checks only the first DEF_MAX_TT entries, that is the minimum size. if
  the hash table is bigger, this fragment is still representative for the
  whole table.
  returns the hash usage in permill.*/
unsigned Hash_Get_Usage(void)
{
    uint32_t i;
    uint64_t hash_used;
    for (i = 0, hash_used = 0; i < DEF_MAX_TT; i++)
    {
        if (T_T[i].flag != 0) hash_used++;
        if (Opp_T_T[i].flag != 0) hash_used++;
    }
    hash_used = (hash_used * (1000U / 2U)) / DEF_MAX_TT;
    return((unsigned) hash_used);
}

void Hash_Init(void)
{
    int i, j;
    Hash_Init_Rand32_MT(3571U);

    for (i = 0; i < PIECEMAX; i++) {
        for (j = 0; j < ENDSQ; j++) {
            hash_board[i][j] = Hash_Get_Random64_MT();
        }
    }
    for (i = 0; i < 64; i++) {
        hash_ep[i] = Hash_Get_Random64_MT();
    }
}

void Hash_Init_Stack(void)
{
    uint64_t ret;
    uint64_t tmp;
    int i;
    ret = move_stack[0].mv_pawn_hash = move_stack[0].material = 0;
    for (i = 0; i < 16; i++) {
        if (Wpieces[i].xy) {
            move_stack[0].material += signed_material_table[Wpieces[i].type];
            tmp = hash_board[ Wpieces[i].type ][Wpieces[i].xy];
            if (Wpieces[i].type == WPAWN) {
                move_stack[0].mv_pawn_hash ^= tmp;
            }
            ret ^= tmp;
        }
        if (Bpieces[i].xy) {
            move_stack[0].material += signed_material_table[Bpieces[i].type];
            tmp = hash_board[ Bpieces[i].type ][Bpieces[i].xy];
            if (Bpieces[i].type == BPAWN) {
                move_stack[0].mv_pawn_hash ^= tmp;
            }
            ret ^= tmp;
        }
    }
    if (en_passant_sq)
        ret ^= hash_ep[boardXY[en_passant_sq]];
    ret ^= gflags;
    move_stack[0].mv_pos_hash = ret;
    move_stack[0].captured = &empty_p;
    move_stack[0].move.u = 0;
    move_stack[0].move.m.flag = 1;
}

int Hash_Repetitions(void)
{
    int i;
    int ret = 1;
    /*the current position counts also as one potential repetition, so we must
      initialise the repetition counter to 1 and not to 0.
      i-=2 because a position repetition is not just an identical board position, but also
      the side to move must be the same.*/
    uint64_t hashP = move_stack[mv_stack_p].mv_pos_hash;
    for (i = mv_stack_p - 2; i >= 0; i -= 2)
    {
        if (hashP == move_stack[i].mv_pos_hash)
            ret++;
    }
    return ret;
}

/*returns whether a position is checkmate. needed for the corner case with the
  50 moves rule: if the 100th ply is checkmate without being a capture or pawn
  move, then the checkmate has priority over the 50 moves draw.
  it looks a bit odd to verify both kings for being in check, but this routine
  is only called for the 100th ply under the 50 moves rule, and then the game
  is a draw anyway - unless one side blunders, of course.*/
static int Hash_Is_Checkmate(void)
{
    MOVE check_attacks[CHECKLISTLEN];
    MOVE movelist[MAXMV];
    int i, move_cnt;
    int n_checks, n_check_pieces;
    if (Mvgen_White_King_In_Check())
    {
        /*White's king is in check, so it must be White's turn.*/
        int non_checking_move = 0;
        n_checks = Mvgen_White_King_In_Check_Info(check_attacks, &n_check_pieces);
        move_cnt = Mvgen_Find_All_White_Evasions(movelist, check_attacks, n_checks, n_check_pieces, UNDERPROM);
        /*is there a legal move?*/
        for (i = 0; i < move_cnt; i++)
        {
            Search_Push_Status();
            Search_Make_Move(movelist[i]);
            if (!Mvgen_White_King_In_Check()) non_checking_move = 1;
            Search_Retract_Last_Move();
            Search_Pop_Status();
            if (non_checking_move) return 0; /*that move gets out of check: no checkmate.*/
        }
        return 1; /*no move gets the white king out of check: checkmate.*/
    }
    if (Mvgen_Black_King_In_Check())
    {
        /*Black's king is in check, so it must be Black's turn.*/
        int non_checking_move = 0;
        n_checks = Mvgen_Black_King_In_Check_Info(check_attacks, &n_check_pieces);
        move_cnt = Mvgen_Find_All_Black_Evasions(movelist, check_attacks, n_checks, n_check_pieces, UNDERPROM);
        /*is there a legal move?*/
        for (i = 0; i < move_cnt; i++)
        {
            Search_Push_Status();
            Search_Make_Move(movelist[i]);
            if (!Mvgen_Black_King_In_Check()) non_checking_move = 1;
            Search_Retract_Last_Move();
            Search_Pop_Status();
            if (non_checking_move) return 0; /*that move gets out of check: no checkmate.*/
        }
        return 1; /*no move gets the black king out of check: checkmate.*/
    }

    return 0; /*no king is in check, so it can't be checkmate.*/
}

/*************************************************************************************
That one is called during the search. It looks for two drawish things:
a) has the one who is about to move already made that position come up before?
    if we hit a capture or pawn move during that backward search, we can
    abort this search.
b) if we hit 100 plies without pawn move or capture (also on behalf of the
    opponent), then the position will be draw even without any kind of repetition.
*************************************************************************************/
int Hash_Check_For_Draw(void)
{
    uint64_t hashP = move_stack[mv_stack_p].mv_pos_hash;
    MVST* p;
    int i;

    /*first step: backward-check from the search tree leaf for repetition.*/
    for (i = mv_stack_p-2; i >= 0; i -= 2)
    {
        p = &move_stack[i];
        /*captures and pawn moves reset the possibility of repetition*/
        if (p->captured->type /* capture */ || p->move.m.flag>1 /* pawn move */)
            break;
        if (hashP == p->mv_pos_hash)
            return 1;
    }

    /*second step, optional: forward-check from the board position for the 50 moves rule.*/
    if (UNLIKELY(fifty_moves >= FIFTY_MOVES_FULL))
    {
        int no_special_moves = fifty_moves;
        for (i = Starting_Mv + 1; ((i <= mv_stack_p) && (no_special_moves <= 100)); i++)
        {
            p = &move_stack[i];
            if (p->captured->type /* capture */ || p->move.m.flag>1 /* pawn move */)
               return 0; /*the tree cannot be a 100 additional plies deep anyway*/
            no_special_moves++;
        }

        /*50 moves without capture or pawn move?*/
        if (no_special_moves >= 100)
        {
            if (no_special_moves > 100) /*too late*/
                return 1;
            if (!Hash_Is_Checkmate()) /*checkmate has priority at ply 100*/
                return 1;
        }
    }
    return 0;
}


uint64_t Hash_Get_Position_Value(uint64_t *pawn_hash)
{
    uint64_t ret;
    uint64_t tmp_pawn_hash;
    int xy1, xy2, ptype;
    MVST* p;
    MVST* p_prev;
    unsigned int cp_entry;
    unsigned int cp_ep_square;

    p  = &move_stack[mv_stack_p];
    p_prev = p-1;
    xy1 = p->move.m.from;
    xy2 = p->move.m.to;
    ret = (p_prev->mv_pos_hash);
    tmp_pawn_hash = p_prev->mv_pawn_hash;
    ptype = board[xy2]->type;
    p->material = p_prev->material;
    if (p->special == NORMAL)
    {
        ret ^= hash_board[ ptype ][xy1];
        if (ptype==WPAWN || ptype==BPAWN)
            tmp_pawn_hash ^= hash_board[ ptype ][xy1];
    } else if (p->special == PROMOT)
    {
        p->material += signed_material_table[ptype];
        if (ptype > BLACK)
        {
            p->material += PAWN_V;
            ret ^= hash_board[ BPAWN ][xy1];
            tmp_pawn_hash ^= hash_board[ BPAWN ][xy1];
        } else
        {
            p->material -= PAWN_V;
            ret ^= hash_board[ WPAWN ][xy1];
            tmp_pawn_hash ^= hash_board[ WPAWN ][xy1];
        }
    } else if (p->special == CASTL)
    {
        ret ^= hash_board[ ptype ][xy1];
        if (xy2==G1)
        {
            ret ^= hash_board[ WROOK ][H1];
            ret ^= hash_board[ WROOK ][F1];
        } else if (xy2==G8) {
            ret ^= hash_board[ BROOK ][H8];
            ret ^= hash_board[ BROOK ][F8];
        } else if (xy2==C1) {
            ret ^= hash_board[ WROOK ][A1];
            ret ^= hash_board[ WROOK ][D1];
        } else if (xy2==C8) {
            ret ^= hash_board[ BROOK ][A8];
            ret ^= hash_board[ BROOK ][D8];
        }
    }
    ret ^= hash_board[ ptype ][xy2];
    if (ptype==WPAWN || ptype==BPAWN)
        tmp_pawn_hash ^= hash_board[ ptype ][xy2];
    ptype = p->captured->type;
    if (ptype)
    {
        p->material -= signed_material_table[ptype];
        ret ^= hash_board[ ptype ][p->capt];
        if (ptype==WPAWN || ptype==BPAWN)
            tmp_pawn_hash ^= hash_board[ ptype ][p->capt];
    }
    cp_entry = cstack[cst_p];
    cp_ep_square = cp_entry >> 9;

    if (cp_ep_square)
        ret ^= hash_ep[boardXY[cp_ep_square]];

    if (en_passant_sq)
        ret ^= hash_ep[boardXY[en_passant_sq]];

    *pawn_hash = tmp_pawn_hash;

    ret ^= (unsigned int)(cp_entry & ALLFLAGS);
    ret ^= gflags;

    return ret;
}

int Hash_Check_TT_PV(const TT_ST *tt, enum E_COLOUR colour, int pdepth, uint64_t pos_hash, int *valueP, MOVE* hmvp)
{
    int i;
    const TT_ST *ttentry;
    uint32_t key32 = pos_hash >> 32;
    uint16_t key32_h = key32 >> 16;
    uint16_t key32_l = key32 & 0xFFFFU;
    uint32_t additional_bits = ((uint32_t) ((pos_hash >> 24) & 0xFCU));

    ttentry = &tt[pos_hash & MAX_TT];
    for (i = 0; i < CLUSTER_SIZE; i++)
    {
        if ((ttentry->pos_hash_upper_h == key32_h) && (ttentry->pos_hash_upper_l == key32_l)) /*potential hit*/
        {
            if (LIKELY((ttentry->flag & 0xFCU) == additional_bits))
            {
                if (ttentry->cmove)
                {
                    MOVE tt_move = Mvgen_Decompress_Move(ttentry->cmove);
                    if (UNLIKELY(Mvgen_Check_Move_Legality(tt_move, colour) == 0))
                        continue;
                    *hmvp = tt_move;
                }
                if (((int) (ttentry->depth & 0x3FU)) >= pdepth)
                {
                    if ((ttentry->flag & 0x03U) == EXACT)
                    {
                        int ttvalue = ttentry->value;

                        /*adjust possible mate distance from node relative
                          to root relative*/
                        if (ttvalue >= 0)
                        {
                            if (ttvalue > MATE_CUTOFF)
                                ttvalue -= mv_stack_p - Starting_Mv;
                        } else if (ttvalue < -MATE_CUTOFF)
                            ttvalue += mv_stack_p - Starting_Mv;

                        *valueP = ttvalue;
                        return 1;
                    }
                }
            }
        }
        ttentry++;
    }
    return 0;
}

int Hash_Check_TT(const TT_ST *tt, enum E_COLOUR colour, int alpha, int beta, int pdepth, uint64_t pos_hash, int *valueP, MOVE* hmvp)
{
    int i;
    const TT_ST *ttentry;
    uint32_t key32 = pos_hash >> 32;
    uint16_t key32_h = key32 >> 16;
    uint16_t key32_l = key32 & 0xFFFFU;
    uint32_t additional_bits = ((uint32_t)((pos_hash >> 24) & 0xFCU));
    ttentry = &tt[pos_hash & MAX_TT];

    for (i = 0; i < CLUSTER_SIZE; i++)
    {
        if ((ttentry->pos_hash_upper_h == key32_h) && (ttentry->pos_hash_upper_l == key32_l)) /*potential hit*/
        {
            if (LIKELY((ttentry->flag & 0xFCU) == additional_bits))
            {
                if (ttentry->cmove)
                {
                    MOVE tt_move = Mvgen_Decompress_Move(ttentry->cmove);
                    if (UNLIKELY(Mvgen_Check_Move_Legality(tt_move, colour) == 0))
                        continue;
                    *hmvp = tt_move;
                }
                if (((int) (ttentry->depth & 0x3FU)) >= pdepth)
                {
                    unsigned int lflag = ttentry->flag & 0x03U;
                    int ttvalue = ttentry->value;

                    /*adjust possible mate distance from node relative
                      to root relative*/
                    if (ttvalue >= 0)
                    {
                        if (ttvalue > MATE_CUTOFF)
                            ttvalue -= mv_stack_p - Starting_Mv;
                    } else if (ttvalue < -MATE_CUTOFF)
                        ttvalue += mv_stack_p - Starting_Mv;

                    switch (lflag)
                    {
                    case CHECK_ALPHA:
                        if (ttvalue <= alpha)
                        {
                            *valueP = alpha;
                            return 1;
                        }
                        break;
                    case CHECK_BETA:
                        if (ttvalue >= beta)
                        {
                            *valueP = beta;
                            return 1;
                        }
                        break;
                    case EXACT:
                        *valueP = ttvalue;
                        return 1;
                    default:
                        break;
                    }
                }
            }
        }
        ttentry++;
    }
    return 0;
}

void Hash_Update_TT(TT_ST *tt, int pdepth, int pvalue, unsigned int pflag, uint64_t pos_hash, MOVE hmv)
{
    TT_ST *ttentry;
    uint32_t key32 = pos_hash >> 32;
    uint16_t key32_h = key32 >> 16;
    uint16_t key32_l = key32 & 0xFFFFU;
    uint32_t additional_bits = (uint32_t) ((pos_hash >> 24) & 0xFCU);
    ttentry = &tt[pos_hash & MAX_TT];

    /*1st cluster place: depth preferred*/
    if (pdepth < (int) (ttentry->depth & 0x3FU))
    {
        /*same position as the current one already saved, only with more depth? then don't store.*/
        if ((ttentry->pos_hash_upper_h == key32_h) && (ttentry->pos_hash_upper_l == key32_l))
        {
            uint32_t tupd_flag = (uint32_t) ttentry->flag;
            if ((tupd_flag & 0xFCU) == additional_bits)
            {
                uint32_t t_flag = tupd_flag & 0x03U;
                if ((t_flag == EXACT) || (t_flag == pflag))
                    return;
            }
        }
        ttentry++;
    }
    /*2nd cluster place: replace if empty or same key*/
    if (ttentry->flag == NO_FLAG) /*if empty, then also the additional bits will be 0*/
    {
        ttentry->cmove = Mvgen_Compress_Move(hmv);
        ttentry->pos_hash_upper_h = key32_h;
        ttentry->pos_hash_upper_l = key32_l;
    } else if ((ttentry->pos_hash_upper_h == key32_h) && (ttentry->pos_hash_upper_l == key32_l) &&
               ((((uint32_t) ttentry->flag) & 0xFCU) == additional_bits)) /*hit*/
    {
        if (hmv.u != MV_NO_MOVE_MASK) /*if no new move then preserve old one*/
        {
            ttentry->cmove = Mvgen_Compress_Move(hmv);
        }
    } else
    {
        /*3rd cluster place: always replace*/
        ttentry++;
        ttentry->cmove = Mvgen_Compress_Move(hmv);
        ttentry->pos_hash_upper_h = key32_h;
        ttentry->pos_hash_upper_l = key32_l;
    }
    ttentry->flag  = (uint8_t) (pflag | additional_bits);
    ttentry->depth = (uint8_t) (((unsigned) pdepth) | (hash_clear_counter << 6));
    /*adjust possible mate distance from root relative to node relative*/
    if (pvalue >= 0)
    {
        if (pvalue > MATE_CUTOFF)
            pvalue += mv_stack_p - Starting_Mv;
    } else if (pvalue < -MATE_CUTOFF)
            pvalue -= mv_stack_p - Starting_Mv;
    ttentry->value = (int16_t) pvalue;
}
