/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2015-2021, Rasmus Althoff <info@ct800.net>
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

CMOVE Mvgen_Compress_Move(MOVE board_move);
MOVE  Mvgen_Decompress_Move(CMOVE comp_move);
enum E_MOVESTATE Mvgen_Is_Move_Safe(MOVE move, int kingsq, enum E_COLOUR colour);
int Mvgen_White_King_In_Check_Info(MOVE *restrict attack_movelist, int *restrict attackers);
int Mvgen_Black_King_In_Check_Info(MOVE *restrict attack_movelist, int *restrict attackers);
int Mvgen_White_King_In_Check(void);
int Mvgen_Black_King_In_Check(void);
int Mvgen_Find_All_White_Evasions(MOVE *restrict movelist, const MOVE *restrict attack_movelist,
                                  int n_attack_moves, int n_attacking_pieces, int underprom);
int Mvgen_Find_All_Black_Evasions(MOVE *restrict movelist, const MOVE *restrict attack_movelist,
                                  int n_attack_moves, int n_attacking_pieces, int underprom);
int Mvgen_Find_All_White_Moves(MOVE *restrict movelist, int level, int underprom);
int Mvgen_Find_All_Black_Moves(MOVE *restrict movelist, int level, int underprom);
int Mvgen_Find_All_White_Captures_And_Promotions(MOVE *restrict movelist, int underprom);
int Mvgen_Find_All_Black_Captures_And_Promotions(MOVE *restrict movelist, int underprom);
int Mvgen_Check_Move_Legality(MOVE move, enum E_COLOUR colour);
void Mvgen_Add_White_King_Moves(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level);
void Mvgen_Add_Black_King_Moves(const PIECE *piece, MOVE *restrict movelist, int *restrict nextfree, int level);

/*for the UCI move parser*/
int Mvgen_Find_All_White_Moves_Piece(MOVE *restrict movelist, int from_sq);
int Mvgen_Find_All_Black_Moves_Piece(MOVE *restrict movelist, int from_sq);

/*now for some macro definitions for colour independence.
macros and not functions for sparing the call overhead.*/
#define Mvgen_King_In_Check(colour) ((colour == WHITE) ? (Mvgen_White_King_In_Check()) : (Mvgen_Black_King_In_Check()))
#define Mvgen_King_In_Check_Info(attack_movelist, attackers, colour) ((colour == WHITE) ? Mvgen_White_King_In_Check_Info(attack_movelist, attackers) : Mvgen_Black_King_In_Check_Info(attack_movelist, attackers))
#define Mvgen_Find_All_Evasions(movelist, attack_movelist, n_attack_moves, n_attacking_pieces, underprom, colour) ((colour == WHITE) ? Mvgen_Find_All_White_Evasions(movelist, attack_movelist, n_attack_moves, n_attacking_pieces, underprom ) : Mvgen_Find_All_Black_Evasions(movelist, attack_movelist, n_attack_moves, n_attacking_pieces, underprom))
#define Mvgen_Find_All_Moves(movelist, lvl, colour, underprom) ((colour == WHITE) ? (Mvgen_Find_All_White_Moves(movelist, lvl, underprom)) : (Mvgen_Find_All_Black_Moves(movelist, lvl, underprom)))
#define Mvgen_Find_All_Captures_And_Promotions(movelist, colour, underprom) ((colour == WHITE) ? (Mvgen_Find_All_White_Captures_And_Promotions(movelist, underprom)) : (Mvgen_Find_All_Black_Captures_And_Promotions(movelist, underprom)))
#define Mvgen_Opp_Colour(colour) ((colour == WHITE) ? (BLACK) : (WHITE))
#define Mvgen_Find_All_Moves_Piece(movelist, from_sq, colour) ((colour == WHITE) ? Mvgen_Find_All_White_Moves_Piece(movelist, from_sq) : Mvgen_Find_All_Black_Moves_Piece(movelist, from_sq))
