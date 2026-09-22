/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2016-2021, Rasmus Althoff <info@ct800.net>
 *  Copyright (C) 2015, Marcel van Kervinck
 *
 *  This file is part of the CT800 (endgame table).
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
 *  Note: the original KPK generator by Marcel van Kervinck was not
 *  released under GPLv3 or later, see the original conditions below.
 *  This derivative work, however, is under GPLv3 or later.
 */

/*----------------------------------------------------------------------+
 |                                                                      |
 |      kpk.h -- pretty fast KPK endgame table generator                |
 |                                                                      |
 +----------------------------------------------------------------------*/

#define KPK_WHITE 0
#define KPK_BLACK 1

/*----------------------------------------------------------------------+
 |      Functions                                                       |
 +----------------------------------------------------------------------*/

/*
 *  Probe a KPK position from the in memory endgame table.
 *  Returns 0 for draw, 1 for win/loss.
 *
 *  The position must be legal for meaningful results.
 *  'side' is KPK_WHITE for white to move and KPK_BLACK for black to move.
 *
 */
unsigned int Kpk_Probe(unsigned int side, unsigned int w_king, unsigned int w_pawn, unsigned int b_king);
unsigned int Kpk_Probe_Reverse(unsigned int side, unsigned int w_king, unsigned int bPawn, unsigned int b_king);
/*----------------------------------------------------------------------+
 |                                                                      |
 +----------------------------------------------------------------------*/
