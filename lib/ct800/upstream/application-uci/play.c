/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2015-2023, Rasmus Althoff <info@ct800.net>
 *  Copyright (C) 2010-2014, George Georgopoulos
 *
 *  This file is part of CT800/NGPlay.
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

/* ------------------------------------------------------ */
/*                      NGplay v 9.86                     */
/*    A C/C++ XBoard NegaScout Alpha/Beta Chess Engine    */
/* Code compiles using GNU g++ under Windows/Linux/Unix   */
/*                                                        */
/*  Features : NegaScout search, +LMR, +futility pruning  */
/*             iterative deepening, Null move heuristic,  */
/*             piece mobility + king safety evaluation,   */
/*             isolated + passed pawn evaluation,         */
/*             Basic Endgames (KQ,KR,KBB,KBN vs K)        */
/*                                                        */
/*         Author: George Georgopoulos (c) 2010-2014      */
/*         You are free to copy/derive from this code     */
/*         as long as you mention this copyright          */
/*         and keep the new code open source              */
/*                                                        */
/*    I should give credit to Tom Kerrigan and his TSCP   */
/* for inspiration about chess programming. Especially    */
/* his code for xboard interface and book opening helped  */
/* me a lot. However the main engine algorithm (search,   */
/* move generation and evaluation) was written completely */
/* from scratch.                                          */
/*    Also credit should be given to Bruce Moreland for   */
/* his excellent site which helped me a lot writing the   */
/* Principal Variation collection code.  thanks G.G.      */
/*                                                        */
/*                                                        */
/*                                                        */
/*                      NGplay v 9.87                     */
/* Few chesswise changes - cleaned up the C code so that  */
/* not just g++, but also gcc and any other ANSI C        */
/* compiler will take the code:                           */
/* - Removed default for the level variable in function   */
/*   headers as C (unlike C++) dows not allow parameter   */
/*   defaults for omitted parameters; instead the calling */
/*   functions use the new define NO_LEVEL which is just  */
/*   -1 and thus the former default value.                */
/* - replaced C++ // style comments by C comments.        */
/* - changed a lot of data types for the globals as to    */
/*   cut down the memory footprint for embedded systems.  */
/* - clarified the long decimal constants in the random   */
/*   function to be unsigned.                             */
/*                                                        */
/* => result: gcc does not complain even when using       */
/*    "gcc -Wall". The code should me more easily to      */
/*     port now, especially for embedded systems with     */
/*     picky compilers.                                   */
/*                                                        */
/* Changed a lot of C structures, defines, fixed some     */
/* bugs with the hash tables and the 3-fold repetition    */
/* recognition, replaced the quicksort by shellsort for   */
/* saving stack memory.                                   */
/* Moved the whole thing to a modest memory               */
/* footprint.                                             */
/* Used explicit NOINLINE when the function has a         */
/* considerable stack usage - inlining would add that up. */
/* Just to be sure that nothing breaks with new compiler  */
/* versions and different inlining behaviour.             */
/*                                                        */
/* Rewrote the opening book handling from line based to   */
/* position based (CRC32), from ASCII based to binary and */
/* made the opening book an inline compiled header file.  */
/*                                                        */
/* Massively enlarged the opening book (by more than a    */
/* factor of ten).                                        */
/*                                                        */
/* enhanced the rook handling (open files), added some    */
/* knight vs. bishop logic, added differently coloured    */
/* bishops logic, reduced the importance of queen         */
/* mobility during the opening, enhanced the king safety  */
/* evaluation, added some endgames (among them KP-K via   */
/* an endgame lookup table using free code from           */
/* Marcel van Kervinck), added backward pawn recognition, */
/* double pawn recognition, devalued wing pawn            */
/* majorities.                                            */
/*                                                        */
/* Thrown out the xboard code.                            */
/*                                                        */
/* Added 50-moves-draw recognition in the search tree if  */
/* a possible draw by that rule is only about 10 plies    */
/* from the board position.                               */
/* Added time controls.                                   */
/* Compeletely new handling of various options.           */
/* Added an embedded position viewer and editor.          */
/* Ported the whole thing to an ARM MCU.                  */
/*                                                        */
/* Author of these changes: Rasmus Althoff, 2016-2019.    */
/*                                                        */
/* ------------------------------------------------------ */

#if defined(_WIN64) || defined(_WIN32)
    #define CTWIN
    #ifdef _WIN64
        #define TARGET_BUILD_STRING "64"
    #else
        #define TARGET_BUILD_STRING "32"
    #endif
#else
    #if defined(__LP64__) || defined(_LP64)
        #define TARGET_BUILD_STRING "64"
    #else
        #define TARGET_BUILD_STRING "32"
    #endif
#endif

#ifdef CTWIN
#include <windows.h>
#include <process.h>
#else
/*for getting the Posix threads, clock_gettime and nanosleep functions with C99.*/
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#endif

#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "ctdefs.h"
#include "util.h"
#include "move_gen.h"
#include "book.h"
#include "hashtables.h"
#include "eval.h"
#include "search.h"

#ifdef AUTOTUNE
void Tune_Run(int iterations, int step, const char *filename, int use_qs);
void Tune_Dump_Params(void);
#endif

/*--------- global variables ------------*/

GAME_INFO game_info;

/* ------------- MAKE MOVE DEFINITIONS ----------------*/

/*bits 0-8: for gflags
bits 9-15: for en passant square
+1 because MAX_STACK is odd, and we can keep the alignment here*/
uint16_t cstack[MAX_STACK+1];
int cst_p;

MVST move_stack[MAX_STACK+1];

/* ---------- TRANSPOSITION TABLE DEFINITIONS ------------- */
size_t MAX_TT;

TT_ST *T_T = NULL;
TT_ST *Opp_T_T = NULL;

/*pawn hash table*/
TT_PTT_ST P_T_T[PMAX_TT+1];

/*separate table to avoid padding of the P_T_T table.*/
TT_PTT_ROOK_ST P_T_T_Rooks[PMAX_TT+1];

unsigned int hash_clear_counter;

/* -------------------- GLOBALS ------------------------- */

PIECE Wpieces[16];
PIECE Bpieces[16];
PIECE empty_p = {NULL, NULL,  0, 0, 0};
PIECE fence_p = {NULL, NULL, -1,-1,-1};

PIECE *board[120] = {
    &fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,
    &fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,
    &fence_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&fence_p,
    &fence_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&fence_p,
    &fence_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&fence_p,
    &fence_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&fence_p,
    &fence_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&fence_p,
    &fence_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&fence_p,
    &fence_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&fence_p,
    &fence_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&empty_p,&fence_p,
    &fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,
    &fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p,&fence_p
};

const int8_t boardXY[120] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7,-1,
    -1, 8, 9,10,11,12,13,14,15,-1,
    -1,16,17,18,19,20,21,22,23,-1,
    -1,24,25,26,27,28,29,30,31,-1,
    -1,32,33,34,35,36,37,38,39,-1,
    -1,40,41,42,43,44,45,46,47,-1,
    -1,48,49,50,51,52,53,54,55,-1,
    -1,56,57,58,59,60,61,62,63,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

const int8_t board64[64] = {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8
};

const int8_t RowNum[120] = {
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,1,1,1,1,1,1,1,1,0,
    0,2,2,2,2,2,2,2,2,0,
    0,3,3,3,3,3,3,3,3,0,
    0,4,4,4,4,4,4,4,4,0,
    0,5,5,5,5,5,5,5,5,0,
    0,6,6,6,6,6,6,6,6,0,
    0,7,7,7,7,7,7,7,7,0,
    0,8,8,8,8,8,8,8,8,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0
};

const int8_t ColNum[120] = {
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,1,2,3,4,5,6,7,8,0,
    0,1,2,3,4,5,6,7,8,0,
    0,1,2,3,4,5,6,7,8,0,
    0,1,2,3,4,5,6,7,8,0,
    0,1,2,3,4,5,6,7,8,0,
    0,1,2,3,4,5,6,7,8,0,
    0,1,2,3,4,5,6,7,8,0,
    0,1,2,3,4,5,6,7,8,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0
};

/*for move masking except MVV/LVA value*/
const MOVE mv_move_mask = {{0xFFU, 0xFFU, 0xFFU, 0}};

int wking, bking;
int en_passant_sq;
unsigned int gflags; /*bit masks, see ctdefs.h*/

int mv_stack_p;

/********** set UCI options **********/
/*activate debug output, for both threads*/
volatile unsigned int uci_debug;
/*internal book on/off*/
unsigned int disable_book;
/*static eval blurring in middle game*/
int32_t eval_noise;
/*curr move update mode*/
enum E_CURRMOVE show_currmove;
/*contempt settings*/
int contempt_val;
int contempt_end;
static int elo_max;
/********** end UCI options **********/

uint64_t g_nodes, g_max_nodes;
int Starting_Mv;
enum E_COLOUR computer_side;
int32_t start_moves;
int fifty_moves;
static int start_fifty_moves;
int game_started_from_0;

LINE GlobalPV;
static MOVE player_move;

int dynamic_resign_threshold; /*not used in UCI version*/

static unsigned int abort_calc;
static unsigned int cmd_read_idx, cmd_write_idx;
static char cmd_buf[CMD_BUF_SIZE + 32L];

/*flag for disabling output during the startup speed calibration*/
static unsigned int no_output;

/*trackers for the perft test with intermediate abort check.*/
static int64_t perft_start_time;
static uint64_t perft_nodes;
static uint64_t perft_check_nodes;
static uint64_t perft_nps_10ms;
static int abort_perft;
static int perft_depth;

#ifdef CTWIN
    /*high resolution time acquisition under Windows*/
    static LARGE_INTEGER clock_ticks_start, clock_ticks_freq;
#else
    /*under POSIX, CLOCK_MONOTONIC is optional.
      cond_clock is for pthread conditions, main_clock for the application
      time tracking. during init in main(), these get set up for use with
      monotonic time. if that fails, these conditions are used with real
      time instead.*/
    #ifdef CLOCK_MONOTONIC
        /*note that ct_cond_clock_mode may fall back to real time.*/
        static const int ct_main_clock_mode = CLOCK_MONOTONIC;
        static int ct_cond_clock_mode = CLOCK_MONOTONIC;
    #else
        static const int ct_main_clock_mode = CLOCK_REALTIME;
        static const int ct_cond_clock_mode = CLOCK_REALTIME;
    #endif
#endif

/*all the locking is done only in this file, using wrapper functions.*/
#ifdef CTWIN
    static CRITICAL_SECTION io_lock;
    static CRITICAL_SECTION print_lock;
    static CRITICAL_SECTION abort_check_lock;
    static HANDLE uci_event;
    static HANDLE cmd_work_event;
    static HANDLE abort_event;
    static HANDLE abort_event_conf;
    /*map the locks to Windows functions*/
    #define Play_Acquire_Lock(x) EnterCriticalSection(x)
    #define Play_Release_Lock(x) LeaveCriticalSection(x)
#else
    static pthread_mutex_t io_lock    = PTHREAD_MUTEX_INITIALIZER;
    static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;
    static pthread_mutex_t abort_check_lock = PTHREAD_MUTEX_INITIALIZER;

    static pthread_mutex_t uci_lock = PTHREAD_MUTEX_INITIALIZER;
    static pthread_cond_t  uci_cond = PTHREAD_COND_INITIALIZER;
    static volatile int uci_event   = 0;

    static pthread_mutex_t cmd_work_lock = PTHREAD_MUTEX_INITIALIZER;
    static pthread_cond_t  cmd_work_cond = PTHREAD_COND_INITIALIZER;
    static volatile int cmd_work_event   = 0;

    static pthread_mutex_t abort_event_lock = PTHREAD_MUTEX_INITIALIZER;
    static pthread_cond_t  abort_event_cond = PTHREAD_COND_INITIALIZER;
    static volatile int abort_event         = 0;

    static pthread_mutex_t abort_event_conf_lock = PTHREAD_MUTEX_INITIALIZER;
    static pthread_cond_t  abort_event_conf_cond = PTHREAD_COND_INITIALIZER;
    static volatile int abort_event_conf         = 0;

    /*map the locks to pthread functions*/
    #define Play_Acquire_Lock(x) (void) pthread_mutex_lock(x)
    #define Play_Release_Lock(x) (void) pthread_mutex_unlock(x)
#endif

/* -------------- UTILITY FUNCTIONS ---------------------------------- */

#ifdef CTWIN
#else
/*adds two times in timespec format and takes care of potential overflow.*/
static void Play_Timespec_Add(struct timespec* dst, const struct timespec* src1, const struct timespec* src2)
{
    time_t sec = ((time_t) src1->tv_sec) + ((time_t) src2->tv_sec);
    int64_t nsec  = ((int64_t) src1->tv_nsec) + ((int64_t) src2->tv_nsec);

    sec += nsec / 1000000000;
    nsec %= 1000000000;

    dst->tv_sec = sec;
    dst->tv_nsec = (long) nsec;
}
#endif

/*init time acquisition*/
static void Play_Init_Millisecs(void)
{
#ifdef CTWIN
    /*QueryPerformanceFrequency doesn't change after Windows has booted*/
    QueryPerformanceFrequency(&clock_ticks_freq);
    QueryPerformanceCounter(&clock_ticks_start);
#endif
}

/*gets the absolute timestamp in milliseconds.
  Windows: since Play_Init_Millisecs() call
  Other:   since Epoch*/
int64_t Play_Get_Millisecs(void)
{
#ifdef CTWIN
    LARGE_INTEGER clock_ticks_now, clock_ticks_elapsed, millisecs_elapsed;

    QueryPerformanceCounter(&clock_ticks_now);
    clock_ticks_elapsed.QuadPart = clock_ticks_now.QuadPart - clock_ticks_start.QuadPart;

    /*ticks to milliseconds:
      the tick counter won't overflow for a 100 years, but the milliseconds
      calculation might overflow if multiplying the tick difference by 1000
      and directly dividing by the clock frequency. That would require
      having the application open for 0.1 years, i.e. 36.5 days, which
      might be feasible.*/

    /*whole seconds*/
    millisecs_elapsed.QuadPart = clock_ticks_elapsed.QuadPart / clock_ticks_freq.QuadPart;
    /*fractional seconds in ticks*/
    clock_ticks_elapsed.QuadPart %= clock_ticks_freq.QuadPart;
    /*whole seconds to milliseconds*/
    millisecs_elapsed.QuadPart *= 1000;
    /*add fractional seconds in milliseconds, rounding downwards*/
    millisecs_elapsed.QuadPart += (clock_ticks_elapsed.QuadPart * 1000) / clock_ticks_freq.QuadPart;
    return((int64_t)millisecs_elapsed.QuadPart);
#else
    struct timespec timebuffer;
    clock_gettime(ct_main_clock_mode, &timebuffer);
    return (((int64_t) timebuffer.tv_sec)*1000 + (int64_t) (timebuffer.tv_nsec / 1000000));
#endif
}

/*only used in buffer contention cases that do not really happen.*/
static void Play_Sleep(int32_t millisecs)
{
#ifdef CTWIN
    Sleep(millisecs);
#else
    struct timespec time_sleep, remain_sleep;

    memset(&time_sleep, 0, sizeof(time_sleep));
    time_sleep.tv_sec  = millisecs / 1000L;
    time_sleep.tv_nsec = (millisecs % 1000L) * 1000000L;

    nanosleep(&time_sleep, &remain_sleep);
#endif
}

/*pauses the UCI processing (worker) thread when waiting for UCI input
  from the input thread. In engine "idle" state, the input thread is
  blocking on stdin, and the UCI thread is blocking on the event. This
  way, the engine does not prevent system-wide energy saving modes by
  constantly waking up.*/
static void Play_Pause_UCI(void)
{
#ifdef CTWIN
    WaitForSingleObject(uci_event, INFINITE);
#else
    (void) pthread_mutex_lock(&uci_lock);
    while (!uci_event)
        (void) pthread_cond_wait(&uci_cond, &uci_lock);
    uci_event = 0;
    (void) pthread_mutex_unlock(&uci_lock);
#endif
}

/*the input thread wakes up the UCI thread when there is work.*/
static void Play_Wakeup_UCI(void)
{
#ifdef CTWIN
    SetEvent(uci_event);
#else
    (void) pthread_mutex_lock(&uci_lock);
    uci_event = 1;
    (void) pthread_cond_signal(&uci_cond);
    (void) pthread_mutex_unlock(&uci_lock);
#endif
}


/*sets the "abort calculations" flag associated with the "stop" and "quit"
  UCI commands.*/
static void Play_Set_Abort(unsigned int new_state)
{
    Play_Acquire_Lock(&abort_check_lock);
    abort_calc = new_state;
    Play_Release_Lock(&abort_check_lock);
}

/*retrieves the "abort calculations" flag associated with the "stop" and
  "quit" UCI commands.*/
unsigned int Play_Get_Abort(void)
{
    unsigned int current_state;
    Play_Acquire_Lock(&abort_check_lock);
    current_state = abort_calc;
    Play_Release_Lock(&abort_check_lock);
    return(current_state);
}

/*the following functions handle the event driven infrastructure of the
  abort flag.*/
