/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Arrocco - CT800 port: the engine's front end.
 *
 * The CT800 core (search.c, eval.c, move_gen.c, hashtables.c, book.c, kpk.c,
 * util.c) is a library without a main: it owns no board. The board, the piece
 * lists, the move stack, the hash table pointers and a handful of settings are
 * globals that its FRONT END defines, and it calls back into seven Play_*
 * functions. Upstream's front end is play.c, a POSIX/Windows UCI program with
 * pthreads, stdio, calloc and a terminal. This file is the front end Arrocco
 * uses instead: no threads, no stdio, no heap, one blocking call.
 *
 * Provenance. Everything between the "--- from upstream play.c ---" markers is
 * copied verbatim from lib/ct800/upstream/application-uci/play.c (CT800 V1.46,
 * Copyright (C) 2015-2024 Rasmus Althoff, Copyright (C) 2010-2014 George
 * Georgopoulos, GPL-3.0-or-later): the global definitions and the position
 * setup (FEN reader, piece lists, move parser). It is copied and not included
 * because play.c also holds the UCI loop, which cannot be compiled here — and
 * because upstream/ must stay byte-identical to what was shipped. The rest of
 * the file is Arrocco's.
 *
 * What is NOT copied from play.c:
 *   - Play_Set_Hashtables: it calloc()s. ct800_glue_init() carves the two
 *     tables out of memory the caller owns instead.
 *   - Play_Print_Output, Play_Read_Input, the UCI parser, perft, the machine
 *     calibration, the Elo conversion (that one lives in arrocco/engine.h,
 *     as the level table).
 *   - the thread, event and lock machinery: there is only one thread in here.
 *
 * ONE instance only: see ct800_glue.h.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ct800_memory.h"
#include "ctdefs.h"
#include "util.h"
#include "move_gen.h"
#include "book.h"
#include "hashtables.h"
#include "eval.h"
#include "search.h"

#include "ct800_glue.h"

/* The engine calls these; they are declared in no upstream header. */
int64_t Play_Get_Millisecs(void);
int64_t Play_Get_Own_Millisecs(void);
unsigned int Play_Get_Abort(void);
void Play_Wait_For_Abort_Event(int32_t millisecs);
void Play_Print(const char *str);
char *Play_Translate_Moves(MOVE m);
int Play_Move_Is_Valid(MOVE key_move, const MOVE *restrict movelist, int move_cnt);

/* ----------------------------- from upstream play.c ----------------------------- */
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

/* ---- position setup, piece lists and FEN reader: also verbatim from play.c ---- */

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

/* -------------------------- end of the upstream copy ---------------------------- */

/* ---------------------------- the Play_* callbacks ------------------------------- */

/* Set once by ct800_glue_set_clock(), read from the search. */
static int64_t (*g_now_ms)(void);
static void (*g_sleep_ms)(int32_t);

/* Written by ct800_glue_abort() from any thread, polled by the search.
   A plain volatile int is enough: the only transition that matters is 0 -> 1,
   and a search that notices it one poll late simply stops one poll later. */
static volatile unsigned int g_abort;
static volatile unsigned int g_busy;

/* Play_Print's ring buffer. The engine prints an "info" line several times a
   second; nothing reads it on the device, so the oldest characters go. */
#define CT800_LOG_SIZE 512u
static char g_log[CT800_LOG_SIZE];
static unsigned int g_log_head;   /* next write position */
static unsigned int g_log_used;   /* valid characters, <= CT800_LOG_SIZE */

void ct800_glue_set_clock(int64_t (*now_ms)(void), void (*sleep_ms)(int32_t))
{
    g_now_ms = now_ms;
    g_sleep_ms = sleep_ms;
}

int64_t Play_Get_Millisecs(void)
{
    return (g_now_ms != NULL) ? g_now_ms() : 0;
}

/* Upstream splits "wall clock" from "own CPU time" so that a throttled search
   sleeps against the wall and measures against the CPU. Here they are the
   same clock: the search task is the only thing running on its core. */
int64_t Play_Get_Own_Millisecs(void)
{
    return Play_Get_Millisecs();
}

unsigned int Play_Get_Abort(void)
{
    return g_abort;
}

/* Upstream blocks on a condition variable that the input thread signals. With
   no second thread to signal anything, this waits in short slices so that an
   abort raised meanwhile is noticed within one slice. */
void Play_Wait_For_Abort_Event(int32_t millisecs)
{
    const int32_t slice = 4;
    while (millisecs > 0)
    {
        int32_t step = (millisecs > slice) ? slice : millisecs;
        if (g_abort)
            return;
        if (g_sleep_ms != NULL)
            g_sleep_ms(step);
        else
            return;   /* no sleep hook: do not spin, just let the search go on */
        millisecs -= step;
    }
}

