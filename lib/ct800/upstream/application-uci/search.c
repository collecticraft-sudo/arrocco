/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2015-2024, Rasmus Althoff <info@ct800.net>
 *  Copyright (C) 2010-2014, George Georgopoulos
 *
 *  This file is part of CT800/NGPlay (search engine).
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

#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ctdefs.h"
#include "move_gen.h"
#include "hashtables.h"
#include "eval.h"
#include "book.h"
#include "util.h"
#include "search.h"

extern char *Play_Translate_Moves(MOVE m);
extern int64_t Play_Get_Millisecs(void);
extern int64_t Play_Get_Own_Millisecs(void);
extern void Play_Print(const char *str);
extern int Play_Get_Abort(void);
extern void Play_Wait_For_Abort_Event(int32_t millisecs);

/*---------- external variables ----------*/
/*-- READ-ONLY  --*/
extern int game_started_from_0;
extern int32_t start_moves;

extern PIECE empty_p;
extern int fifty_moves;
extern int dynamic_resign_threshold;
extern enum E_COLOUR computer_side;
extern uint64_t g_max_nodes;
extern PIECE Wpieces[16];
extern PIECE Bpieces[16];
extern const MOVE mv_move_mask;

/*UCI options*/
extern unsigned int disable_book;
extern enum E_CURRMOVE show_currmove;
extern int contempt_val;
extern int contempt_end;
extern volatile unsigned int uci_debug;
extern int32_t eval_noise;

/*-- READ-WRITE --*/
extern PIECE *board[120];
extern int mv_stack_p;
extern MVST move_stack[MAX_STACK+1];
extern int cst_p;
extern uint16_t cstack[MAX_STACK+1];
extern int Starting_Mv;
extern int wking, bking;
extern int en_passant_sq;
extern unsigned int gflags;
extern GAME_INFO game_info;
extern LINE GlobalPV;
extern uint64_t g_nodes;

extern TT_ST     *T_T;
extern TT_ST *Opp_T_T;
extern unsigned int hash_clear_counter;


/*---------- module global variables ----------*/

static enum E_TIMEOUT time_is_up;

/*for every root move, the expected reply from the opponent is cached in this
  array during the iterative deepening to help with the move sorting. for the
  UCI version, this is not that important because the hashtables are big enough,
  but for the embedded version with small hash tables, this is quite helpful.
  however, it is not damaging for the UCI version, either.*/
static CMOVE root_refutation_table[MAXMV];

/*use the Ciura sequence for the shell sort. more than 57 is not needed because
  the rare maximum of pseudo-legal moves in real game positions is about 80 to 90.*/
static const int shell_sort_gaps[] = {1, 4, 10, 23, 57 /*, 132, 301, 701*/};

/*knight and bishop values must be the same*/
static const int PieceValFromType[PIECEMAX]= {0, 0, 100, 400, 400, 650, 1300, INFINITY_, 0, 0,
                                              0, 0, 100, 400, 400, 650, 1300, INFINITY_
                                             };

/*for the root aspiration window - as add-on values.
  in absolute terms, that is {20, 80, 230}.*/
#define WINDOW_STEPS    3
static const int window_margins[WINDOW_STEPS] = {20, 60, 150};

/* --------- FUTILITY PRUNING DEFINITIONS ---------- */
#define FUTIL_DEPTH     5
static const int FutilityMargins[FUTIL_DEPTH] = {0, 130, 130, 240, 240};

/*same margins in both directions*/
#define RVRS_FUTIL_D    FUTIL_DEPTH
#define RVRS_FutilMargs FutilityMargins

/* ------------- GLOBAL KILLERS/HISTORY TABLES ----------------*/

uint16_t W_history[6][ENDSQ], B_history[6][ENDSQ];
CMOVE W_Killers[2][MAX_DEPTH], B_Killers[2][MAX_DEPTH];

/* ------------- CHECK LIST BUFFER ----------------*/

/*the check list is only used as intermediate buffer back to back with
  finding evasions as move generation if being in check. works because
  of single thread and reduces stack usage.*/
static MOVE search_check_attacks_buf[CHECKLISTLEN];

/* ------------- TIME/UCI CONTROL ----------------*/

static char printbuf[512]; /*with the depth limitation, this is more than sufficient.*/

static int64_t start_time, start_time_nps, stop_time, output_time, throttle_time, sleep_time;
static uint64_t nps_1ms, last_nodes, last_throttle_nodes, nps_startup_phase, nodes_current_second;

static uint64_t effective_max_nps_rate; /*kicks in after pre-search*/
static int effective_cpu_speed;

static MOVE uci_curr_move;
static unsigned int uci_curr_move_number;

#ifdef DBGCUTOFF
static uint64_t cutoffs_on_1st_move, total_cutoffs;
#endif

/*---------- local functions ----------*/

static int64_t Time_Passed(void)
{
    return(Play_Get_Millisecs() - start_time);
}

static void Time_Calc_Throttle(int64_t current_time)
{
    int64_t remaining_time = stop_time - current_time;
    /*will the move time be over in the next second?*/
    if (remaining_time < 1000)
    {
        if (remaining_time > 0) /*should be the case*/
        {
            /*adjust the effective CPU speed*/
            if (effective_cpu_speed < 100)
            {
                /*reduce the CPU speed proportionally to the shorter time
                  frame that remains.*/
                int64_t tmp_speed = effective_cpu_speed;
                tmp_speed *= remaining_time;
                tmp_speed += 500; /*rounding*/
                tmp_speed /= 1000;
                effective_cpu_speed = (int) tmp_speed;
            }

            /*adjust the kNPS rate*/
            effective_max_nps_rate *= remaining_time;
            effective_max_nps_rate += 500U; /*rounding*/
            effective_max_nps_rate /= 1000U;
        } else
        {
            effective_cpu_speed = 0;
            effective_max_nps_rate = 0;
        }
    }
}

/*prints general statistics once per second during calculation.*/
static void Time_Output(int64_t current_time, int64_t subtract_time)
{
    if (current_time >= output_time)
    {
        int64_t time_passed, time_passed_calib;
        uint64_t nps_stat;
        uint16_t hash_used;
        int len;

        time_passed = current_time - start_time;

        if (time_passed > 0)
            nps_stat = (g_nodes * 1000) / time_passed;
        else
            nps_stat = 0;

        time_passed_calib = current_time - start_time_nps - subtract_time;

        /*update the NPS rate for time checking continuously. It may well change
          over time, e.g. when the hash tables get full.*/
        if (time_passed_calib > 0)
            nps_1ms = g_nodes / time_passed_calib;

        hash_used = (uint16_t) Hash_Get_Usage();

        /*convert the data to the UCI info string.*/
        strcpy(printbuf, "info time ");
        len = 10;
        len += Util_Tostring_I64(printbuf + len, time_passed);
        strcpy(printbuf + len, " nodes ");
        len += 7;
        len += Util_Tostring_U64(printbuf + len, g_nodes);
        strcpy(printbuf + len, " nps ");
        len += 5;
        len += Util_Tostring_U64(printbuf + len, nps_stat);
        strcpy(printbuf + len, " hashfull ");
        len += 10;
        len += Util_Tostring_U16(printbuf + len, hash_used);

        if (show_currmove == CURR_UPDATE)
        {
            char *mv_str;

            strcpy(printbuf + len, " currmove ");
            len += 10;
            mv_str = Play_Translate_Moves(uci_curr_move);
            strcpy(printbuf + len, mv_str);
            len += (mv_str[4] == 0) ? 4 : 5; /*move with or without promotion*/
            strcpy(printbuf + len, " currmovenumber ");
            len += 16;
            len += Util_Tostring_U16(printbuf + len, (uint16_t)(uci_curr_move_number + 1U));
        }
        printbuf[len++] = '\n';
        printbuf[len] = '\0';

        Play_Print(printbuf);

        nodes_current_second = 0; /*for the next second*/

        /*reduce effective throttle speed if the move time will be over
          in the next second.*/
        Time_Calc_Throttle(output_time);

        output_time += 1000;
    }
}

/*checks whether the allocated move time or node count has been reached.
  if not, the software CPU throttling is done here by inserting waiting
  slices.*/
static enum E_TIMEOUT Time_Check_Throttle(void)
{
    int64_t current_time;
    enum E_THROTTLE throttle_mode;

    /*check whether we need to check the time. that would involve a system
      call and is quite expensive, so it's only done about every millisecond.
      since the node rate is known / calibrated, this boils down to checking
      the time every so many nodes.*/
    nodes_current_second += g_nodes - last_throttle_nodes;
    last_throttle_nodes = g_nodes;
    if ((g_nodes - last_nodes < nps_1ms) && (nodes_current_second < effective_max_nps_rate))
        return(TM_NO_TIMEOUT);

    current_time = Play_Get_Millisecs();

    /*move time over?*/
    if (current_time >= stop_time)
        return(TM_TIMEOUT);

    /*search with total nodes limit?*/
    if ((g_max_nodes) && (g_nodes + (nps_1ms * 5) / 4 >= g_max_nodes))
        return(TM_NODES);

    /*UCI stop or quit command?*/
    if (Play_Get_Abort())
        return(TM_ABORT);

    /*during initial NPS calibration, calculate the NPS
      relative to the absolute nodes vs. absolute time.*/
    if (nps_startup_phase)
    {
        int64_t time_passed_calib = current_time - start_time_nps - sleep_time;
        if (time_passed_calib > 0)
        {
            nps_1ms = g_nodes / time_passed_calib;
            if (nps_1ms < 500) nps_1ms = 500; /*can happen with 10ms timer resolution*/
            /*10ms initial calibration time because the CPU throttling in percentage
              mode works with 10ms slices. continuous calibration happens once per second
              in Time_Output().*/
            if (time_passed_calib >= 10)
                nps_startup_phase = 0;
        }
    }

    last_nodes = g_nodes;

    /*does throttling kick in, and if so, which one?*/
    if (nodes_current_second >= effective_max_nps_rate)
        throttle_mode = THROTTLE_NPS_RATE;
    else if (current_time >= throttle_time)
        throttle_mode = THROTTLE_CPU_PERCENT;
    else
        throttle_mode = THROTTLE_NONE;

    /*the throtting divides a second into a 100 slices of 10 ms each.
      n% CPU speed means 100-x% throttling, so 100% CPU speed is 0%
      throttling, which is no throttling. 1% CPU speed (the minimum)
      is 99% throttling.

      in case of NPS related throttling, the idea is to use every second
      until the nodes allowed in one second are spent, and then wait for
      the rest of the second.*/
    if (throttle_mode != THROTTLE_NONE)
    {
        int64_t stop_sleep_time, stop_throttle_time, start_throttle_time;

        start_throttle_time = current_time;

        /*output_time gets updated in Time_Output() during the waiting loop.*/
        stop_throttle_time = output_time;

        if (throttle_mode == THROTTLE_CPU_PERCENT)
        {
            /*partial sleep to circumvent the CPU wakeup to full speed,
              with busy waiting for the last 50 ms. the percentage refers
              to the full CPU speed, after all.*/
            stop_sleep_time = stop_throttle_time - 50;
            if (uci_debug)
            {
                int len;
                strcpy(printbuf, "info string debug: CPU [%] throttling at: ");
                len = 42;
                len += Util_Tostring_U16(printbuf + len, (uint16_t) effective_cpu_speed);
                strcpy(printbuf + len, " %\n");
                Play_Print(printbuf);
            }
        } else
        {
            /*full sleep. it doesn't matter whether the first 10-20 ms after
              wakeup still run at reduced CPU frequency because in kNPS mode,
              the engine can just calculate a bit longer in the next second.*/
            stop_sleep_time = stop_throttle_time;
            if (uci_debug)
            {
                /*scale down the NPS to kNPS with rounding*/
                int len;

                strcpy(printbuf, "info string debug: CPU [kNPS] throttling at: ");
                len = 45;
                len += Util_Tostring_U64(printbuf + len, (effective_max_nps_rate + 500U) / 1000U);
                strcpy(printbuf + len, " kNPS\n");
                Play_Print(printbuf);
            }
        }

        /*check whether the move time limits the sleeps.*/
        if (stop_sleep_time > stop_time)
            stop_sleep_time = stop_time;

        while (current_time < stop_throttle_time)
        {
            /*add 1 ms to account for time resolution*/
            if (current_time < stop_sleep_time)
                Play_Wait_For_Abort_Event((int32_t) (stop_sleep_time - current_time + 1));

            current_time = Play_Get_Millisecs();

            if (current_time >= stop_time) /*move time over*/
                return(TM_TIMEOUT);

            if (Play_Get_Abort()) /*UCI stop or quit command*/
                return(TM_ABORT);

            Time_Output(current_time, sleep_time + current_time - start_throttle_time);
        }

        /*if percentage throttling could be possible, reset the status*/
        if (current_time >= throttle_time - 2000)
        {
            int64_t max_throttle_time;

            /*baseline: the next N 10 ms frames for computing.*/
            throttle_time = current_time + effective_cpu_speed * 10;

            /*output_time already updated? should be, actually.*/
            if (current_time >= output_time)
                max_throttle_time = output_time + 1000; /*not yet*/
            else
                max_throttle_time = output_time; /*already updated*/

            /*don't leak into another 1 second frame.*/
            if (throttle_time > max_throttle_time)
                throttle_time = max_throttle_time;
        }

        sleep_time += current_time - start_throttle_time;
    }

    Time_Output(current_time, sleep_time);

    return(TM_NO_TIMEOUT);
}

/*used for timing until move time is over or the user stops the engine.
  necessary for fixed UCI move time or infinite when there is nothing
  to calculate because e.g. maximum depth has been reached or a mate is
  already found.*/
static void Time_Wait_For_Abort(void)
{
    uint16_t hash_used = Hash_Get_Usage();

    for (;;)
    {
        int64_t time_passed, current_time, wakeup_time;

        current_time = Play_Get_Millisecs();

        /*move time over?*/
        if (current_time >= stop_time)
            return;

        /*has someone set the abort flag?*/
        if (Play_Get_Abort()) return;

        time_passed = current_time - start_time;

        if (current_time >= output_time)
        {
            int len;

            strcpy(printbuf, "info time ");
            len = 10;
            len += Util_Tostring_I64(printbuf + len, time_passed);
            strcpy(printbuf + len, " nodes ");
            len += 7;
            len += Util_Tostring_U64(printbuf + len, g_nodes);
            strcpy(printbuf + len, " nps 0 hashfull ");
            len += 16;
            len += Util_Tostring_U16(printbuf + len, hash_used);
            printbuf[len++] = '\n';
            printbuf[len] = '\0';
            Play_Print(printbuf);

            output_time += 1000;
        }

        /*next scheduled wakeup is either when the next outut is due or when
          the move time is over. or an abort event can be triggered via UCI
          stop/quit commands.*/
        if (stop_time > output_time)
            wakeup_time = output_time;
        else
            wakeup_time = stop_time;

        if (current_time < wakeup_time)
            Play_Wait_For_Abort_Event((int32_t) (wakeup_time - current_time));
    }
}

/*that's a shell sort which
  a) doesn't need recursion, unlike quicksort, and
  b) is faster than quicksort on lists with less than 100 entries.*/
void Search_Do_Sort(MOVE *restrict movelist, int N)
{
    int sizeIndex = sizeof(shell_sort_gaps)/sizeof(shell_sort_gaps[0]) - 1;

    do {
        int gap = shell_sort_gaps[sizeIndex];

        for (int i = gap; i < N; i++)
        {
            int value = movelist[i].m.mvv_lva;
            uint32_t tmp_move = movelist[i].u;
            int j;

            for (j = i - gap; j >= 0; j -= gap)
            {
                MOVE current_move;

                current_move.u = movelist[j].u;
                if (current_move.m.mvv_lva >= value)
                    break;
                movelist[j + gap].u = current_move.u;
            }
            movelist[j + gap].u = tmp_move;
        }
    } while (--sizeIndex >= 0);
}

/*shell sort for smaller capture lists*/
static void Search_Do_Sort_QS(MOVE *restrict movelist, int N)
{
#define GAP_1 4
#define GAP_0 1
    for (int i = GAP_1; i < N; i++)
    {
        int value = movelist[i].m.mvv_lva;
        uint32_t tmp_move = movelist[i].u;
        int j;

        for (j = i - GAP_1; j >= 0; j -= GAP_1)
        {
            MOVE current_move;

            current_move.u = movelist[j].u;
            if (current_move.m.mvv_lva >= value)
                break;
            movelist[j + GAP_1].u = current_move.u;
        }
        movelist[j + GAP_1].u = tmp_move;
    }

    for (int i = GAP_0; i < N; i++)
    {
        int value = movelist[i].m.mvv_lva;
        uint32_t tmp_move = movelist[i].u;
        int j;

        for (j = i - GAP_0; j >= 0; j -= GAP_0)
        {
            MOVE current_move;

            current_move.u = movelist[j].u;
            if (current_move.m.mvv_lva >= value)
                break;
            movelist[j + GAP_0].u = current_move.u;
        }
        movelist[j + GAP_0].u = tmp_move;
    }
#undef GAP_1
#undef GAP_0
}

