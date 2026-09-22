/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2015-2024, Rasmus Althoff <info@ct800.net>
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

int     Eval_Is_Light_Square(int square);
int     Eval_Static_Evaluation(int *restrict enough_material, enum E_COLOUR side_to_move,
                               unsigned *is_endgame);
int     Eval_Setup_Initial_Material(void);
void    Eval_Zero_Initial_Material(void);