/*used in throttled mode or when waiting with infinite move time.*/
void Play_Wait_For_Abort_Event(int32_t millisecs)
{
#ifdef CTWIN
    WaitForSingleObject(abort_event, (DWORD) millisecs);
#else
    struct timespec abs_time;

    if (millisecs > 0) /*zero timeout is for resetting the predicate*/
    {
        /*the cond wait takes absolute time, not an interval. the potential
          wait for the mutex already counts in our timeout, so get the absolute
          target time before trying to get the mutex.*/
        struct timespec delay_time, now_time;

        clock_gettime(ct_cond_clock_mode, &now_time);
        delay_time.tv_sec = millisecs / 1000L;
        delay_time.tv_nsec = (millisecs % 1000L) * 1000000L;
        /*deal with potential overflow when adding the delay*/
        Play_Timespec_Add(&abs_time, &now_time, &delay_time);
    }

    (void) pthread_mutex_lock(&abort_event_lock);
    while ((!abort_event) && (millisecs > 0))
    {
        if (ETIMEDOUT == pthread_cond_timedwait(&abort_event_cond, &abort_event_lock, &abs_time))
            break;
    }
    abort_event = 0;
    (void) pthread_mutex_unlock(&abort_event_lock);
#endif
}

/*sets the abort flag, which is polled in normal search, and the event,
  which is used for waiting in throttled mode and when waiting with infinite
  move time.*/
static void Play_Set_Abort_Event(void)
{
    Play_Set_Abort(1U);
#ifdef CTWIN
    SetEvent(abort_event);
#else
    (void) pthread_mutex_lock(&abort_event_lock);
    abort_event = 1;
    (void) pthread_cond_signal(&abort_event_cond);
    (void) pthread_mutex_unlock(&abort_event_lock);
#endif
}

/*after issueing the abort flag to the search thread, the input thread
  waits for the UCI thread to process the stop/quit command.*/
static void Play_Wait_For_Abort_Event_Confirmation(int32_t millisecs)
{
#ifdef CTWIN
    WaitForSingleObject(abort_event_conf, (DWORD) millisecs);
#else
    struct timespec abs_time;

    if (millisecs > 0) /*zero timeout is for resetting the predicate*/
    {
        /*the cond wait takes absolute time, not an interval. the potential
          wait for the mutex already counts in our timeout, so get the absolute
          target time before trying to get the mutex.*/
        struct timespec delay_time, now_time;

        clock_gettime(ct_cond_clock_mode, &now_time);
        delay_time.tv_sec = millisecs / 1000L;
        delay_time.tv_nsec = (millisecs % 1000L) * 1000000L;
        /*deal with potential overflow when adding the delay*/
        Play_Timespec_Add(&abs_time, &now_time, &delay_time);
    }

    (void) pthread_mutex_lock(&abort_event_conf_lock);
    while ((!abort_event_conf) && (millisecs > 0))
    {
        if (ETIMEDOUT == pthread_cond_timedwait(&abort_event_conf_cond, &abort_event_conf_lock, &abs_time))
            break;
    }
    abort_event_conf = 0;
    (void) pthread_mutex_unlock(&abort_event_conf_lock);
#endif
}

/*after having seen a stop/quit command, the UCI thread has to acknowledge
  that is has processed the command. otherwise, repeated stop/go might lead
  to lost stop commands.*/
static void Play_Set_Abort_Event_Confirmation(void)
{
    Play_Set_Abort(0);
#ifdef CTWIN
    SetEvent(abort_event_conf);
#else
    (void) pthread_mutex_lock(&abort_event_conf_lock);
    abort_event_conf = 1;
    (void) pthread_cond_signal(&abort_event_conf_cond);
    (void) pthread_mutex_unlock(&abort_event_conf_lock);
#endif
}

/*after issueing work-involving commands (ucinewgame and hash size setting)
  to the search thread, the input thread waits for the UCI thread to process
  the command.
  Otherwise, a following "isready" by the GUI would be answered immediately
  although the search thread is still doing work, e.g. clearing the hash tables.
  This can easily cost a 100 ms at the beginning of the game, which is a lot
  in extreme bullet games of 1 second per game.*/
static void Play_Wait_For_Cmd_Work(int32_t millisecs)
{
#ifdef CTWIN
    WaitForSingleObject(cmd_work_event, (DWORD) millisecs);
#else
    struct timespec abs_time;

    if (millisecs > 0) /*zero timeout is for resetting the predicate*/
    {
        /*the cond wait takes absolute time, not an interval. the potential
          wait for the mutex already counts in our timeout, so get the absolute
          target time before trying to get the mutex.*/
        struct timespec delay_time, now_time;

        clock_gettime(ct_cond_clock_mode, &now_time);
        delay_time.tv_sec = millisecs / 1000L;
        delay_time.tv_nsec = (millisecs % 1000L) * 1000000L;
        /*deal with potential overflow when adding the delay*/
        Play_Timespec_Add(&abs_time, &now_time, &delay_time);
    }

    (void) pthread_mutex_lock(&cmd_work_lock);
    while ((!cmd_work_event) && (millisecs > 0))
    {
        if (ETIMEDOUT == pthread_cond_timedwait(&cmd_work_cond, &cmd_work_lock, &abs_time))
            break;
    }
    cmd_work_event = 0;
    (void) pthread_mutex_unlock(&cmd_work_lock);
#endif
}

/*after having processed a command that requires work,
  the worker thread signals completion.*/
static void Play_Set_Cmd_Work(void)
{
#ifdef CTWIN
    SetEvent(cmd_work_event);
#else
    (void) pthread_mutex_lock(&cmd_work_lock);
    cmd_work_event = 1;
    (void) pthread_cond_signal(&cmd_work_cond);
    (void) pthread_mutex_unlock(&cmd_work_lock);
#endif
}

/*actually, the C stdio functions like fgets and fputs could have been used,
  but there's already a whole lot of buffering going on in the engine.
  plus that it's easier to test when the buffering scheme is the same on
  all platforms.*/

/*prints a null terminated string.*/
static void Play_Print_Output(const char *str)
{
    size_t len = strlen(str);
    while (len > 0)
    {
        ssize_t bytes_written;
        int get_error;

        do {
            bytes_written = write(STDOUT_FILENO, str, len);
            get_error = errno;
#if defined (EAGAIN) && defined (EWOULDBLOCK)
#if (EAGAIN != EWOULDBLOCK) /*avoid compiler warning if these are equal*/
            if (get_error == EWOULDBLOCK)
                get_error = EAGAIN;
#endif
#endif
            /*actually, EAGAIN and EWOULDBLOCK are not supposed to happen
              because stdin/out are set to blocking mode, but if that has
              failed somehow, then this is covered here.

              busy wating here in this case because output is also done
              from the search which should not be put to sleep because
              of output issues. And the input thread with readyok should
              neither be forced to sleep because printing is locked via
              mutex so that this could impact the search output, too.*/
        } while ((bytes_written < 0) && ((get_error == EINTR) || (get_error == EAGAIN)));

        if (bytes_written < 0) /*end of stream*/
        {
            /*the engine design is that output will just be cancelled,
              but the input thread will notice that the input pipe has
              also gone if the GUI has exited/crashed without issueing
              the "quit" command. then the input thread will fake a
              "quit" to the main thread and exit.*/
            return;
        }
        if (((size_t) bytes_written) > len) /*should not happen*/
            bytes_written = (ssize_t) len;
        len -= (size_t) bytes_written; /*might not be all at once*/
        str += (size_t) bytes_written;
    }
}

/*the print function needs to get serialised because the UCI input thread
  will answer "isready" directly with "readyok". This answer might get
  garbled with the print output from the main/search thread, so some
  locking is useful here.*/
void Play_Print(const char *str)
{
    /*the startup phase does the machine speed calibration for the Elo
      setting via UCI, but there must not be search output during that phase.*/
    if (no_output)
        return;

    Play_Acquire_Lock(&print_lock);
    Play_Print_Output(str);
    Play_Release_Lock(&print_lock);
}

/*gets a character directly from the stdin file number while using
  a 2 kB buffer for avoiding hundreds of read-calls for one command.
  the UCI "position" command with the move list can become really long.*/
static int Play_Get_Char(char *ch_ptr)
{
    static char stdin_buf[STDIN_BUF_SIZE];
    static size_t stdin_buf_read_idx, stdin_buf_write_idx;
    int res = 0;
    for (;;)
    {
        if (stdin_buf_read_idx == stdin_buf_write_idx)
        /*buffer empty? then refill. note that this is not a ring buffer.*/
        {
            ssize_t bytes_read;
            int get_error;

            do {
                bytes_read = read(STDIN_FILENO, (void *) stdin_buf, STDIN_BUF_SIZE);
                get_error = errno;
#if defined (EAGAIN) && defined (EWOULDBLOCK)
#if (EAGAIN != EWOULDBLOCK) /*avoid compiler warning if these are equal*/
                if (get_error == EWOULDBLOCK)
                    get_error = EAGAIN;
#endif
#endif
                /*actually, EAGAIN and EWOULDBLOCK are not supposed to happen
                  because stdin/out are set to blocking mode, but if that has
                  failed somehow, then this is covered up here in order not to
                  drain batteries.*/
                if ((bytes_read < 0) && (get_error == EAGAIN))
                    Play_Sleep(10);
            } while ((bytes_read < 0) && ((get_error == EINTR) || (get_error == EAGAIN)));

            /*bytes_read<0 is obviously the end, but for a blocking stream, and
              bytes_read==0 can only happen if EOF is reached, and that means
              there is nothing more to read. even if stdin is non-blocking somehow,
              that would stay in the read loop because EGAIN/EWOULDBLOCK are
              caught.*/
            if (bytes_read <= 0)
            {
                res = -1;
                break;
            }
            stdin_buf_read_idx = 0;
            stdin_buf_write_idx = (size_t) bytes_read;
        }

        if (stdin_buf_read_idx < stdin_buf_write_idx)
        /*something in the buffer to read?*/
        {
            *ch_ptr = stdin_buf[stdin_buf_read_idx];
            stdin_buf_read_idx++;
            res = 1;
            break;
        }
    }
    return (res);
}

/*replacement for fgets(), but also discards the rest of too long lines,
  converts tabs to spaces and eliminates consecutive, leading and trailing
  whitespace. it also discards the trailing newline.
  this does not need locks because it is not reading into the shared ring
  buffer.*/
int Play_Read_Input(char *buf, int buf_len)
{
    int read_idx = 0;
    char ch, last_char = ' ';

    /*sanity check*/
    if (buf_len <= 1)
    {
        if (buf_len == 1)
            buf[0] = '\0';
        return(0);
    }
    buf_len--; /*room for null termination*/

    for (;;)
    {
        int get_status = Play_Get_Char(&ch);
        if (get_status < 0) /*read error*/
            return(-1);
        if (get_status == 0) /*nothing read?!*/
            continue;

        if (ch == '\t') ch = ' '; /*map tabs to spaces*/
        if ((ch == ' ') && (last_char == ' '))
            continue; /*filter out multiple whitespace*/

        /*\r gets mapped to \n because UCI specifies any combination of \r
          and \n as valid line ends, and that includes \r only. the downside
          is that with \r\n, an empty command will be recognised, but that
          will be filtered out anyway before transferring it into the ring
          buffer.
          not sure how a null character in text input could happen, but just
          in case.*/
        if ((ch == '\r') || (ch == '\0'))
            ch = '\n';

        if (ch == '\n') /*enter*/
        {
            if ((read_idx > 0) && (buf[read_idx - 1] == ' '))
                read_idx--; /*remove trailing space*/
            buf[read_idx] = '\0';
            return(read_idx);
        }
        if (read_idx >= buf_len)
            break;
        last_char = ch;
        buf[read_idx] = ch;
        read_idx++;
    }
    /*input line longer than buffer: discard rest*/
    buf[read_idx] = '\0';
    /*now eat up the remaining characters of the line*/
    do {
        int get_status = Play_Get_Char(&ch);
        if (get_status < 0) /*read error*/
            return(-1);
        if (get_status == 0) /*nothing read?!*/
            ch = '\0';
        else if ((ch == '\r') || (ch == '\0'))
            ch = '\n';
    } while (ch != '\n');
    return(read_idx);
}

/*default values:
  12 Bytes per TT entry, and there are two TTs. (MAX_TT)
  8 Bytes per PTT entry, and there is one PTT. (PMAX_TT)
  2 Bytes per rook TT entry, and there is one rook TT. (PMAX_TT)

  The pawn hash table size is fixed with 16k entries and takes
  up 10 bytes per entry, i.e. 160 kB. Still has about 95 % hit rate,
  so increasing that isn't useful.

  The default hash table size is 848 kB (counting both hash tables)*/
static int Play_Set_Hashtables(size_t hash_size)
{
    size_t multiplier, pawn_hash_size, table_size_default, tt_size;
    static size_t last_hash_size = 0;

    if (hash_size < HASH_MIN)
        hash_size = HASH_MIN; /*1 MB minimum*/
    if (hash_size > HASH_MAX)
        hash_size = HASH_MAX; /*1 GB maximum*/

    hash_size *= 1024; /*now in kilobytes*/

    pawn_hash_size = ((sizeof(TT_PTT_ROOK_ST) + sizeof(TT_PTT_ST)) * (PMAX_TT+1)) / 1024;
    table_size_default = (sizeof(TT_ST) * 2 * DEF_MAX_TT) / 1024;

    for (multiplier = 1; 2 * multiplier * table_size_default + pawn_hash_size <= hash_size; multiplier *= 2)
    {
        ;
    }

    MAX_TT  = DEF_MAX_TT * multiplier;
    MAX_TT--;

    /*clear pawn hash tables: probably also intended when setting the hash size.*/
    memset(P_T_T,       0, (PMAX_TT+1)*sizeof(TT_PTT_ST));
    memset(P_T_T_Rooks, 0, (PMAX_TT+1)*sizeof(TT_PTT_ROOK_ST));

    /*freeing and allocating the same hash size doesn't make sense.*/
    if ((last_hash_size == MAX_TT) && (T_T != NULL) && (Opp_T_T != NULL))
    {
        Hash_Clear_Tables();
        if (uci_debug) Play_Print("info string debug: redundant hash size setting, hash cleared.\n");
        return(0);
    }

    /*free existing hash tables.*/
    if (T_T     != NULL) free(T_T);
    if (Opp_T_T != NULL) free(Opp_T_T);

    T_T     = (TT_ST *) calloc(MAX_TT + CLUSTER_SIZE, sizeof(TT_ST));
    Opp_T_T = (TT_ST *) calloc(MAX_TT + CLUSTER_SIZE, sizeof(TT_ST));

    if ((T_T == NULL) || (Opp_T_T == NULL))
    {
        if (T_T     != NULL) free(T_T);
        if (Opp_T_T != NULL) free(Opp_T_T);

        T_T = Opp_T_T = NULL;
        last_hash_size = 0;
        return(1);
    }

    /*force the OS to actually blend in the pages.*/
    tt_size = MAX_TT + CLUSTER_SIZE;
    tt_size *= sizeof(TT_ST);

    memset(T_T,     42, tt_size);
    memset(Opp_T_T, 42, tt_size);
    __sync_synchronize();

    memset(T_T,     0, tt_size);
    memset(Opp_T_T, 0, tt_size);
    __sync_synchronize();

    last_hash_size = (size_t) MAX_TT;
    return(0);
}

static void Play_Init_Pieces(void)
{
    int i;

    memset(Wpieces, 0, sizeof(Wpieces));
    memset(Bpieces, 0, sizeof(Bpieces));

    Wpieces[0].type = WKING;
    Wpieces[0].next = &Wpieces[1];
    Wpieces[0].prev = NULL;

    Wpieces[1].type = WQUEEN;
    Wpieces[1].next = &Wpieces[2];
    Wpieces[1].prev = &Wpieces[0];

    Wpieces[2].type = WROOK;
    Wpieces[2].next = &Wpieces[3];
    Wpieces[2].prev = &Wpieces[1];

    Wpieces[3].type = WROOK;
    Wpieces[3].next = &Wpieces[4];
    Wpieces[3].prev = &Wpieces[2];

    Wpieces[4].type = WBISHOP;
    Wpieces[4].next = &Wpieces[5];
    Wpieces[4].prev = &Wpieces[3];

    Wpieces[5].type = WBISHOP;
    Wpieces[5].next = &Wpieces[6];
    Wpieces[5].prev = &Wpieces[4];

    Wpieces[6].type = WKNIGHT;
    Wpieces[6].next = &Wpieces[7];
    Wpieces[6].prev = &Wpieces[5];

    Wpieces[7].type = WKNIGHT;
    Wpieces[7].next = &Wpieces[8];
    Wpieces[7].prev = &Wpieces[6];

    for (i=8; i<16; i++) {
        Wpieces[i].type = WPAWN;
        if (i==15) {
            Wpieces[i].next = NULL;
        } else {
            Wpieces[i].next = &Wpieces[i+1];
        }
        Wpieces[i].prev = &Wpieces[i-1];
    }

    Bpieces[0].type = BKING;
    Bpieces[0].next = &Bpieces[1];
    Bpieces[0].prev = NULL;

    Bpieces[1].type = BQUEEN;
    Bpieces[1].next = &Bpieces[2];
    Bpieces[1].prev = &Bpieces[0];

    Bpieces[2].type = BROOK;
    Bpieces[2].next = &Bpieces[3];
    Bpieces[2].prev = &Bpieces[1];

    Bpieces[3].type = BROOK;
    Bpieces[3].next = &Bpieces[4];
    Bpieces[3].prev = &Bpieces[2];

    Bpieces[4].type = BBISHOP;
    Bpieces[4].next = &Bpieces[5];
    Bpieces[4].prev = &Bpieces[3];

    Bpieces[5].type = BBISHOP;
    Bpieces[5].next = &Bpieces[6];
    Bpieces[5].prev = &Bpieces[4];

    Bpieces[6].type = BKNIGHT;
    Bpieces[6].next = &Bpieces[7];
    Bpieces[6].prev = &Bpieces[5];

    Bpieces[7].type = BKNIGHT;
    Bpieces[7].next = &Bpieces[8];
    Bpieces[7].prev = &Bpieces[6];

    for (i=8; i<16; i++) {
        Bpieces[i].type = BPAWN;
        if (i==15) {
            Bpieces[i].next = NULL;
        } else {
            Bpieces[i].next = &Bpieces[i+1];
        }
        Bpieces[i].prev = &Bpieces[i-1];
    }
}