/*that's a shell sort for the Search_Play_And_Sort_Moves() routine. Sorts by the position value in sortV[].*/
static void Search_Do_Sort_Value(MOVE *restrict movelist, int *restrict sortV, int N)
{
    int sizeIndex = sizeof(shell_sort_gaps)/sizeof(shell_sort_gaps[0]) - 1;

    do {
        int gap = shell_sort_gaps[sizeIndex];

        for (int i = gap; i < N; i++)
        {
            uint32_t tmp_move = movelist[i].u;
            int tmp_sortV = sortV[i];
            int j;

            for (j = i - gap; ((j >= 0) && (sortV[j] < tmp_sortV)); j -= gap)
            {
                movelist[j + gap].u = movelist[j].u;
                sortV[j + gap] = sortV[j];
            }
            movelist[j + gap].u = tmp_move;
            sortV[j + gap] = tmp_sortV;
        }
    } while (--sizeIndex >= 0);
}

static int Search_Print_PV_Line(const LINE *aPVp, char *outbuf)
{
    int i, len;

    for (i = 0, len = 0; i < aPVp->line_len; i++)
    {
        MOVE decomp_move;
        char *mv_str;

        /*string the PV together. not using strcat in a loop because that
          creates hidden n^2 algorithms, which wouldn't really matter with the
          typical search depth, but still.*/
        outbuf[len++] = ' ';
        decomp_move = Mvgen_Decompress_Move(aPVp->line_cmoves[i]);
        mv_str = Play_Translate_Moves(decomp_move);
        strcpy(outbuf + len, mv_str);
        /*length goes up by 5 with promotion move, else by 4.*/
        len += (mv_str[4] == 0) ? 4 : 5;
    }
    return(len);
}

/*find the move "key_move" in the list and put it to the top,
  moving the following moves down the list.
  used for getting PV moves to the top of the list.*/
static void Search_Find_Put_To_Top(MOVE *restrict movelist, int len, MOVE key_move)
{
    uint32_t mv_mask = mv_move_mask.u;

    for (int i = 0; i < len; i++)
    {
        if (((movelist[i].u ^ key_move.u) & mv_mask) == 0)
        {
            uint32_t listed_key;

            if (i == 0) /*already at the top*/
                return;

            listed_key = movelist[i].u;

            for ( ; i > 0; i--)
                movelist[i].u = movelist[i-1].u;

            movelist[0].u = listed_key;
            return;
        }
    }
}

/*find the move "key_move" in the list and put it to the top,
  moving the following moves down the list, and handle the associated
  answer compressed move list, too.
  used for getting PV moves to the top of the list.*/
static void Search_Find_Put_To_Top_Root(MOVE *restrict movelist, CMOVE *restrict comp_answerlist, int len, MOVE key_move)
{
    uint32_t mv_mask = mv_move_mask.u;

    for (int i = 0; i < len; i++)
    {
        if (((movelist[i].u ^ key_move.u) & mv_mask) == 0)
        {
            uint32_t listed_key = movelist[i].u;
            CMOVE listed_answer = comp_answerlist[i];

            for ( ; i > 0; i--)
            {
                movelist[i].u      = movelist[i - 1].u;
                comp_answerlist[i] = comp_answerlist[i - 1];
            }

            movelist[0].u      = listed_key;
            comp_answerlist[0] = listed_answer;

            return;
        }
    }
}

/*find the move best mvv/lva move in the list and swap it to the top.
  note that list length 0 theoretically would yield a buffer overflow,
  but all places where this function is called from actually have a
  sufficient buffer - it could just be that there is no valid move
  inside.*/
static void Search_Swap_Best_To_Top(MOVE *movelist, int len)
{
    int best_val;
    MOVE tmp_move;
    MOVE *list_ptr, *end_ptr, *best_ptr;

    tmp_move.u = movelist->u;
    best_val = tmp_move.m.mvv_lva;
    best_ptr = movelist;
    list_ptr = movelist + 1;
    end_ptr = movelist + len;

    while (list_ptr < end_ptr)
    {
        int cur_val = list_ptr->m.mvv_lva;
        if (cur_val > best_val)
        {
            best_val = cur_val;
            best_ptr = list_ptr;
        }
        list_ptr++;
    }
    movelist->u = best_ptr->u;
    best_ptr->u = tmp_move.u;
}

/*returns whether a mate has been seen despite the noise setting.
  for each move of depth, the engine shall overlook the mate with a probability
  of "eval_noise".*/
static int Search_Mate_Noise(int depth)
{
    unsigned int prob, eval_real;

    prob = 100U;
    eval_real = 100U - ((unsigned) eval_noise); /*non-noise part*/
    depth /= 2; /*plies depth to moves depth*/

    for (int i = 0; i < depth; i++)
    {
        prob *= eval_real;
        prob += 50U;         /*rounding*/
        prob /= 100U;        /*scale back to 0..100%*/
    }

    /*the probability of mate detection decreases with depth.*/
    if (Util_Rand(101U) > prob)
        return(0); /*mate overlooked*/

    return(1);
}

/*20 moves without capture or pawn move, that might tend drawish.
  so start to ramp down the difference from 100% at move 20 to 10% target at move 50.
  From some moves before move 50, the looming draw will also appear,
  but if it has already been flattened before, that will not come as surprisingly.

  flatten steadily as to encourage staying draw in worse positions, but don't
  introduce a sudden drop which might cause panic in better situations.

  note that this routine is ONLY called if fifty_moves >= NO_ACTION_PLIES already.*/
static int32_t Search_Flatten_Difference(int32_t eval)
{
    int i, fifty_moves_search;

    /*basic endgames*/
    if ((Wpieces[0].next == NULL) || (Wpieces[0].next->next == NULL) ||
        (Bpieces[0].next == NULL) || (Bpieces[0].next->next == NULL))
    {
        return(eval);
    }

    for (i = Starting_Mv + 1, fifty_moves_search = fifty_moves; i <= mv_stack_p; i++, fifty_moves_search++)
    {
        MVST* p = &move_stack[i];
        if ((p->captured->type) /* capture */ || (p->move.m.flag > 1) /* pawn move */)
            return (eval); /*such moves will reset the drawish tendency*/

        /*50 moves draw detected*/
        if (fifty_moves_search >= 100)
            return(0);
    }

    /*90% discount during 60 plies (30 moves).*/
    eval *= (107 - fifty_moves_search); /*fifty_moves_search is >= NO_ACTION_PLIES*/
    eval /= (107 - NO_ACTION_PLIES);
    return(eval);
}

/*mini SEE for pruning in QS*/
static int Search_SEE_Prune(MOVE move, enum E_COLOUR colour)
{
    int to_sq, mov_type;

    /*good or equal capture (ignoring e.p.)*/
    mov_type = board[move.m.from]->type;
    to_sq = move.m.to;
    if (PieceValFromType[mov_type] <= PieceValFromType[board[to_sq]->type])
        return(0);

    if (colour == BLACK)
    {
        /*BPAWN: catch e.p.*/
        if (mov_type == BPAWN)
            return(0);
        /*HxL defended by P, or moving a piece to such a square*/
        if ((board[to_sq - 11]->type == WPAWN) || (board[to_sq - 9]->type == WPAWN))
            return(1);
    } else {
        /*WPAWN: catch e.p.*/
        if (mov_type == WPAWN)
            return(0);
        /*HxL defended by P, or moving a piece to such a square*/
        if ((board[to_sq + 11]->type == BPAWN) || (board[to_sq + 9]->type == BPAWN))
            return(1);
    }
    /*default: no pruning*/
    return(0);
}

static int Search_Quiescence(int alpha, int beta, enum E_COLOUR colour, int do_checks, int qs_depth)
{
    MOVE movelist[MAXCAPTMV];
    enum E_COLOUR next_colour;
    int e, i, move_cnt, recapt;
    int is_material_enough, n_checks, n_checks_valid, n_check_pieces, kingsq;
    unsigned has_move;

    g_nodes++;
    if (colour==BLACK) {
        /*using has_move as dummy*/
        e = -Eval_Static_Evaluation(&is_material_enough, BLACK, &has_move);
        if (UNLIKELY(!is_material_enough))
            return 0;
        if (UNLIKELY(fifty_moves >= NO_ACTION_PLIES))
            e = Search_Flatten_Difference(e);
        /*try to delay bad positions and go for good positions faster,
          but don't change the eval sign*/
        if (e > 0)
        {
            e -= (mv_stack_p - Starting_Mv);
            if (e <= 0) e = 1;
        } else if (e < 0)
        {
            e += (mv_stack_p - Starting_Mv);
            if (e >= 0) e = -1;
        }

        /*prevent stack overflow*/
        if (UNLIKELY (mv_stack_p - Starting_Mv >= MAX_DEPTH+MAX_QIESC_DEPTH-1) )
            return e;

        /*in pre-search or after 4 plies QS, don't do check extensions.
          depth 0 cannot have checks because Negascout does not enter QS
          when in check, and the pre-search does not request QS check extension.*/
        if ((qs_depth < QS_CHECK_DEPTH) && (qs_depth > 0) && (do_checks != QS_NO_CHECKS))
        {
            n_checks = Mvgen_Black_King_In_Check_Info(search_check_attacks_buf, &n_check_pieces);
            n_checks_valid = 1;
        } else
            n_checks = n_checks_valid = 0;

        if (n_checks == 0)
        {
            if (e >= beta)
                return e;

            /*check for stalemate against lone king.
              needed in some endgames like these:
              8/8/1b5p/8/6P1/8/5k1K/8 w - - 0 1
              6K1/5P2/8/5q2/2k5/8/8/8 b - - 0 1*/
            if (Bpieces[0].next == NULL)
            {
                move_cnt = has_move = 0;
                Mvgen_Add_Black_King_Moves(&Bpieces[0], movelist, &move_cnt, NO_LEVEL);
                for (i = 0; i < move_cnt; i++)
                {
                    Search_Push_Status();
                    Search_Make_Move(movelist[i]);
                    if (!Mvgen_Black_King_In_Check())
                    {
                        /*no stalemate*/
                        Search_Retract_Last_Move();
                        Search_Pop_Status();
                        has_move = 1U;
                        break;
                    }
                    Search_Retract_Last_Move();
                    Search_Pop_Status();
                }
                if (!has_move)
                    return 0;
            }
            /*ignore underpromotion in quiescence - not worth the effort.*/
            move_cnt = Mvgen_Find_All_Black_Captures_And_Promotions(movelist, QUEENING);

            if (move_cnt == 0)
                return e;
            if (alpha < e)
                alpha = e;
        } else
        {
            /*in QS, drop underpromotion.*/
            move_cnt = Mvgen_Find_All_Black_Evasions(movelist, search_check_attacks_buf, n_checks, n_check_pieces, QUEENING);
            if (alpha < -MATE_CUTOFF)
            {
                int mate_score = -INFINITY_ + (mv_stack_p - Starting_Mv);

                if (alpha < mate_score)
                    alpha = mate_score;
            }
        }
        kingsq = bking;
        next_colour = WHITE;
    } else {
        /*using has_move as dummy*/
        e = Eval_Static_Evaluation(&is_material_enough, WHITE, &has_move);
        if (UNLIKELY(!is_material_enough))
            return 0;
        if (UNLIKELY(fifty_moves >= NO_ACTION_PLIES))
            e = Search_Flatten_Difference(e);
        /*try to delay bad positions and go for good positions faster,
          but don't change the eval sign*/
        if (e > 0)
        {
            e -= (mv_stack_p - Starting_Mv);
            if (e <= 0) e = 1;
        } else if (e < 0)
        {
            e += (mv_stack_p - Starting_Mv);
            if (e >= 0) e = -1;
        }

        /*prevent stack overflow*/
        if (UNLIKELY (mv_stack_p - Starting_Mv >= MAX_DEPTH+MAX_QIESC_DEPTH-1) )
            return e;

        /*in pre-search or after 4 plies QS, don't do check extensions.
          depth 0 cannot have checks because Negascout does not enter QS
          when in check, and the pre-search does not request QS check extension.*/
        if ((qs_depth < QS_CHECK_DEPTH) && (qs_depth > 0) && (do_checks != QS_NO_CHECKS))
        {
            n_checks = Mvgen_White_King_In_Check_Info(search_check_attacks_buf, &n_check_pieces);
            n_checks_valid = 1;
        } else
            n_checks = n_checks_valid = 0;

        if (n_checks == 0)
        {
            if (e >= beta)
                return e;

            /*check for stalemate against lone king.
              needed in some endgames like these:
              8/8/1b5p/8/6P1/8/5k1K/8 w - - 0 1
              6K1/5P2/8/5q2/2k5/8/8/8 b - - 0 1*/
            if (Wpieces[0].next == NULL)
            {
                move_cnt = has_move = 0;
                Mvgen_Add_White_King_Moves(&Wpieces[0], movelist, &move_cnt, NO_LEVEL);
                for (i = 0; i < move_cnt; i++)
                {
                    Search_Push_Status();
                    Search_Make_Move(movelist[i]);
                    if (!Mvgen_White_King_In_Check())
                    {
                        /*no stalemate*/
                        Search_Retract_Last_Move();
                        Search_Pop_Status();
                        has_move = 1U;
                        break;
                    }
                    Search_Retract_Last_Move();
                    Search_Pop_Status();
                }
                if (!has_move)
                    return 0;
            }
            /*ignore underpromotion in quiescence - not worth the effort.*/
            move_cnt = Mvgen_Find_All_White_Captures_And_Promotions(movelist, QUEENING);

            if (move_cnt == 0)
                return e;
            if (alpha < e)
                alpha = e;
        } else
        {
            /*in QS, drop underpromotion.*/
            move_cnt = Mvgen_Find_All_White_Evasions(movelist, search_check_attacks_buf, n_checks, n_check_pieces, QUEENING);
            if (alpha < -MATE_CUTOFF)
            {
                int mate_score = -INFINITY_ + (mv_stack_p - Starting_Mv);

                if (alpha < mate_score)
                    alpha = mate_score;
            }
        }
        kingsq = wking;
        next_colour = BLACK;
    }

    if (!n_checks_valid)
        n_checks = Mvgen_King_In_Check(colour);

    Search_Swap_Best_To_Top(movelist, move_cnt);
    recapt = (qs_depth < QS_RECAPT_DEPTH) ? 0 : move_stack[mv_stack_p].move.m.to;
    qs_depth++;

    for (i = 0; i < move_cnt; i++)
    {
        int score, to, from, delta;
        enum E_MOVESTATE move_state;
        MOVE currmove;

        /*after first move (usually cuts), sort if there are more moves*/
        if ((i == 1) && (move_cnt >= 3))
            Search_Do_Sort_QS(movelist + 1, move_cnt - 1);

        /*delta pruning*/
        currmove.u = movelist[i].u;
        to = currmove.m.to;
        from = currmove.m.from;
        delta = PieceValFromType[board[to]->type];
        if (colour == BLACK)
        {
            if ((board[from]->type == BPAWN) && (to <= H1))
                delta += QS_QUEEN_PAWN;
        } else
        {
            if ((board[from]->type == WPAWN) && (to >= A8))
                delta += QS_QUEEN_PAWN;
        }
        if (e + delta + QS_DELTA_MARGIN < alpha)
        {
            /*if this move has no chance of improving alpha, the others are
              even less likely to do so because of MVV/LVA sorting.*/
            return alpha;
        }

        /*recapture-only after 5 plies QS, and SEE pruning*/
        if (((recapt != 0) && (recapt != to)) ||
            (((n_checks == 0) || (!n_checks_valid)) && (Search_SEE_Prune(currmove, colour))))
        {
            /*in-check, we did the delta pruning above because it does not
              matter whether it is mate or material fail-low. However, giving
              back some material in an advantageous position may be the best
              way to defend against the check, hence no SEE pruning.*/
            continue;
        }

        move_state = (n_checks == 0) ? Mvgen_Is_Move_Safe(currmove, kingsq, colour) : MOVE_UNKNOWN;
        if (move_state == MOVE_UNSAFE)
            continue;

        Search_Push_Status();
        Search_Make_Move(movelist[i]);

        if ((move_state == MOVE_UNKNOWN) && (Mvgen_King_In_Check(colour)))
        {
            Search_Retract_Last_Move();
            Search_Pop_Status();
            continue;
        }

        score = -Search_Quiescence(-beta, -alpha, next_colour, do_checks, qs_depth);
        Search_Retract_Last_Move();
        Search_Pop_Status();

        if (score >= beta)
            return score;
        if (score > alpha)
            alpha = score;
    }

    return alpha;
}

