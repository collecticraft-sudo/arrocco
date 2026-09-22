/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2023-2024, Rasmus Althoff <info@ct800.net>
 *
 *  This file is part of CT800/NGPlay (auto-tuner).
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

#ifdef AUTOTUNE /*spans whole tuner source*/

#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ctdefs.h"
#include "move_gen.h"
#include "hashtables.h"
#include "eval.h"
#include "search.h"

/*---------- external variables ----------*/
/*-- READ-ONLY --*/
extern const int8_t board64[64];
extern int8_t FlipXY[120];
extern int8_t WhiteSq[120];

/*-- READ-WRITE --*/
extern PIECE Wpieces[16];
extern PIECE Bpieces[16];
extern PIECE *board[120];
extern unsigned int gflags;
extern int psqt[6][120][2];
extern int bishop_bonus[17];
extern int material_pst[5][2];

/*-- External functions --*/
int Search_PV_Quiescence(int alpha, int beta, enum E_COLOUR colour, int do_checks, int qs_depth, LINE *pline);
enum E_POS_VALID Play_Read_FEN_Position(char *fen_line);

/*expected EPD format with the game result (Ethereal):
  8/3kp3/1p3bp1/8/1B1P1PK1/4P3/8/8 b - - 0 40 [0.0]
  [0.0], [0.5], [1.0] from White's POV.

  alternative format (Zurichess):
  r6r/n6p/6p1/1p3pb1/q7/P1B1kP1P/6P1/6K1 b - - c9 "0-1";
  8/6P1/8/1pk1r3/6RP/8/3K4/8 b - - c9 "1-0";
  8/1R6/1p6/3pkp2/P6p/1P1K3P/4r1P1/8 b - - c9 "1/2-1/2";
  game result from White's POV.

  alternative format (c-chess-cli):
  8/2Q2bk1/8/1p4p1/3P1pq1/1PP2R2/4rPP1/5NK1 w - - 1 49,327,2
  game result 0=loss, 1=draw, 2=win from STM's POV.
*/

#define PARAM_NO 763
#define MIN_MG_PHASE  0
#define MAX_EG_PHASE 24
#define K_FILE_ENTRIES 256
#define LAMBDA_L1_REG 0.01
/*tested with 0.005, 0.01, 0.02, 0.05.
  0.01, 0.02: equal (best), 0.05: -4 Elo, 0.005: -7 Elo.*/

/*mapping parameter indices to eval features*/
#define PST_KNIGHT_MG    64
#define PST_KNIGHT_EG   128
#define PST_BISHOP_MG   192
#define PST_BISHOP_EG   256
#define PST_ROOK_MG     320
#define PST_ROOK_EG     384
#define PST_QUEEN_MG    448
#define PST_QUEEN_EG    512
#define PST_KING_MG     576
#define PST_KING_EG     640
#define PST_PAWN_MG     688
#define PST_PAWN_EG     736
#define PST_BISHOP_PAIR 753
#define PST_MAT_MG      758
#define PST_MAT_EG      763

typedef struct {
    char filename[256];
    double K;
    long int N;
} T_K_FILES;

static T_K_FILES k_files[K_FILE_ENTRIES];