static void Play_Empty_Board(void)
{
    int i;
    Play_Init_Pieces();
    for (i = 0; i < 64; i++) {
        board[board64[i]]=&empty_p;
    }
}

static void Play_Reset_Position_Status(void)
{
    Hash_Clear_Tables();
    Search_Reset_History();
    memset(P_T_T,       0, (PMAX_TT+1)*sizeof(TT_PTT_ST));
    memset(P_T_T_Rooks, 0, (PMAX_TT+1)*sizeof(TT_PTT_ROOK_ST));
    memset(&GlobalPV, 0, sizeof(GlobalPV));
    hash_clear_counter = 0;
    game_info.valid = EVAL_INVALID;
    game_info.last_valid_eval = NO_RESIGN;
    player_move.u = MV_NO_MOVE_MASK;
}

/* ---------------- TEXT INPUT/OUTPUT ----------------------- */

char *Play_Translate_Moves(MOVE m)
{
    static char mov[6];

    if (m.u != MV_NO_MOVE_MASK)
    {
        char fromx,fromy,tox,toy;

        fromx = m.m.from%10 - 1;
        tox   = m.m.to%10 - 1;
        fromy = m.m.from/10 - 2;
        toy   = m.m.to/10 - 2;

        mov[0] = fromx + 'a';
        mov[1] = fromy + '1';
        mov[2] = tox + 'a';
        mov[3] = toy + '1';

        switch(m.m.flag)
        {
        case WROOK  :
        case BROOK  :
            mov[4] = BROOK_CHAR;
            mov[5] = '\0';
            break;
        case WKNIGHT:
        case BKNIGHT:
            mov[4] = BKNIGHT_CHAR;
            mov[5] = '\0';
            break;
        case WBISHOP:
        case BBISHOP:
            mov[4] = BBISHOP_CHAR;
            mov[5] = '\0';
            break;
        case WQUEEN :
        case BQUEEN :
            mov[4] = BQUEEN_CHAR;
            mov[5] = '\0';
            break;
        default     :
            mov[4] = '\0';
            break;
        }
    } else
        strcpy(mov, "0000");
    return mov;
}

/* ----------------- MAIN FUNCTIONS ------------------------------------- */

static void Play_Set_Starting_Position(void)
{
    int i;
    Play_Empty_Board();

    Eval_Zero_Initial_Material();

    board[A1]=&Wpieces[2];
    Wpieces[2].xy = A1;
    board[H1]=&Wpieces[3];
    Wpieces[3].xy = H1; /* WROOKS */
    board[A8]=&Bpieces[2];
    Bpieces[2].xy = A8;
    board[H8]=&Bpieces[3];
    Bpieces[3].xy = H8; /* BROOKS */
    board[G1]=&Wpieces[6];
    Wpieces[6].xy = G1;
    board[B1]=&Wpieces[7];
    Wpieces[7].xy = B1; /* WKNIGHTS */
    board[G8]=&Bpieces[6];
    Bpieces[6].xy = G8;
    board[B8]=&Bpieces[7];
    Bpieces[7].xy = B8; /* BKNIGHTS */
    board[F1]=&Wpieces[4];
    Wpieces[4].xy = F1;
    board[C1]=&Wpieces[5];
    Wpieces[5].xy = C1; /* WBISHOPS */
    board[F8]=&Bpieces[4];
    Bpieces[4].xy = F8;
    board[C8]=&Bpieces[5];
    Bpieces[5].xy = C8; /* BBISHOPS */
    board[D1]=&Wpieces[1];
    Wpieces[1].xy=D1; /* WQUEEN */
    board[D8]=&Bpieces[1];
    Bpieces[1].xy=D8; /* BQUEEN */
    board[E1]=&Wpieces[0];
    Wpieces[0].xy = E1; /* WKING */
    board[E8]=&Bpieces[0];
    Bpieces[0].xy = E8; /* BKING */
    for (i = 0; i<8; i++) {
        board[i+A2] = &Wpieces[i+8];
        Wpieces[i+8].xy = i+A2; /* WPAWNS */
        board[i+A7] = &Bpieces[i+8];
        Bpieces[i+8].xy = i+A7; /* BPAWNS */
    }

    /*if white starts, then the (imaginary) move in move_stack[0] would
      have been a black move. reset all other flags.*/
    gflags = BLACK_MOVED;

    en_passant_sq = 0;
    wking = E1;
    bking = E8;
    /*Move Stack Pointers reset */
    cst_p = 0;
    mv_stack_p = 0;
    /*zero out the stacks themselves. Not strictly necessary, but in case
      of problems, it is impossible to use bug reports which might depend on
      the last umpteen games played.*/
    memset(move_stack, 0, sizeof(move_stack));
    memset(cstack, 0, sizeof(cstack));
    start_moves = 0;
    start_fifty_moves = 0;
    fifty_moves = start_fifty_moves;
    game_started_from_0 = 1;
    dynamic_resign_threshold = RESIGN_EVAL;
    Hash_Init_Stack();
}


/* ------------------- GAME CONSOLE ----------------------------------- */

/*update the 50 moves counter*/
static void Play_Update_Special_Conditions(MOVE amove)
{
    int moving_piece = board[amove.m.from]->type;

    /* pawn move or capture?*/
    if ((moving_piece == WPAWN) ||
        (moving_piece == BPAWN) ||
        (board[amove.m.to]->type > 0))
    {
        fifty_moves = 0;
    } else
        fifty_moves++;
}

static void Play_Update_Fifty_Moves(void)
{
    int i = mv_stack_p;

    fifty_moves = 0;

    while (i > 0) /*the stack entry 0 itself does NOT contain a valid move.*/
    {
        if (!(move_stack[i].captured->type /* capture */ || move_stack[i].move.m.flag>1 /* pawn move */))
            fifty_moves++;
        else /*we just hit the most recent pawn or capture move, so stop the search.*/
            return;
        i--;
    }

    /*we have been searching back right to the starting position, which can
      be an entered one, and no resetting move has been found. So add upp the
      initial 50 move counter, which can also be an entered one.*/
    fifty_moves += start_fifty_moves;
}

/*checks whether a move is in a move list.*/
int Play_Move_Is_Valid(MOVE key_move, const MOVE *restrict movelist, int move_cnt)
{
    uint32_t mv_mask = mv_move_mask.u;

    for (int i = 0; i < move_cnt; i++)
        if (((movelist[i].u ^ key_move.u) & mv_mask) == 0)
            return(1);

    return(0);
}

/*checks whether a move is allowed.*/
static int Play_Move_Is_Legal(MOVE key_move, enum E_COLOUR colour)
{
    int ret=0;
    if (key_move.u != MV_NO_MOVE_MASK)
    {
        MOVE movelist[MAXMV];
        int move_cnt;

        /*only generate the moves for the moving piece. this speeds up
          the UCI move parser with long move lists, which may be helpful
          in extreme bullet games.*/
        move_cnt = Mvgen_Find_All_Moves_Piece(movelist, key_move.m.from, colour);

        if (Play_Move_Is_Valid(key_move, movelist, move_cnt))
        {
            /*the move is pseudo-legal. check for full legality.*/
            Search_Try_Move(key_move);
            if (!Mvgen_King_In_Check(colour))
                ret = 1;
            Search_Retract_Last_Move();
        }
    }
    return(ret);
}

/*the move list has already been converted to lower case*/
static int Play_Parse_Move(const char* buf, MOVE *mp)
{
    int x1,y1,x2,y2;

    mp->m.flag = 0; /*assume invalid move*/

    /*check the index format - case handling has already been done*/
    if ((buf[0] >= 'a') && (buf[0] <= 'h')) x1 = buf[0] - 'a'; else return(0);
    if ((buf[1] >= '1') && (buf[1] <= '8')) y1 = buf[1] - '1'; else return(0);
    if ((buf[2] >= 'a') && (buf[2] <= 'h')) x2 = buf[2] - 'a'; else return(0);
    if ((buf[3] >= '1') && (buf[3] <= '8')) y2 = buf[3] - '1'; else return(0);

    mp->m.from = 10*y1 + x1 + 21;
    mp->m.to   = 10*y2 + x2 + 21;
    mp->m.mvv_lva = 0;
    if (board[mp->m.from]->type == WPAWN){
        if (mp->m.to >= A8){
            switch(buf[4]) {
            case 'r':
                mp->m.flag = WROOK;
                break;
            case 'n':
                 mp->m.flag = WKNIGHT;
                break;
            case 'b':
                mp->m.flag = WBISHOP;
                break;
            case 'q':
            default:
                mp->m.flag = WQUEEN;
                break;
            }
        } else mp->m.flag = WPAWN;
    } else if (board[mp->m.from]->type == BPAWN) {
        if (mp->m.to <= H1) {
            switch(buf[4]) {
            case 'r':
                mp->m.flag = BROOK;
                break;
            case 'n':
                mp->m.flag = BKNIGHT;
                break;
            case 'b':
                mp->m.flag = BBISHOP;
                break;
            case 'q':
            default:
                mp->m.flag = BQUEEN;
                break;
            }
        } else mp->m.flag = BPAWN;
    } else {
        mp->m.flag = 1;
    }
    return 1;
}

static void Play_Transfer_Piece_Board(int piece_type, int piece_square)
{
    int i;
    switch (piece_type)
    {
    case WKING:
        wking = piece_square;
        Wpieces[0].xy = wking;
        board[wking] = &Wpieces[0];
        break;
    case BKING:
        bking = piece_square;
        Bpieces[0].xy = bking;
        board[bking] = &Bpieces[0];
        break;
    case WQUEEN:
        if (Wpieces[1].xy==0) {
            board[piece_square] = &Wpieces[1];
            Wpieces[1].xy = piece_square;
        } else { /* promoted piece */
            for (i=8; i<16; i++) {
                if (Wpieces[i].xy==0) {
                    board[piece_square] = &Wpieces[i];
                    Wpieces[i].xy = piece_square;
                    Wpieces[i].type = WQUEEN;
                    break;
                }
            }
        }
        break;
    case BQUEEN:
        if (Bpieces[1].xy==0) {
            board[piece_square] = &Bpieces[1];
            Bpieces[1].xy = piece_square;
        } else { /* promoted piece */
            for (i=8; i<16; i++) {
                if (Bpieces[i].xy==0) {
                    board[piece_square] = &Bpieces[i];
                    Bpieces[i].xy = piece_square;
                    Bpieces[i].type = BQUEEN;
                    break;
                }
            }
        }
        break;
    case WROOK:
        if (Wpieces[2].xy==0) {
            board[piece_square] = &Wpieces[2];
            Wpieces[2].xy = piece_square;
        } else if (Wpieces[3].xy==0) {
            board[piece_square] = &Wpieces[3];
            Wpieces[3].xy = piece_square;
        } else { /* promoted piece */
            for (i=8; i<16; i++) {
                if (Wpieces[i].xy==0) {
                    board[piece_square] = &Wpieces[i];
                    Wpieces[i].xy = piece_square;
                    Wpieces[i].type = WROOK;
                    break;
                }
            }
        }
        break;
    case BROOK:
        if (Bpieces[2].xy==0) {
            board[piece_square] = &Bpieces[2];
            Bpieces[2].xy = piece_square;
        } else if (Bpieces[3].xy==0) {
            board[piece_square] = &Bpieces[3];
            Bpieces[3].xy = piece_square;
        } else { /* promoted piece */
            for (i=8; i<16; i++) {
                if (Bpieces[i].xy==0) {
                    board[piece_square] = &Bpieces[i];
                    Bpieces[i].xy = piece_square;
                    Bpieces[i].type = BROOK;
                    break;
                }
            }
        }
        break;
    case WBISHOP:
        if (Wpieces[4].xy==0) {
            board[piece_square] = &Wpieces[4];
            Wpieces[4].xy = piece_square;
        } else if (Wpieces[5].xy==0) {
            board[piece_square] = &Wpieces[5];
            Wpieces[5].xy = piece_square;
        } else { /* promoted piece */
            for (i=8; i<16; i++) {
                if (Wpieces[i].xy==0) {
                    board[piece_square] = &Wpieces[i];
                    Wpieces[i].xy = piece_square;
                    Wpieces[i].type = WBISHOP;
                    break;
                }
            }
        }
        break;
    case BBISHOP:
        if (Bpieces[4].xy==0) {
            board[piece_square] = &Bpieces[4];
            Bpieces[4].xy = piece_square;
        } else if (Bpieces[5].xy==0) {
            board[piece_square] = &Bpieces[5];
            Bpieces[5].xy = piece_square;
        } else { /* promoted piece */
            for (i=8; i<16; i++) {
                if (Bpieces[i].xy==0) {
                    board[piece_square] = &Bpieces[i];
                    Bpieces[i].xy = piece_square;
                    Bpieces[i].type = BBISHOP;
                    break;
                }
            }
        }
        break;
    case WKNIGHT:
        if (Wpieces[6].xy==0) {
            board[piece_square] = &Wpieces[6];
            Wpieces[6].xy = piece_square;
        } else if (Wpieces[7].xy==0) {
            board[piece_square] = &Wpieces[7];
            Wpieces[7].xy = piece_square;
        } else { /* promoted piece */
            for (i=8; i<16; i++) {
                if (Wpieces[i].xy==0) {
                    board[piece_square] = &Wpieces[i];
                    Wpieces[i].xy = piece_square;
                    Wpieces[i].type = WKNIGHT;
                    break;
                }
            }
        }
        break;
    case BKNIGHT:
        if (Bpieces[6].xy==0) {
            board[piece_square] = &Bpieces[6];
            Bpieces[6].xy = piece_square;
        } else if (Bpieces[7].xy==0) {
            board[piece_square] = &Bpieces[7];
            Bpieces[7].xy = piece_square;
        } else { /* promoted piece */
            for (i=8; i<16; i++) {
                if (Bpieces[i].xy==0) {
                    board[piece_square] = &Bpieces[i];
                    Bpieces[i].xy = piece_square;
                    Bpieces[i].type = BKNIGHT;
                    break;
                }
            }
        }
        break;
    case WPAWN:
        for (i=8; i<16; i++) {
            if (Wpieces[i].xy==0) {
                board[piece_square] = &Wpieces[i];
                Wpieces[i].xy = piece_square;
                break;
            }
        }
        break;
    case BPAWN:
        for (i=8; i<16; i++) {
            if (Bpieces[i].xy==0) {
                board[piece_square] = &Bpieces[i];
                Bpieces[i].xy = piece_square;
                break;
            }
        }
        break;
    } /*case piece switch*/
}

static void Play_Transfer_Board(BPOS *edit_pos)
{
    enum E_BP_SQUARE square;
    for (square = BP_A1; square <= BP_H8; square++)
    {
        int piece_type = edit_pos->board[square];
        if (piece_type != NO_PIECE)
            Play_Transfer_Piece_Board(piece_type, board64[square]);
    }

    /* Fix non board piece pointers*/
    for (PIECE *p=Bpieces[0].next; p!=NULL; p=p->next) {
        if (p->xy == 0) {
            p->prev->next = p->next;
            if (p->next)
                p->next->prev = p->prev;
        }
    }
    for (PIECE *p=Wpieces[0].next; p!=NULL; p=p->next) {
        if (p->xy == 0) {
            p->prev->next = p->next;
            if (p->next)
                p->next->prev = p->prev;
        }
    }
}

static enum E_POS_VALID Play_Add_Piece_Edit_Board(BPOS *edit_pos, char piece_char, int rank_start, int file)
{
    int piece_square;
    if ((rank_start < BP_A1) || (file > BP_FILE_H))
        return(POS_BAD_COORD);
    piece_square = rank_start + file;
    switch (piece_char)
    {
        case 'K': edit_pos->board[piece_square] = WKING; break;
        case 'Q': edit_pos->board[piece_square] = WQUEEN; break;
        case 'R': edit_pos->board[piece_square] = WROOK; break;
        case 'B': edit_pos->board[piece_square] = WBISHOP; break;
        case 'N': edit_pos->board[piece_square] = WKNIGHT; break;
        case 'P': edit_pos->board[piece_square] = WPAWN; break;
        case 'k': edit_pos->board[piece_square] = BKING; break;
        case 'q': edit_pos->board[piece_square] = BQUEEN; break;
        case 'r': edit_pos->board[piece_square] = BROOK; break;
        case 'b': edit_pos->board[piece_square] = BBISHOP; break;
        case 'n': edit_pos->board[piece_square] = BKNIGHT; break;
        case 'p': edit_pos->board[piece_square] = BPAWN; break;
        default: return(POS_BAD_PIECE);
    }
    return(POS_OK);
}

static enum E_POS_VALID Play_Check_Pieces(BPOS *edit_pos)
{
    enum E_BP_SQUARE square;
    int promoted_pieces;
    int wkings=0, wqueens=0, wbishops=0, wknights=0, wrooks=0, wpawns=0, wpieces=0;
    int bkings=0, bqueens=0, bbishops=0, bknights=0, brooks=0, bpawns=0, bpieces=0;
    int wking_pos=0, bking_pos=0, king_diff_col, king_diff_row;