#ifdef AUTOTUNE
/*special QS variant that returns a PV.
  useful for auto tuning with non-quiet positions.
  not in the normal engine to save stack in the embedded version.*/
int Search_PV_Quiescence(int alpha, int beta, enum E_COLOUR colour, int do_checks, int qs_depth, LINE *pline)
{
    MOVE movelist[MAXCAPTMV];
    enum E_COLOUR next_colour;
    int e, i, move_cnt, recapt;
    int is_material_enough, n_checks, n_checks_valid, n_check_pieces, kingsq;
    unsigned has_move;
    LINE line;

    pline->line_len = 0;
    g_nodes++;
    if (colour==BLACK) {
        /*using has_move as dummy*/
        e = -Eval_Static_Evaluation(&is_material_enough, BLACK, &has_move);
        if (UNLIKELY(!is_material_enough))
            return 0;
        if (UNLIKELY(fifty_moves >= NO_ACTION_PLIES))
            e = Search_Flatten_Difference(e);
        /*try to delay bad positions and go for good positions faster,
          but don't change the eval sign*/
        if (e > 0)
        {
            e -= (mv_stack_p - Starting_Mv);
            if (e <= 0) e = 1;
        } else if (e < 0)
        {
            e += (mv_stack_p - Starting_Mv);
            if (e >= 0) e = -1;
        }

        /*prevent stack overflow*/
        if (UNLIKELY (mv_stack_p - Starting_Mv >= MAX_DEPTH+MAX_QIESC_DEPTH-1) )
            return e;

        /*in pre-search or after 4 plies QS, don't do check extensions.*/
        if ((qs_depth < QS_CHECK_DEPTH) && (do_checks != QS_NO_CHECKS))
        {
            n_checks = Mvgen_Black_King_In_Check_Info(search_check_attacks_buf, &n_check_pieces);
            n_checks_valid = 1;
        } else
            n_checks = n_checks_valid = 0;

        if (n_checks == 0)
        {
            if (e >= beta)
                return e;

            /*check for stalemate against lone king.
              needed in some endgames like these:
              8/8/1b5p/8/6P1/8/5k1K/8 w - - 0 1
              6K1/5P2/8/5q2/2k5/8/8/8 b - - 0 1*/
            if (Bpieces[0].next == NULL)
            {
                move_cnt = has_move = 0;
                Mvgen_Add_Black_King_Moves(&Bpieces[0], movelist, &move_cnt, NO_LEVEL);
                for (i = 0; i < move_cnt; i++)
                {
                    Search_Push_Status();
                    Search_Make_Move(movelist[i]);
                    if (!Mvgen_Black_King_In_Check())
                    {
                        /*no stalemate*/
                        Search_Retract_Last_Move();
                        Search_Pop_Status();
                        has_move = 1U;
                        break;
                    }
                    Search_Retract_Last_Move();
                    Search_Pop_Status();
                }
                if (!has_move)
                    return 0;
            }
            /*ignore underpromotion in quiescence - not worth the effort.*/
            move_cnt = Mvgen_Find_All_Black_Captures_And_Promotions(movelist, QUEENING);

            if (move_cnt == 0)
                return e;
            if (alpha < e)
                alpha = e;
        } else
        {
            /*in QS, drop underpromotion.*/
            move_cnt = Mvgen_Find_All_Black_Evasions(movelist, search_check_attacks_buf, n_checks, n_check_pieces, QUEENING);
            if (alpha < -MATE_CUTOFF)
            {
                int mate_score = -INFINITY_ + (mv_stack_p - Starting_Mv);

                if (alpha < mate_score)
                    alpha = mate_score;
            }
        }
        kingsq = bking;
        next_colour = WHITE;
    } else {
        /*using has_move as dummy*/
        e = Eval_Static_Evaluation(&is_material_enough, WHITE, &has_move);
        if (UNLIKELY(!is_material_enough))
            return 0;
        if (UNLIKELY(fifty_moves >= NO_ACTION_PLIES))
            e = Search_Flatten_Difference(e);
        /*try to delay bad positions and go for good positions faster,
          but don't change the eval sign*/
        if (e > 0)
        {
            e -= (mv_stack_p - Starting_Mv);
            if (e <= 0) e = 1;
        } else if (e < 0)
        {
            e += (mv_stack_p - Starting_Mv);
            if (e >= 0) e = -1;
        }

        /*prevent stack overflow*/
        if (UNLIKELY (mv_stack_p - Starting_Mv >= MAX_DEPTH+MAX_QIESC_DEPTH-1) )
            return e;

        /*in pre-search or after 4 plies QS, don't do check extensions.*/
        if ((qs_depth < QS_CHECK_DEPTH) && (do_checks != QS_NO_CHECKS))
        {
            n_checks = Mvgen_White_King_In_Check_Info(search_check_attacks_buf, &n_check_pieces);
            n_checks_valid = 1;
        } else
            n_checks = n_checks_valid = 0;

        if (n_checks == 0)
        {
            if (e >= beta)
                return e;

            /*check for stalemate against lone king.
              needed in some endgames like these:
              8/8/1b5p/8/6P1/8/5k1K/8 w - - 0 1
              6K1/5P2/8/5q2/2k5/8/8/8 b - - 0 1*/
            if (Wpieces[0].next == NULL)
            {
                move_cnt = has_move = 0;
                Mvgen_Add_White_King_Moves(&Wpieces[0], movelist, &move_cnt, NO_LEVEL);
                for (i = 0; i < move_cnt; i++)
                {
                    Search_Push_Status();
                    Search_Make_Move(movelist[i]);
                    if (!Mvgen_White_King_In_Check())
                    {
                        /*no stalemate*/
                        Search_Retract_Last_Move();
                        Search_Pop_Status();
                        has_move = 1U;
                        break;
                    }
                    Search_Retract_Last_Move();
                    Search_Pop_Status();
                }
                if (!has_move)
                    return 0;
            }
            /*ignore underpromotion in quiescence - not worth the effort.*/
            move_cnt = Mvgen_Find_All_White_Captures_And_Promotions(movelist, QUEENING);

            if (move_cnt == 0)
                return e;
            if (alpha < e)
                alpha = e;
        } else
        {
            /*in QS, drop underpromotion.*/
            move_cnt = Mvgen_Find_All_White_Evasions(movelist, search_check_attacks_buf, n_checks, n_check_pieces, QUEENING);
            if (alpha < -MATE_CUTOFF)
            {
                int mate_score = -INFINITY_ + (mv_stack_p - Starting_Mv);

                if (alpha < mate_score)
                    alpha = mate_score;
            }
        }
        kingsq = wking;
        next_colour = BLACK;
    }

    if (!n_checks_valid)
        n_checks = Mvgen_King_In_Check(colour);

    Search_Swap_Best_To_Top(movelist, move_cnt);
    recapt = (qs_depth < QS_RECAPT_DEPTH) ? 0 : move_stack[mv_stack_p].move.m.to;
    qs_depth++;

    for (i = 0; i < move_cnt; i++)
    {
        int score, to, from, delta;
        enum E_MOVESTATE move_state;
        MOVE currmove;

        /*after first move (usually cuts), sort if there are more moves*/
        if ((i == 1) && (move_cnt >= 3))
            Search_Do_Sort_QS(movelist + 1, move_cnt - 1);

        /*delta pruning*/
        currmove.u = movelist[i].u;
        to = currmove.m.to;
        from = currmove.m.from;
        delta = PieceValFromType[board[to]->type];
        if (colour == BLACK)
        {
            if ((board[from]->type == BPAWN) && (to <= H1))
                delta += QS_QUEEN_PAWN;
        } else
        {
            if ((board[from]->type == WPAWN) && (to >= A8))
                delta += QS_QUEEN_PAWN;
        }
        if (e + delta + QS_DELTA_MARGIN < alpha)
        {
            /*if this move has no chance of improving alpha, the others are
              even less likely to do so because of MVV/LVA sorting.*/
            return alpha;
        }

        /*recapture-only after 5 plies QS, and SEE pruning*/
        if (((recapt != 0) && (recapt != to)) ||
            (((n_checks == 0) || (!n_checks_valid)) && (Search_SEE_Prune(currmove, colour))))
        {
            /*in-check, we did the delta pruning above because it does not
              matter whether it is mate or material fail-low. However, giving
              back some material in an advantageous position may be the best
              way to defend against the check, hence no SEE pruning.*/
            continue;
        }

        move_state = (n_checks == 0) ? Mvgen_Is_Move_Safe(currmove, kingsq, colour) : MOVE_UNKNOWN;
        if (move_state == MOVE_UNSAFE)
            continue;

        Search_Push_Status();
        Search_Make_Move(movelist[i]);

        if ((move_state == MOVE_UNKNOWN) && (Mvgen_King_In_Check(colour)))
        {
            Search_Retract_Last_Move();
            Search_Pop_Status();
            continue;
        }

        score = -Search_PV_Quiescence(-beta, -alpha, next_colour, do_checks, qs_depth, &line);
        Search_Retract_Last_Move();
        Search_Pop_Status();

        if (score > alpha)
        {
            alpha = score;
            pline->line_cmoves[0] = Mvgen_Compress_Move(currmove);
            for (int j = 0; j < line.line_len; j++)
                pline->line_cmoves[j + 1] = line.line_cmoves[j];
            pline->line_len = line.line_len + 1;
        }
        if (score >= beta)
            return score;
    }

    return alpha;
}
#endif

static void Search_Update_Killers_History(enum E_COLOUR colour, int depth, int level,
                                          const MOVE *restrict movelist, int last_mv_idx)
{
    MOVE last_move = movelist[last_mv_idx];
    int last_mv_from = last_move.m.from,
        last_mv_to   = last_move.m.to;
    int last_mv_piece_type = board[last_mv_from]->type;

    if (colour == BLACK)
    {
        /*don't use promotions and e.p. (protect the killer slots)*/
        if ((last_mv_piece_type != BPAWN) ||
            ((last_mv_to >= A2) &&
             (((last_mv_from - last_mv_to) & 1) == 0)))
        {
            uint32_t depth_sq = ((uint32_t) depth) * ((uint32_t) depth);
            CMOVE cmove = Mvgen_Compress_Move(last_move);

            /*black depth killers*/
            if (B_Killers[0][level] != cmove)
            {
                B_Killers[1][level] = B_Killers[0][level];
                B_Killers[0][level] = cmove;
            }

            /*black history update (reward this move). not for king moves
              against lone king because that ruins e.g. KNB-K.*/
            if ((last_mv_piece_type != BKING) || (Wpieces[0].next != NULL))
            {
                uint16_t *hist_ptr = &B_history[last_mv_piece_type - BPAWN][last_mv_to];
                uint32_t hist_val = *hist_ptr + depth_sq;

                if (hist_val >= HIST_MAX) /*dynamic history ageing / overflow*/
                {
                    uint16_t *h_ptr = (uint16_t *) B_history;
                    uint16_t *h_end_ptr = h_ptr + 6 * ENDSQ;

                    do {
                        *h_ptr >>= 1; h_ptr++;
                        *h_ptr >>= 1; h_ptr++;
                        *h_ptr >>= 1; h_ptr++;
                        *h_ptr >>= 1; h_ptr++;
                        *h_ptr >>= 1; h_ptr++;
                        *h_ptr >>= 1; h_ptr++;
                    } while (h_ptr < h_end_ptr);

                    depth_sq >>= 1;
                    hist_val >>= 1;
                }
                *hist_ptr = hist_val;
            }

            /*black history update (punish previous quiet moves)*/
            depth_sq >>= 1;
            depth_sq++;
            for (int mcnt = 0; mcnt < last_mv_idx; mcnt++)
            {
                MOVE move = movelist[mcnt];

                /*find quiet and legal moves that have been tried.
                  e.p. and promotions can be ignored here:
                  e.p. not cutting and then moving another pawn on
                  the e.p. field causing a cut-off (searched earlier with
                  better history) is extremely rare.
                  promotions don't derive their mvv/lva from history.*/
                if ((move.m.flag != 0) && (board[move.m.to]->type == NO_PIECE))
                {
                    uint16_t *hist_ptr = &B_history[board[move.m.from]->type - BPAWN][move.m.to];
                    uint32_t hist_val = *hist_ptr;

                    /*subtract 1+(depth^2)/2 and prevent underflow*/
                    hist_val = (hist_val > depth_sq) ? hist_val - depth_sq : 0;
                    *hist_ptr = hist_val;
                }
            }
        }
    } else /*white*/
    {
        /*don't use promotions and e.p. (protect the killer slots)*/
        if ((last_mv_piece_type != WPAWN) ||
            ((last_mv_to <= H7) &&
             (((last_mv_to - last_mv_from) & 1) == 0)))
        {
            uint32_t depth_sq = ((uint32_t) depth) * ((uint32_t) depth);
            CMOVE cmove = Mvgen_Compress_Move(last_move);

            /*white depth killers*/
            if (W_Killers[0][level] != cmove)
            {
                W_Killers[1][level] = W_Killers[0][level];
                W_Killers[0][level] = cmove;
            }

            /*white history update (reward this move). not for king moves
              against lone king because that ruins e.g. KNB-K.*/
            if ((last_mv_piece_type != WKING) || (Bpieces[0].next != NULL))
            {
                uint16_t *hist_ptr = &W_history[last_mv_piece_type - WPAWN][last_mv_to];
                uint32_t hist_val = *hist_ptr + depth_sq;

                if (hist_val >= HIST_MAX) /*dynamic history ageing / overflow*/
                {
                    uint16_t *h_ptr = (uint16_t *) W_history;
                    uint16_t *h_end_ptr = h_ptr + 6 * ENDSQ;

                    do {
                        *h_ptr >>= 1; h_ptr++;
                        *h_ptr >>= 1; h_ptr++;
                        *h_ptr >>= 1; h_ptr++;
                        *h_ptr >>= 1; h_ptr++;
                        *h_ptr >>= 1; h_ptr++;
                        *h_ptr >>= 1; h_ptr++;
                    } while (h_ptr < h_end_ptr);

                    depth_sq >>= 1;
                    hist_val >>= 1;
                }
                *hist_ptr = hist_val;
            }

            /*white history update (punish previous quiet moves)*/
            depth_sq >>= 1;
            depth_sq++;
            for (int mcnt = 0; mcnt < last_mv_idx; mcnt++)
            {
                MOVE move = movelist[mcnt];

                /*find quiet and legal moves that have been tried.
                  e.p. and promotions can be ignored here:
                  e.p. not cutting and then moving another pawn on
                  the e.p. field causing a cut-off (searched earlier with
                  better history) is extremely rare.
                  promotions don't derive their mvv/lva from history.*/
                if ((move.m.flag != 0) && (board[move.m.to]->type == NO_PIECE))
                {
                    uint16_t *hist_ptr = &W_history[board[move.m.from]->type - WPAWN][move.m.to];
                    uint32_t hist_val = *hist_ptr;

                    /*subtract 1+(depth^2)/2 and prevent underflow*/
                    hist_val = (hist_val > depth_sq) ? hist_val - depth_sq : 0;
                    *hist_ptr = hist_val;
                }
            }
        }
    }
}

/*only adjust stuff if there is something to adjust. moving the checks
  outside the for loop increases the NPS rate by about 1%.
  len is assumed to be > 0, which is always the case because there are
  always pseudo-legal moves.*/
