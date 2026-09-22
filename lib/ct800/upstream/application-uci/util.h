/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2016-2021, Rasmus Althoff <info@ct800.net>
 *
 *  This file is part of the CT800 (utility functions).
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

uint32_t    Util_Crc32(const void *buffer, size_t len);
uint8_t     Util_Crc8(const void *buffer, size_t len);
void        Util_Seed(uint32_t seed);
uint32_t    Util_Rand(uint32_t range);
uint32_t    Util_Hex_Long_To_Int(const uint8_t *buffer);
int         Util_Tostring_U64(char *buf, uint64_t val);
int         Util_Tostring_I64(char *buf,  int64_t val);
int         Util_Tostring_U32(char *buf, uint32_t val);
int         Util_Tostring_I32(char *buf,  int32_t val);
int         Util_Tostring_U16(char *buf, uint16_t val);
int         Util_Tostring_I16(char *buf,  int16_t val);