/*interfacing the PSTs as tuning parameters*/
static int Tune_Read_Param(int para)
{
    if (para < PST_KNIGHT_MG)
        return(psqt[WKNIGHT - WPAWN][board64[para]][MG_PST]);
    if (para < PST_KNIGHT_EG)
        return(psqt[WKNIGHT - WPAWN][board64[para - (PST_KNIGHT_EG - 64)]][EG_PST]);
    if (para < PST_BISHOP_MG)
        return(psqt[WBISHOP - WPAWN][board64[para - (PST_BISHOP_MG - 64)]][MG_PST]);
    if (para < PST_BISHOP_EG)
        return(psqt[WBISHOP - WPAWN][board64[para - (PST_BISHOP_EG - 64)]][EG_PST]);
    if (para < PST_ROOK_MG)
        return(psqt[WROOK   - WPAWN][board64[para - (PST_ROOK_MG   - 64)]][MG_PST]);
    if (para < PST_ROOK_EG)
        return(psqt[WROOK   - WPAWN][board64[para - (PST_ROOK_EG   - 64)]][EG_PST]);
    if (para < PST_QUEEN_MG)
        return(psqt[WQUEEN  - WPAWN][board64[para - (PST_QUEEN_MG  - 64)]][MG_PST]);
    if (para < PST_QUEEN_EG)
        return(psqt[WQUEEN  - WPAWN][board64[para - (PST_QUEEN_EG  - 64)]][EG_PST]);
    if (para < PST_KING_MG)
        return(psqt[WKING   - WPAWN][board64[para - (PST_KING_MG   - 64)]][MG_PST]);
    if (para < PST_KING_EG)
        return(psqt[WKING   - WPAWN][board64[para - (PST_KING_EG   - 64)]][EG_PST]);
    if (para < PST_PAWN_MG)
        return(psqt[WPAWN   - WPAWN][board64[para - (PST_PAWN_MG   - 64 + 8)]][MG_PST]);
    if (para < PST_PAWN_EG)
        return(psqt[WPAWN   - WPAWN][board64[para - (PST_PAWN_EG   - 64 + 8)]][EG_PST]);
    if (para < PST_BISHOP_PAIR)
        return(bishop_bonus[para - (PST_BISHOP_PAIR - 17)]);
    if (para < PST_MAT_MG)
        return(material_pst[para - (PST_MAT_MG - 5)][MG_PST]);
    if (para < PST_MAT_EG)
        return(material_pst[para - (PST_MAT_EG - 5)][EG_PST]);

    printf("Error: unknown read parameter %d\n", para);
    return(0);
}

static void Tune_Write_Param(int para, int value)
{
    if (para < PST_KNIGHT_MG)
        psqt[WKNIGHT - WPAWN][board64[para]][MG_PST] = value;
    else if (para < PST_KNIGHT_EG)
        psqt[WKNIGHT - WPAWN][board64[para - (PST_KNIGHT_EG - 64)]][EG_PST] = value;
    else if (para < PST_BISHOP_MG)
        psqt[WBISHOP - WPAWN][board64[para - (PST_BISHOP_MG - 64)]][MG_PST] = value;
    else if (para < PST_BISHOP_EG)
        psqt[WBISHOP - WPAWN][board64[para - (PST_BISHOP_EG - 64)]][EG_PST] = value;
    else if (para < PST_ROOK_MG)
        psqt[WROOK   - WPAWN][board64[para - (PST_ROOK_MG   - 64)]][MG_PST] = value;
    else if (para < PST_ROOK_EG)
        psqt[WROOK   - WPAWN][board64[para - (PST_ROOK_EG   - 64)]][EG_PST] = value;
    else if (para < PST_QUEEN_MG)
        psqt[WQUEEN  - WPAWN][board64[para - (PST_QUEEN_MG  - 64)]][MG_PST] = value;
    else if (para < PST_QUEEN_EG)
        psqt[WQUEEN  - WPAWN][board64[para - (PST_QUEEN_EG  - 64)]][EG_PST] = value;
    else if (para < PST_KING_MG)
        psqt[WKING   - WPAWN][board64[para - (PST_KING_MG   - 64)]][MG_PST] = value;
    else if (para < PST_KING_EG)
        psqt[WKING   - WPAWN][board64[para - (PST_KING_EG   - 64)]][EG_PST] = value;
    else if (para < PST_PAWN_MG)
        psqt[WPAWN   - WPAWN][board64[para - (PST_PAWN_MG   - 64 + 8)]][MG_PST] = value;
    else if (para < PST_PAWN_EG)
        psqt[WPAWN   - WPAWN][board64[para - (PST_PAWN_EG   - 64 + 8)]][EG_PST] = value;
    else if (para < PST_BISHOP_PAIR)
        bishop_bonus[para - (PST_BISHOP_PAIR - 17)] = value;
    else if (para < PST_MAT_MG)
        material_pst[para - (PST_MAT_MG - 5)][MG_PST] = value;
    else if (para < PST_MAT_EG)
        material_pst[para - (PST_MAT_EG - 5)][EG_PST] = value;
    else
        printf("Error: unknown write parameter %d %d\n", para, value);
}