static void Search_Adjust_Priorities(MOVE * restrict movelist, int len,
                                     uint8_t * restrict should_iid,
                                     MOVE pv_move, MOVE hash_move)
{
    #define NONE_ADJ 0U
    #define PV_ADJ   1U
    #define HS_ADJ   2U
    int i;
    unsigned int search_mode = NONE_ADJ, search_done;
    uint32_t mv_mask = mv_move_mask.u;

    /*what we are looking for to adjust. only consider the hash move
      if it is different from the PV move, similarly for the threat move.*/
    if (pv_move.u != MV_NO_MOVE_MASK)
        search_mode = PV_ADJ;
    if ((hash_move.u != MV_NO_MOVE_MASK) && (((hash_move.u ^ pv_move.u) & mv_mask) != 0))
        search_mode |= HS_ADJ;

    switch (search_mode)
    {
    case NONE_ADJ: /*0*/
        return;
    case PV_ADJ: /*1*/
        i = 0;
        do {
            if (((movelist[i].u ^ pv_move.u) & mv_mask) == 0)
            {
                movelist[i].m.mvv_lva = MVV_LVA_PV; /* Move follows PV*/
                *should_iid = 0;
                return;
            }
            i++;
        } while (i < len);
        return;
    case HS_ADJ: /*2*/
        i = 0;
        do {
            if (((movelist[i].u ^ hash_move.u) & mv_mask) == 0)
            {
                movelist[i].m.mvv_lva = MVV_LVA_HASH; /* Move from Hash table */
                *should_iid = 0;
                return;
            }
            i++;
        } while (i < len);
        return;
    case PV_ADJ | HS_ADJ: /*3*/
        search_done = 0;
        i = 0;
        do {
            if (((movelist[i].u ^ pv_move.u) & mv_mask) == 0)
            {
                movelist[i].m.mvv_lva = MVV_LVA_PV; /* Move follows PV*/
                *should_iid = 0;
                if (search_done == HS_ADJ) /*both found*/
                    return;
                search_done = PV_ADJ;
            } else if (((movelist[i].u ^ hash_move.u) & mv_mask) == 0)
            {
                movelist[i].m.mvv_lva = MVV_LVA_HASH; /* Move from Hash table */
                *should_iid = 0;
                if (search_done == PV_ADJ) /*both found*/
                    return;
                search_done = HS_ADJ;
            }
            i++;
        } while (i < len);
        return;
    }
    #undef NONE_ADJ
    #undef PV_ADJ
    #undef HS_ADJ
}

static int Search_Endgame_Reduct(void)
{
    /*basic endgames*/
    if ((Wpieces[0].next == NULL) || (Wpieces[0].next->next == NULL) ||
        (Bpieces[0].next == NULL) || (Bpieces[0].next->next == NULL))
    {
        return(0);
    }

    /*promotion threat*/
    for (int i = A2; i <= H2; i += FILE_DIFF)
    {
        if ((board[i]->type == BPAWN) ||
            (board[i + 5*RANK_DIFF]->type == WPAWN))
        {
            return(0);
        }
    }
    return(1);
}

/* -------------------------------- NEGA SCOUT ALGORITHM -------------------------------- */

static int Search_Negascout(int CanNull, int level, LINE *restrict pline, MOVE *restrict mlst,
                            int n, int depth, int alpha, int beta, enum E_COLOUR colour,
                            int *restrict best_move_index, int is_pv_node, int being_in_check,
                            int following_pv, int lmp_active)
{
    const int mate_score = INFINITY_ - (mv_stack_p - Starting_Mv);

    pline->line_len = 0;
    *best_move_index = TERMINAL_NODE;

    /*mate distance pruning*/
    if (alpha >= mate_score) return alpha;
    if (beta <= -mate_score) return beta;

    if ((depth <= 0) || /*node is a terminal node*/
        (UNLIKELY(mv_stack_p - Starting_Mv >= MAX_DEPTH-1))) /*we are too deep*/
    {
        if (eval_noise < HIGH_EVAL_NOISE)
            return Search_Quiescence(alpha, beta, colour, QS_CHECKS, 0);
        else
            return Search_Quiescence(alpha, beta, colour, QS_NO_CHECKS, 0);
    } else
    {
        static int root_move_index;
        MOVE x2movelst[MAXMV];
        MOVE threat_best, hash_best;
        enum E_COLOUR next_colour;
        int i, e, t, a, x2movelen, next_depth, node_moves;
        int iret, is_material_enough, n_check_pieces, kingsq;
        unsigned is_endgame;
        LINE line;
        uint8_t should_iid=1, hash_move_mode, level_gt_1, node_pruned_moves;

        level_gt_1 = (level > 1);
        hash_best.u = MV_NO_MOVE_MASK;
        g_nodes++;

        /* Check Transposition Table for a match */
        if (!is_pv_node) {
            if (level & 1) { /* Our side to move */
                if (Hash_Check_TT(    T_T, colour, alpha, beta, depth, move_stack[mv_stack_p].mv_pos_hash, &t, &hash_best)) {
                    if (hash_best.u != MV_NO_MOVE_MASK)
                    {
                        pline->line_cmoves[0] = Mvgen_Compress_Move(hash_best);
                        pline->line_len = 1;
                    }
                    return t;
                }
            } else { /* Opponent time to move */
                if (Hash_Check_TT(Opp_T_T, colour, alpha, beta, depth, move_stack[mv_stack_p].mv_pos_hash, &t, &hash_best)) {
                    if (hash_best.u != MV_NO_MOVE_MASK)
                    {
                        pline->line_cmoves[0] = Mvgen_Compress_Move(hash_best);
                        pline->line_len = 1;
                    }
                    return t;
                }
            }
        } else if (level_gt_1)
        /*for PV nodes, don't return because that causes PV truncation. Only use the
          hash best move for move ordering.*/
        {
            if (level & 1) /* Our side to move */
                Hash_Check_TT_PV(    T_T, colour, depth, move_stack[mv_stack_p].mv_pos_hash, &t, &hash_best);
            else           /* Opponent time to move */
                Hash_Check_TT_PV(Opp_T_T, colour, depth, move_stack[mv_stack_p].mv_pos_hash, &t, &hash_best);
        }

        /*level 2 has a dedicated move cache.*/
        if ((level == 2) && (hash_best.u == MV_NO_MOVE_MASK))
            hash_best = Mvgen_Decompress_Move(root_refutation_table[root_move_index]);

        if (colour == BLACK) {
            e = -Eval_Static_Evaluation(&is_material_enough, BLACK, &is_endgame);
            kingsq = bking;
            next_colour = WHITE;
        } else {
            e = Eval_Static_Evaluation(&is_material_enough, WHITE, &is_endgame);
            kingsq = wking;
            next_colour = BLACK;
        }
        if (!is_material_enough)
        /*if this node has insufficient material, that cannot change further
          down towards the leaves of the search tree.*/
        {
            MOVE smove;
            smove.u = MV_NO_MOVE_MASK;
            if (level & 1)
                Hash_Update_TT(    T_T, depth, 0, EXACT, move_stack[mv_stack_p].mv_pos_hash, smove);
            else
                Hash_Update_TT(Opp_T_T, depth, 0, EXACT, move_stack[mv_stack_p].mv_pos_hash, smove);
            return 0;
        }
        if (UNLIKELY(fifty_moves >= NO_ACTION_PLIES))
            e = Search_Flatten_Difference(e);

        if ((!is_pv_node) && (!being_in_check))
        {
            /*Reverse Futility Pruning*/
            if ((depth < RVRS_FUTIL_D) && (e - RVRS_FutilMargs[depth] >= beta) &&
                ((is_material_enough >= EG_PIECES) || Search_Endgame_Reduct()))
            {
                return e;
            }
            /*Null search. Allow null move pruning if the side to move has enough pieces.*/
            if (CanNull && (e >= beta) && (depth >= NULL_START_DEPTH) && (!(is_endgame & (1U << colour))))
            {
                int null_depth = depth - (3 + depth / 4) - (e >= beta + PAWN_V),
                    store_ep_sq = en_passant_sq;

                /*this can fall right into QS which does not do check evasions at
                  QS level 0. But this is OK because the other side cannot be in check
                  given that it is actually our turn here, i.e. without null move.*/
                en_passant_sq = 0;
                t = -Search_Negascout(0, level + 1, &line, x2movelst, 0, null_depth, -beta, -beta + 1, next_colour, &iret, 0, 0,  0, lmp_active);
                en_passant_sq = store_ep_sq;
                if (t >= beta)
                    return t;
            }
        }

        /*late move generation*/
        hash_move_mode = 0;
        if (n == 0)
        {
            /*defer that if we have a hash move - a beta cutoff is likely.*/
            if ((hash_best.u == MV_NO_MOVE_MASK) || (following_pv))
            {
                MOVE GPVmove;

                n = Mvgen_Find_All_Moves(mlst, level-1, colour, UNDERPROM);

                /* Adjust move priorities */
                if ((following_pv) && (GlobalPV.line_len > level-1))
                    GPVmove = Mvgen_Decompress_Move(GlobalPV.line_cmoves[level-1]);
                else
                    GPVmove.u = MV_NO_MOVE_MASK;

                Search_Adjust_Priorities(mlst, n, &should_iid, GPVmove, hash_best);
            } else
            {
                /*if we are not in a PV node and there is a hash best move, then this
                  move is 90% likely to cause a cutoff here. The move list has not
                  yet been generated, so n is 0, and the hash move is the only move
                  for now. It has been validated for pseudo legality upon retrieval
                  from the hash table. The in-check verification remains to do.*/
                hash_move_mode = 1;
                mlst[0].u = hash_best.u;
                mlst[1].u = MV_NO_MOVE_MASK; /*swap to top keeps the order with both MVV/LVA as 0*/
                n = 2;
                should_iid = 0;
            }
        } else if (level_gt_1) /*early move generation - check evasions*/
        {
            MOVE GPVmove;

            /* Adjust move priorities */
            if ((following_pv) && (GlobalPV.line_len > level-1))
                GPVmove = Mvgen_Decompress_Move(GlobalPV.line_cmoves[level-1]);
            else
                GPVmove.u = MV_NO_MOVE_MASK;

            Search_Adjust_Priorities(mlst, n, &should_iid, GPVmove, hash_best);
        }

        /*root move list is already sorted in the main ID loop.*/
        if (level_gt_1)
        {
            if (should_iid && (depth > IID_DEPTH))
            {
                /*Internal Iterative Deepening
                  level > 1: no IID in the root node because the pre-sorting in
                  Search_Play_And_Sort_Moves() has already done that.
                  should_iid would have been set to false if we had had a hash or PV move available.*/
                Search_Negascout(CanNull, level, &line, mlst, n, depth/3, alpha, beta, colour,
                                 &iret, is_pv_node, being_in_check, following_pv, lmp_active);
                if (iret >= 0)
                    mlst[iret].m.mvv_lva = MVV_LVA_HASH;
            }
            Search_Swap_Best_To_Top(mlst, n);
        }

        a = alpha;
        node_moves = node_pruned_moves = 0;

        if (time_is_up == TM_NO_TIMEOUT)
            time_is_up = Time_Check_Throttle();

        for (i = 0; i < n; i++)
        {
            /*foreach child of node*/
            int curr_move_follows_pv, can_reduct, n_checks;
            enum E_MOVESTATE move_state;

            if (level_gt_1) /*initial move list is already sorted*/
            {
                if (i == 1)
                {
                    /*even later move generation*/
                    if (hash_move_mode)
                    {
                        MOVE GPVmove;
                        GPVmove.u = MV_NO_MOVE_MASK;

                        n = Mvgen_Find_All_Moves(mlst, level-1, colour, UNDERPROM);
                        /*if there is only one pseudo legal move, that must have been
                          the hash move which has already been tried.*/
                        if (n <= 1)
                            break;

                        /* Adjust move priorities */
                        Search_Adjust_Priorities(mlst, n, &should_iid, GPVmove, hash_best);
                        Search_Do_Sort(mlst, n); /*hash move will be at the top*/
                    } else
                        Search_Do_Sort(mlst + 1, n - 1);
                }
            } else /*level 1 is root moves.*/
            {
                uci_curr_move.u = mlst[i].u;
                uci_curr_move_number = i;
                root_move_index = i;
                if ((show_currmove == CURR_ALWAYS) && (time_is_up == TM_NO_TIMEOUT))
                {
                  /*output_time gets initialised to start_time + 1000LL. if it is
                    greater, then the first output_time must be through, which means
                    that output_time is now at least start_time + 2000LL, so at least
                    1 second is passed: start the output after 1 second as per the
                    UCI spec.
                    this still gets around always calling the time functions, which are
                    expensive.*/
                    if (output_time >= start_time + 1500LL)
                    {
                        int len;
                        char *mv_str;
                        strcpy(printbuf, "info currmove ");
                        mv_str = Play_Translate_Moves(mlst[i]);
                        strcpy(printbuf + 14, mv_str);
                        len = (mv_str[4] == 0) ? (14 + 4) : (14 + 5);
                        strcpy(printbuf + len, " currmovenumber ");
                        len += 16;
                        len += Util_Tostring_U16(printbuf + len, (uint16_t)(i + 1));
                        printbuf[len++] = '\n';
                        printbuf[len] = '\0';
                        Play_Print(printbuf);
                    }
                }
            }
            move_state = (!being_in_check) ? Mvgen_Is_Move_Safe(mlst[i], kingsq, colour) : MOVE_UNKNOWN;
            if (move_state == MOVE_UNSAFE)
            {
                mlst[i].m.flag = 0; /*for history: mark as illegal*/
                continue;
            }
            Search_Push_Status();
            Search_Make_Move(mlst[i]);
            if ((move_state == MOVE_UNKNOWN) && (Mvgen_King_In_Check(colour)))
            {
                Search_Retract_Last_Move();
                Search_Pop_Status();
                mlst[i].m.flag = 0; /*for history: mark as illegal*/
                continue;
            }
            threat_best.u = MV_NO_MOVE_MASK;
            if (Hash_Check_For_Draw())
            {
                if ((mv_stack_p + start_moves < contempt_end) && (game_started_from_0))
                {
                    if (colour == computer_side)
                        t = contempt_val;
                    else
                        t = -contempt_val;
                } else
                    t = 0;
            } else
            {
                if (colour == BLACK)
                {
                    /* if our move just played gives check, generate evasions and do not reduce depth so we can search deeper */
                    n_checks = Mvgen_White_King_In_Check_Info(search_check_attacks_buf, &n_check_pieces);
                    if (n_checks) { /* early move generation */
                        can_reduct = 0;
                        if ((depth <= CHECK_DEPTH) && (eval_noise < HIGH_EVAL_NOISE))
                            /*track checks if the search tree were to end, but don't replicate high-level trees.*/
                            next_depth = depth;
                        else
                            next_depth = depth - 1;
                        x2movelen = Mvgen_Find_All_White_Evasions(x2movelst, search_check_attacks_buf, n_checks, n_check_pieces, UNDERPROM);
                    } else {
                        can_reduct = (!being_in_check) && (mlst[i].m.mvv_lva < MVV_LVA_TACTICAL) &&
                                      ((is_material_enough >= EG_PIECES) || Search_Endgame_Reduct());
                        /*futility pruning*/
                        if ( can_reduct && (!is_pv_node) && (depth < FUTIL_DEPTH) && (e+FutilityMargins[depth] < a) ) {
                            Search_Retract_Last_Move();
                            Search_Pop_Status();
                            node_pruned_moves = 1U; /*a pruned legal move still is a legal move - for the stalemate recognition at the end of this routine.*/
                            continue;
                        }
                        x2movelen = 0;
                        next_depth = depth - 1;
                    }
                } else { /* colour == WHITE */
                    /* if our move just played gives check do not reduce depth so we can search deeper */
                    n_checks = Mvgen_Black_King_In_Check_Info(search_check_attacks_buf, &n_check_pieces);
                    if (n_checks) { /* early move generation */
                        can_reduct = 0;
                        if ((depth <= CHECK_DEPTH) && (eval_noise < HIGH_EVAL_NOISE))
                            /*track checks if the search tree were to end, but don't replicate high-level trees.*/
                            next_depth = depth;
                        else
                            next_depth = depth - 1;
                        x2movelen=Mvgen_Find_All_Black_Evasions(x2movelst, search_check_attacks_buf, n_checks, n_check_pieces, UNDERPROM);
                    } else {
                        can_reduct = (!being_in_check) && (mlst[i].m.mvv_lva < MVV_LVA_TACTICAL) &&
                                      ((is_material_enough >= EG_PIECES) || Search_Endgame_Reduct());
                        /*futility pruning*/
                        if ( can_reduct && (!is_pv_node) && (depth < FUTIL_DEPTH) && (e+FutilityMargins[depth] < a) ) {
                            Search_Retract_Last_Move();
                            Search_Pop_Status();
                            node_pruned_moves = 1U; /*a pruned legal move still is a legal move - for the stalemate recognition at the end of this routine.*/
                            continue;
                        }
                        x2movelen = 0;
                        next_depth = depth - 1;
                    }
                }
                curr_move_follows_pv = 0;
                if ((following_pv) && (GlobalPV.line_len > level-1) &&
                    (Mvgen_Compress_Move(mlst[i]) == GlobalPV.line_cmoves[level-1]))
                {
                    curr_move_follows_pv = 1;
                }
                if (node_moves == 0) { /* First move to search- full window [-beta,-alpha] used */
                    t = (beta > a + 1) ? PV_NODE : CUT_NODE;
                    t = -Search_Negascout(1, level+1, &line, x2movelst, x2movelen, next_depth, -beta, -a, next_colour, &iret, t, n_checks, curr_move_follows_pv, lmp_active);
                } else {
                    if (can_reduct)
                    {
                        if ((i > n/3) && (depth <= LMP_DEPTH_LIMIT) && (a > -MATE_CUTOFF) && lmp_active)
                        {
                            /*late move pruning*/
                            t = a;
                            iret = TERMINAL_NODE;
                        } else if ((node_moves >= LMR_MOVES) && (depth >= LMR_DEPTH_LIMIT))
                        {
                            /* LMR - Search with reduced depth and scout window [-alpha-1,-alpha].*/
                            if (node_moves < 2*LMR_MOVES)
                                t = depth - 2;
                            else if (node_moves < 3*LMR_MOVES)
                                t = depth - 3;
                            else
                                t = depth - 4;
                            /*Don't fall straight into quiescence.*/
                            if (t <= 0)
                                t = 1;
                            t = -Search_Negascout(1, level+1, &line, x2movelst, x2movelen, t, -a-1, -a, next_colour, &iret, CUT_NODE, n_checks, curr_move_follows_pv, lmp_active);
                        } else t = a + 1;
                    } else t = a + 1;  /* Ensure that re-search is done. */

                    if (t > a) {
                        /* Search normal depth and scout window [-alpha-1,-alpha] */
                        t = -Search_Negascout(1, level+1, &line, x2movelst, x2movelen, next_depth, -a-1, -a, next_colour, &iret, CUT_NODE, n_checks, curr_move_follows_pv, lmp_active);
                        if ((t > a) && (t < beta)) {
                            /* re-search using full window for PV nodes */
                            t = -Search_Negascout(1, level+1, &line, x2movelst, x2movelen, next_depth, -beta, -a, next_colour, &iret, PV_NODE, n_checks, curr_move_follows_pv, lmp_active);
                        }
                    }
                }
                if (iret >= 0) /* Update Best defense for later use in PV line update */
                    threat_best.u = x2movelst[iret].u;
            }
            Search_Retract_Last_Move();
            Search_Pop_Status();

            if (time_is_up != TM_NO_TIMEOUT)
                return a;

            /*if we are in level 1, then store the best answer from level 2
              for the next main depth iteration.*/
            if ((!level_gt_1) && (threat_best.u != MV_NO_MOVE_MASK))
                root_refutation_table[i] = Mvgen_Compress_Move(threat_best);

            /*the following constitutes alpha-beta pruning*/
            if (t > a) {
                a = t;
                *best_move_index = i;
                /* Update Principal Variation */
                if (threat_best.u != MV_NO_MOVE_MASK) {
                    pline->line_cmoves[0] = Mvgen_Compress_Move(threat_best);
                    memcpy(pline->line_cmoves + 1, line.line_cmoves, sizeof(CMOVE) * line.line_len);
                    pline->line_len = line.line_len + 1;
                } else
                    pline->line_len = 0;
                if (a >= beta) { /*-- cut-off --*/
#ifdef DBGCUTOFF
                    if (node_moves == 0) /* First move of search */
                        cutoffs_on_1st_move++;
                    total_cutoffs++;
#endif
                    /*for non-captures/promotions, update depth killers and history*/
                    if (board[mlst[i].m.to]->type == NO_PIECE)
                        Search_Update_Killers_History(colour, depth, level-1, mlst, i);

                    /* Update Transposition table */
                    if (level & 1)
                        Hash_Update_TT(    T_T, depth, a, CHECK_BETA, move_stack[mv_stack_p].mv_pos_hash, mlst[i]);
                    else
                        Hash_Update_TT(Opp_T_T, depth, a, CHECK_BETA, move_stack[mv_stack_p].mv_pos_hash, mlst[i]);

                    return a;
                }
            }
            node_moves++;
        }/* for each node */

        if (node_moves == 0) /*no useful and legal moves*/
        {
            if (!node_pruned_moves) /*there are no legal moves.*/
            {
                if (being_in_check) /*mate */
                {
                    if ((eval_noise <= 0) || (Search_Mate_Noise(mv_stack_p - Starting_Mv)))
                        a = -mate_score;
                    else
                        a = e; /*mate overlooked*/
                } else /*stalemate */
                    a = 0;
            } else
            /*there are legal moves, but they were all cut away in the
              futility pruning.*/
            {
                /*'a' just stays alpha because no move has improved alpha.*/
            }
            *best_move_index = TERMINAL_NODE;
        }
        /* Update Transposition table */
        if (a > alpha)
        {
            if ( *best_move_index != TERMINAL_NODE )
                hash_best = mlst[*best_move_index];
            else
                hash_best.u = MV_NO_MOVE_MASK;

            if (level & 1)
                Hash_Update_TT(    T_T, depth, a, EXACT, move_stack[mv_stack_p].mv_pos_hash, hash_best);
            else
                Hash_Update_TT(Opp_T_T, depth, a, EXACT, move_stack[mv_stack_p].mv_pos_hash, hash_best);
        } else
        {
            hash_best.u = MV_NO_MOVE_MASK;
            if (level & 1)
                Hash_Update_TT(    T_T, depth, a, CHECK_ALPHA, move_stack[mv_stack_p].mv_pos_hash, hash_best);
            else
                Hash_Update_TT(Opp_T_T, depth, a, CHECK_ALPHA, move_stack[mv_stack_p].mv_pos_hash, hash_best);
        }
        return a;
    }
}