    for (square = BP_A1; square <= BP_H8; square++)
    {
        int piece_type = edit_pos->board[square];

        /*check that the amount of pieces is legal*/
        switch (piece_type)
        {
            case WKING:   wkings++;   wpieces++; wking_pos = square; break;
            case WQUEEN:  wqueens++;  wpieces++; break;
            case WROOK:   wrooks++;   wpieces++; break;
            case WBISHOP: wbishops++; wpieces++; break;
            case WKNIGHT: wknights++; wpieces++; break;
            case WPAWN:   wpawns++;   wpieces++;
                          if ((square < BP_A2) || (square > BP_H7))
                              return(POS_PAWN_RANK);
                          break;
            case BKING:   bkings++;   bpieces++; bking_pos = square; break;
            case BQUEEN:  bqueens++;  bpieces++; break;
            case BROOK:   brooks++;   bpieces++; break;
            case BBISHOP: bbishops++; bpieces++; break;
            case BKNIGHT: bknights++; bpieces++; break;
            case BPAWN:   bpawns++;   bpieces++;
                          if ((square < BP_A2) || (square > BP_H7))
                              return(POS_PAWN_RANK);
                          break;
        }
    }

    if ((wkings != 1) || (bkings != 1)) return(POS_NO_KING);

    king_diff_col = (wking_pos % (BP_A8 - BP_A7)) - (bking_pos % (BP_A8 - BP_A7));
    if (king_diff_col < 0)
        king_diff_col = -king_diff_col;
    king_diff_row = (wking_pos / (BP_A8 - BP_A7)) - (bking_pos / (BP_A8 - BP_A7));
    if (king_diff_row < 0)
        king_diff_row = -king_diff_row;

    if ((king_diff_col <= 1) && (king_diff_row <= 1))
       return(POS_KING_CLOSE);

    if ((wpieces > 16) || (bpieces > 16)) return(POS_TOO_MANY_PIECES);
    if ((wpawns > 8) || (bpawns > 8))     return(POS_TOO_MANY_PAWNS);

    promoted_pieces = 0;
    if (wqueens > 1)  promoted_pieces += wqueens - 1;
    if (wrooks > 2)   promoted_pieces += wrooks - 2;
    if (wbishops > 2) promoted_pieces += wbishops - 2;
    if (wknights > 2) promoted_pieces += wknights - 2;
    if (promoted_pieces > 8 - wpawns) return(POS_OVERPROM);

    promoted_pieces = 0;
    if (bqueens > 1)  promoted_pieces += bqueens - 1;
    if (brooks > 2)   promoted_pieces += brooks - 2;
    if (bbishops > 2) promoted_pieces += bbishops - 2;
    if (bknights > 2) promoted_pieces += bknights - 2;
    if (promoted_pieces > 8 - bpawns) return(POS_OVERPROM);

    return(POS_OK);
}

static void Play_Sanitise_Castling_Rights(void)
{
    if ((wking==E1) && ((board[A1]->type == WROOK) || (board[H1]->type == WROOK)))
    {
        if (board[A1]->type != WROOK) gflags |= WRA1MOVED;
        if (board[H1]->type != WROOK) gflags |= WRH1MOVED;
    } else
        gflags |= (WKMOVED | WRA1MOVED | WRH1MOVED);

    if ((bking==E8) && ((board[A8]->type == BROOK) || (board[H8]->type == BROOK)))
    {
        if (board[A8]->type != BROOK) gflags |= BRA8MOVED;
        if (board[H8]->type != BROOK) gflags |= BRH8MOVED;
    } else
        gflags |= (BKMOVED | BRA8MOVED | BRH8MOVED);
}

#if !defined(AUTOTUNE) /*tuner.c reads positions from training data*/
static 
#endif
enum E_POS_VALID Play_Read_FEN_Position(char *fen_line)
{
    int rank_start, file;
    enum E_POS_VALID pos_validity;
    BPOS edit_pos;
    char ch;

    /*fen_line points to the beginning of the FEN string.*/