void Play_Print(const char *str)
{
    size_t len;

    if (str == NULL)
        return;
    len = strlen(str);
    /* Only the tail can survive anyway, so skip straight to it. */
    if (len > CT800_LOG_SIZE)
    {
        str += len - CT800_LOG_SIZE;
        len = CT800_LOG_SIZE;
    }
    while (len-- > 0)
    {
        g_log[g_log_head] = *str++;
        g_log_head = (g_log_head + 1u) % CT800_LOG_SIZE;
        if (g_log_used < CT800_LOG_SIZE)
            g_log_used++;
    }
}

int ct800_glue_log_drain(char *out, int out_size)
{
    unsigned int start, i, n;

    if ((out == NULL) || (out_size <= 0))
        return 0;
    n = g_log_used;
    if (n > (unsigned int)(out_size - 1))
        n = (unsigned int)(out_size - 1);
    /* The n newest characters, oldest first. */
    start = (g_log_head + CT800_LOG_SIZE - n) % CT800_LOG_SIZE;
    for (i = 0; i < n; i++)
        out[i] = g_log[(start + i) % CT800_LOG_SIZE];
    out[n] = '\0';
    g_log_used = 0;
    g_log_head = 0;
    return (int)n;
}

/* ------------------------------- the public API ---------------------------------- */

static size_t g_hash_entries;   /* per table, a power of two */
static int g_ready;

size_t ct800_glue_hash_entries(void)
{
    return g_hash_entries;
}

void ct800_glue_seed(uint32_t seed)
{
    Util_Seed(seed);
}

int ct800_glue_init(void *hash_memory, size_t bytes)
{
    /* The pawn hash tables sit at the front of the block; see ct800_memory.h.
       Both sizes are already multiples of 8, so nothing needs padding. */
    const size_t pawn_bytes = (PMAX_TT + 1) * sizeof(TT_PTT_ST);
    const size_t rook_bytes = (PMAX_TT + 1) * sizeof(TT_PTT_ROOK_ST);
    size_t entries, per_table, tt_bytes;
    unsigned char *base = (unsigned char *)hash_memory;

    g_ready = 0;
    g_hash_entries = 0;
    T_T = NULL;
    Opp_T_T = NULL;
    ct800_pawn_hash = NULL;
    ct800_rook_hash = NULL;
    if ((hash_memory == NULL) || (g_now_ms == NULL) || (bytes < CT800_HASH_MIN_BYTES))
        return 1;

    ct800_pawn_hash = (TT_PTT_ST (*)[PMAX_TT + 1])(void *)base;
    ct800_rook_hash = (TT_PTT_ROOK_ST (*)[PMAX_TT + 1])(void *)(base + pawn_bytes);
    base += pawn_bytes + rook_bytes;
    tt_bytes = bytes - (pawn_bytes + rook_bytes);

    /* MAX_TT is used as an index MASK (hashtables.c: tt[pos_hash & MAX_TT]), so
       the entry count per table has to be a power of two, and at least
       DEF_MAX_TT because Hash_Get_Usage() scans that many entries whatever the
       real size. CLUSTER_SIZE extra entries sit past the mask as the cluster
       overhang, exactly as upstream allocates them. */
    entries = DEF_MAX_TT;
    for (;;)
    {
        size_t next = entries * 2u;
        if (next < entries) /* overflow */
            break;
        per_table = (next + CLUSTER_SIZE) * sizeof(TT_ST);
        if (per_table > tt_bytes / 2u)
            break;
        entries = next;
    }
    per_table = (entries + CLUSTER_SIZE) * sizeof(TT_ST);

    T_T     = (TT_ST *)(void *)base;
    Opp_T_T = (TT_ST *)(void *)(base + per_table);
    MAX_TT  = entries - 1u;           /* the mask */
    g_hash_entries = entries;

    memset(T_T,     0, per_table);
    memset(Opp_T_T, 0, per_table);
    memset(P_T_T,       0, (PMAX_TT + 1) * sizeof(TT_PTT_ST));
    memset(P_T_T_Rooks, 0, (PMAX_TT + 1) * sizeof(TT_PTT_ROOK_ST));

    Hash_Init();
    Util_Seed((uint32_t)Play_Get_Millisecs());

    /* Upstream's UCI defaults, minus everything that only makes sense with a GUI. */
    uci_debug = 0;
    disable_book = 0;
    eval_noise = 0;
    show_currmove = CURR_NEVER;
    contempt_val = CONTEMPT_VAL;
    contempt_end = CONTEMPT_END;
    dynamic_resign_threshold = NO_RESIGN;   /* the UI decides about resigning, not the engine */
    computer_side = NONE;
    g_abort = 0;
    g_busy = 0;

    Play_Set_Starting_Position();
    Play_Reset_Position_Status();
    g_ready = 1;
    return 0;
}

int ct800_glue_side_to_move(void)
{
    return ((gflags & BLACK_MOVED) != 0) ? 0 : 1;
}

/* "fen " + the longest FEN, plus room for the NUL. */
#define CT800_FEN_BUF 128