static int Search_Negamate(int depth, int alpha, int beta, enum E_COLOUR colour,
                           MOVE *restrict movelist, int move_cnt,
                           int check_depth, LINE *restrict pline, int in_check,
                           enum E_MATE_CHECK check_type)
{
    MOVE xmvlist[MAXMV];
    MOVE dummy;
    LINE line;
    enum E_COLOUR next_colour;
    int i, a, checking, tt_value, actual_move_cnt, root_node, kingsq;

    g_nodes++;
    pline->line_len = 0;

    /*prevent stack overflow.*/
    if (UNLIKELY(mv_stack_p - Starting_Mv >= MAX_DEPTH-1 ))
    /* We are too deep */
        return(0);

    if (Hash_Check_For_Draw())
        return(0);
    if (colour == WHITE) /*slightly different way to use the hash tables*/
    {
        if (Hash_Check_TT(    T_T, colour, alpha, beta, depth, move_stack[mv_stack_p].mv_pos_hash, &tt_value, &dummy))
            return(tt_value);
        kingsq = wking;
    } else /*black*/
    {
        if (Hash_Check_TT(Opp_T_T, colour, alpha, beta, depth, move_stack[mv_stack_p].mv_pos_hash, &tt_value, &dummy))
            return(tt_value);
        kingsq = bking;
    }

    /*first phase: get the moves, filter out the legal ones,
      prioritise check delivering moves, and if it is checkmate, return.*/

    if (move_cnt == 0) /*deferred move generation*/
    {
        root_node = 0;
        /*the history is used differently here because the depth serves as level.
          This is possible because Negamate does not have selective deepening.*/
        if (in_check == 0)
            move_cnt = Mvgen_Find_All_Moves(movelist, depth, colour, UNDERPROM);
        else
        {
            int n_checks, n_check_pieces;
            /*in mate problems, always consider underpromotion even with evasions as
              this might be part of the puzzle.*/
            if (colour == WHITE)
            {
                n_checks = Mvgen_White_King_In_Check_Info(search_check_attacks_buf, &n_check_pieces);
                move_cnt = Mvgen_Find_All_White_Evasions(movelist, search_check_attacks_buf, n_checks, n_check_pieces, UNDERPROM);
            } else
            {
                n_checks = Mvgen_Black_King_In_Check_Info(search_check_attacks_buf, &n_check_pieces);
                move_cnt = Mvgen_Find_All_Black_Evasions(movelist, search_check_attacks_buf, n_checks, n_check_pieces, UNDERPROM);
            }
        }
    } else
        root_node = 1;

    next_colour = Mvgen_Opp_Colour(colour);

    for (i = 0, actual_move_cnt = 0, checking = 0; i < move_cnt; i++)
    {
        enum E_MOVESTATE move_state;

        move_state = (!in_check) ? Mvgen_Is_Move_Safe(movelist[i], kingsq, colour) : MOVE_UNKNOWN;
        if (move_state == MOVE_UNSAFE)
        {
            movelist[i].m.mvv_lva = MVV_LVA_ILLEGAL;
            movelist[i].m.flag = 0; /*for history: mark as illegal*/
            continue;
        }
        Search_Push_Status();
        Search_Make_Move(movelist[i]);
        if ((move_state == MOVE_UNKNOWN) && (Mvgen_King_In_Check(colour)))
        {
            movelist[i].m.mvv_lva = MVV_LVA_ILLEGAL;
            movelist[i].m.flag = 0; /*for history: mark as illegal*/
        } else
        {
            /*if there is a legal move at depth == 0, then it isn't checkmate.*/
            if (depth == 0)
            {
                Search_Retract_Last_Move();
                Search_Pop_Status();
                return(0);
            }
            actual_move_cnt++;
            if (Mvgen_King_In_Check(next_colour))
            /*prioritise check giving moves*/
            {
                checking++;
                movelist[i].m.mvv_lva = MVV_LVA_CHECK;
            }
        }
        Search_Retract_Last_Move();
        Search_Pop_Status();
    }

    /*at depth==1, only check giving moves are tried. so at this point, there
      are no legal moves, otherwise we would have returned, and we are in check.
      Must be checkmate.*/
    if (depth == 0)
        return(-INFINITY_ + (mv_stack_p - Starting_Mv));

    if (actual_move_cnt == 0)
    {
        if (in_check)
            return(-INFINITY_ + (mv_stack_p - Starting_Mv));
        else
            return(0);
    }

    /*if no move delivers check, then it won't be checkmate.*/
    if (depth & 1)
    {
        if (((check_type != EXACT_CHECK_DEPTH) && (depth <= check_depth)) ||
            ((check_type == EXACT_CHECK_DEPTH) && (depth != check_depth)))
        {
            if (checking == 0)
                return(0);
            else
            {
                /*if the depth is below the "from here on only checking moves" threshold,
                  only consider the checking moves. only for odd depths because the side that
                  is supposed to be mated always must have all possibilities evaluated.*/
                actual_move_cnt = checking;
            }
        }
    }

    Search_Swap_Best_To_Top(movelist, move_cnt);

    if (time_is_up == TM_NO_TIMEOUT)
        time_is_up = Time_Check_Throttle();

    if (time_is_up != TM_NO_TIMEOUT)
        return(0);

    line.line_len = 0;
    a = alpha;
    /*second phase: iterate deeper.*/
    for (i = 0; i < actual_move_cnt; i++)
    {
        int score;

        if (i == 1)
            Search_Do_Sort(movelist+1, move_cnt-1);

        Search_Push_Status();
        Search_Make_Move(movelist[i]);

        if (root_node)
        {
            uci_curr_move.u = movelist[i].u;
            uci_curr_move_number = i;
            if ((show_currmove == CURR_ALWAYS) && (time_is_up == TM_NO_TIMEOUT))
            {
                /*output_time gets initialised to start_time + 1000LL. if it is
                  greater, then the first output_time must be through, which means
                  that output_time out at least start_time + 2000LL, so at least
                  1 second is passed: start the output after 1 second as per the
                  UCI spec.
                  this still gets around always calling the time functions, which are
                  expensive.*/
                if (output_time >= start_time + 1500LL)
                {
                    int len;
                    char *mv_str;
                    strcpy(printbuf, "info currmove ");
                    mv_str = Play_Translate_Moves(movelist[i]);
                    strcpy(printbuf + 14, mv_str);
                    len = (mv_str[4] == 0) ? (14 + 4) : (14 + 5);
                    strcpy(printbuf + len, " currmovenumber ");
                    len += 16;
                    len += Util_Tostring_U16(printbuf + len, (uint16_t)(i + 1));
                    printbuf[len++] = '\n';
                    printbuf[len] = '\0';
                    Play_Print(printbuf);
                }
            }
        }

        score = -Search_Negamate(depth-1, -beta, -a, next_colour, xmvlist, 0, check_depth, &line,
                                 (movelist[i].m.mvv_lva == MVV_LVA_CHECK), check_type);

        if (score == 0) /*don't risk messing up the PV with the hash tables.*/
        {
            MOVE no_move;
            no_move.u = MV_NO_MOVE_MASK; /*don't store moves, doesn't matter anyway.*/
            /*since we already have made the move and are before retracting it, the depth is
              one less. And it must be the opponent's hash table because he is to move with the
              board position before retracting the move.*/
            if (colour == WHITE)
                Hash_Update_TT(Opp_T_T, depth-1, 0, EXACT, move_stack[mv_stack_p].mv_pos_hash, no_move);
            else
                Hash_Update_TT(T_T, depth-1, 0, EXACT, move_stack[mv_stack_p].mv_pos_hash, no_move);
        }

        Search_Retract_Last_Move();
        Search_Pop_Status();

        if (score > a)
        {
            a = score;
            /*update the PV*/
            pline->line_cmoves[0] = Mvgen_Compress_Move(movelist[i]);
            memcpy(pline->line_cmoves + 1, line.line_cmoves, sizeof(CMOVE) * line.line_len);
            pline->line_len = line.line_len + 1;

            if ((root_node) && (score > MATE_CUTOFF)) /*all we are looking for*/
                return(score);

            if (score >= beta)
            {
                /*for non-captures/promotions, update depth killers and history*/
                if (board[movelist[i].m.to]->type == NO_PIECE)
                    Search_Update_Killers_History(colour, depth, depth, movelist, i);

                return(score);
            }
        }
    } /*for each node*/

    return(a);
}

/*perform a shallow search of 1 ply + QS at root to sort the root move list.*/
static int Search_Play_And_Sort_Moves(MOVE *restrict movelist, int len,
                                      enum E_COLOUR next_colour, int *restrict score_drop)
{
    int sortV[MAXMV], i;

    /*this is just in case that during future works, this routine gets called
      with an empty move list (len == 0), i.e. mate or stalemate. usually,
      this should have been handled before calling this routine. but this way,
      the program would resign immediately, making it obvious that something
      is wrong. otherwise, if len==0, we would return an uninitialised value.*/
    sortV[0] = -INFINITY_;

    for (i = 0; i < len; i++)
    {
        int current_score;

        if (movelist[i].m.flag == 0) /*illegal move - should not happen*/
            current_score = -INFINITY_;
        else
        {
            Search_Push_Status();
            Search_Make_Move(movelist[i]);

            if (Hash_Check_For_Draw())
            {
                if ((mv_stack_p + start_moves < contempt_end) && (game_started_from_0))
                    current_score = contempt_val;
                else
                    current_score = 0;
            } else
                current_score = -Search_Quiescence(-INFINITY_, INFINITY_, next_colour, QS_NO_CHECKS, 0);

            Search_Retract_Last_Move();
            Search_Pop_Status();
        }
        sortV[i] = current_score;
    }

    /*sort movelist using sortV.*/
    if (len > 1)
    {
        Search_Do_Sort_Value(movelist, sortV, len);
        *score_drop = sortV[0] - sortV[1];
    } else
        *score_drop = SORT_THRESHOLD;

    return(sortV[0]);
}