static int Tune_Get_Game_Phase(int *asym_bishop_pair, int *all_pawns)
{
    int game_phase = 0, w_bishop_colour = 0, b_bishop_colour = 0, pawn_cnt = 0;

    for (PIECE *p = Wpieces[0].next; p != NULL; p = p->next)
    {
        int p_xy = p->xy;
        switch (p->type)
        {
        case WPAWN:
            pawn_cnt++;
            break;
        case WROOK:
            game_phase += 2;
            break;
        case WKNIGHT:
            game_phase += 1;
            break;
        case WBISHOP:
            w_bishop_colour |= WhiteSq[p_xy];
            game_phase += 1;
            break;
        case WQUEEN:
            game_phase += 4;
            break;
        default:
            break;
        }
    }
    for (PIECE *p = Bpieces[0].next; p != NULL; p = p->next)
    {
        int p_xy = p->xy;
        switch (p->type)
        {
        case BPAWN:
            pawn_cnt++;
            break;
        case BROOK:
            game_phase += 2;
            break;
        case BKNIGHT:
            game_phase += 1;
            break;
        case BBISHOP:
            b_bishop_colour |= WhiteSq[p_xy];
            game_phase += 1;
            break;
        case BQUEEN:
            game_phase += 4;
            break;
        default:
            break;
        }
    }
    *asym_bishop_pair = ((w_bishop_colour == TWO_COLOUR) && (b_bishop_colour != TWO_COLOUR)) ||
                        ((w_bishop_colour != TWO_COLOUR) && (b_bishop_colour == TWO_COLOUR));
    *all_pawns = pawn_cnt;
    return(game_phase);
}

