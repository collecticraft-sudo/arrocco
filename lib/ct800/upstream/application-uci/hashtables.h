/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2015-2021, Rasmus Althoff <info@ct800.net>
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

void        Hash_Clear_Tables(void);
void        Hash_Cut_Tables(unsigned int clear_counter);
void        Hash_Init_Stack(void);
void        Hash_Init(void);
int         Hash_Repetitions(void);
int         Hash_Check_For_Draw(void);
unsigned    Hash_Get_Usage(void);
uint64_t    Hash_Get_Position_Value(uint64_t *pawn_hash);
int         Hash_Check_TT_PV(const TT_ST *tt, enum E_COLOUR colour, int pdepth, uint64_t pos_hash, int *valueP, MOVE* hmvp);
int         Hash_Check_TT(const TT_ST *tt, enum E_COLOUR colour, int alpha, int beta, int pdepth, uint64_t pos_hash, int *valueP, MOVE* hmvp);
void        Hash_Update_TT(TT_ST *tt, int pdepth, int pvalue, unsigned int pflag, uint64_t pos_hash, MOVE hmv);