static void Search_Print_Move_Output(int depth, int score, int64_t time_passed, int hash_report)
{
    uint64_t nps;
    int len;

    if (time_passed > 0)
        nps = (g_nodes * 1000U) / time_passed;
    else
        nps = 0;

    /*avoid sprintf for speed reasons in extreme bullet games where fast
      I/O can become important since the search depth is low and the output
      frequent in relation to the thinking time.
      using strcpy with the right length is also a bit faster than repeated
      strcat.*/
    strcpy(printbuf, "info depth ");
    len = 11;
    len += Util_Tostring_U16(printbuf + len, (uint16_t) depth);
    strcpy(printbuf + len, " seldepth ");
    len += 10;

    if (GlobalPV.line_len > depth)
        len += Util_Tostring_U16(printbuf + len, (uint16_t) GlobalPV.line_len);
    else
        len += Util_Tostring_U16(printbuf + len, (uint16_t) depth);

    if (score > MATE_CUTOFF)
    {
        int mate_moves = INFINITY_ - score;
        mate_moves++;
        mate_moves /= 2;
        strcpy(printbuf + len, " score mate ");
        len += 12;
        len += Util_Tostring_U16(printbuf + len, (uint16_t) mate_moves);
    } else if (score < -MATE_CUTOFF)
    {
        int mate_moves = INFINITY_ + score;
        mate_moves++;
        mate_moves /= 2;
        strcpy(printbuf + len, " score mate -");
        len += 13;
        len += Util_Tostring_U16(printbuf + len, (uint16_t) mate_moves);
    } else
    {
        strcpy(printbuf + len, " score cp ");
        len += 10;
        len += Util_Tostring_I16(printbuf + len, (int16_t) score);
    }

    strcpy(printbuf + len, " time ");
    len += 6;
    len += Util_Tostring_I64(printbuf + len, time_passed);

    strcpy(printbuf + len, " nodes ");
    len += 7;
    len += Util_Tostring_U64(printbuf + len, g_nodes);

    strcpy(printbuf + len, " nps ");
    len += 5;
    len += Util_Tostring_U64(printbuf + len, nps);

    if (hash_report)
    {
        uint16_t hash_used = Hash_Get_Usage();

        strcpy(printbuf + len, " hashfull ");
        len += 10;
        len += Util_Tostring_U16(printbuf + len, hash_used);
    }

    if (GlobalPV.line_len > 0)
    {
        strcpy(printbuf + len, " pv");
        len += 3;
        len += Search_Print_PV_Line(&GlobalPV, printbuf + len);
    }

    printbuf[len++] = '\n';
    printbuf[len] = '\0';

    Play_Print(printbuf); /*print everything at once so that the other thread won't interfere*/
}

/*tests whether the position is checkmate for the relevant colour.*/
static int Search_Is_Checkmate(enum E_COLOUR colour)
{
    MOVE check_attacks[CHECKLISTLEN];
    MOVE movelist[MAXMV];
    int i, move_cnt;
    int n_checks, n_check_pieces;
    if (colour == WHITE)
    {
        if (Mvgen_White_King_In_Check())
        {
            int non_checking_move = 0;
            n_checks = Mvgen_White_King_In_Check_Info(check_attacks, &n_check_pieces);
            move_cnt = Mvgen_Find_All_White_Evasions(movelist, check_attacks, n_checks, n_check_pieces, UNDERPROM);
            /*is there a legal move?*/
            for (i = 0; i < move_cnt; i++)
            {
                g_nodes++;
                Search_Push_Status();
                Search_Make_Move(movelist[i]);
                if (!Mvgen_White_King_In_Check()) non_checking_move = 1;
                Search_Retract_Last_Move();
                Search_Pop_Status();
                if (non_checking_move) return(0); /*that move gets out of check: no checkmate.*/
            }
            return(1); /*no move gets the white king out of check: checkmate.*/
        }
    } else
    {
        if (Mvgen_Black_King_In_Check())
        {
            int non_checking_move = 0;
            n_checks = Mvgen_Black_King_In_Check_Info(check_attacks, &n_check_pieces);
            move_cnt = Mvgen_Find_All_Black_Evasions(movelist, check_attacks, n_checks, n_check_pieces, UNDERPROM);
            /*is there a legal move?*/
            for (i = 0; i < move_cnt; i++)
            {
                g_nodes++;
                Search_Push_Status();
                Search_Make_Move(movelist[i]);
                if (!Mvgen_Black_King_In_Check()) non_checking_move = 1;
                Search_Retract_Last_Move();
                Search_Pop_Status();
                if (non_checking_move) return(0); /*that move gets out of check: no checkmate.*/
            }
            return(1); /*no move gets the black king out of check: checkmate.*/
        }
    }
    return(0); /*no king is in check, so it can't be checkmate.*/
}

void Search_Reset_History(void)
{
    memset(W_history, 0, sizeof(W_history));
    memset(B_history, 0, sizeof(B_history));
    memset(W_Killers, 0, sizeof(W_Killers));
    memset(B_Killers, 0, sizeof(B_Killers));
}


/* ---------- global functions ----------*/


void Search_Make_Move(MOVE amove)
{
    int xy1, xy2, flag, ptype1, xyc, xydif;
    MVST* p;

    xy1  = amove.m.from;
    xy2  = amove.m.to;
    flag = amove.m.flag;
    ptype1 = board[xy1]->type;
    mv_stack_p++;
    p = &move_stack[mv_stack_p];
    p->move.u = amove.u;
    en_passant_sq = 0;
    p->special = NORMAL;

    /*en passant captures: with a null move, the e.p. square is cleared
      beforehand and restored afterwards.*/

    if (ptype1 == WPAWN) {
        xydif = xy2-xy1;
        if (((xydif == 11) || (xydif == 9)) && (board[xy2]->type == 0)) { /*En Passant*/
            xyc = xy2-10;
            p->captured = board[xyc];
            p->capt = xyc;
        } else {
            if ((flag >= WKNIGHT) && (flag < WKING)) { /* white pawn promotion. Piece in flag */
                board[xy1]->type = flag;
                p->special = PROMOT;
            }
            xyc = xy2;
            p->captured = board[xyc];
            p->capt = xyc;
            if (xydif == 20) {
                if ((board[xy2+1]->type == BPAWN) || (board[xy2-1]->type == BPAWN)) {
                    en_passant_sq = xy1+10;
                }
            }
        }
    } else if (ptype1 == BPAWN) {
        xydif = xy2-xy1;
        if (((xydif == -11) || (xydif == -9)) && (board[xy2]->type == 0)) { /*En Passant*/
            xyc = xy2+10;
            p->captured = board[xyc];
            p->capt = xyc;
        } else {
            if ((flag >= BKNIGHT) && (flag < BKING)) { /* black pawn promotes to flag */
                board[xy1]->type = flag;
                p->special = PROMOT;
            }
            xyc = xy2;
            p->captured = board[xyc];
            p->capt = xyc;
            if (xydif == -20) {
                if ((board[xy2+1]->type == WPAWN) || (board[xy2-1]->type == WPAWN)) {
                    en_passant_sq = xy1-10;
                }
            }
        }
    } else {
        xyc = xy2;
        p->captured = board[xyc];
        p->capt = xyc;
    }
    /* Captured piece struct modified */
    if (board[xyc]->type) {
        PIECE *piece_p = board[xyc];
        piece_p->xy = 0;
        piece_p->prev->next = piece_p->next;
        if (piece_p->next) {
            piece_p->next->prev = piece_p->prev;
        }
        board[xyc] = &empty_p;
    }
    board[xy1]->xy = xy2;    /* Moving piece struct modified */
    board[xy2] = board[xy1];
    board[xy1] = &empty_p;
    /* Update Flags */
    if (board[xy2]->type > BLACK) {
        gflags |= BLACK_MOVED; /*black move*/
        if (ptype1 == BROOK) {
            if (xy1 == A8) {
                gflags |= BRA8MOVED; /*  bra8moved=1;*/
                if (gflags & BRH8MOVED) /*if both rooks have moved, set the king also moved*/
                    gflags |= BKMOVED;
            } else if (xy1 == H8) {
                gflags |= BRH8MOVED; /*  brh8moved=1;*/
                if (gflags & BRA8MOVED) /*if both rooks have moved, set the king also moved*/
                    gflags |= BKMOVED;
            }
        } else if (ptype1 == BKING) {
            bking = xy2;
            if (xy1 == E8) {
                gflags |= (BKMOVED | BRA8MOVED | BRH8MOVED);
                if (xy2 == G8) { /* black short castle */
                    board[F8] = board[H8];
                    board[F8]->xy = F8;
                    board[H8] = &empty_p;
                    p->special = CASTL;
                } else if (xy2 == C8) { /* black long castle */
                    board[D8] = board[A8];
                    board[D8]->xy = D8;
                    board[A8] = &empty_p;
                    p->special = CASTL;
                }
            }
        }
        /*if a rook is captured on its origin square and the other side
          takes back with another rook, the capturing rook has no castling
          right.*/
        if (!(gflags & WKMOVED))
        {
            if (xy2 == A1)
            {
                if (gflags & WRH1MOVED)
                    gflags |= (WKMOVED | WRA1MOVED);
                else
                    gflags |= WRA1MOVED;
            } else if (xy2 == H1)
            {
                if (gflags & WRA1MOVED)
                    gflags |= (WKMOVED | WRH1MOVED);
                else
                    gflags |= WRH1MOVED;
            }
        }
    } else {
        gflags &= ~BLACK_MOVED; /*white move*/
        if (ptype1 == WROOK) {
            if (xy1 == A1) {
                gflags |= WRA1MOVED;
                if (gflags & WRH1MOVED) /*if both rooks have moved, set the king also moved*/
                    gflags |= WKMOVED;
            } else if (xy1 == H1) {
                gflags |= WRH1MOVED;
                if (gflags & WRA1MOVED) /*if both rooks have moved, set the king also moved*/
                    gflags |= WKMOVED;
            }
        } else if (ptype1 == WKING) {
            wking = xy2;
            if (xy1 == E1) {
                gflags |= (WKMOVED | WRA1MOVED | WRH1MOVED);
                if (xy2 == G1) { /* white short castle */
                    board[F1] = board[H1];
                    board[F1]->xy = F1;
                    board[H1] = &empty_p;
                    p->special = CASTL;
                } else if (xy2 == C1) { /* white long castle */
                    board[D1] = board[A1];
                    board[D1]->xy = D1;
                    board[A1] = &empty_p;
                    p->special = CASTL;
                }
            }
        }
        /*if a rook is captured on its origin square and the other side
          takes back with another rook, the capturing rook has no castling
          right.*/
        if (!(gflags & BKMOVED))
        {
            if (xy2 == A8)
            {
                if (gflags & BRH8MOVED)
                    gflags |= (BKMOVED | BRA8MOVED);
                else
                    gflags |= BRA8MOVED;
            } else if (xy2 == H8)
            {
                if (gflags & BRA8MOVED)
                    gflags |= (BKMOVED | BRH8MOVED);
                else
                    gflags |= BRH8MOVED;
            }
        }
    }
    p->mv_pos_hash = Hash_Get_Position_Value(&p->mv_pawn_hash);
}

void Search_Retract_Last_Move(void)
{
    int xy1=move_stack[mv_stack_p].move.m.from;
    int xy2=move_stack[mv_stack_p].move.m.to;
    int cpt=move_stack[mv_stack_p].capt;
    board[xy1] = board[xy2];
    board[xy1]->xy = xy1;
    board[xy2] = &empty_p;
    board[cpt] = move_stack[mv_stack_p].captured;
    if (board[cpt] != &empty_p) {
        board[cpt]->xy = cpt;
        board[cpt]->prev->next = board[cpt];
        if (board[cpt]->next)
            board[cpt]->next->prev = board[cpt];
    }

    if (move_stack[mv_stack_p].special==PROMOT) {
        if (xy1>=A7) { /* white pawn promotion */
            board[xy1]->type  = WPAWN;
        } else { /* black pawn promotion */
            board[xy1]->type  = BPAWN;
        }
    } else {
        if (board[xy1]->type==WKING) {
            wking=xy1;
        } else if (board[xy1]->type==BKING) {
            bking=xy1;
        }
        if (move_stack[mv_stack_p].special==CASTL) {
            if (xy1==E1) { /* white castle */
                if (xy2==G1) {
                    board[H1] = board[F1];
                    board[H1]->xy = H1;
                    board[F1] = &empty_p;
                } else if (xy2==C1) {
                    board[A1] = board[D1];
                    board[A1]->xy = A1;
                    board[D1] = &empty_p;
                }
            } else if (xy1==E8) { /* black castle */
                if (xy2==G8) {
                    board[H8] = board[F8];
                    board[H8]->xy = H8;
                    board[F8] = &empty_p;
                } else if (xy2==C8) {
                    board[A8] = board[D8];
                    board[A8]->xy = A8;
                    board[D8] = &empty_p;
                }
            }
        }
    }
    mv_stack_p--;
}

void Search_Try_Move(MOVE amove) /* No ep/castle flags checked */
{
    int xyc, xydif;
    int xy1 =amove.m.from;
    int xy2 =amove.m.to;
    int flag=amove.m.flag;
    mv_stack_p++;

    move_stack[mv_stack_p].move.u = amove.u;
    move_stack[mv_stack_p].special = NORMAL;
    if (board[xy1]->type==WPAWN) {
        xydif = xy2-xy1;
        if ((xydif==11 || xydif==9) && (board[xy2]->type==0)) { /*En Passant*/
            xyc = xy2-10;
            move_stack[mv_stack_p].captured = board[xyc];
            move_stack[mv_stack_p].capt = xyc;
        } else {
            if (flag>=WKNIGHT && flag<WKING) { /* white pawn promotion. Piece in flag */
                board[xy1]->type = flag;
                move_stack[mv_stack_p].special=PROMOT;
            }
            xyc=xy2;
            move_stack[mv_stack_p].captured = board[xyc];
            move_stack[mv_stack_p].capt = xyc;
        }
    } else if (board[xy1]->type==BPAWN) {
        xydif = xy2-xy1;
        if ((xydif==-11 || xydif==-9) && (board[xy2]->type==0)) { /*En Passant*/
            xyc = xy2+10;
            move_stack[mv_stack_p].captured = board[xyc];
            move_stack[mv_stack_p].capt = xyc;
        } else {
            if (flag>=BKNIGHT && flag<BKING) { /* black pawn promotes to flag */
                board[xy1]->type = flag;
                move_stack[mv_stack_p].special=PROMOT;
            }
            xyc=xy2;
            move_stack[mv_stack_p].captured = board[xyc];
            move_stack[mv_stack_p].capt = xyc;
        }
    } else {
        xyc=xy2;
        move_stack[mv_stack_p].captured = board[xyc];
        move_stack[mv_stack_p].capt = xyc;
    }
    board[xyc]->xy = 0;      /* Captured piece struct modified */
    board[xy1]->xy = xy2;    /* Moving piece struct modified */
    board[xyc] = &empty_p;
    board[xy2] = board[xy1];
    board[xy1] = &empty_p;
    if (board[xy2]->type==WKING) {
        wking = xy2;
        if (xy1==E1) {
            if (xy2==G1) { /* white short castle */
                board[F1] = board[H1];
                board[F1]->xy = F1;
                board[H1] = &empty_p;
                move_stack[mv_stack_p].special = CASTL;
            } else if (xy2==C1) { /* white long castle */
                board[D1] = board[A1];
                board[D1]->xy = D1;
                board[A1] = &empty_p;
                move_stack[mv_stack_p].special = CASTL;
            }
        }
    } else if (board[xy2]->type==BKING) {
        bking = xy2;
        if (xy1==E8) {
            if (xy2==G8) { /* black short castle */
                board[F8] = board[H8];
                board[F8]->xy = F8;
                board[H8] = &empty_p;
                move_stack[mv_stack_p].special = CASTL;
            } else if (xy2==C8) { /* black long castle */
                board[D8] = board[A8];
                board[D8]->xy = D8;
                board[A8] = &empty_p;
                move_stack[mv_stack_p].special = CASTL;
            }
        }
    }
}

/*the mate solver.*/
static enum E_COMP_RESULT
Search_Get_Mate_Solution(int mate_depth_mv, MOVE *restrict movelist, int move_cnt,
                         LINE *restrict pline, enum E_COLOUR colour, int in_check)
{
    int res = 0, max_d;
    int Alpha = 0;
    int Beta  = INFINITY_;

    pline->line_len = 0;
    Starting_Mv = mv_stack_p;
    max_d = (mate_depth_mv * 2) - 1;

    /*first try to look for a mate when only considering check-delivering
      moves in the last "check_depth" moves. the idea is that the last
      few plies in a mate problem usually is a series of checks, so limit
      the moves for the attacking side to checks. the defending side, of
      course, always has the full tange of answer moves.
      if the approach of "all attacking moves deliver check" does not work
      out, then try to allow non-checking moves in the first ply and then only
      checks. if that doesn't work, allow all moves for the first two attack
      plies, and then only checks. and so on. it is amazing how much time this
      little scheme can save.
      since the last ply must deliver check (or else it cannot be mate),
      check_depth has to stay greater than zero.*/

    /*checks only*/
    Search_Reset_History();
    Hash_Clear_Tables();
    res = Search_Negamate(max_d, Alpha, Beta, colour, movelist, move_cnt, max_d, pline, in_check, MIN_CHECK_DEPTH);

    /*non-checks at exactly one depth*/
    if ((res <= MATE_CUTOFF) && (time_is_up == TM_NO_TIMEOUT))
    {
        res = 0;
        for (int non_check_depth = max_d;
             ((non_check_depth > 1) && (res <= MATE_CUTOFF) && (time_is_up == TM_NO_TIMEOUT)); non_check_depth -= 2)
        {
            Search_Reset_History();
            Hash_Clear_Tables();
            res = Search_Negamate(max_d, Alpha, Beta, colour, movelist, move_cnt, non_check_depth, pline, in_check, EXACT_CHECK_DEPTH);
        }
    }

    /*non-checks at increasing depth - root level has already been searched*/
    if ((res <= MATE_CUTOFF) && (time_is_up == TM_NO_TIMEOUT))
    {
        res = 0;
        for (int check_depth = max_d - 4;
             ((check_depth > 0) && (res <= MATE_CUTOFF) && (time_is_up == TM_NO_TIMEOUT)); check_depth -= 2)
        {
            Search_Reset_History();
            Hash_Clear_Tables();
            res = Search_Negamate(max_d, Alpha, Beta, colour, movelist, move_cnt, check_depth, pline, in_check, MIN_CHECK_DEPTH);
        }
    }

    Search_Reset_History();
    Hash_Clear_Tables();

    if ((res > MATE_CUTOFF) && (time_is_up == TM_NO_TIMEOUT))
        return (COMP_MOVE_FOUND);
    else
        return(COMP_NO_MOVE);
}