static int Tune_Param_Ignored(int para, int game_phase, int asym_bishop_pair, int all_pawns)
{
    if (para < PST_KNIGHT_MG)
    {
        /*MG knight*/
        if (game_phase < MIN_MG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para]]->type,
                bpiece = board[FlipXY[board64[para]]]->type;
            return ((wpiece != WKNIGHT) && (bpiece != BKNIGHT));
        }
    } else if (para < PST_KNIGHT_EG)
    {
        /*EG knight*/
        if (game_phase > MAX_EG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para - (PST_KNIGHT_EG - 64)]]->type,
                bpiece = board[FlipXY[board64[para - (PST_KNIGHT_EG - 64)]]]->type;
            return ((wpiece != WKNIGHT) && (bpiece != BKNIGHT));
        }
    } else if (para < PST_BISHOP_MG)
    {
        /*MG bishop*/
        if (game_phase < MIN_MG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para - (PST_BISHOP_MG - 64)]]->type,
                bpiece = board[FlipXY[board64[para - (PST_BISHOP_MG - 64)]]]->type;
            return ((wpiece != WBISHOP) && (bpiece != BBISHOP));
        }
    } else if (para < PST_BISHOP_EG)
    {
        /*EG bishop*/
        if (game_phase > MAX_EG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para - (PST_BISHOP_EG - 64)]]->type,
                bpiece = board[FlipXY[board64[para - (PST_BISHOP_EG - 64)]]]->type;
            return ((wpiece != WBISHOP) && (bpiece != BBISHOP));
        }
    } else if (para < PST_ROOK_MG)
    {
        /*MG rook*/
        if (game_phase < MIN_MG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para - (PST_ROOK_MG - 64)]]->type,
                bpiece = board[FlipXY[board64[para - (PST_ROOK_MG - 64)]]]->type;
            return ((wpiece != WROOK) && (bpiece != BROOK));
        }
    } else if (para < PST_ROOK_EG)
    {
        /*EG rook*/
        if (game_phase > MAX_EG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para - (PST_ROOK_EG - 64)]]->type,
                bpiece = board[FlipXY[board64[para - (PST_ROOK_EG - 64)]]]->type;
            return ((wpiece != WROOK) && (bpiece != BROOK));
        }
    } else if (para < PST_QUEEN_MG)
    {
        /*MG queen*/
        if (game_phase < MIN_MG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para - (PST_QUEEN_MG - 64)]]->type,
                bpiece = board[FlipXY[board64[para - (PST_QUEEN_MG - 64)]]]->type;
            return ((wpiece != WQUEEN) && (bpiece != BQUEEN));
        }
    } else if (para < PST_QUEEN_EG)
    {
        /*EG queen*/
        if (game_phase > MAX_EG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para - (PST_QUEEN_EG - 64)]]->type,
                bpiece = board[FlipXY[board64[para - (PST_QUEEN_EG - 64)]]]->type;
            return ((wpiece != WQUEEN) && (bpiece != BQUEEN));
        }
    } else if (para < PST_KING_MG)
    {
        /*MG king*/
        if (game_phase < MIN_MG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para - (PST_KING_MG - 64)]]->type,
                bpiece = board[FlipXY[board64[para - (PST_KING_MG - 64)]]]->type;
            return ((wpiece != WKING) && (bpiece != BKING));
        }
    } else if (para < PST_KING_EG)
    {
        /*EG king*/
        if (game_phase > MAX_EG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para - (PST_KING_EG - 64)]]->type,
                bpiece = board[FlipXY[board64[para - (PST_KING_EG - 64)]]]->type;
            return ((wpiece != WKING) && (bpiece != BKING));
        }
    } else if (para < PST_PAWN_MG)
    {
        /*MG pawn*/
        if (game_phase < MIN_MG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para - (PST_PAWN_MG - 64 + 8)]]->type,
                bpiece = board[FlipXY[board64[para - (PST_PAWN_MG - 64 + 8)]]]->type;
            return ((wpiece != WPAWN) && (bpiece != BPAWN));
        }
    } else if (para < PST_PAWN_EG)
    {
        /*EG pawn*/
        if (game_phase > MAX_EG_PHASE)
            return(1);
        else
        {
            int wpiece = board[board64[para - (PST_PAWN_EG - 64 + 8)]]->type,
                bpiece = board[FlipXY[board64[para - (PST_PAWN_EG - 64 + 8)]]]->type;
            return ((wpiece != WPAWN) && (bpiece != BPAWN));
        }
    } else if (para < PST_BISHOP_PAIR)
    {
        /*bishop pair*/
        return((!asym_bishop_pair) || (para - (PST_BISHOP_PAIR - 17) != all_pawns));
    } else if (para < PST_MAT_MG)
    {
        /*mg material*/
        return(game_phase < MIN_MG_PHASE);
    } else if (para < PST_MAT_EG)
    {
        /*eg material*/
        return(game_phase > MAX_EG_PHASE);
    } else
    {
        printf("Error: unknown parameter %d\n", para);
        return(1);
    }
}

static double Tune_Get_Para_Reg(int para, int value)
{
    if (para < PST_PAWN_EG) /*PSTs*/
        return(Abs(value));
    else if (para < PST_BISHOP_PAIR) /*BSP*/
    {
        return(Abs(value)/4.0);
    } else if ((para == PST_MAT_MG - 5) || (para == PST_MAT_EG - 5)) /*material, pawn*/
    {
        value -= TUNE_PAWN_V;
        return(Abs(value)/(0.01 * TUNE_PAWN_V));
    } else if ((para == PST_MAT_MG - 4) || (para == PST_MAT_EG - 4)) /*material, knight*/
    {
        value -= TUNE_KNIGHT_V;
        return(Abs(value)/(0.01 * TUNE_KNIGHT_V));
    } else if ((para == PST_MAT_MG - 3) || (para == PST_MAT_EG - 3)) /*material, bishop*/
    {
        value -= TUNE_BISHOP_V;
        return(Abs(value)/(0.01 * TUNE_BISHOP_V));
    } else if ((para == PST_MAT_MG - 2) || (para == PST_MAT_EG - 2)) /*material, rook*/
    {
        value -= TUNE_ROOK_V;
        return(Abs(value)/(0.01 * TUNE_ROOK_V));
    } else if ((para == PST_MAT_MG - 1) || (para == PST_MAT_EG - 1)) /*material, queen*/
    {
        value -= TUNE_QUEEN_V;
        return(Abs(value)/(0.01 * TUNE_QUEEN_V));
    }
    return(0); /*should not happen*/
}