int ct800_glue_set_position(const char *fen, const char *uci_moves)
{
    char fen_buf[CT800_FEN_BUF];
    enum E_COLOUR side;

    if (!g_ready)
        return 1;

    if ((fen == NULL) || (*fen == '\0'))
    {
        strcpy(fen_buf, "startpos");
    } else
    {
        size_t len = strlen(fen);
        if (len + 5u > sizeof(fen_buf))
            return 1;
        strcpy(fen_buf, "fen ");
        strcpy(fen_buf + 4, fen);
    }

    if (Play_Read_FEN_Position(fen_buf) != POS_OK)
        return 1;
    /* Upstream's "position" command does exactly this, right here (play.c:2983), and
       it is not optional: Play_Read_FEN_Position() rewrites the board but leaves
       move_stack[0] holding the PREVIOUS position's material, position hash, pawn
       hash and "last move". eval.c reads move_stack[mv_stack_p].material as the
       material balance, so without this the engine evaluates a position entered as a
       FEN differently from the same position entered as startpos + moves. */
    Hash_Init_Stack();
    Play_Reset_Position_Status();

    side = (enum E_COLOUR)(((gflags & BLACK_MOVED) != 0) ? WHITE : BLACK);

    if ((uci_moves != NULL) && (*uci_moves != '\0'))
    {
        const char *p = uci_moves;
        while (*p != '\0')
        {
            char mv[6];
            int i;
            MOVE cur_move;

            while (*p == ' ')
                p++;
            if (*p == '\0')
                break;
            for (i = 0; (i < 5) && (*p > ' '); i++, p++)
            {
                char c = *p;
                if ((c >= 'A') && (c <= 'Z'))
                    c = (char)(c + ('a' - 'A'));
                mv[i] = c;
            }
            mv[i] = '\0';
            if ((i < 4) || (*p > ' '))   /* not 4 or 5 characters: malformed */
                return 1;
            if (!Play_Parse_Move(mv, &cur_move))
                return 1;
            if (!Play_Move_Is_Legal(cur_move, side))
                return 1;
            if (mv_stack_p >= MAX_PLIES - 1)
                return 1;
            Play_Update_Special_Conditions(cur_move);
            Search_Push_Status();
            Search_Make_Move(cur_move);
            side = (enum E_COLOUR)Mvgen_Opp_Colour(side);
        }
        Play_Update_Fifty_Moves();
    }
    return 0;
}

int ct800_glue_think(int32_t time_ms, int max_depth, int cpu_speed, uint64_t max_nps,
                     int32_t noise, int use_book, char best_uci[6], uint64_t *nodes,
                     int64_t *spent_ms)
{
    MOVE amove, pmove;
    enum E_COMP_RESULT res;
    enum E_COLOUR side;
    uint64_t spent_nodes = 0;
    int64_t spent_time = 0;
    const char *mv_str;

    if (best_uci != NULL)
        strcpy(best_uci, "0000");
    if (!g_ready)
        return 0;

    if (time_ms < 1)
        time_ms = 1;
    if (max_depth < 1)
        max_depth = 1;
    if (max_depth > MAX_DEPTH - 1)
        max_depth = MAX_DEPTH - 1;
    if (cpu_speed < 10)
        cpu_speed = 10;
    if (cpu_speed > 100)
        cpu_speed = 100;
    if (noise < 0)
        noise = 0;
    if (noise > 100)
        noise = 100;

    side = (enum E_COLOUR)(((gflags & BLACK_MOVED) != 0) ? WHITE : BLACK);

    eval_noise = noise;
    disable_book = use_book ? 0u : 1u;
    computer_side = side;
    dynamic_resign_threshold = NO_RESIGN;
    game_info.valid = EVAL_INVALID;
    game_info.eval = 0;

    pmove.u = MV_NO_MOVE_MASK;
    amove.u = MV_NO_MOVE_MASK;

    g_abort = 0;
    g_busy = 1;
    res = Search_Get_Best_Move(&amove, pmove, (int64_t)time_ms, (int)DEFAULT_MOVE_OVERHEAD,
                               /*exact_time=*/1, max_depth, cpu_speed,
                               (max_nps == 0u) ? NO_THROTTLE_NPS : max_nps,
                               side, NULL, -1, 0, 0, &spent_nodes, &spent_time);
    g_busy = 0;
    computer_side = NONE;

    if (nodes != NULL)
        *nodes = spent_nodes;
    if (spent_ms != NULL)
        *spent_ms = spent_time;

    if (res != COMP_MOVE_FOUND)
        return 0;

    mv_str = Play_Translate_Moves(amove);
    if (best_uci != NULL)
    {
        strcpy(best_uci, mv_str);   /* at most 5 characters plus the NUL */
    }
    return 1;
}

void ct800_glue_abort(void)
{
    g_abort = 1;
}

int ct800_glue_busy(void)
{
    return (int)g_busy;
}