/*50 moves rule: every move will draw except captures and pawn moves,
  and if these had been useful, they would have been chosen before.
  just don't make a move that allows the opponent to capture. that
  would still be a draw, but some GUIs might have issues in noticing
  that. besides, it would look silly.
  however, if the opponent has hung a piece and now it isn't draw, then
  we'll catch that because it's only about pre-sorting the list. on the
  other hand, if everything draws, then the list order will not be changed
  in search.*/
static void Search_Sort_50_Moves(MOVE *restrict player_move, MOVE *restrict movelist,
                                 int move_cnt, enum E_COLOUR colour)
{
    if (fifty_moves >= 99)
    {
        MOVE opp_movelist[MAXMV];
        enum E_COLOUR next_colour = Mvgen_Opp_Colour(colour);
        int i;

        for (i = 0; i < move_cnt; i++)
        {
            int moving_piece, moving_to;

            if (movelist[i].m.mvv_lva == MVV_LVA_MATE_1) /*mate already found*/
                continue;
            moving_piece = board[movelist[i].m.from]->type;
            moving_to = movelist[i].m.to;

            if ((moving_piece != WPAWN) && (moving_piece != BPAWN) &&
                (board[movelist[i].m.to]->type == NO_PIECE))
            {
                int opp_capture_moves, is_checking, take_moving_piece = 0;

                Search_Push_Status();
                Search_Make_Move(movelist[i]);

                /*prefer check giving moves. if those lead to legal
                  captures, they will be de-prioritised anyway.*/
                if (Mvgen_King_In_Check(next_colour))
                    is_checking = 1;
                else
                    is_checking = 0;

                /*find the opponent's capture moves.*/
                opp_capture_moves = Mvgen_Find_All_Captures_And_Promotions(opp_movelist, next_colour, QUEENING);

                /*if there are any, don't count those that would put the
                  opponent in check. that increases the chance to find
                  suitable own moves.*/
                if (opp_capture_moves > 0)
                {
                    int k, legal_opp_moves = 0;
                    for (k = 0; k < opp_capture_moves; k++)
                    {
                        Search_Push_Status();
                        Search_Make_Move(opp_movelist[k]);
                        if (!Mvgen_King_In_Check(next_colour))
                        {
                            legal_opp_moves = 1;
                            /*can take the piece that we have moved?*/
                            if (opp_movelist[k].m.to == moving_to)
                                take_moving_piece = 1;
                        }
                        Search_Retract_Last_Move();
                        Search_Pop_Status();
                    }
                    if (legal_opp_moves == 0) /*no legal captures.*/
                        opp_capture_moves = 0;
                }
                if (opp_capture_moves == 0)
                {
                    /*a checking move that can't be answered by a capture is
                      bullet-proof - it might even be checkmate if the opponent
                      does not know that checkmate has priority over the 50 moves
                      draw. otherwise, at least a move where there are no captures.*/
                    movelist[i].m.mvv_lva = MVV_LVA_50_OK + is_checking;
                } else
                {
                    /*a check giving move that can be answered by a capture
                      will capture the check-giving piece!*/
                    movelist[i].m.mvv_lva = MVV_LVA_50_NOK - is_checking - take_moving_piece;
                }

                Search_Retract_Last_Move();
                Search_Pop_Status();
            } else
                movelist[i].m.mvv_lva = MVV_LVA_50_NOK;
        }
        Search_Do_Sort(movelist, move_cnt);
        player_move->u = MV_NO_MOVE_MASK; /*trash PV*/
    }
}

static int Search_Get_Root_Move_List(MOVE *restrict movelist, int *restrict move_cnt, enum E_COLOUR colour)
{
    enum E_COLOUR next_colour;
    int i, mv_len, actual_move_cnt, n_checks, n_check_pieces;

    if (colour == WHITE)
    {
        next_colour = BLACK;
        n_checks = Mvgen_White_King_In_Check_Info(search_check_attacks_buf, &n_check_pieces);
        if (n_checks != 0)
            mv_len = Mvgen_Find_All_White_Evasions(movelist, search_check_attacks_buf, n_checks, n_check_pieces, UNDERPROM);
        else
            mv_len = Mvgen_Find_All_White_Moves(movelist, NO_LEVEL, UNDERPROM);
    } else
    {
        next_colour = WHITE;
        n_checks = Mvgen_Black_King_In_Check_Info(search_check_attacks_buf, &n_check_pieces);
        if (n_checks != 0)
            mv_len = Mvgen_Find_All_Black_Evasions(movelist, search_check_attacks_buf, n_checks, n_check_pieces, UNDERPROM);
        else
            mv_len = Mvgen_Find_All_Black_Moves(movelist, NO_LEVEL, UNDERPROM);
    }

    for (i = 0, actual_move_cnt = 0; i < mv_len; i++)
    {
        /*filter out all moves that would put or let our king in check*/
        Search_Push_Status();
        Search_Make_Move(movelist[i]);
        if (Mvgen_King_In_Check(colour))
        {
            movelist[i].m.mvv_lva = MVV_LVA_ILLEGAL;
            movelist[i].m.flag = 0;
            Search_Retract_Last_Move();
            Search_Pop_Status();
            continue;
        }
        /*rank mate in 1 high: useful for pathological test positions like this:
          1QqQqQq1/r6Q/Q6q/q6Q/B2q4/q6Q/k6K/1qQ1QqRb w - -
          1Qq1qQrB/K6k/Q6q/b2Q4/Q6q/q6Q/R6q/1qQqQqQ1 b - -
          1qQ1QqRb/k6K/q6Q/B2q4/q6Q/Q6q/r6Q/1QqQqQq1 w - -
          1qQqQqQ1/R6q/q6Q/Q6q/b2Q4/Q6q/K6k/1Qq1qQrB b - -
          doesn't cost significant time in normal play, and the CT800 shall
          always be able to deliver mate no matter what.*/
        if (Search_Is_Checkmate(next_colour))
            movelist[i].m.mvv_lva = MVV_LVA_MATE_1;
        Search_Retract_Last_Move();
        Search_Pop_Status();
        actual_move_cnt++;
    }

    /*the illegal moves get sorted to the end of the list.*/
    Search_Do_Sort(movelist, mv_len);
    *move_cnt = actual_move_cnt;
    return(n_checks);
}