static double Tune_Sigmoid(int score, double K)
{
    double exponent = -K*((double)score)/400.0;
    return(1.0/(1.0 + pow(10.0, exponent)));
}

static double Tune_Get_Error(double K, FILE *epd_file, long int *pN, MOVE *movelist, char *epd_line, int use_qs)
{
    /*the design with the error and the error accu aims at reducing the
      difference in orders of magnitude when adding variables, which is
      more stable numerically.*/
    double E = 0.0, E_accu = 0.0;
    long int N = 0;
    unsigned accu_cnt = 0;

    /*the FEN buffer is 256 long, reading into it from offset 4 to keep the
      leading "fen " for the FEN parsing routine, that leaves 12 zero bytes at
      the end as safety region for the lookahead scanner below.*/
    while (fgets(epd_line+4, 240, epd_file))
    {
        double game_result, error_summand;
        int i, score, dummy, stm_game_result = 0;
        unsigned udummy;

        for (i = 0; epd_line[i] != '\0'; i++)
            if ((epd_line[i] == '[') ||
                ((epd_line[i] == 'c') && (epd_line[i + 1] == '9') &&
                 (epd_line[i + 2] == ' ') && (epd_line[i + 3] == '"')) ||
                 (epd_line[i] == ','))
                break;
        if (epd_line[i] == '\0') /*malformed line*/
            continue;
        if (epd_line[i] == '[')
        {
            /*format 8/3kp3/1p3bp1/8/1B1P1PK1/4P3/8/8 b - - 0 40 [0.0] */
            i++;
            if (epd_line[i] == '1')
                game_result = 1.0;
            else if (epd_line[i + 2] == '0')
                game_result = 0.0;
            else
                game_result = 0.5;
        } else if (epd_line[i] == 'c')
        {
            /*format r6r/n6p/6p1/1p3pb1/q7/P1B1kP1P/6P1/6K1 b - - c9 "0-1";*/
            i += 4;
            if (epd_line[i] == '0')
                game_result = 0.0;
            else if (epd_line[i + 2] == '0')
                game_result = 1.0;
            else
                game_result = 0.5;
        } else
        {
            /*format 8/2Q2bk1/8/1p4p1/3P1pq1/1PP2R2/4rPP1/5NK1 w - - 1 49,327,2*/
            for (i++; epd_line[i] != '\0'; i++)
                if (epd_line[i] == ',')
                    break;
            i++;
            game_result = ((double)(epd_line[i] - '0'))/2.0;
            stm_game_result = 1;
        }
        if (K != 0.0) /*first pass with K=0.0 erases the score anyway*/
        {
            LINE line;

            if (Play_Read_FEN_Position(epd_line) != POS_OK)
                continue;
            if ((stm_game_result) && (!(gflags & BLACK_MOVED)))
                game_result = 1.0 - game_result;
            Hash_Init_Stack();
            if (use_qs)
            {
                Search_PV_Quiescence(-INFINITY_, INFINITY_, (gflags & BLACK_MOVED) ? WHITE : BLACK, QS_CHECKS, 0, &line);
                for (i = 0; i < line.line_len; i++)
                    Search_Make_Move(Mvgen_Decompress_Move(line.line_cmoves[i]));
            }
            /*update mobility*/
            Mvgen_Find_All_White_Moves(movelist, NO_LEVEL, UNDERPROM);
            Mvgen_Find_All_Black_Moves(movelist, NO_LEVEL, UNDERPROM);
            score = Eval_Static_Evaluation(&dummy, (gflags & BLACK_MOVED) ? WHITE : BLACK, &udummy);
            //printf("Res: %1.1f Score: %d %s", game_result, score, epd_line);
            N++;
        } else
        {
            score = 0;
            N++;
        }
        error_summand = game_result - Tune_Sigmoid(score, K);
        error_summand = error_summand * error_summand;
        E_accu += error_summand;
        if (++accu_cnt >= 1024)
        {
            E += E_accu;
            E_accu = 0.0;
            accu_cnt = 0;
        }
    }
    *pN = N;
    E += E_accu;
    fseek(epd_file, 0L, SEEK_SET);
    return(E);
}

