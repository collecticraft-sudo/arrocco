/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2015-2021, Rasmus Althoff <info@ct800.net>
 *
 *  This file is part of CT800/NGPlay (opening book).
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

int  Book_Is_Line(int *restrict book_movelist_index, const MOVE *restrict movelist, int moves, int is_full_list);
void Book_Get_Moves(MOVE *restrict result_movelist, int *result_moves, enum E_COLOUR colour);