enum E_COMP_RESULT
Search_Get_Best_Move(MOVE *restrict answer_move, MOVE player_move, int64_t full_move_time,
                     int move_overhead, int exact_time, int max_depth, int cpu_speed,
                     uint64_t max_nps_rate, enum E_COLOUR colour, const MOVE *restrict given_moves,
                     int given_moves_len, int mate_mode, int mate_depth_mv,
                     uint64_t *restrict spent_nodes, int64_t *restrict spent_time)
{
    uint64_t printed_nodes;
    int64_t time_passed=0LL;
    int ret_mv_idx=0, move_cnt, is_material_enough, in_check, i, easy_depth,
        mate_in_1, min_thinking_time, is_normal_time, is_analysis;
    MOVE movelist[MAXMV];
    LINE line;

    start_time = Play_Get_Millisecs();
    start_time_nps = start_time; /*can be overwritten later after hash aging*/
    sleep_time = 0;

    /*get the time when to start the first throttle sleep phase in CPU
      percentage mode, but only after the pre-search*/
    throttle_time = start_time + INFINITE_TIME;

    /*limiter only after pre-search*/
    effective_max_nps_rate = NO_THROTTLE_NPS;
    effective_cpu_speed = 100;

    /*also throttle the easy move depth threshold*/
    easy_depth = EASY_DEPTH;

    if ((cpu_speed <= 5) || (max_nps_rate <= 50000U))
        easy_depth = EASY_DEPTH - 2;
    else if ((cpu_speed <= 20) || (max_nps_rate <= 200000U))
        easy_depth = EASY_DEPTH - 1;

    output_time = start_time + 1000; /*output of hash filling etc. every second*/

    /*for extreme bullet games (1 second per game):
      calculate minimum thinking time.
      move:
       1  -  9: 10 ms
      10 -  35: 15 ms (most difficult game phase)
      36 -  40: 12 ms
      41 -  60: 10 ms
      61 -  80:  5 ms
      81 - 120:  2 ms
      > 120   :  1 ms
      in-check moves get half of the that time, rounded up.*/
    if (game_started_from_0)
    {
        int ply_number = (start_moves + mv_stack_p) / 2 + 1;
        if (ply_number < 10)
            min_thinking_time = 10;
        else if (ply_number <= 35)
            min_thinking_time = 15;
        else if (ply_number <= 40)
            min_thinking_time = 12;
        else if (ply_number <= 60)
            min_thinking_time = 10;
        else if (ply_number <= 80)
            min_thinking_time = 5;
        else if (ply_number <= 120)
            min_thinking_time = 2;
        else
            min_thinking_time = 1;
    } else
    {
        /*no game information available, use 10 ms fixed.*/
        min_thinking_time = 10;
    }

    stop_time = start_time + full_move_time - move_overhead; /*account for GUI delays*/

    if (stop_time < start_time + min_thinking_time)
    {
        if (!exact_time)
            stop_time = start_time + min_thinking_time;
        /*with bullet time, drop the hash full report because that involves
          a scan over the first 32k of each main hash table.*/
        is_normal_time = 0;
        /*if unthrottled: reduce the "easy move" depth to something useful*/
        if (easy_depth == EASY_DEPTH)
            easy_depth = EASY_DEPTH - 2;
    } else
        is_normal_time = 1;

    time_is_up = TM_NO_TIMEOUT;

    uci_curr_move.u = MV_NO_MOVE_MASK;
    uci_curr_move_number = 0;
    g_nodes = 1; /*this node*/
    nodes_current_second = 1;
    printed_nodes = 0;
    last_nodes = 0;
    last_throttle_nodes = 0;
    nps_1ms = 500;
    nps_startup_phase = 1;
    *spent_nodes = 1U;
    *spent_time = 0;
#ifdef DBGCUTOFF
    cutoffs_on_1st_move = total_cutoffs = 0;
#endif

    answer_move->u = MV_NO_MOVE_MASK;
    mate_in_1 = 0;
    is_analysis = ((exact_time) && (full_move_time == INFINITE_TIME));

    /*static history ageing*/
    {
        uint16_t *hw_ptr = (uint16_t *) W_history,
                 *hb_ptr = (uint16_t *) B_history;
        uint16_t *hw_end_ptr = hw_ptr + 6 * ENDSQ;

        do {
            *hw_ptr >>= 3; hw_ptr++;
            *hb_ptr >>= 3; hb_ptr++;
            *hw_ptr >>= 3; hw_ptr++;
            *hb_ptr >>= 3; hb_ptr++;
            *hw_ptr >>= 3; hw_ptr++;
            *hb_ptr >>= 3; hb_ptr++;
        } while (hw_ptr < hw_end_ptr);
    }
    memset(W_Killers, 0, sizeof(W_Killers));
    memset(B_Killers, 0, sizeof(B_Killers));

    is_material_enough = Eval_Setup_Initial_Material();
    if ((!is_material_enough) && (uci_debug))
         Play_Print("info string debug: insufficient material draw.\n");

    Starting_Mv = mv_stack_p;
    in_check = Search_Get_Root_Move_List(movelist, &move_cnt, colour);

    if (move_cnt == 0) /*the GUI should have filtered this*/
    {
        if (in_check)
            return (COMP_MATE);
        else
            return(COMP_STALE);
    }

    if (given_moves_len >= 0)
    {
        int actual_move_cnt;
        uint32_t mv_mask = mv_move_mask.u;

        for (i = 0, actual_move_cnt = 0;
             ((i < given_moves_len) && (actual_move_cnt < move_cnt)); i++)
        {
            Search_Find_Put_To_Top(movelist + actual_move_cnt, move_cnt - actual_move_cnt, given_moves[i]);
            /*if the given move has been legal, it must be found at the top now.
              filters also away double moves.*/
            if (((movelist[actual_move_cnt].u ^ given_moves[i].u) & mv_mask) == 0)
            {
                actual_move_cnt++;
                /*as soon as actual_move_cnt reaches move_cnt, the loop has to end
                  because all legal moves have already been selected.*/
            }
        }
        if (actual_move_cnt == 0) /*no legal moves given*/
            return(COMP_NO_MOVE);
        else if (move_cnt == actual_move_cnt)
            given_moves_len = -1; /*full move list selected*/
        else
            move_cnt = actual_move_cnt;

        /*resort per MVV/LVA*/
        Search_Do_Sort(movelist, move_cnt);
    }

    /*immediate mate found and not sorted out?*/
    if (movelist[0].m.mvv_lva == MVV_LVA_MATE_1)
        mate_in_1 = 1;

    /*dedicated mate searcher mode*/
    if (mate_mode)
    {
        enum E_COMP_RESULT ret_mv_status;

        memset(&line, 0, sizeof(LINE));

        /*now activate potential throttling since no pre-search is done.*/

        effective_max_nps_rate = max_nps_rate;
        effective_cpu_speed = cpu_speed;
        /*reduce effective throttle speed if the move time will be over
          in the next second.*/
        Time_Calc_Throttle(start_time);
        /*set percentage throttling.*/
        if (effective_cpu_speed < 100)
            throttle_time = start_time + effective_cpu_speed * 10;

        if (!mate_in_1)
            ret_mv_status = Search_Get_Mate_Solution(mate_depth_mv, movelist, move_cnt, &line, colour, in_check);
        else
        {
            ret_mv_status = COMP_MOVE_FOUND;
            line.line_cmoves[0] = Mvgen_Compress_Move(movelist[0]);
            line.line_len = 1;
        }
        time_passed = Time_Passed();
        if (ret_mv_status == COMP_MOVE_FOUND)
        {
            memcpy(&GlobalPV, &line, sizeof(LINE));
            Search_Print_Move_Output(line.line_len, INFINITY_ - line.line_len, time_passed, is_normal_time);
            *answer_move = Mvgen_Decompress_Move(line.line_cmoves[0]);
            /*for mate mode, only wait with "go infinite"*/
            if ((is_analysis) && (time_is_up != TM_ABORT))
            {
                Time_Wait_For_Abort();
                time_passed = Time_Passed();
                Search_Print_Move_Output(line.line_len, INFINITY_ - line.line_len, time_passed, is_normal_time);
            }
        } else
        {
            GlobalPV.line_len = 0;
            Search_Print_Move_Output(mate_depth_mv * 2 - 1, 0, time_passed, is_normal_time);
            answer_move->u = MV_NO_MOVE_MASK;
            /*for mate mode, only wait with "go infinite"*/
            if ((is_analysis) && (time_is_up != TM_ABORT))
            {
                Time_Wait_For_Abort();
                time_passed = Time_Passed();
                Search_Print_Move_Output(mate_depth_mv * 2 - 1, 0, time_passed, is_normal_time);
            }
        }
        *spent_nodes = g_nodes;
        *spent_time = time_passed;
        return(ret_mv_status);
    } /*end mate searcher mode*/

    if ((disable_book == 0) && (full_move_time < INFINITE_TIME) &&
        (Book_Is_Line(&ret_mv_idx, movelist, move_cnt, given_moves_len < 0)))
    {
        game_info.valid = EVAL_BOOK;
        answer_move->u = movelist[ret_mv_idx].u;
        GlobalPV.line_len = 1;
        GlobalPV.line_cmoves[0] = Mvgen_Compress_Move(movelist[ret_mv_idx]);
        time_passed = Time_Passed();
        Search_Print_Move_Output(1, 1, time_passed, is_normal_time);
        *spent_time = time_passed;
        *spent_nodes = g_nodes;
        return(COMP_MOVE_FOUND);
    } else {
        static int64_t hash_clear_time = 0;
        int64_t reduced_move_time;
        MOVE decomp_move;
        int d, sort_max, pv_hit = 0, score_drop, pos_score, nscore;
        CMOVE failsafe_cmove;
        uint8_t mate_conf;

        /*if 50 moves draw is close, re-sort the list*/
        Search_Sort_50_Moves(&player_move, movelist, move_cnt, colour);

        /*drop age clearing under extreme time pressure*/
        if ((full_move_time >= move_overhead * 10LL) && (full_move_time >= hash_clear_time * 10LL))
        {
            int64_t start_clear_time = Play_Get_Millisecs();
            Hash_Cut_Tables(hash_clear_counter);
            hash_clear_time = Play_Get_Millisecs() - start_clear_time;
        }

        /*clearing the hash tables takes time that should not be used for
          NPS calibration.*/
        start_time_nps = Play_Get_Millisecs();

        /*if too much time has been used, don't start another iteration as it will not finish anyway.*/
        if (exact_time == 0)
        {
            if (in_check) /*there are not many moves anyway*/
            {
                int half_min_time = (min_thinking_time + 1)/2;

                full_move_time = (full_move_time * 2) / 3;
                stop_time = start_time + full_move_time - move_overhead;
                if (stop_time < start_time + half_min_time)
                    stop_time = start_time + half_min_time;
            }
            reduced_move_time = ((stop_time - start_time) * 55 + 50) / 100;
        } else
            reduced_move_time = full_move_time;

        if (mate_in_1)
        {
            GlobalPV.line_cmoves[0] = Mvgen_Compress_Move(movelist[0]);
            GlobalPV.line_len = 1;
            if (fifty_moves < 100)
                game_info.eval = sort_max = pos_score = INF_MATE_1;
            else
                game_info.eval = sort_max = pos_score = 0;
            game_info.valid = EVAL_MOVE;
            game_info.depth = 1;
            player_move.u = MV_NO_MOVE_MASK; /*trash PV*/
            score_drop = 2 * EASY_THRESHOLD;
            /*save the best move from the pre-scan*/
            failsafe_cmove = GlobalPV.line_cmoves[0];
        } else
        {
            MOVE hash_best;
            int dummy;

            hash_best.u = MV_NO_MOVE_MASK;
            /*check with alpha=+INF and beta=-INF so that only the depth check
              is actually performed.*/
            if (!Hash_Check_TT(T_T, colour, INFINITY_, -INFINITY_, PRE_DEPTH, move_stack[mv_stack_p].mv_pos_hash, &dummy, &hash_best))
                 hash_best.u = MV_NO_MOVE_MASK;

            if (uci_debug)
            {
                if (hash_best.u != MV_NO_MOVE_MASK)
                {
                    char *mv_str;
                    int len;
                    strcpy(printbuf, "info string debug: root hash hit ");
                    len = 33;
                    mv_str = Play_Translate_Moves(hash_best);
                    strcpy(printbuf + len, mv_str);
                    len += (mv_str[4] == 0) ? 4 : 5; /*move with or without promotion*/
                    strcpy(printbuf + len, ".\n");
                    Play_Print(printbuf);
                }
            }

            if ((player_move.u != MV_NO_MOVE_MASK) && (GlobalPV.line_len >= 3) &&
                (GlobalPV.line_cmoves[1] == Mvgen_Compress_Move(player_move)))
            {
                /*shift down the global PV*/
                for (i = 0; i < GlobalPV.line_len - 2; i++)
                    GlobalPV.line_cmoves[i] = GlobalPV.line_cmoves[i+2];
                GlobalPV.line_len -= 2;
                if (GlobalPV.line_len > PRE_DEPTH) /*else the pre-search is better*/
                    pv_hit = 1;
                decomp_move = Mvgen_Decompress_Move(GlobalPV.line_cmoves[0]);
                Search_Find_Put_To_Top(movelist, move_cnt, decomp_move);
            } else
            {
                /*the opponent didn't follow the PV, so trash it.*/
                GlobalPV.line_len = 0;
                GlobalPV.line_cmoves[0] = MV_NO_MOVE_CMASK;
            }

            /*do the move presorting at depth=1.*/
            sort_max = Search_Play_And_Sort_Moves(movelist, move_cnt, Mvgen_Opp_Colour(colour), &score_drop);

            game_info.valid = EVAL_MOVE;

            /*prepare the game info structure. if the move time is about 0, then the iterative
              deepening loop will not be finished even for depth 2.*/
            if (!pv_hit)
            {
                GlobalPV.line_cmoves[0] = Mvgen_Compress_Move(movelist[0]);
                GlobalPV.line_len = 1;
                pos_score = game_info.eval = sort_max;
                game_info.depth = PRE_DEPTH;
            } else /*PV hit*/
            {
                if (game_info.last_valid_eval != NO_RESIGN)
                {
                    /*adjust possible mate distance*/
                    if (game_info.last_valid_eval > MATE_CUTOFF)
                        game_info.eval = game_info.last_valid_eval + 2;
                    else if (game_info.last_valid_eval < -MATE_CUTOFF)
                        game_info.eval = game_info.last_valid_eval - 2;
                    else
                        game_info.eval = game_info.last_valid_eval;
                } else
                    game_info.eval = sort_max;
                pos_score = game_info.eval;
                game_info.depth = GlobalPV.line_len;

                /*if this is a forced move and we have a PV anyway, just play the move.*/
                if ((move_cnt < 2) && (exact_time == 0))
                {
                    if (uci_debug) Play_Print("info string debug: PV hit and forced move.\n");
                    time_passed = Time_Passed();
                    Search_Print_Move_Output(game_info.depth, game_info.eval, time_passed, is_normal_time);
                    *answer_move = Mvgen_Decompress_Move(GlobalPV.line_cmoves[0]);
                    *spent_time = time_passed;
                    *spent_nodes = g_nodes;
                    return(COMP_MOVE_FOUND);
                }
            }
            /*save the best move from the pre-scan*/
            failsafe_cmove = Mvgen_Compress_Move(movelist[0]);

            /*put the hash move (if available) to the top before doing so with the
              PV move - if both are equal, nothing happens, and otherwise, the hash move
              will come at place 2.*/
            if (hash_best.u != MV_NO_MOVE_MASK)
                Search_Find_Put_To_Top(movelist, move_cnt, hash_best);
        }

        /* If opponent answered following previous PV, add the computer reply choice first in the moves list*/
        if (pv_hit)
        {
            decomp_move = Mvgen_Decompress_Move(GlobalPV.line_cmoves[0]);

            if (uci_debug)
            {
                char *mv_str;
                int len;
                strcpy(printbuf, "info string debug: PV hit ");
                len = 26;

                mv_str = Play_Translate_Moves(decomp_move);
                strcpy(printbuf + len, mv_str);
                len += (mv_str[4] == 0) ? 4 : 5; /*move with or without promotion*/
                strcpy(printbuf + len, ".\n");
                Play_Print(printbuf);
            }
            Search_Find_Put_To_Top(movelist, move_cnt, decomp_move);
        }

        /*look whether the easy move detection is
          a) a PV hit
          OR
          b) mate in 1
          OR
          c) bad enough to be realistic: yes, the opponent might just hang a piece. But it could also be a trap.
          AND
          d) good enough: maybe in the deep search, we know we have a mate, then don't be content with a piece.*/
        if ((!(
            ((pv_hit) && (failsafe_cmove == GlobalPV.line_cmoves[0])) ||
            (mate_in_1) ||
            ((game_info.last_valid_eval != NO_RESIGN) &&
                ((sort_max - game_info.last_valid_eval) < EASY_MARGIN_UP) &&
                ((sort_max - game_info.last_valid_eval) > EASY_MARGIN_DOWN))
           )) ||
           (exact_time)) /*don't use time savings with exact time or analysis*/
        {
            score_drop = 0;
        }

        /*clear the level 2 move cache.*/
        memset(root_refutation_table, 0, sizeof(root_refutation_table));

        /*now activate throttling since the pre-search is over.*/
        effective_max_nps_rate = max_nps_rate;
        effective_cpu_speed = cpu_speed;
        /*reduce effective throttle speed if the move time will be over
          in the next second.*/
        Time_Calc_Throttle(start_time);
        /*set percentage throttling.*/
        if (effective_cpu_speed < 100)
            throttle_time = start_time + effective_cpu_speed * 10;

        nscore = pos_score;
        mate_conf = 0;

        for (d=START_DEPTH; ((d < MAX_DEPTH) && (d <= max_depth) &&
                             ((!g_max_nodes) || (g_nodes < g_max_nodes))); d++)
        { /* Iterative deepening method*/
            const int alpha_full = -INFINITY_;
            const int beta_full  =  INFINITY_;
            int alpha, beta, lmp_active, window_cnt = 0;

            /*set aspiration window.*/
            if (d >= ID_WINDOW_DEPTH)
            {
                alpha = nscore - window_margins[window_cnt];
                if (alpha < alpha_full) alpha = alpha_full;
                beta  = nscore + window_margins[window_cnt++];
                if (beta > beta_full) beta = beta_full;
                lmp_active = 1;
            } else
            {
                /*use full window and no late move pruning at low depth.*/
                alpha = alpha_full;
                beta  = beta_full;
                lmp_active = 0;
            }

            /*widen window until neither fail high nor low.*/
            for (;;)
            {
                nscore = Search_Negascout(0, 1, &line, movelist, move_cnt, d, alpha, beta, colour,
                                          &ret_mv_idx, PV_NODE, in_check, 1, lmp_active);

                /*search with full window should not fail, but
                  just for robustness.*/
                if (((alpha == alpha_full) && (beta == beta_full)) ||
                    ((alpha == alpha_full) && (nscore <= alpha_full)) ||
                    ((beta == beta_full) && (nscore >= beta_full)))
                {
                    break;
                }

                if (time_is_up != TM_NO_TIMEOUT)
                    break;

                if ((g_max_nodes) && (g_nodes >= g_max_nodes))
                    break;

                if (nscore <= alpha) /*fail low*/
                {
                    /*note that a fail low implies ret_mv_idx == -1 because
                      the Negascout initialises the return index to that value
                      and only sets it up when a move raises alpha, which no
                      move does in case of a fail low.*/
                    if (window_cnt < WINDOW_STEPS)
                    {
                        alpha -= window_margins[window_cnt++];
                        if (alpha < alpha_full) alpha = alpha_full;
                    } else
                        alpha = alpha_full;
                } else if (nscore >= beta) /*fail high*/
                {
                    if (window_cnt < WINDOW_STEPS)
                    {
                        beta += window_margins[window_cnt++];
                        if (beta > beta_full) beta = beta_full;
                    } else
                        beta = beta_full;

                    if (ret_mv_idx > 0) /*not already the main PV?*/
                    {
                        MOVE ret_move = movelist[ret_mv_idx];

                        /*found new best move. update ranking and PV immediately to get this
                          move searched before the old PV root move. We may not have time
                          to search both, and this move failed high while the old PV root
                          move didn't.*/
                        Search_Find_Put_To_Top_Root(movelist, root_refutation_table, move_cnt, ret_move);
                        /*update global PV*/
                        GlobalPV.line_cmoves[0] = Mvgen_Compress_Move(ret_move);
                        if (line.line_len > 0)
                        {
                            /*we have a (maybe truncated) PV despite fail high*/
                            memcpy(GlobalPV.line_cmoves + 1, line.line_cmoves, sizeof(CMOVE) * line.line_len);
                            GlobalPV.line_len = line.line_len + 1;
                        } else if (root_refutation_table[0] != MV_NO_MOVE_CMASK)
                        {
                            /*otherwise, the reply move from the opponent move cache
                              failed low for the opponent, like all reply moves, but
                              it is at least somewhat reasonable*/
                            GlobalPV.line_cmoves[1] = root_refutation_table[0];
                            GlobalPV.line_len = 2;
                        } else
                            GlobalPV.line_len = 1; /*not even opponent cache move is available*/
                    }
                } else
                    break;
            }

            time_passed = Time_Passed();
            if (ret_mv_idx >= 0)
            {
                /*retain the PV if possible. this helps the move ordering and is
                  especially useful with PV hits.*/
                int copy_line_pv = 0;
                /*first move of PV changed, or new PV is at least as long?*/
                if ((GlobalPV.line_cmoves[0] != Mvgen_Compress_Move(movelist[ret_mv_idx])) ||
                    (GlobalPV.line_len <= line.line_len + 1))
                {
                    copy_line_pv = 1;
                } else
                {
                    /*something in the new, but shorter PV changed?*/
                    for (i = 0; i < line.line_len; i++)
                    {
                        if (GlobalPV.line_cmoves[i+1] != line.line_cmoves[i])
                        {
                            copy_line_pv = 1;
                            break;
                        }
                    }
                }

                if (copy_line_pv)
                {
                    game_info.valid = EVAL_MOVE;
                    game_info.eval = pos_score = nscore;
                    game_info.depth = d;

                    decomp_move = movelist[ret_mv_idx];
                    GlobalPV.line_cmoves[0] = Mvgen_Compress_Move(decomp_move);
                    memcpy(GlobalPV.line_cmoves + 1, line.line_cmoves, sizeof(CMOVE) * line.line_len);
                    GlobalPV.line_len = line.line_len + 1;
                    Search_Find_Put_To_Top_Root(movelist, root_refutation_table, move_cnt, decomp_move);
                }

                Search_Print_Move_Output(d, pos_score, time_passed, is_normal_time);
                printed_nodes = g_nodes; /*avoid double PV with fixed depth search.*/

                /*with mate score, allow one more iteration for confirmation.*/
                if ((pos_score > MATE_CUTOFF) || (pos_score < -MATE_CUTOFF))
                    mate_conf++;
                else
                    mate_conf = 0;

                if ((((mate_conf >= 2U) || (move_cnt < 2)) && (exact_time == 0))
                    || (time_is_up != TM_NO_TIMEOUT))
                    break;
            }
            /*if the pre-sorting has shown an outstanding move, and this move
              is still at the head of the PV, then just do it. Saves time and
              prevents the opponent from having a certain ponder hit in case he
              is using pondering.
              if we have an easy move that also is a PV hit, then the PV is
              retained if it is longer than the "easy depth", which is the
              desired effect.*/
            if ((score_drop >= EASY_THRESHOLD) && (d >= easy_depth) &&
                (failsafe_cmove == GlobalPV.line_cmoves[0]))
            {
                if (uci_debug) Play_Print("info string debug: quick reply, easy move detected.\n");
                break;
            }

            if (time_passed > reduced_move_time) /*more than 55% already used except with exact time*/
            {
                time_is_up = TM_TIMEOUT;
                break;
            }
        } /*for (d=...) end of Iterative Deepening loop*/

        time_passed = Time_Passed();

        if (printed_nodes < g_nodes) /*avoid double PV with fixed depth search.*/
            Search_Print_Move_Output(game_info.depth, game_info.eval, time_passed, is_normal_time);

        if (pos_score < -dynamic_resign_threshold) /*does not happen in UCI mode*/
        {
            /*computer resigns, but still return the move found in case the player wants to play it out*/
            *answer_move = Mvgen_Decompress_Move(GlobalPV.line_cmoves[0]);
            if ((is_analysis) && (time_is_up != TM_ABORT))
            /*timeout is also caused by abort command*/
            {
                Time_Wait_For_Abort();
                time_passed = Time_Passed();
                Search_Print_Move_Output(game_info.depth, game_info.eval, time_passed, is_normal_time);
            }
            return(COMP_RESIGN);
        }
    }
#ifdef DBGCUTOFF
    printf("info string cutoff %u\n", (unsigned)((cutoffs_on_1st_move * 1000U) / total_cutoffs));
#endif

    *answer_move = Mvgen_Decompress_Move(GlobalPV.line_cmoves[0]);
    if ((is_analysis) && (time_is_up != TM_ABORT))
    /*timeout is also caused by abort command*/
    {
        Time_Wait_For_Abort();
        time_passed = Time_Passed();
        Search_Print_Move_Output(game_info.depth, game_info.eval, time_passed, is_normal_time);
    }
    *spent_nodes = g_nodes;
    *spent_time = time_passed;
    return(COMP_MOVE_FOUND);
}