void Tune_Dump_Params(void)
{
    const char *piece_comment[6] = {"pawns", "knights", "bishops",
                                    "rooks", "queens", "kings"};
    printf("material_pst[5][2] = /*MG/EG*/\r\n{");
    for (int i = 0; i < 5; i++)
    {
        printf("{");
        for (int j= 0; j < 2; j++)
        {
            printf("%d", material_pst[i][j]);
            if (j < 1)
                printf(", ");
        }
        printf("}");
        if (i < 4)
            printf(", ");
    }
    printf("};\r\n\r\n");

    printf("bishop_bonus[17] =\r\n{");
    for (int i = 0; i < 16; i++)
        printf("%d, ", bishop_bonus[i]);
    printf("%d};\r\n\r\n", bishop_bonus[16]);

    printf("psqt[6][120][2] =\r\n{\r\n");
    for (int i = 0; i < 6; i++)
    {
        printf("    { /*%s MG/EG*/\r\n        ", piece_comment[i]);
        for (int j = 0; j < 120; j++)
        {
            printf("{");
            for (int k = 0; k < 2; k++)
            {
                printf("% 4d", psqt[i][j][k]);
                if (k < 1)
                    printf(",");
            }
            printf("}");
            if (j < 119)
                printf(",");
            if ((j + 1) % 10 == 0)
            {
                printf("\r\n");
                if (j < 119)
                    printf("        ");
            }
            else if (j < 119)
                printf(" ");
        }
        printf("    }");
        if (i < 5)
            printf(",");
        printf("\r\n");
    }
    printf("};\r\n\r\n");
}