    /*is it the starting position?*/
    if (strncmp(fen_line, "startpos", 8) == 0)
    {
        Play_Set_Starting_Position();
        return(POS_OK);
    } else
    {
        if (strncmp(fen_line, "fen ", 4) == 0)
            fen_line += 4;
        else
            return(POS_NO_FEN);

        if ((strncmp(fen_line, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR ", 44) == 0) &&
            ((fen_line[44] == 'w') || (fen_line[44] == 'W')) &&
            (fen_line[45] == ' ') &&
                ((strncmp(fen_line+50, " - 0 1", 6) == 0) || /*full rest string*/
                (fen_line[50] == '\0') || /*missing EP and moves, assume "- 0 1" */
                (fen_line[52] == '\0'))   /*missing move counters, assume "0 1"*/
            )
        {
            unsigned int i, castlings;
            for (i = 46, castlings = 0; i < 50; i++)
            {
                char c = fen_line[i];
                switch (c)
                {
                    case 'K': castlings |= 0x01u; break;
                    case 'Q': castlings |= 0x02u; break;
                    case 'k': castlings |= 0x04u; break;
                    case 'q': castlings |= 0x08u; break;
                }
            }
            if (castlings == 0x0Fu) /*starting position recognised.*/
            {
                Play_Set_Starting_Position();
                return(POS_OK);
            }
        }
    }

    Play_Empty_Board();
    en_passant_sq = 0;
    gflags = (WKMOVED | WRA1MOVED | WRH1MOVED | BKMOVED | BRA8MOVED | BRH8MOVED);
    cst_p = 0;
    mv_stack_p = 0;
    game_started_from_0 = 0;
    player_move.u = MV_NO_MOVE_MASK;
    start_moves = 0;
    start_fifty_moves = 0;
    fifty_moves = 0;

    memset(&edit_pos, 0, sizeof(BPOS));

    for (rank_start = BP_A8, file = BP_FILE_A; *fen_line > ' '; fen_line++)
    {
        ch = *fen_line;

        if (ch == '/')
        {
            rank_start -= BP_RANK_DIFF;
            file = BP_FILE_A;
            continue;
        }
        if ((ch >= '0') && (ch <= '8'))
        {
            file += (ch - '0');
            continue;
        }

        pos_validity = Play_Add_Piece_Edit_Board(&edit_pos, ch, rank_start, file);
        if (pos_validity != POS_OK)
            return(pos_validity);
        file++;
    }

    pos_validity = Play_Check_Pieces(&edit_pos);
    if (pos_validity != POS_OK)
        return(pos_validity);

    Play_Transfer_Board(&edit_pos);

    if (*fen_line != ' ') return(POS_NO_SIDE);
    fen_line++;

    /*now for the side to move*/
    ch = *fen_line;
    if ((ch == 'w') || (ch == 'W'))
    {
        gflags |= BLACK_MOVED;
        /*the side that is not to move must not be in check*/
        if (Mvgen_King_In_Check(BLACK)) return(POS_IN_CHECK);
    }
    else if ((ch == 'b') || (ch == 'B'))
    {
        /*the side that is not to move must not be in check*/
        if (Mvgen_King_In_Check(WHITE)) return(POS_IN_CHECK);
    } else
        /*no side to move given*/
        return(POS_NO_SIDE);

    fen_line++;
    if (*fen_line != ' ') return(POS_OK);
    fen_line++;
    if (*fen_line == '\0') return(POS_OK);

    /*castling rights*/
    ch = *fen_line;
    if ((ch == '-') || (ch == 'K') || (ch == 'Q') || (ch == 'k') || (ch == 'q'))
    {
        if (ch == '-')
            fen_line++;
        else
        {
            for (; (*fen_line > ' '); fen_line++)
            {
                ch = *fen_line;
                switch (ch)
                {
                    case 'K': gflags &= ~(WKMOVED | WRH1MOVED); break;
                    case 'Q': gflags &= ~(WKMOVED | WRA1MOVED); break;
                    case 'k': gflags &= ~(BKMOVED | BRH8MOVED); break;
                    case 'q': gflags &= ~(BKMOVED | BRA8MOVED); break;
                    default: break;
                }
            }
            /*if they are nonsense, the engine can correct this itself; no reason to
              reject the position.*/
            Play_Sanitise_Castling_Rights();
        }
        if (*fen_line != ' ') return(POS_OK);
        fen_line++;
        if (*fen_line == '\0') return(POS_OK);
    }

    /*en passant square; an invalid or malformed EP square is just ignored.
      unfortunately, FEN allows nonsense EP squares.*/
    ch = *fen_line;
    if ((ch == '-') || ((ch >= 'a') && (ch <= 'h')) || ((ch >= 'A') && (ch <= 'H')))
    {
        if (ch != '-')
        {
            char file_ch, rank_ch;

            file_ch = *fen_line++;
            if ((file_ch <= 'Z') && (file_ch >= 'A')) file_ch += 'a' - 'A';
            en_passant_sq = ((int)(file_ch - 'a')) + A1;

            rank_ch = *fen_line;
            if (rank_ch <= ' ')
            {
                en_passant_sq = 0;
                return(POS_OK);
            }

            en_passant_sq += ((int)(rank_ch - '1')) * RANK_DIFF;

            /*check whether the EP square is a board square, on rank 6/3 depending on who is
              to move, and that it is free.*/
            if ((file_ch < 'a') || (file_ch > 'h') || (rank_ch < '1') || (rank_ch > '8'))
                en_passant_sq = 0;
            else if ((((gflags & BLACK_MOVED) != 0) && ((en_passant_sq < A6) || (en_passant_sq > H6))) ||
                (((gflags & BLACK_MOVED) == 0) && ((en_passant_sq < A3) || (en_passant_sq > H3))))
                en_passant_sq = 0;
            else if (board[en_passant_sq]->type != NO_PIECE)
                en_passant_sq = 0;

            /*if we still have an EP square, check that it is behind a pawn, that
              this pawn can have made a double step and that there is an opposing
              pawn that can capture.*/
            if (en_passant_sq != 0)
            {
                if ((gflags & BLACK_MOVED) != 0) /*last move was Black's*/
                {
                    if (board[en_passant_sq + RANK_DIFF]->type != NO_PIECE) /*double pawn step*/
                        en_passant_sq = 0;
                    else if (board[en_passant_sq - RANK_DIFF]->type != BPAWN) /*target pawn*/
                        en_passant_sq = 0;
                    else if ((board[en_passant_sq - (RANK_DIFF + FILE_DIFF)]->type != WPAWN) && /*capturing pawn*/
                             (board[en_passant_sq - (RANK_DIFF - FILE_DIFF)]->type != WPAWN))
                            en_passant_sq = 0;
                } else /*last move was White's*/
                {
                    if (board[en_passant_sq - RANK_DIFF]->type != NO_PIECE) /*double pawn step*/
                        en_passant_sq = 0;
                    else if (board[en_passant_sq + RANK_DIFF]->type != WPAWN) /*target pawn*/
                        en_passant_sq = 0;
                    else if ((board[en_passant_sq + (RANK_DIFF + FILE_DIFF)]->type != BPAWN) && /*capturing pawn*/
                             (board[en_passant_sq + (RANK_DIFF - FILE_DIFF)]->type != BPAWN))
                            en_passant_sq = 0;
                }
            }
        }
        fen_line++;
        if (*fen_line != ' ') return(POS_OK);
        fen_line++;
        if (*fen_line == '\0') return(POS_OK);
    }

    /*fifty moves counter, in plies (not moves)*/
    for (; ((*fen_line >= '0') && (*fen_line <= '9')); fen_line++)
    {
        if (start_fifty_moves < 100)
        {
            start_fifty_moves *= 10;
            start_fifty_moves += *fen_line - '0';
        }
    }

    fifty_moves = start_fifty_moves;

    if (*fen_line != ' ') return(POS_OK);
    fen_line++;
    if (*fen_line == '\0') return(POS_OK);

    /*absolute moves counter, in moves (not plies)*/
    for (; ((*fen_line >= '0') && (*fen_line <= '9')); fen_line++)
    {
        if (start_moves < 1000)
        {
            start_moves *= 10;
            start_moves += *fen_line - '0';
        }
    }

    /*is it useful? start position with move 1 has already been tested.*/
    if (start_moves > 1)
    {
        start_moves--;
        start_moves *= 2; /*in plies*/
        game_started_from_0 = 1;
    } else
        start_moves = 0;
    /*if black is to move, the white ply has already been done*/
    if (!(gflags & BLACK_MOVED))
        start_moves++;

    return(POS_OK);
}

/*perft with evasions and UCI "stop" / "quit" check.
  UCI "stop" or "quit" can abort the perft.
  must be called with depth >= 1 because the depth check is in the move
  loop: this saves useless function calls at the leaves.*/
static void Play_Perft(int depth, enum E_COLOUR colour, int is_cperft)
{
    MOVE movelist[MAXMV];
    static MOVE check_attacks_buf[CHECKLISTLEN]; /*static saves stack*/
    enum E_COLOUR next_colour;
    enum E_MOVESTATE move_state;
    int move_cnt, i, n_checks, n_check_pieces, end_recursion, kingsq;

    kingsq = (colour == BLACK) ? bking : wking;
    if (depth <= 0) /*recursion ends*/
    {
        if (is_cperft) /*count capture leaves only?*/
        {
            /*captures and quiet queen promotions, but no underpromotions,
              not even underpromotions that are also captures*/
            if (colour == WHITE)
            {
                n_checks = Mvgen_White_King_In_Check();
                move_cnt = Mvgen_Find_All_White_Captures_And_Promotions(movelist, QUEENING);
            } else
            {
                n_checks = Mvgen_Black_King_In_Check();
                move_cnt = Mvgen_Find_All_Black_Captures_And_Promotions(movelist, QUEENING);
            }

            for (i = 0; i < move_cnt; i++)
            {
                move_state = (n_checks == 0) ? Mvgen_Is_Move_Safe(movelist[i], kingsq, colour) : MOVE_UNKNOWN;
                if (move_state != MOVE_UNKNOWN)
                {
                    if (move_state == MOVE_SAFE)
                        perft_nodes++;
                    continue;
                }
                Search_Push_Status();
                Search_Make_Move(movelist[i]);
                if (!Mvgen_King_In_Check(colour))
                    perft_nodes++;
                Search_Retract_Last_Move();
                Search_Pop_Status();
            }
        } else
            perft_nodes++; /*this node, regular perft mode*/
        return;
    }

    if (abort_perft)
        return;

    /*is the abort flag check due?
      should be checked every 10 ms using node rate auto calibration.*/
    if (perft_nodes >= perft_check_nodes)
    {
        int64_t perft_total_time;
        if (Play_Get_Abort())
        {
            /*UCI "stop" or "quit" has been issued.*/
            abort_perft = 1;
            return;
        }

        perft_total_time = Play_Get_Millisecs() - perft_start_time;

        /*initial note rate calibration necessary?*/
        if (perft_nps_10ms == 0)
        {
            /*note rate calibration possible, i.e. after 100ms?*/
            if (perft_total_time >= 100)
                perft_nps_10ms = PERFT_CHECK_NODES; /*trigger the calibrated path*/
        }

        if (perft_nps_10ms > 0)
        {
            /*use the calibrated node rate for scheduling the next check*/
            perft_nps_10ms = (perft_nodes * 10U) / perft_total_time;
            perft_check_nodes = perft_nodes + perft_nps_10ms;

        } else
        {
            /*calibrated node rate not yet available.*/
            perft_check_nodes = perft_nodes + PERFT_CHECK_NODES;
        }
    }

    /*if in check, use evasions.*/
    if (colour == WHITE)
    {
        n_checks = Mvgen_White_King_In_Check_Info(check_attacks_buf, &n_check_pieces);

        if (n_checks == 0)
            move_cnt = Mvgen_Find_All_White_Moves(movelist, NO_LEVEL, UNDERPROM);
        else
            move_cnt = Mvgen_Find_All_White_Evasions(movelist, check_attacks_buf, n_checks, n_check_pieces, UNDERPROM);

        next_colour = BLACK;
    } else
    {
        n_checks = Mvgen_Black_King_In_Check_Info(check_attacks_buf, &n_check_pieces);

        if (n_checks == 0)
            move_cnt = Mvgen_Find_All_Black_Moves(movelist, NO_LEVEL, UNDERPROM);
        else
            move_cnt = Mvgen_Find_All_Black_Evasions(movelist, check_attacks_buf, n_checks, n_check_pieces, UNDERPROM);

        next_colour = WHITE;
    }

    end_recursion = (--depth == 0) && (!is_cperft);
    for (i = 0; i < move_cnt; i++)
    {
        move_state = (n_checks == 0) ? Mvgen_Is_Move_Safe(movelist[i], kingsq, colour) : MOVE_UNKNOWN;
        if (move_state == MOVE_UNSAFE)
            continue;
        if ((move_state == MOVE_SAFE) && end_recursion)
        {
            perft_nodes++; /*recursion ends, semi bulk counting*/
            continue;
        }
        Search_Push_Status();
        Search_Make_Move(movelist[i]);
        if ((move_state == MOVE_SAFE) || (!Mvgen_King_In_Check(colour)))
        {
            if (end_recursion)
                perft_nodes++; /*recursion ends*/
            else
                Play_Perft(depth, next_colour, is_cperft);
        }
        Search_Retract_Last_Move();
        Search_Pop_Status();
    }
}

/*get the data from the ring buffer. guarded with a mutex because this isn't
  time critical (two UCI commands, position and go, per move during the game)
  and saves the mess of throwing around a truckload of memory barriers to
  ensure that buffer content and indices are written in the right order on
  any CPU, even with weak memory ordering.*/
static int Play_Read_Cmd(char *line)
{
    int res = 0;

    Play_Acquire_Lock(&io_lock);
    if (cmd_read_idx != cmd_write_idx) /*ring buffer empty condition*/
    {
        unsigned int cmd_len;
        uint8_t cmd_byte;

        /*the command length is done via memcpy because the command buffer
          is type char, which may be signed. reading that bytewise and
          shifting it up may not work as expected. memcpy is a clean and
          portable way to deal with that, and the compiler will optimise
          the function call away. it has to be split because the command
          might be at the wrap-around.*/

        /*command length, MSB*/
        memcpy(&cmd_byte, cmd_buf + cmd_read_idx, sizeof(uint8_t));
        cmd_len = cmd_byte;
        cmd_len <<= 8;
        if (cmd_read_idx < CMD_BUF_SIZE-1)
            cmd_read_idx++;
        else
            cmd_read_idx = 0;

        /*command length, LSB*/
        memcpy(&cmd_byte, cmd_buf + cmd_read_idx, sizeof(uint8_t));
        cmd_len |= cmd_byte;
        if (cmd_read_idx < CMD_BUF_SIZE-1)
            cmd_read_idx++;
        else
            cmd_read_idx = 0;

        if (cmd_read_idx + cmd_len <= CMD_BUF_SIZE) /*no wrap-around*/
        {
            memcpy(line, cmd_buf + cmd_read_idx, cmd_len);
            cmd_read_idx += cmd_len;
            if (cmd_read_idx == CMD_BUF_SIZE)
                cmd_read_idx = 0;
        } else /*wrap-around*/
        {
            unsigned int first_bytes, last_bytes;

            first_bytes = CMD_BUF_SIZE - cmd_read_idx;
            last_bytes  = cmd_len - first_bytes;

            memcpy(line, cmd_buf + cmd_read_idx, first_bytes);
            memcpy(line + first_bytes, cmd_buf, last_bytes);

            cmd_read_idx = last_bytes;
        }
        line[cmd_len] = '\0'; /*force null termination*/
        res = 1;
    }
    Play_Release_Lock(&io_lock);
    return(res);
}

static enum E_TOKEN_TYPE Play_UCI_Go_Get_Next_Token(const char *line, int *index, char *token, int64_t *value)
{
    int64_t tmp_value, sign;
    int token_index, line_index, value_start_index;
    char ch;

    /*get the text part of the next token*/
    for (token_index = 0, line_index = *index;
        ((token_index < 127) && (line[line_index] != '\0') && (line[line_index] != ' '));
        token_index++, line_index++)
    {
        token[token_index] = line[line_index];
    }
    token[token_index] = '\0';
    *value = 0;

    if (token_index == 0)
        return(TOKEN_NONE); /*end of line*/

    /*move lists are not handled here.*/
    if ((!strcmp(token, "searchmoves")) || (!strcmp(token, "ponder")))
    {
        if (line[line_index] != '\0') /*if more tokens (moves) follow*/
            line_index++;
        *index = line_index;
        return(TOKEN_MOVELIST);
    }

    if (line[line_index] == '\0') /*end of line, but with token*/
    {
        *index = line_index;
        return(TOKEN_TEXT);
    }

    /*get the value part of the next token - if present.*/
    line_index++;

    ch = line[line_index];
    if (!(((ch >= '0') && (ch <= '9')) ||
          ((ch == '+') && (line[line_index+1] >= '0') && (line[line_index+1] <= '9')) ||
          ((ch == '-') && (line[line_index+1] >= '0') && (line[line_index+1] <= '9'))))
    {
        /*no value following, so this is a text only token.*/
        *index = line_index;
        return(TOKEN_TEXT);
    }

    /*save the current index in case the "value" turns out not to be
      numerical after all, in which case it is the next text token.*/
    value_start_index = line_index;

    /*potential sign treatment*/
    if (ch == '-')
    {
        /*treat negative values as 0. UCI does not transmit negative
          parameters in the "go" command.*/
        sign = 0;
        line_index++;
        ch = line[line_index];
    } else {
        sign = 1;
        if (ch == '+')
        {
            line_index++;
            ch = line[line_index];
        }
    }

    /*get the supposed value token*/
    tmp_value = 0;
    while ((ch >= '0') && (ch <= '9'))
    {
        tmp_value *= 10;
        tmp_value += ch - '0';
        line_index++;
        ch = line[line_index];
    }

    if ((ch != '\0') && (ch != ' '))
    /*value not parsed until the end - looks like the value isn't an integer
      number. maybe another text token that happens to start with a number.*/
    {
        *index = value_start_index;
        return(TOKEN_TEXT);
    }

    /*if there are more tokens, jump to the next one*/
    if (ch != '\0')
        line_index++;

    *index = line_index;
    *value = tmp_value * sign;
    return(TOKEN_TEXT_VALUE);
}

static int64_t Play_Time_Increment(int64_t remaining_time, int64_t increment)
{
    int64_t add_up;
    /*time >= 2.4 Inc: consume 1.4  Inc
      time >= 1.5 Inc: consume 1.0  Inc
      time >= 1.0 Inc: consume 0.75 Inc
      time <  1.0 Inc: consume 0.5  Inc*/
    if (remaining_time >= (increment * 12) / 5)
        add_up = (increment * 7) / 5;
    else if (remaining_time >= (increment * 3) / 2)
        add_up = increment;
    else if (remaining_time >= increment)
        add_up = (increment * 3) / 4;
    else
        add_up = increment / 2;

    return(add_up);
}

static void Play_UCI_Process_Go(const char *line, int *exact_time, int *max_depth,
                                uint64_t *max_nodes, int *ponder_mode, MOVE *ponder_move,
                                int64_t *wmove_time, int64_t *bmove_time, int *mate_mode,
                                int *mate_depth_mv, MOVE *given_moves, int *given_moves_len)
{
    int line_index=0, moves_to_go=0;
    enum E_TOKEN_TYPE uci_go_token;
    enum E_TIME_CONTROL time_mode;
    char token[128];
    int64_t value, wtime=INFINITE_TIME - 42, btime=INFINITE_TIME - 42, winc=0, binc=0,
            wtime_given=0, btime_given=0;

    /*set defaults*/
    *exact_time = 0;
    *max_depth = MAX_DEPTH-1;
    *max_nodes = 0; /*no limit*/
    *wmove_time = INFINITE_TIME - 42;
    *bmove_time = INFINITE_TIME - 42;
    *ponder_mode = 0;
    ponder_move->u = MV_NO_MOVE_MASK;
    *given_moves_len = -1;
    *mate_mode = 0;
    *mate_depth_mv = 0;
    time_mode = TM_TIME_NONE;

    do {
        uci_go_token = Play_UCI_Go_Get_Next_Token(line, &line_index, token, &value);
        if (uci_go_token == TOKEN_TEXT_VALUE)
        {
            if (!strcmp(token, "wtime"))
            {
                wtime = value;
                wtime_given = 1;
                time_mode = TM_TIME_CONTROLS;
                continue;
            }
            if (!strcmp(token, "btime"))
            {
                btime = value;
                btime_given = 1;
                time_mode = TM_TIME_CONTROLS;
                continue;
            }
            if (!strcmp(token, "winc"))
            {
                winc = value;
                time_mode = TM_TIME_CONTROLS;
                continue;
            }
            if (!strcmp(token, "binc"))
            {
                binc = value;
                time_mode = TM_TIME_CONTROLS;
                continue;
            }
            if (!strcmp(token, "movestogo"))
            {
                moves_to_go = value;
                continue;
            }
            if (!strcmp(token, "movetime"))
            {
                *wmove_time = value;
                *bmove_time = value;
                *exact_time = 1;
                time_mode = TM_TIME_PER_MOVE;
                continue;
            }
            if (!strcmp(token, "depth"))
            {
                if (value < 1) *max_depth = 1;
                else if (value > MAX_DEPTH-1) *max_depth = MAX_DEPTH-1;
                else *max_depth = value;
                continue;
            }
            if (!strcmp(token, "mate"))
            {
                *mate_mode = 1;
                if (value > (MAX_DEPTH-1)/2) *mate_depth_mv = (MAX_DEPTH-1)/2;
                else if (value < 1) *mate_depth_mv = 1;
                else *mate_depth_mv = value;
                continue;
            }
            if (!strcmp(token, "nodes"))
            {
                *max_nodes = value;
                continue;
            }
        } else if (uci_go_token == TOKEN_TEXT)
        {
            if (!strcmp(token, "infinite"))
            {
                *exact_time = 1;
                *wmove_time = INFINITE_TIME;
                *bmove_time = INFINITE_TIME;
                time_mode = TM_TIME_PER_MOVE;
                continue;
            }
        } else if (uci_go_token == TOKEN_MOVELIST)
        {
            if (!strcmp(token, "searchmoves"))
            {
                MOVE amove;

                *given_moves_len = 0;
                /*scan until line end or the next item that isn't a move*/
                while (Play_Parse_Move(line + line_index, &amove))
                {
                    if (*given_moves_len < MAXMV) /*prevent buffer overflow*/
                    {
                        given_moves[*given_moves_len].u = amove.u;
                        (*given_moves_len)++;
                    }
                    line_index += 4;
                    if ((line[line_index] != ' ') && (line[line_index] != '\0'))
                        line_index++; /*jump over promotion parameter*/
                    if (line[line_index] == ' ')
                        line_index++; /*to next item*/
                }
                continue;
            }
            if (!strcmp(token, "ponder"))
            {
                *ponder_mode = 0;
                /*next item should be a move*/
                if (Play_Parse_Move(line + line_index, ponder_move))
                {
                    line_index += 4;
                    if ((line[line_index] != ' ') && (line[line_index] != '\0'))
                        line_index++; /*jump over promotion parameter*/
                    if (line[line_index] == ' ')
                        line_index++; /*to next item*/
                    *ponder_mode = 1;
                }
                continue;
            }
        }
    } while (uci_go_token != TOKEN_NONE);

    /*calculate the move time if the game is with time controls*/
    if (time_mode == TM_TIME_CONTROLS)
    {
        int movenumber = ((mv_stack_p + start_moves) / 2) + 1;

        *exact_time = 0;
        /*if moves_to_go more than 80, fall back to fixed game time
          because the game will likely be over before the time control
          anyway*/
        if ((moves_to_go > 0) && (moves_to_go <= 80))
        {
            /*keep a 100 ms as safeguard buffer*/
            if (wtime_given)
                *wmove_time = (wtime - 100) / (moves_to_go);
            if (btime_given)
                *bmove_time = (btime - 100) / (moves_to_go);

            if ((movenumber >= 10) && (moves_to_go >= 10)) /*account for time savings*/
            {
                if (wtime_given)
                    *wmove_time = (*wmove_time * 5) / 4;
                if (btime_given)
                    *bmove_time = (*bmove_time * 5) / 4;
            }
        } else /*fixed time per game, or rest of the game*/
        {
            int expected_moves;

            if (movenumber >= 70)
                expected_moves = 20;
            else
                expected_moves = 48 - (movenumber * 2) / 5;

            /*keep a 100 ms as safeguard buffer*/
            if (wtime_given)
                *wmove_time = (wtime - 100) / expected_moves;
            if (btime_given)
                *bmove_time = (btime - 100) / expected_moves;

            if ((movenumber >= 10) && (movenumber <= 30)) /*account for time savings*/
            {
                if (wtime_given)
                    *wmove_time = (*wmove_time * 5) / 4;
                if (btime_given)
                    *bmove_time = (*bmove_time * 5) / 4;
            }
        }
        if ((winc) && (wtime_given))
        {
            /*keep a 100 ms as safeguard buffer*/
            int64_t max_time = wtime - 100;
            *wmove_time += Play_Time_Increment(wtime, winc);
            if (*wmove_time > max_time) *wmove_time = max_time;
        }
        if ((binc) && (btime_given))
        {
            /*keep a 100 ms as safeguard buffer*/
            int64_t max_time = btime - 100;
            *bmove_time += Play_Time_Increment(btime, binc);
            if (*bmove_time > max_time) *bmove_time = max_time;
        }
        if (*wmove_time < 0) *wmove_time = 0;
        if (*bmove_time < 0) *bmove_time = 0;
    }
}

/*if the GUI doesn't use "startpos" plus all moves, but only the move list
  from the last irreversible move, like Droidfish does, then how to find
  out whether we have a continuing game and a PV hit?
  By executing the best move and then all legal opponent moves, storing
  their position hashes and storing what index our expected PV answer has.
  That's a bit of an ugly hack, but it will work even with Droidfish,
  and other GUIs like Arena will still be compatible.*/
static void Play_Gather_Cont_Pos(MOVE ct_move, int side, uint64_t *cont_pos_hashes, int *cont_pos_num, int *cont_pos_pv)
{
    int i, move_cnt, actual_moves;
    MOVE movelist[MAXMV], pv_move;
    uint32_t mv_mask = mv_move_mask.u;

    /*store the hash of the continued game*/

    Search_Push_Status();
    Search_Make_Move(ct_move); /*the CT answer move*/

    side = Mvgen_Opp_Colour(side);
    move_cnt = Mvgen_Find_All_Moves(movelist, NO_LEVEL, side, UNDERPROM);

    /*get the expected opponent's answer, if available. minimum PV length
      must be 3, or else we don't have an answering move anyway.*/
    if (GlobalPV.line_len >= 3)
        pv_move = Mvgen_Decompress_Move(GlobalPV.line_cmoves[1]);
    else
        pv_move.u = MV_NO_MOVE_MASK;

    *cont_pos_pv = -1; /*invalidate by default*/

    /*now try all available opponent's moves after out answer move and
      gather their hashes*/
    for (i = 0, actual_moves = 0; i < move_cnt; i++)
    {
        Search_Push_Status();
        Search_Make_Move(movelist[i]);
        if (!Mvgen_King_In_Check(side))
        {
            if (((movelist[i].u ^ pv_move.u) & mv_mask) == 0)
            {
                *cont_pos_pv = actual_moves;
            }
            cont_pos_hashes[actual_moves] = move_stack[mv_stack_p].mv_pos_hash;
            actual_moves++;
        }
        Search_Retract_Last_Move();
        Search_Pop_Status();
    }

    *cont_pos_num = actual_moves;

    Search_Retract_Last_Move(); /*the CT answer move*/
    Search_Pop_Status();
}

/*get the next move from the "position" list and return the move string length*/
static int Play_Get_Next_Pos_Move(const char *command_string, char *move_string)
{
    int i;
    for (i = 0; i < 5; i++)
    {
        char ch = command_string[i];
        if ((ch == ' ') || (ch == '\0'))
            break;
        move_string[i] = ch;
    }
    move_string[i] = '\0';
    return(i);
}

static void Play_Conv_Elo_Nps(int elo_throttle, int elo_setting, int cpu_throttle, int cpu_speed,
                              uint64_t max_nps_rate, int32_t noise_setting, int64_t move_time,
                              int *restrict effective_cpu_speed, uint64_t *effective_max_nps_rate,
                              int32_t *noise_level)
{
    if (!elo_throttle)
    {
        if (cpu_throttle)
        {
            *effective_cpu_speed = cpu_speed;
            *effective_max_nps_rate = max_nps_rate;
        } else
        {
            *effective_cpu_speed = 100;
            *effective_max_nps_rate = NO_THROTTLE_NPS;
        }
        *noise_level = noise_setting;
    } else
    {
        uint64_t calc_nodes;
        int calc_elo;

        *effective_cpu_speed = 100;

        if (move_time > 15000)
        {
            /*for slower time controls, increase the Elo setting because humans
              don't miss that many tactics when they think longer. that gets
              up to 50 Elo added for long time controls.*/
            if (move_time > 115000) /*cap at about 2 minutes.*/
                move_time = 115000;
            move_time -= 15000;
            move_time /= 1000;
            /*move_time now has range 0 - 100 in seconds.
             increase at 0.5 Elo per second.*/
            elo_setting += move_time/2;
        } else if (move_time < 15000)
        {
            /*for fast time controls, decrease the Elo setting by up to
              50 Elo to account for more human errors.*/
            if (move_time < 5000)
                move_time = 5000;
            move_time -= 5000;
            /*move_time is now in the range of 0 - 5000 in milliseconds.
              decrease at 10 Elo per second.*/
            elo_setting -= (5000 - move_time) / 100;
            if (elo_setting < 1000)
                elo_setting = 1000;
        }

        /*2300 Elo at 30 kNPS, roughly.*/
        calc_elo   = BASE_ELO;
        calc_nodes = BASE_NODES;

        /*scale nps upwards. assume about 56 Elo per speed doubling.*/
        if (elo_setting >= BASE_ELO)
        {
            while (calc_elo + ELO_DOUBLE <= elo_setting)
            {
                calc_elo += ELO_DOUBLE;
                calc_nodes *= 2U;
            }
            while (calc_elo + ELO_DOUBLE/2 <= elo_setting)
            {
                calc_elo += ELO_DOUBLE/2;
                calc_nodes *= 14142U;
                calc_nodes /= 10000U;
            }
            while (calc_elo + ELO_DOUBLE/4 <= elo_setting)
            {
                calc_elo += ELO_DOUBLE/4;
                calc_nodes *= 11892U;
                calc_nodes /= 10000U;
            }
            while (calc_elo + ELO_DOUBLE/8 <= elo_setting)
            {
                calc_elo += ELO_DOUBLE/8;
                calc_nodes *= 10905U;
                calc_nodes /= 10000U;
            }
        } else /*72 Elo per speed doubling for lower speed.*/
        {
            while (calc_elo - ELO_HALF >= elo_setting)
            {
                calc_elo -= ELO_HALF;
                calc_nodes /= 2U;
            }
            while (calc_elo - ELO_HALF/2 >= elo_setting)
            {
                calc_elo -= ELO_HALF/2;
                calc_nodes *= 10000U;
                calc_nodes /= 14142U;
            }
            while (calc_elo - ELO_HALF/4 >= elo_setting)
            {
                calc_elo -= ELO_HALF/4;
                calc_nodes *= 10000U;
                calc_nodes /= 11892U;
            }
            while (calc_elo - ELO_HALF/8 >= elo_setting)
            {
                calc_elo -= ELO_HALF/8;
                calc_nodes *= 10000U;
                calc_nodes /= 10905U;
            }
            /*keep minimum node rate.*/
            if (calc_nodes < MIN_NODES) calc_nodes = MIN_NODES;
        }

        *effective_max_nps_rate = calc_nodes;

        /*noise level setting:
          -   0% for 1900+ Elo
          -  30% for 1500  Elo
          - 100% for 1000- Elo
          check the plot at Wolfram Alpha: https://www.wolframalpha.com/
          Plot[Piecewise[{{(41*(x-1000)-34500)*(x-1000)*(x-1000)/50000000+100, x<1500}, {((1900-x)*3)/40, x>=1500}}], {x,1000,1900}]
        */
        if (elo_setting >= 1900)
            *noise_level = 0;
        if (elo_setting <= 1000)
            *noise_level = 100;
        else if (elo_setting >= 1500)
        {
            /*linear decay from 30% to 0 between 1500 and 1900 Elo*/
            *noise_level = ((1900 - elo_setting) * 3) / 40;
        } else /*between 1000 and 1500 Elo*/
        {
            /*the coefficients the 3rd degree interpolation polynomial were
              determined as follows:
              100% at 1000 Elo
               30% at 1500 Elo
              derivation at 1000 Elo = 0
              derivation at 1500 Elo = -3/40 like in the next interval*/
            int64_t tmp;

            /*range shift to make calculations numerically easier*/
            elo_setting -= 1000;

            tmp  = 41LL*((int64_t)elo_setting) - 34500LL;
            tmp *= ((int64_t)elo_setting);
            tmp *= ((int64_t)elo_setting);
            tmp /= 50000000LL;
            tmp += 100LL;

            *noise_level = (int)tmp;
        }
    }
}

static void Play_UCI(void)
{
    static char ALIGN_4 line[CMD_UCI_LEN + 16]; /*static saves stack*/
    static char ALIGN_4 valid_pos_str[CMD_UCI_LEN + 16];
    static unsigned int valid_pos_len=0;
    char command[32], printbuf[512];
    static uint64_t cont_pos_hashes[MAXMV];
    uint64_t old_pos_hash=0;
    int cont_pos_num=0, cont_pos_pv=-1, keep_hash;
    int side, pos_illegal=0, max_depth, ponder_mode=0, exact_time=0;
    int given_moves_len=-1, mate_mode, mate_depth_mv, move_overhead, uci_data_ready;
    int cpu_throttle, cpu_speed, elo_setting, elo_throttle;
    int32_t uci_noise;
    uint64_t max_nps_rate;
    uint64_t spent_nodes=0;
    int64_t spent_time=0;
    MOVE given_moves[MAXMV], amove;
    int64_t wmove_time, bmove_time, move_time;

    disable_book = 0;            /*internal opening book is active*/
    elo_throttle = 0;            /*Elo limit disabled*/
    elo_setting = elo_max;       /*use maximum throttled strength when throttled*/
    cpu_throttle = 0;            /*no CPU throttling, neither percentage nor NPS*/
    cpu_speed = 100;             /*100% is full speed: no SW throttling*/
    max_nps_rate = MAX_THROTTLE_KNPS * 1000ULL; /*no NPS throttling*/
    move_overhead = DEFAULT_MOVE_OVERHEAD; /*50 ms*/
    show_currmove = CURR_UPDATE; /*show current root move once per second*/
    eval_noise = 0;               /*in percent; 0 means "no noise"*/
    uci_noise = 0;
    contempt_val = CONTEMPT_VAL; /*avoid early draw unless the engine is worse than -30*/
    contempt_end = CONTEMPT_END; /*in plies from the start position*/
    uci_debug = 0;               /*no debug output*/

    side = WHITE;
    max_depth = MAX_DEPTH-1;
    mate_mode = 0;
    mate_depth_mv = 0;
    wmove_time = 0LL;
    bmove_time = 0LL;
    move_time = 0LL;
    keep_hash = 1;

    for (;;) {
        *line = '\0'; /*init as empty*/

        do {
            uci_data_ready = Play_Read_Cmd(line);
            if (uci_data_ready == 0)
                Play_Pause_UCI();
        } while (uci_data_ready == 0);

        /*note that the command is de-cased at this point, except FEN strings.*/
        line[CMD_UCI_LEN - 1] = '\0'; /*force null termination*/

        {
            int i;
            for (i = 0; i < 31; i++)
            {
                if ((line[i] == ' ') || (line[i] == '\0'))
                    break;
                command[i] = line[i];
            }
            command[i] = '\0'; /*force null termination*/
        }

        if (uci_debug)
        {
            /*truncate "line" output to prevent overrun in print buffer.*/
            if (strlen(line) <= 450)
                sprintf(printbuf, "info string debug: input line is ->%s<-\n", line);
            else
                sprintf(printbuf, "info string debug: input line starts with ->%.450s<-\n", line);
            Play_Print(printbuf);
            sprintf(printbuf, "info string debug: command is ->%s<-\n", command);
            Play_Print(printbuf);
        }

        if ((!strcmp(command, "go")) || ((!strcmp(command, "ponderhit")) && (ponder_mode)))
        {
            enum E_COMP_RESULT search_res;

            if (pos_illegal)
            {
                Play_Print("info string error (illegal position)\nbestmove 0000\n");
                continue;
            }
            if (Mvgen_King_In_Check(Mvgen_Opp_Colour(side)))
            {
                Play_Print("info string error (illegal position)\nbestmove 0000\n");
                continue;
            }

            /*if "ponderhit" arrives, just calculate on the given move list.
              ponder isn't supported, so this is hacking around GUIs that
              might expect support.*/
            if (!strcmp(command, "go"))
            {
                MOVE ponder_move;

                if (ponder_mode) /*should not even happen?!*/
                {
                    Search_Retract_Last_Move();
                    Search_Pop_Status();
                    Play_Update_Fifty_Moves();
                    side = Mvgen_Opp_Colour(side);
                    ponder_mode = 0;
                }

                Play_UCI_Process_Go(line, &exact_time, &max_depth, &g_max_nodes,
                                    &ponder_mode, &ponder_move, &wmove_time,
                                    &bmove_time, &mate_mode, &mate_depth_mv,
                                    given_moves, &given_moves_len);

                if (side == WHITE) move_time = wmove_time; else move_time = bmove_time;

                if (ponder_mode)
                {
                    if (!Play_Move_Is_Legal(ponder_move, side))
                    {
                        /*hope for a valid position/moves command. there should not be
                          a ponderhit for an illegal move anyway.*/
                        pos_illegal = 1;
                        continue;
                    }
                    Play_Update_Special_Conditions(ponder_move);
                    Search_Push_Status();
                    Search_Make_Move(ponder_move);
                    side = Mvgen_Opp_Colour(side);
                    continue;
                }
            }

            if (!strcmp(command, "ponderhit")) ponder_mode = 0;

            /*is the position the continued game, or is it the same as
              last time, or something different?*/
            if (keep_hash)
            {
                uint64_t current_pos_hash = move_stack[mv_stack_p].mv_pos_hash;

                /*same position as last time, i.e. no moves made?*/
                if (old_pos_hash == current_pos_hash)
                {
                    /*PV doesn't apply, but the hash tables and history can.
                      don't increase the hash age counter because it is the same
                      position.*/
                    player_move.u = MV_NO_MOVE_MASK;

                    if (uci_debug)
                        Play_Print("info string debug: keeping hash tables, same position.\n");
                } else if (cont_pos_num > 0) /*any previous moves there?*/
                {
                    int i, is_cont=0, pv_hit=0;

                    /*is the current position among those that the opponent
                      could have reached after our last answer move?*/
                    for (i = 0; i < cont_pos_num; i++)
                    {
                        if (current_pos_hash == cont_pos_hashes[i])
                        {
                            is_cont = 1;
                            if (i == cont_pos_pv) pv_hit = 1;
                            break;
                        }
                    }

                    if (is_cont) /*game continues*/
                    {
                        if ((pv_hit) && (GlobalPV.line_len >= 3))
                        {
                            player_move = Mvgen_Decompress_Move(GlobalPV.line_cmoves[1]);
                        } else
                            player_move.u = MV_NO_MOVE_MASK;

                        /*increase the hash age counter because we're one move further.*/
                        if (hash_clear_counter < MAX_AGE_CNT)
                            hash_clear_counter++;
                        else
                            hash_clear_counter = 0;
                        if (uci_debug)
                            Play_Print("info string debug: keeping hash tables, continued position.\n");
                    } else /*game doesn't continue*/
                    {
                        Play_Reset_Position_Status();
                        if (uci_debug)
                            Play_Print("info string debug: resetting hash tables.\n");
                    }
                } else /*some completely new game*/
                {
                    if (old_pos_hash != 0) /*already reset by "ucinewgame"*/
                    {
                        Play_Reset_Position_Status();
                        if (uci_debug)
                            Play_Print("info string debug: resetting hash tables.\n");
                    } else
                    {
                        if (uci_debug)
                            Play_Print("info string debug: keeping hash tables, initialised game.\n");
                    }
                }
            } else
            {
                /*configured not to keep hash tables*/
                Play_Reset_Position_Status();
                if (uci_debug)
                    Play_Print("info string debug: keeping hash tables deactivated.\n");
            }

            dynamic_resign_threshold = NO_RESIGN;
            game_info.valid = EVAL_INVALID;
            game_info.eval = 0;
            computer_side = side;

            {
                int effective_cpu_speed;
                uint64_t effective_max_nps_rate;

                /*Elo setting overrides CPU throttling and noise.*/
                Play_Conv_Elo_Nps(elo_throttle, elo_setting, cpu_throttle, cpu_speed, max_nps_rate, uci_noise,
                                  move_time, &effective_cpu_speed, &effective_max_nps_rate, &eval_noise);

                search_res = Search_Get_Best_Move(&amove, player_move, move_time, move_overhead,
                                                  exact_time, max_depth, effective_cpu_speed,
                                                  effective_max_nps_rate, side, given_moves,
                                                  given_moves_len, mate_mode, mate_depth_mv,
                                                  &spent_nodes, &spent_time);
            }

            if (game_info.valid == EVAL_MOVE)
                game_info.last_valid_eval = game_info.eval;
            computer_side = NONE;

            /*store the hash of the current position*/
            old_pos_hash = move_stack[mv_stack_p].mv_pos_hash;

            if (search_res != COMP_MOVE_FOUND)
            /*no move has been returned*/
            {
                switch (search_res)
                {
                case COMP_MATE:
                    strcpy(printbuf, "info score mate 0 pv 0000\n");
                    if (uci_debug)
                        strcat(printbuf, "info string debug: position is mate\n");
                    break;
                case COMP_MAT_DRAW: /*does not happen in UCI version*/
                case COMP_STALE:
                    strcpy(printbuf, "info score cp 0 pv 0000\n");
                    if (uci_debug)
                        strcat(printbuf, "info string debug: position is draw.\n");
                    break;
                case COMP_RESIGN:
                    /*does not happen in UCI mode because dynamic_resign_threshold
                      has been set to NO_RESIGN*/
                    break;
                case COMP_NO_MOVE:
                    if (mate_mode)
                        strcpy(printbuf, "info string error (no mate found)\n");
                    else if (given_moves_len >= 0)
                        /*when searchmoves has only illegal moves*/
                        strcpy(printbuf, "info string error (no legal search move)\n");
                    else
                        strcpy(printbuf, "info string error (no move available)\n");
                    break;
                 default:
                    *printbuf = '\0';
                    break;
                }
                cont_pos_num = 0;
                cont_pos_pv = -1;
                strcat(printbuf, "bestmove 0000\n");
                Play_Print(printbuf);
                continue;
            }

            /*avoid a nearly useless sprintf here - optimise for bullet games.*/
            {
                char *mv_str = Play_Translate_Moves(amove);
                strcpy(printbuf, "bestmove ");
                printbuf[9]  = mv_str[0];
                printbuf[10] = mv_str[1];
                printbuf[11] = mv_str[2];
                printbuf[12] = mv_str[3];
                if (mv_str[4] == '\0') /*no promotion move*/
                {
                    printbuf[13] = '\n';
                    printbuf[14] = '\0';
                } else
                {
                    printbuf[13] = mv_str[4];
                    printbuf[14] = '\n';
                    printbuf[15] = '\0';
                }
            }
            Play_Print(printbuf);

            /*store the hashes of the continued game - on the opponent's time.*/
            Play_Gather_Cont_Pos(amove, side, cont_pos_hashes, &cont_pos_num, &cont_pos_pv);
            continue;
        }
        if (!strcmp(command, "position"))
        {
            char *mv_line_ptr;
            char *fen_line = line + 8; /*jump over "position"*/
            enum E_POS_VALID pos_validity;

            if (*fen_line == '\0') /*ignore empty "position" command*/
                continue;

            if (*fen_line == ' ') fen_line++; /*following whitespace*/

            pos_illegal = 0; /*assume that something good will follow*/
            ponder_mode = 0;

            for (mv_line_ptr = fen_line; *mv_line_ptr != 'm' && *mv_line_ptr != '\0'; mv_line_ptr++) ;

            if (*mv_line_ptr == 'm') /*moves following after FEN string*/
            {
                *(mv_line_ptr - 1) = '\0'; /*terminate string for FEN scanner*/

                if (strncmp(mv_line_ptr, "moves ", 6) != 0) /*should not happen*/
                    *mv_line_ptr = '\0'; /*don't evaluate move list*/
            }

            pos_validity = Play_Read_FEN_Position(fen_line);
            computer_side = NONE;
            if (gflags & BLACK_MOVED) side = WHITE; else side = BLACK;

            if (pos_validity != POS_OK) /*none of that should even happen.*/
            {
                /*if the position is malformed, that could lead to an engine
                  crash, and that must not happen no matter how malformed the
                  input is. refusing the input is OK, but crashing isn't
                  because that might even lead to security problems.*/
                switch (pos_validity)
                {
                case POS_NO_FEN:
                    Play_Print("info string error (illegal position: FEN / startpos missing)\n");
                    break;
                case POS_BAD_COORD:
                    Play_Print("info string error (illegal position: bad coordinates)\n");
                    break;
                case POS_BAD_PIECE:
                    Play_Print("info string error (illegal position: unknown piece)\n");
                    break;
                case POS_IN_CHECK:
                    Play_Print("info string error (illegal position: side to move giving check)\n");
                    break;
                case POS_NO_SIDE:
                    Play_Print("info string error (illegal position: side to move missing)\n");
                    break;
                case POS_OVERPROM:
                    Play_Print("info string error (illegal position: too many promoted pieces)\n");
                    break;
                case POS_TOO_MANY_PIECES:
                    Play_Print("info string error (illegal position: too many pieces)\n");
                    break;
                case POS_TOO_MANY_PAWNS:
                    Play_Print("info string error (illegal position: too many pawns)\n");
                    break;
                case POS_NO_KING:
                    Play_Print("info string error (illegal position: wrong number of kings)\n");
                    break;
                case POS_KING_CLOSE:
                    Play_Print("info string error (illegal position: kings too close)\n");
                    break;
                case POS_PAWN_RANK:
                    Play_Print("info string error (illegal position: pawn on bad rank)\n");
                    break;
                case POS_ERROR:
                default:
                    Play_Print("info string error (illegal position)\n");
                    break;
                }
                valid_pos_len = 0;
                pos_illegal = 1;
                continue;
            }

            Hash_Init_Stack();

            /*move list supplied? full "moves " token already checked above*/
            if (*mv_line_ptr == 'm')
            {
                MOVE cur_move;
                int move_len;
                char move_string[6];
                char *validated_line_part;

                /*if the current position and movelist doesn't continue a
                  game, then check every move; otherwise, only check the move
                  legality in the new part.*/
                if ((valid_pos_len != 0) && (memcmp(line, valid_pos_str, valid_pos_len)))
                    valid_pos_len = 0;

                validated_line_part = line + valid_pos_len;

                pos_validity = POS_OK;
                mv_line_ptr += 6; /*jump over "moves "*/
                move_len = Play_Get_Next_Pos_Move(mv_line_ptr, move_string);

                while (move_len > 0)
                {
                    if (!Play_Parse_Move(move_string, &cur_move))
                    {
                        pos_validity = POS_BAD_MOVE_FORMAT;
                        break;
                    }
                    /*if the move list is malformed, that could lead to an engine
                      crash, and that must not happen no matter how malformed the
                      input is. refusing the input is OK, but crashing isn't
                      because that might even lead to security problems.*/
                    if ((mv_line_ptr >= validated_line_part) && (!Play_Move_Is_Legal(cur_move, side)))
                    {
                        pos_validity = POS_ILLEGAL_MOVE;
                        break;
                    }
                    Play_Update_Special_Conditions(cur_move);
                    Search_Push_Status();
                    Search_Make_Move(cur_move);
                    side = Mvgen_Opp_Colour(side);

                    if (mv_stack_p >= MAX_PLIES) /*move list too long*/
                    {
                        pos_validity = POS_TOO_MANY_MOVES;
                        break;
                    }

                    mv_line_ptr += move_len;
                    if (*mv_line_ptr == ' ') /*another move following?*/
                        mv_line_ptr++;
                    move_len = Play_Get_Next_Pos_Move(mv_line_ptr, move_string);
                }
                if (pos_validity != POS_OK)
                {
                    move_string[5] = '\0'; /*should be zero terminated anyway*/
                    switch (pos_validity)
                    {
                    case POS_BAD_MOVE_FORMAT:
                        sprintf(printbuf, "info string error (wrong move format: %s)\n", move_string);
                        break;
                    case POS_ILLEGAL_MOVE:
                        sprintf(printbuf, "info string error (illegal move: %s)\n", move_string);
                        break;
                    case POS_TOO_MANY_MOVES:
                        sprintf(printbuf, "info string error (move list longer than %"PRId32" plies)\n", (int32_t)(MAX_PLIES - 1L));
                        break;
                    default:
                        strcpy(printbuf, "info string error (unknown)\n"); /*should not happen*/
                        break;
                    }
                    Play_Print(printbuf);
                    valid_pos_len = 0;
                    pos_illegal = 1;
                } else
                {
                    unsigned int new_valid_pos_len = (unsigned int)(mv_line_ptr - line);
                    /*+1 to also copy over the trailing 0.*/
                    memcpy(valid_pos_str + valid_pos_len, validated_line_part, new_valid_pos_len - valid_pos_len + 1);
                    valid_pos_len = new_valid_pos_len;
                    /*corner case: if the last move has been a promotion with implicit queening,
                      which the engine accepts despite violating UCI, and the next time, there is
                      explicit promotion in that move, then this is not checked for legality. there
                      is no case where a queen promotion is legal but underpromotion isn't.*/
                }
            } else
            {
                /*position without movelist, so there are no valid moves.*/
                valid_pos_len = 0;
            }
            continue;
        }
        if (!strcmp(command, "quit"))
        {
            /*make sure that the abort event is reset*/
            Play_Wait_For_Abort_Event(0);
            /*confirm the reset*/
            Play_Set_Abort_Event_Confirmation();
            return;
        }
        if (!strcmp(command, "stop"))
        {
            /*make sure that the abort event is reset*/
            Play_Wait_For_Abort_Event(0);
            /*confirm the reset*/
            Play_Set_Abort_Event_Confirmation();
            continue;
        }
        if (!strcmp(command, "ucinewgame"))
        {
            pos_illegal = 0;
            ponder_mode = 0;
            old_pos_hash = 0;
            cont_pos_num = 0;
            cont_pos_pv = -1;
            valid_pos_len = 0;
            side = WHITE;
            Play_Set_Starting_Position();
            Play_Reset_Position_Status();
            Play_Set_Cmd_Work();
            continue;
        }

        /*various UCI configuration options follow.
          note: "isready" and "uci" are handled in the other thread.*/

        if (!strcmp(command, "setoption"))
        {
            /*line+9 because the first 9 characters of line are 'setoption'.*/
            if (!strncmp(line+9, " name hash value ", 17))
            {
                int64_t hash_size=0;
                size_t used_hash_size;

                sscanf(line+9+17, "%"SCNd64, &hash_size);
                /*clip to valid range*/
                if (hash_size < HASH_MIN) hash_size = HASH_MIN;
                if (hash_size > HASH_MAX) hash_size = HASH_MAX;

                /*try to allocate the hash tables. in case of failure, retry
                  with half the previous size until the minimum has been
                  reached.*/
                for (used_hash_size = hash_size; used_hash_size >= HASH_MIN; used_hash_size /= 2)
                {
                    if (Play_Set_Hashtables(used_hash_size) == 0)
                        break;
                    if (used_hash_size == HASH_MIN) /*failed despite minimum value*/
                    {
                        Play_Print("info string error (can't alloc hash tables: exiting)\n");
                        return;
                    }
                }
                if (used_hash_size < (size_t) hash_size)
                {
                    sprintf(printbuf, "info string error (can't alloc hash tables: reducing to %"PRId64" MB)\n", (int64_t) used_hash_size);
                    Play_Print(printbuf);
                }
                Play_Set_Cmd_Work();
                continue;
            }
            if (!strncmp(line+9, " name keep hash tables value ", 29))
            {
                if (!strncmp(line+9+29, "true", 4))
                    keep_hash = 1;
                else if (!strncmp(line+9+29, "false", 5))
                    keep_hash = 0;
                continue;
            }
            if (!strncmp(line+9, " name clear hash", 16))
            {
                Play_Reset_Position_Status();
                Play_Print("info hashfull 0\n");
                continue;
            }
            if (!strncmp(line+9, " name contempt value [cps] value ", 33))
            {
                int32_t value=0;
                sscanf(line+9+33, "%"SCNd32, &value);
                /*clip to valid range*/
                if (value < -300) value = -300;
                if (value > 300) value = 300;
                /*internally, negative contempt values are optimistic, but e.g.
                  Stockfish uses it the other way around - that's what people will
                  expect.*/
                contempt_val = -value;
                continue;
            }
            if (!strncmp(line+9, " name contempt end [moves] value ", 33))
            {
                int32_t value=0;
                sscanf(line+9+33, "%"SCNd32, &value);
                /*clip to valid range*/
                if (value < 0) value = 0;
                if (value > MAX_PLIES/2) value = MAX_PLIES/2;
                contempt_end = value * 2; /*moves to plies*/
                continue;
            }
            if (!strncmp(line+9, " name ownbook value ", 20))
            {
                if (!strncmp(line+9+20, "true", 4))
                    disable_book = 0;
                else if (!strncmp(line+9+20, "false", 5))
                    disable_book = 1U;
                continue;
            }
            if (!strncmp(line+9, " name book moves", 16))
            {
                MOVE book_list[MAX_BOOK_MATCH];
                int book_list_len, i;
                if (pos_illegal)
                {
                    Play_Print("info string No book moves found.\n");
                    continue;
                }
                Book_Get_Moves(book_list, &book_list_len, side);
                if (book_list_len == 0)
                {
                    Play_Print("info string No book moves found.\n");
                    continue;
                }
                strcpy(printbuf, "info string Available book moves:");
                for (i = 0; i < book_list_len; i++)
                {
                    strcat(printbuf, " ");
                    strcat(printbuf, Play_Translate_Moves(book_list[i]));
                }
                strcat(printbuf, "\n");
                Play_Print(printbuf);
                continue;
            }
            if (!strncmp(line+9, " name show current move value ", 30))
            {
                if (!strncmp(line+9+30, "continuously", 12))
                    show_currmove = CURR_ALWAYS;
                else if (!strncmp(line+9+30, "every second", 12))
                    show_currmove = CURR_UPDATE;
                continue;
            }
            if (!strncmp(line+9, " name uci_limitstrength value ", 30))
            {
                if (!strncmp(line+9+30, "true", 4))
                    elo_throttle = 1;
                else if (!strncmp(line+9+30, "false", 5))
                    elo_throttle = 0;
                continue;
            }
            if (!strncmp(line+9, " name uci_elo value ", 20))
            {
                int32_t value=0;
                sscanf(line+9+20, "%"SCNd32, &value);
                /*clip to valid range*/
                if (value < 1000) value = 1000;
                if (value > elo_max) value = elo_max;
                elo_setting = value;
                continue;
            }
            if (!strncmp(line+9, " name cpu speed throttle value ", 31))
            {
                if (!strncmp(line+9+31, "true", 4))
                    cpu_throttle = 1;
                else if (!strncmp(line+9+31, "false", 5))
                    cpu_throttle = 0;
                continue;
            }
            if (!strncmp(line+9, " name cpu speed [%] value ", 26))
            {
                int32_t value=0;
                sscanf(line+9+26, "%"SCNd32, &value);
                /*clip to valid range*/
                if (value < 1) value = 1;
                if (value > 100) value = 100;
                cpu_speed = value;
                continue;
            }
            if (!strncmp(line+9, " name cpu speed [knps] value ", 29))
            {
                int64_t value=0;
                sscanf(line+9+29, "%"SCNd64, &value);
                /*clip to valid range*/
                if (value < MIN_THROTTLE_KNPS) value = MIN_THROTTLE_KNPS;
                if (value > MAX_THROTTLE_KNPS) value = MAX_THROTTLE_KNPS;
                max_nps_rate = (uint64_t) (value * 1000LL); /*convert kNPS to NPS*/
                continue;
            }
            if (!strncmp(line+9, " name move overhead [ms] value ", 31))
            {
                int32_t value=0;
                sscanf(line+9+31, "%"SCNd32, &value);
                /*clip to valid range*/
                if (value < 0) value = 0;
                if (value > 1000) value = 1000;
                move_overhead = value;
                continue;
            }
            if (!strncmp(line+9, " name eval noise [%] value ", 27))
            {
                int32_t value=0;
                sscanf(line+9+27, "%"SCNd32, &value);
                /*clip to valid range*/
                if (value > 100) value = 100;
                if (value < 0) value = 0;
                uci_noise = value;
                continue;
            }

            continue;
        }

        if ((!strcmp(command, "perft")) || (!strcmp(command, "cperft")))
        {
            /*cperft is perft plus one QS capture level in the leaf nodes
              where captures and non-capture queening moves are counted, but
              no underpromotions (not even if they are also captures).*/
            int is_cperft = !strcmp(command, "cperft");

            if (line[5 + is_cperft] == ' ') /*parameter follows*/
            {
                int64_t total_time;
                uint64_t nps;
                int depth=0;

                sscanf(line+6+is_cperft, "%d", &depth);
                /*clip to valid range*/
                if (depth < 0) depth = 0;
                if (depth > 20) depth = 20;
                perft_depth = depth;
                perft_nodes = 0;
                perft_check_nodes = PERFT_CHECK_NODES;
                perft_nps_10ms = 0;
                abort_perft = 0;

                perft_start_time = Play_Get_Millisecs();
                Play_Perft(depth, side, is_cperft);
                total_time = Play_Get_Millisecs() - perft_start_time;
                if (total_time > 0)
                    nps = (perft_nodes * 1000U) / total_time;
                else
                    nps = 0;

                sprintf(printbuf, "info string %s depth %d nodes %"PRIu64" time %"PRId64" nps %"PRIu64"\n",
                                  !is_cperft ? "perft" : "cperft", depth, perft_nodes, total_time, nps);
                Play_Print(printbuf);
            }
            continue;
        }

#ifdef AUTOTUNE
        if ((!strcmp(command, "tunese")) || (!strcmp(command, "tuneqs")))
        {
            int iterations = 0, step = 0, use_qs;
            char filename[256];

            use_qs = !strcmp(command, "tuneqs");
            line[255] = '\0';
            memset(filename, 0, sizeof(filename));
            sscanf(line + 7, "%d %d %255s", &iterations, &step, filename);
            filename[255] = '\0';
            if ((iterations < 0) || (step <= 0) || (strlen(filename) == 0))
            {
                puts("Error: tune [iter (num >= 0)] [step (num > 0)] [filename]");
                continue;
            }
            Tune_Run(iterations, step, filename, use_qs);
            continue;
        }
#endif
    }
}

/*answer to the "uci" command*/
static void Play_Print_UCI_Info(void)
{
    char printbuf[1536];

    /*putting everything into one buffer results in only one write() system call.*/

    sprintf(printbuf, "id name " VERSION_INFO_DIALOGUE_LINE_1 " " TARGET_BUILD_STRING " bit\n" \
               "id author Rasmus Althoff\n" \
               "option name Hash type spin default 8 min 1 max 1024\n" \
               "option name Keep Hash Tables type check default true\n" \
               "option name Clear Hash type button\n" \
               "option name Book Moves type button\n" \
               "option name OwnBook type check default true\n" \
               "option name Contempt Value [cps] type spin default %"PRId32" min -300 max 300\n" \
               "option name Contempt End [moves] type spin default %"PRId32" min 0 max %"PRId32"\n" \
               "option name Eval Noise [%%] type spin default 0 min 0 max 100\n" \
               "option name Move Overhead [ms] type spin default %"PRId64" min 0 max 1000\n" \
               "option name UCI_Elo type spin default %d min %d max %d\n" \
               "option name UCI_LimitStrength type check default false\n" \
               "option name CPU Speed Throttle type check default false\n" \
               "option name CPU Speed [%%] type spin default 100 min 1 max 100\n" \
               "option name CPU Speed [kNPS] type spin default %"PRId32" min %"PRId32" max %"PRId32"\n" \
               "option name Show Current Move type combo default Every Second var Every Second var Continuously\n" \
               "option name UCI_EngineAbout type string default The CT800 is free software under GPLv3+. Website: www.ct800.net\n" \
               "uciok\n", (int32_t) -CONTEMPT_VAL, (int32_t) CONTEMPT_END/2, (int32_t) MAX_PLIES/2,
                          (int64_t) DEFAULT_MOVE_OVERHEAD,
                          elo_max, 1000, elo_max,
                          (int32_t) MAX_THROTTLE_KNPS, (int32_t) MIN_THROTTLE_KNPS, (int32_t) MAX_THROTTLE_KNPS);
    /*internally, negative contempt values are optimistic, but e.g. Stockfish
      uses it the other way around - that's what people will expect.
      and the internal contempt end is in plies, not moves.*/
    Play_Print(printbuf);
}

/*fill the ring buffer for the other thread. CMD_POSITION is set when
  transferring the position command. up to "FEN" itself, everything is case
  insensitive and gets converted to lower case, but the FEN string itself IS
  case sensitive. Only the W/B for the side to move in the FEN is case
  insensitive, but that's handled in the FEN parser.

  actually, that was intended as lockless queue, but it ended up with messy
  memory barriers because it must be ensured that the other thread never gets
  the updated write index before the buffer content is updated. with the weak
  memory ordering of ARM CPUs (Android version), it was easier to just use a
  mutex and be done. it's not time critical anyway.*/
static void Play_Write_Cmd(const char *line, unsigned int cmd_len,
                           enum E_CMD_TYPE cmd_flag)
{
    int64_t buf_stop_time;
    unsigned int i, line_len;
    uint8_t cmd_byte;
    enum E_CMD_CAN_WRITE can_write;

    /*the abort signal has to be sent before the "stop" command is issued so
      that no search takes place until the "stop" command has been processed.*/
    if (cmd_flag == CMD_STOP) Play_Set_Abort_Event();

    Play_Acquire_Lock(&io_lock);

    /*+2 for command length and +1 for avoiding the ring buffer empty condition.*/
    line_len = cmd_len + 3;

    /*if there is danger of a buffer overrun which would swallow commands,
      give up to 1 second time to the other thread to consume the messages.
      no events used here because that does not really happen anyway as
      the buffer is big compared to UCI commands, even "position" with a
      full game using "moves".*/
    buf_stop_time = Play_Get_Millisecs() + 1000LL;
    do {
        can_write = CMD_CAN_WRITE_OK;
        if (cmd_read_idx != cmd_write_idx) /*then the ring buffer would be empty*/
        {
            /*check whether the command will fit in the ring buffer*/
            if (cmd_read_idx > cmd_write_idx) /*without wrap-around*/
            {
                if (cmd_read_idx <= cmd_write_idx + line_len)
                    can_write = CMD_CAN_WRITE_WAIT; /*won't fit*/
            } else /*with wrap-around*/
            {
                if (cmd_read_idx + CMD_BUF_SIZE <= cmd_write_idx + line_len)
                    can_write = CMD_CAN_WRITE_WAIT; /*won't fit*/
            }
            if (can_write == CMD_CAN_WRITE_WAIT) /*give time to the other thread*/
            {
                if (Play_Get_Millisecs() < buf_stop_time) /*timeout?*/
                {
                    /*releasing the mutex here is OK because the write index
                      is not changed from the other thread, only the read
                      index, and that is reloaded at the top of the loop.*/
                    Play_Release_Lock(&io_lock);
                    Play_Sleep(10); /*no, wait*/
                    Play_Acquire_Lock(&io_lock);
                }
                else
                {
                    can_write = CMD_CAN_WRITE_FORCE; /*yes, proceed*/
                    /*set buffer to empty because overwrite would garble up the
                      contents anyway, and even worse. besides, this only happens
                      if the GUI issues a lot of commands during search, which it
                      should not do, and the buffer size is sufficient for all the
                      options and a long position/moves command and then some more.*/
                    cmd_read_idx = 0;
                    cmd_write_idx = 0;
                }
            }
        }
    } while (can_write == CMD_CAN_WRITE_WAIT);

    /*the command length is done via memcpy because the command buffer is type
      char, which may be signed. reading that bytewise and shifting it up may
      not work as expected. memcpy is a clean and portable way to deal with
      that, and the compiler will optimise the function call away. it has to
      be split because the command might be at the wrap-around.*/

    /*command length, MSB*/
    cmd_byte = (uint8_t) ((cmd_len >> 8u) & 0xFFU);
    memcpy(cmd_buf + cmd_write_idx, &cmd_byte, sizeof(uint8_t));
    if (cmd_write_idx < CMD_BUF_SIZE-1)
        cmd_write_idx++;
    else
        cmd_write_idx = 0;

    /*command length, LSB*/
    cmd_byte = (uint8_t) (cmd_len & 0xFFU);
    memcpy(cmd_buf + cmd_write_idx, &cmd_byte, sizeof(uint8_t));
    if (cmd_write_idx < CMD_BUF_SIZE-1)
        cmd_write_idx++;
    else
        cmd_write_idx = 0;

    if (cmd_flag == CMD_POSITION)
    {
        /*only with the "position" command:
          - stop de-casing after FEN.
          - start de-casing from "moves" on.*/
        enum E_FEN_SCAN fen_state;

        for (i = 0, fen_state = FEN_SCAN_F; i < cmd_len; i++)
        {
            /*copy over the input line*/
            char ch = line[i];

            if (fen_state < FEN_SCAN_DONE)
            {
                switch (fen_state)
                {
                case FEN_SCAN_F:
                    if ((ch == 'f') || (ch == 'F')) fen_state++;
                    break;
                case FEN_SCAN_E:
                    if ((ch == 'e') || (ch == 'E')) fen_state++;
                    else fen_state = FEN_SCAN_F;
                    break;
                case FEN_SCAN_N:
                    if ((ch == 'n') || (ch == 'N'))
                    {
                        ch = 'n'; /*be sure to de-case "FEN" completely*/
                        fen_state++;
                    } else fen_state = FEN_SCAN_F;
                    break;
                case FEN_SCAN_MOVES: /*look for start of the "moves" parameter*/
                    if ((ch == 'm') || (ch == 'M')) fen_state++;
                    break;
                default:
                    break;
                }
                if (fen_state != FEN_SCAN_MOVES) /*the FEN string is case sensitive*/
                {
                    if ((ch <= 'Z') && (ch >= 'A'))
                        ch += 'a' - 'A';
                }
            } else
            {
                if ((ch <= 'Z') && (ch >= 'A'))
                    ch += 'a' - 'A';
            }

            cmd_buf[cmd_write_idx] = ch;
            if (cmd_write_idx < CMD_BUF_SIZE-1)
                cmd_write_idx++;
            else
                cmd_write_idx = 0;
        }
    } else /*not a "position" command, drop the FEN scan.*/
    {
        for (i = 0; i < cmd_len; i++)
        {
            /*copy over the input line*/
            char ch = line[i];

            if ((ch <= 'Z') && (ch >= 'A'))
                ch += 'a' - 'A';

            cmd_buf[cmd_write_idx] = ch;
            if (cmd_write_idx < CMD_BUF_SIZE-1)
                cmd_write_idx++;
            else
                cmd_write_idx = 0;
        }
    }

    /*no null termination because commands are handed over as 2 bytes
      for the length followed by command data.*/

    Play_Release_Lock(&io_lock);

    /*do the printing outside of the IO lock, just to be safe from deadlocks*/
    if ((can_write == CMD_CAN_WRITE_FORCE) && (uci_debug))
        Play_Print("info string debug: UCI buffer overrun.\n");

    /*wake up the other thread if it is sleeping*/
    Play_Wakeup_UCI();
}

/*compare up to N characters, case insensitive, and expect a command end*/
static int Play_Strnicmp_End(const char *tst_string, const char *ref_string, size_t nbytes)
{
    size_t i;
    char ch1;
    ch1 = tst_string[nbytes];
    if ((ch1 != ' ') && (ch1 != '\0')) /*\t, \r and \n have been remapped*/
        return(1);
    for (i = 0; i < nbytes; i++)
    {
        char ch2;

        ch1 = tst_string[i];
        if ((ch1 <= 'Z') && (ch1 >= 'A'))
            ch1 += 'a' - 'A';

        ch2 = ref_string[i];
        if ((ch2 <= 'Z') && (ch2 >= 'A'))
            ch2 += 'a' - 'A';

        if (ch1 != ch2)
            return(2);
    }
    return(0);
}

/*coarse calibration of the machine performance for the Elo range*/
static void Play_Calibrate_Machine(void)
{
    MOVE amove, pmove;
    uint64_t calib_nps, spent_nodes=0, calc_nodes;
    int64_t end_wait_time, spent_time=0;

    pmove.u = MV_NO_MOVE_MASK;
    disable_book = 1;            /*internal opening book is inactive*/
    show_currmove = CURR_UPDATE; /*show current root move once per second*/
    eval_noise = 0;              /*in percent; 0 means "no noise"*/
    contempt_val = CONTEMPT_VAL; /*avoid early draw unless the engine is worse than -30*/
    contempt_end = CONTEMPT_END; /*in plies from the start position*/
    uci_debug = 0;               /*no debug output*/

    Hash_Init();
    Play_Set_Starting_Position();
    Play_Reset_Position_Status();

    /*wait for CPU to ramp up the clock if necessary.*/
    end_wait_time = Play_Get_Millisecs() + 50;
    do { } while (Play_Get_Millisecs() < end_wait_time);

    Search_Get_Best_Move(&amove, pmove, 250, 0, 1, MAX_DEPTH-1, 100, NO_THROTTLE_NPS,
                         WHITE, NULL, -1, 0, 0, &spent_nodes, &spent_time);

    if (spent_time > 0)
        calib_nps = (spent_nodes * 1000U) / spent_time; /*time is in milliseconds*/
    else
        calib_nps = 1000U * 1000U;

    /*derive available Elo range from the calibrated NPS.
      assume about 2300 elo at 30 kNPS.*/
    elo_max    = BASE_ELO;
    calc_nodes = BASE_NODES;

    /*assume roughly 56 elo per speed doubling.
      sqrt(sqrt(2)) = 1.1892, but avoid pulling in the float libraries.*/
    while (calc_nodes * 2U <= calib_nps)
    {
        calc_nodes *= 2U;
        elo_max += ELO_DOUBLE;
    }
    while ((calc_nodes * 14142U) / 10000U <= calib_nps)
    {
        calc_nodes *= 14142U;
        calc_nodes /= 10000U;
        elo_max += ELO_DOUBLE/2;
    }
    while ((calc_nodes * 11892U) / 10000U <= calib_nps)
    {
        calc_nodes *= 11892U;
        calc_nodes /= 10000U;
        elo_max += ELO_DOUBLE/4;
    }
    while ((calc_nodes * 10905U) / 10000U <= calib_nps)
    {
        calc_nodes *= 10905U;
        calc_nodes /= 10000U;
        elo_max += ELO_DOUBLE/8;
    }

    /*round downwards for a smooth 50 value.*/
    elo_max /= 50;
    elo_max *= 50;

    /*subtract 50 Elo for having room for the Elo increase with long time controls.*/
    elo_max -= 50;

    Play_Set_Starting_Position();
    Play_Reset_Position_Status();
}

static
#ifdef CTWIN
void
#else
void *
#endif
Play_Get_UCI_Input_Thrd(VAR_UNUSED void *data)
{
    /*make the line 16 bytes longer to prevent out of bounds access
     with Play_Strnicmp_End() if the user enters a lot of whitespaces*/
    static char line[CMD_UCI_LEN + 16]; /*static saves stack*/

    /*announce the available UCI options*/
    Play_Print_UCI_Info();

    for (;;)
    {
        int do_wait = 0;
        enum E_CMD_TYPE cmd_flag = CMD_GENERIC;
        int cmd_len = Play_Read_Input(line, CMD_UCI_LEN);

        if (cmd_len < 0) /*end the program*/
            break;

        if (cmd_len == 0) /*that was an empty command*/
            continue;

        if (!Play_Strnicmp_End(line, "stop", 4))
        /*interrupt calculations*/
        {
            /*make sure the confirmation event is reset. can happen if the
              threads had come out of sync before, which should not happen.*/
            Play_Wait_For_Abort_Event_Confirmation(0);

            /*the write command needs to be before the completion check
              loop below because it's the processing of the stop command in the
              other thread that will reset the abort flag.*/
            Play_Write_Cmd(line, cmd_len, CMD_STOP);

            /*wait for the consumer thread to process the stop command and
              reset the abort flag. otherwise, multiple go/stop commands at
              a fast rate might lead to a situation where the latest stop
              command is copied over to the other thread, but the processing
              of the stop command before has reset the abort flag, in which
              case the current stop command would not abort the search.

              not necessary for the quit command because there cannot be
              multiple quits, and since stop commands are synced here, there
              cannot be leftover stops.*/

            Play_Wait_For_Abort_Event_Confirmation(5000L);

            if (Play_Get_Abort() != 0)
            /*somehow, the threads have come out of sync. should not happen.*/
                Play_Print("info string error (thread sync failed)\n");

            continue;
        }

        if (!Play_Strnicmp_End(line, "quit", 4))
            break;

        if (!Play_Strnicmp_End(line, "isready", 7))
        {
            Play_Print("readyok\n");
            continue;
        }

        /*handle "uci" here because it is static output anyway, plus that
          the correct answer sequence with uci / isready can only be kept
          in line if "uci" is handled in this thread. it could be confusing
          to GUIs if "readyok" arrived before the answer to "uci".
          on the other hand, this will also answer "uci" during search with
          the search output going on, but Shredder does the same.*/
        if (!Play_Strnicmp_End(line, "uci", 3))
        {
            Play_Print_UCI_Info();
            continue;
        }

        /*handle debug on/off also during search.*/
        if (!Play_Strnicmp_End(line, "debug", 5))
        {
            if (!Play_Strnicmp_End(line+6, "on", 2))
                uci_debug = 1U;
            else if (!Play_Strnicmp_End(line+6, "off", 3))
                uci_debug = 0;
            /*sync up to get the current debug setting to the other thread.*/
            __sync_synchronize();
            continue;
        }

        if (!Play_Strnicmp_End(line, "position", 8))
            cmd_flag = CMD_POSITION;

        /*the following commands require actual work.*/
        if ((!Play_Strnicmp_End(line, "ucinewgame", 10)) ||
            (!Play_Strnicmp_End(line, "setoption name hash value ", 26)))
        {
            /*reset confirmation event.*/
            Play_Wait_For_Cmd_Work(0);
            do_wait = 1;
        }

        /*cmd_len <= 0 has already been caught above.*/
        Play_Write_Cmd(line, (unsigned int) cmd_len, cmd_flag);

        /*wait for up to 5 seconds with work commands.*/
        if (do_wait)
            Play_Wait_For_Cmd_Work(5000L);
    }

    /*termination*/
    Play_Write_Cmd("quit", 4, CMD_STOP);
#ifdef CTWIN
    _endthread();
#else
    pthread_exit(NULL);
    return(NULL);
#endif
}

int main(VAR_UNUSED int argc, VAR_UNUSED char **argv)
{
    enum E_PROT_TYPE protocol = PROT_NONE;
#ifdef CTWIN
#else
    int io_flags;
    pthread_t input_thread;
#endif

    Play_Init_Millisecs();

    /*no buffering - read/write via file numbers directly*/
    (void) setvbuf(stdin,  NULL, _IONBF, 0);
    (void) setvbuf(stdout, NULL, _IONBF, 0);

#ifdef CTWIN
    /*Windows has no fcntl(), but has stdin/out blocking anyway.*/
#else
    /*try to set stdin to blocking mode, if possible*/
    io_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (io_flags >= 0)
    {
        if ((io_flags & O_NONBLOCK) != 0)
        {
            io_flags &= ~O_NONBLOCK;
            (void) fcntl(STDIN_FILENO, F_SETFL, io_flags);
        }
    }

    /*try to set stdout to blocking mode, if possible*/
    io_flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
    if (io_flags >= 0)
    {
        if ((io_flags & O_NONBLOCK) != 0)
        {
            io_flags &= ~O_NONBLOCK;
            (void) fcntl(STDOUT_FILENO, F_SETFL, io_flags);
        }
    }
#endif

    Play_Print_Output(VERSION_INFO_DIALOGUE_LINE_1 " " TARGET_BUILD_STRING " bit UCI version\n" \
                      VERSION_INFO_DIALOGUE_LINE_2 "\n" \
                      VERSION_INFO_DIALOGUE_LINE_3 "\n");

    /*try default hash table usage*/
    if (Play_Set_Hashtables(HASH_DEFAULT) != 0)
    {
        /*if that is not possible, try minimum hash tables.*/
        if (Play_Set_Hashtables(HASH_MIN) != 0)
        {
            Play_Print_Output("info string error (can't alloc hash tables: exiting)\n");
            return(1);
        }
    }

    {
        uint32_t rand_seed;
#ifdef CTWIN
        /*Windows: Play_Get_Millisecs() gives milliseconds since
          our last Play_Init_Millisecs() call, but GetTickCount()
          counts since system startup, with roll-over after 49.7 days.*/
        rand_seed = (uint32_t) GetTickCount();
#else
        rand_seed = (uint32_t) Play_Get_Millisecs();
#endif
        Util_Seed(rand_seed);
    }

    while (protocol == PROT_NONE)
    {
        char line[32];
        *line = '\0'; /*init to empty string*/
        if (Play_Read_Input(line, sizeof(line)) < 0)
            break;
        line[31] = '\0'; /*force null termination*/
        if (!Play_Strnicmp_End(line, "uci", 3))
            protocol = PROT_UCI;
        else if ((!Play_Strnicmp_End(line, "quit", 4)) ||
                 (!Play_Strnicmp_End(line, "exit", 4)) ||
                 (!Play_Strnicmp_End(line, "bye", 3)))
            break;
        else if ((!Play_Strnicmp_End(line, "help", 4)) ||
                 (!Play_Strnicmp_End(line, "?", 1)))
        {
            Play_Print_Output("The CT800 chess engine is designed for use with the UCI protocol.\n" \
                              "Install a chess GUI that supports this protocol, and register the\n" \
                              "CT800 chess engine in that GUI. Use \"quit\" to exit.\n");
        }
    }

    if (protocol == PROT_UCI) /*start UCI mode*/
    {
        /*initialise the mutexes and events. yes, this is using goto, but
          it's the right thing for error handling, especially stacked one.*/
#ifdef CTWIN
        InitializeCriticalSection(&print_lock);
        InitializeCriticalSection(&io_lock);
        InitializeCriticalSection(&abort_check_lock);
        uci_event        = CreateEvent(NULL, FALSE, FALSE, NULL);
        cmd_work_event   = CreateEvent(NULL, FALSE, FALSE, NULL);
        abort_event      = CreateEvent(NULL, FALSE, FALSE, NULL);
        abort_event_conf = CreateEvent(NULL, FALSE, FALSE, NULL);
        if ((uci_event == NULL) || (cmd_work_event == NULL) || (abort_event == NULL) || (abort_event_conf == NULL))
        {
            Play_Print_Output("info string error (can't create event: exiting)\n");
            goto cleanup_sections;
        }
#else
        /*mutexes have already been initialised statically. if monotonic clock
          is supported (optional in Posix), then try to set the time waiting
          conditions to use monotonic clock. if that fails, use CLOCK_REALTIME
          on them.*/
        #ifdef CLOCK_MONOTONIC
        #ifndef NO_MONO_COND
        {
            int error_cnt = 0;
            pthread_condattr_t attr;

            /*prepare condition attribute*/
            if (pthread_condattr_init(&attr) != 0)
                error_cnt = 1;

            if (error_cnt == 0)
            {
                if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) != 0)
                {
                    error_cnt = 2;
                    pthread_condattr_destroy(&attr);
                }
            }

            if (error_cnt == 0)
            {
                int err_cmd_work_cond, err_abort_cond, err_abort_conf_cond;

                err_cmd_work_cond   = pthread_cond_init(&cmd_work_cond, &attr);
                err_abort_cond      = pthread_cond_init(&abort_event_cond, &attr);
                err_abort_conf_cond = pthread_cond_init(&abort_event_conf_cond, &attr);

                if ((err_cmd_work_cond) || (err_abort_cond) || (err_abort_conf_cond))
                {
                    error_cnt = 3;
                    /*destroy the conditions because they will be re-initialised.*/
                    if (!err_cmd_work_cond)
                        pthread_cond_destroy (&cmd_work_cond);
                    if (!err_abort_cond)
                        pthread_cond_destroy (&abort_event_cond);
                    if (!err_abort_conf_cond)
                        pthread_cond_destroy (&abort_event_conf_cond);
                }
                pthread_condattr_destroy(&attr);
            }

            /*if some error has occurred, use real time for pthread wait conditions.*/
            if (error_cnt != 0)
            /*reset to default conditions*/
            {
                pthread_cond_init(&cmd_work_cond, NULL);
                pthread_cond_init(&abort_event_cond, NULL);
                pthread_cond_init(&abort_event_conf_cond, NULL);
                ct_cond_clock_mode = CLOCK_REALTIME;
            }
        }
        #else
        /*Android NDK before API level 21 does not support monotonic time in conditions.*/
        ct_cond_clock_mode = CLOCK_REALTIME;
        #endif
        #endif
#endif

        no_output = 1;
        Play_Calibrate_Machine();
        no_output = 0;

        /*start the UCI input thread*/
#ifdef CTWIN
        _beginthread(Play_Get_UCI_Input_Thrd, 0, NULL );
#else
        if (pthread_create(&input_thread, NULL, Play_Get_UCI_Input_Thrd, NULL))
        {
            Play_Print_Output("info string error (can't create input thread: exiting)\n");
            goto mutex_clean_all;
        }
#endif

        /*common code path: launch the UCI/worker thread*/
        Play_UCI();

        /*now for resource clean up. actually, that is not necessary because
          the operating system releases all resources when the process finishes.
          However, I think it is still cleaner to tidy up oneself.*/

        /*clean up the mutexes. the input thread is self cleaning.*/
#ifdef CTWIN
cleanup_sections:
        DeleteCriticalSection(&abort_check_lock);
        DeleteCriticalSection(&io_lock);
        DeleteCriticalSection(&print_lock);
        if (uci_event        != NULL) CloseHandle(uci_event);
        if (cmd_work_event   != NULL) CloseHandle(cmd_work_event);
        if (abort_event      != NULL) CloseHandle(abort_event);
        if (abort_event_conf != NULL) CloseHandle(abort_event_conf);
#else
mutex_clean_all:
        /*mutexes*/
        pthread_mutex_destroy(&abort_check_lock);
        pthread_mutex_destroy(&io_lock);
        pthread_mutex_destroy(&print_lock);
        /*mutex / condition combinations*/
        pthread_mutex_destroy(&uci_lock);
        pthread_cond_destroy (&uci_cond);
        pthread_mutex_destroy(&cmd_work_lock);
        pthread_cond_destroy (&cmd_work_cond);
        pthread_mutex_destroy(&abort_event_lock);
        pthread_cond_destroy (&abort_event_cond);
        pthread_mutex_destroy(&abort_event_conf_lock);
        pthread_cond_destroy (&abort_event_conf_cond);
#endif
    }

    /*deallocate the hash tables.*/
    if (T_T     != NULL) free(T_T);
    if (Opp_T_T != NULL) free(Opp_T_T);

    return(0);
}