void Tune_Run(int iterations, int step, const char *filename, int use_qs)
{
    MOVE movelist[MAXMV];
    char epd_line[256]; /*only using up to 244 characters to keep trailing zeros*/
    FILE *epd_file;
    double K;
    long int N, k_file_index;

    epd_file = fopen(filename, "r");
    if (epd_file == NULL)
    {
        printf("Error: cannot open %s\n", filename);
        return;
    }
    printf("Iterations: %d Step: %d File: %s\n", iterations, step, filename);
    Eval_Zero_Initial_Material();
    memset(epd_line, 0, sizeof(epd_line));
    strcpy(epd_line, "fen ");
    printf("Calculating K...\n");

    K = 0.0;
    N = 0;
    for (k_file_index = 0; k_file_index < K_FILE_ENTRIES - 1 && k_files[k_file_index].N != 0; k_file_index++)
    {
        if (!strcmp(k_files[k_file_index].filename, filename))
        {
            K = k_files[k_file_index].K;
            N = k_files[k_file_index].N;
            break;
        }
    }
    if (N == 0)
    {
        double E = Tune_Get_Error(K, epd_file, &N, movelist, epd_line, use_qs);

        if (N == 0)
        {
            puts("Error: no positions.");
            fclose(epd_file);
            return;
        }
        E /= N;
        printf("K: %f E: %f\n", K, E);
        for (double K_step = 1.0/4.0; Abs(K_step) >= 1.0/256.0; K_step /= 2.0)
        {
            for (K += K_step; K <= 8.0 && K >= 0.0; K += K_step)
            {
                double E_new = Tune_Get_Error(K, epd_file, &N, movelist, epd_line, use_qs);

                /*for K != 0, the returned N might be different because the
                  position legality is also checked, unlike for K == 0.*/
                if (N == 0)
                {
                    puts("Error: no valid positions.");
                    fclose(epd_file);
                    return;
                }
                E_new /= N;
                printf("K: %f E: %f\n", K, E_new);
                if (E_new > E)
                {
                    K -= K_step;
                    K_step *= -1.0;
                    break;
                }
                E = E_new;
            }
        }
        printf("K: %f E: %f N: %ld\n", K, E, N);
        k_files[k_file_index].K = K;
        k_files[k_file_index].N = N;
        strncpy(k_files[k_file_index].filename, filename, sizeof(k_files[k_file_index].filename));
        k_files[k_file_index].filename[sizeof(k_files[k_file_index].filename) - 1] = '\0';
    } else
        printf("K: %f N: %ld\n", K, N);

    /*tuning here*/
    for (int iter = 0; iter < iterations; iter++)
    {
        /*the design with the error and the error accu aims at reducing the
          difference in orders of magnitude when adding variables, which is
          more stable numerically.*/
        unsigned accu_cnt = 0;
        static double error_base, error_base_accu,
                      error_up[PARAM_NO],      error_dn[PARAM_NO],
                      error_up_accu[PARAM_NO], error_dn_accu[PARAM_NO];

        printf("Iteration %2d out of %d...", iter+1, iterations);
        error_base = error_base_accu = 0.0;
        memset(error_up, 0, sizeof(error_up));
        memset(error_dn, 0, sizeof(error_dn));
        memset(error_up_accu, 0, sizeof(error_up_accu));
        memset(error_dn_accu, 0, sizeof(error_dn_accu));

        while (fgets(epd_line+4, 240, epd_file))
        {
            LINE line;
            double game_result, error_summand;
            int i, score, dummy, game_phase, asym_bishop_pair, all_pawns, stm_game_result = 0;
            unsigned udummy;

            for (i = 0; epd_line[i] != '\0'; i++)
                if ((epd_line[i] == '[') ||
                    ((epd_line[i] == 'c') && (epd_line[i + 1] == '9') &&
                     (epd_line[i + 2] == ' ') && (epd_line[i + 3] == '"')) ||
                     (epd_line[i] == ','))
                    break;
            if (epd_line[i] == '\0') /*malformed line*/
                continue;
            if (epd_line[i] == '[')
            {
                /*format 8/3kp3/1p3bp1/8/1B1P1PK1/4P3/8/8 b - - 0 40 [0.0] */
                i++;
                if (epd_line[i] == '1')
                    game_result = 1.0;
                else if (epd_line[i + 2] == '0')
                    game_result = 0.0;
                else
                    game_result = 0.5;
            } else if (epd_line[i] == 'c')
            {
                /*format r6r/n6p/6p1/1p3pb1/q7/P1B1kP1P/6P1/6K1 b - - c9 "0-1";*/
                i += 4;
                if (epd_line[i] == '0')
                    game_result = 0.0;
                else if (epd_line[i + 2] == '0')
                    game_result = 1.0;
                else
                    game_result = 0.5;
            } else
            {
                /*format 8/2Q2bk1/8/1p4p1/3P1pq1/1PP2R2/4rPP1/5NK1 w - - 1 49,327,2*/
                for (i++; epd_line[i] != '\0'; i++)
                    if (epd_line[i] == ',')
                        break;
                i++;
                game_result = ((double)(epd_line[i] - '0'))/2.0;
                stm_game_result = 1;
            }
            if (Play_Read_FEN_Position(epd_line) != POS_OK)
                continue;
            if ((stm_game_result) && (!(gflags & BLACK_MOVED)))
                game_result = 1.0 - game_result;
            Hash_Init_Stack();
            if (use_qs)
            {
                Search_PV_Quiescence(-INFINITY_, INFINITY_, (gflags & BLACK_MOVED) ? WHITE : BLACK, QS_CHECKS, 0, &line);
                for (i = 0; i < line.line_len; i++)
                    Search_Make_Move(Mvgen_Decompress_Move(line.line_cmoves[i]));
            }
            /*update mobility*/
            Mvgen_Find_All_White_Moves(movelist, NO_LEVEL, UNDERPROM);
            Mvgen_Find_All_Black_Moves(movelist, NO_LEVEL, UNDERPROM);
            score = Eval_Static_Evaluation(&dummy, (gflags & BLACK_MOVED) ? WHITE : BLACK, &udummy);
            error_summand = game_result - Tune_Sigmoid(score, K);
            error_summand = error_summand * error_summand;
            error_base_accu += error_summand;
            game_phase = Tune_Get_Game_Phase(&asym_bishop_pair, &all_pawns);
            for (int para = 0; para < PARAM_NO; para++)
            {
                double para_summand;
                int value;

                if (Tune_Param_Ignored(para, game_phase, asym_bishop_pair, all_pawns))
                {
                    error_up_accu[para] += error_summand;
                    error_dn_accu[para] += error_summand;
                    continue;
                }
                value = Tune_Read_Param(para);
                /*test increasing the value*/
                value += step;
                if ((para >= PST_MAT_MG - 5) || (value <= 127)) /*PST / BSP limit*/
                {
                    Tune_Write_Param(para, value);
                    score = Eval_Static_Evaluation(&dummy, (gflags & BLACK_MOVED) ? WHITE : BLACK, &udummy);
                    para_summand = game_result - Tune_Sigmoid(score, K);
                    para_summand = para_summand * para_summand;
                } else
                    para_summand = error_summand;
                error_up_accu[para] += para_summand;
                /*test decreasing the value*/
                value -= 2*step;
                if ((para >= PST_MAT_MG - 5) || (value >= -127)) /*PST / BSP limit*/
                {
                    Tune_Write_Param(para, value);
                    score = Eval_Static_Evaluation(&dummy, (gflags & BLACK_MOVED) ? WHITE : BLACK, &udummy);
                    para_summand = game_result - Tune_Sigmoid(score, K);
                    para_summand = para_summand * para_summand;
                } else
                    para_summand = error_summand;
                error_dn_accu[para] += para_summand;
                /*restore the value*/
                value += step;
                Tune_Write_Param(para, value);
            }
            if (++accu_cnt >= 1024)
            {
                for (int para = 0; para < PARAM_NO; para++)
                {
                    error_up[para] += error_up_accu[para];
                    error_dn[para] += error_dn_accu[para];
                }
                memset(error_up_accu, 0, sizeof(error_up_accu));
                memset(error_dn_accu, 0, sizeof(error_dn_accu));
                error_base += error_base_accu;
                error_base_accu = 0.0;
                accu_cnt = 0;
            }
        }
        for (int para = 0; para < PARAM_NO; para++)
        {
            error_up[para] += error_up_accu[para];
            error_dn[para] += error_dn_accu[para];
        }
        error_base += error_base_accu;
        fseek(epd_file, 0L, SEEK_SET);
        printf(" E: %f\n", error_base/N);
        for (int para = 0; para < PARAM_NO; para++)
        {
            int value =  Tune_Read_Param(para);
            double error_orig, error_inc, error_dec;

            error_orig = error_base + LAMBDA_L1_REG * Tune_Get_Para_Reg(para, value);
            error_inc = error_up[para] + LAMBDA_L1_REG * Tune_Get_Para_Reg(para, value + step);
            error_dec = error_dn[para] + LAMBDA_L1_REG * Tune_Get_Para_Reg(para, value - step);
            if ((error_inc < error_orig) || (error_dec < error_orig))
            {
                if (error_inc < error_dec)
                    value += step;
                else if (error_dec < error_inc)
                    value -= step;
                else if (value > 0)
                    value -= step;
                else
                    value += step;
                Tune_Write_Param(para, value);
            }
        }
    }
    fclose(epd_file);
    Tune_Dump_Params();
}
#else /*no AUTOTUNE*/
/*ISO C does not allow empty translation units*/
typedef int FOO_COMPILER_DUMMY;
#endif
