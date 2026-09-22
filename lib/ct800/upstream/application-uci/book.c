/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2015-2024, Rasmus Althoff <info@ct800.net>
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

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "ctdefs.h"
#include "util.h"
#include "move_gen.h"
#include "search.h"

/*---------- external variables ----------*/
/*-- READ-ONLY --*/
extern int mv_stack_p;
extern MVST move_stack[MAX_STACK+1];
extern int cst_p;
extern uint16_t cstack[MAX_STACK+1];
extern int en_passant_sq;
extern const int8_t board64[64];
extern const int8_t RowNum[120];
extern PIECE *board[120];
extern unsigned int gflags;

extern const uint8_t ctbook_crc_dat[];
extern const uint32_t ctbook_crc_dat_len;
extern const uint32_t book_scan_crc_shift;
extern const uint32_t book_crc_index_cache[];

/*-- READ-WRITE --*/

/*---------- external functions ----------*/

extern int Play_Move_Is_Valid(MOVE key_move, const MOVE *restrict movelist, int move_cnt);
extern void Search_Do_Sort(MOVE *restrict movelist, int N);

/*---------- local functions ----------*/

static void Book_Convert_Move(const uint8_t *restrict buf, MOVE *restrict mp)
{
    /*convert from 8x8 to 10x12*/
    mp->m.from = board64[buf[0]];
    mp->m.to   = board64[buf[1]];
    mp->m.mvv_lva = 0;
    /*get the move type*/
    if (board[mp->m.from]->type == WPAWN)
        mp->m.flag = (mp->m.to < A8) ? WPAWN : WQUEEN; /*implicit queening*/
    else if (board[mp->m.from]->type == BPAWN)
        mp->m.flag = (mp->m.to > H1) ? BPAWN : BQUEEN; /*implicit queening*/
    else
        mp->m.flag = 1; /*normal move*/
}

/*returns 1 if the current position has a CRC in the bookbase, 0 else.
  result_len is the length of the opening book line if one is found.

  Layout for the binary opening book: N binary data lines, each line like this:
  CCCCLaAbB...

  CCCC is the 32bit CRC, most significant byte first. L is the length byte,
  indicating how many moves (not bytes!) the line contains. aA is a move where
  'a' denotes the "from" square in 8x8 notation, 'A' the "to" square.

  8x8 means: square A1 is 0, B1 is 1, C1 is 2, ...,
  A2 is 8, B2 is 9, ..., G8 is 62, H8 is 63.*/
static int Book_Get_Position_Line_NormBoard(uint8_t *restrict buffer, int *restrict result_len, const BPOS *restrict board_pos)
{
    uint32_t bufindex, fileindex, board_crc32, fileend, i, board_crc8_valid = 0;
    uint8_t board_crc8 = 0;

    /*at least another crc (4 bytes) and the length field (1 byte) must be ahead,
      or else the file end has been reached.*/
    fileend = ctbook_crc_dat_len - (sizeof(uint32_t) + sizeof(uint8_t));

    board_crc32 = Util_Crc32(board_pos, sizeof(BPOS));

    /*use the cached indices, that speeds up the search.
      the point here is that the opening book is sorted by ascending CRC-32.*/
    fileindex = book_crc_index_cache[board_crc32 >> book_scan_crc_shift];

    do
    {
        uint32_t book_crc32, line_len;

        /*start of the line: get the CRC out*/
        book_crc32 = Util_Hex_Long_To_Int(ctbook_crc_dat + fileindex);
        fileindex += sizeof(uint32_t); /*past the crc*/

        /*read the number of bytes for this position and move the file pointer
          forward to the beginning of the first move*/
        line_len  = ctbook_crc_dat[fileindex++] & 0x0FU;
        line_len *= 2U; /*convert moves to follow to bytes to follow*/

        if (book_crc32 == board_crc32) /*we probably found the current position!*/
        {
            if (LIKELY(line_len > 0)) /*must contain at least one move, i.e. two move bytes*/
            {
                uint8_t book_crc8;

                /*the CRC-32 matches, but what about the additional CRC-8?*/
                if (!board_crc8_valid)
                {
                    board_crc8 = Util_Crc8(board_pos, sizeof(BPOS));
                    board_crc8_valid = 1U;
                }

                /*we need the upper half of the length byte where the upper half
                  of the CRC-8 is stored.*/
                book_crc8  =  ctbook_crc_dat[fileindex - 1] & 0xF0U;
                /*the other 4 bits are in the 2 MSBs of the first move bytes.
                  that's a bit of a hack.*/
                book_crc8 |= (ctbook_crc_dat[fileindex    ] & 0xC0U) >> 4;
                book_crc8 |= (ctbook_crc_dat[fileindex + 1] & 0xC0U) >> 6;

                if (book_crc8 == board_crc8) /*everything matching!*/
                {
                    bufindex = 0;
                    /*get the found moves in 8x8 binary notation.*/
                    for (i = 0; ((i < line_len) && (bufindex < MAX_BOOK_MATCH * 2));)
                    {
                        buffer[bufindex++] = ctbook_crc_dat[fileindex + i++] & BP_MV_MASK; /*from*/
                        buffer[bufindex++] = ctbook_crc_dat[fileindex + i++] & BP_MV_MASK; /*to*/
                    }
                    *result_len = bufindex;
                    return(1);
                } else if (book_crc8 > board_crc8)
                {
                    /*since the CRCs are sorted, no match can happen anymore*/
                    return(0);
                } else
                {
                    /*failed on the additional CRC-8, but match may still happen*/
                    fileindex += line_len; /*so many bytes follow in this line*/
                }
            } else /*position found, but no moves in it?! should not happen.*/
            {
                return(0);
            }
        } else
        /*scan forward until the start of the next line.*/
        {
            /*the crc column is sorted in ascending order in the book.
              if the current line has a greater crc than our board position,
              there cannot be a match anymore, and we can exit without match.
              that way, we have only to scan through half the relevant
              opening book range on average for non-matching positions.*/
            if (book_crc32 > board_crc32)
                return(0);

            fileindex += line_len; /*so many bytes follow in this line*/
        }

    } while (fileindex < fileend);

    return(0);
}

/*converts a move list given 8x8 from/to buffer into the binary format,
  including the move flags. returns how many moves the result list contains.*/
static int Book_Convert_Movelist(const uint8_t *restrict buffer, MOVE *restrict book_movelist, int book_line_len)
{
    int i, listlen;

    for (i = 0, listlen = 0; ((i < book_line_len) && (listlen < MAX_BOOK_MATCH)); i += 2)
    {
        /*the next 2 bytes contain a move that we'll convert.*/
        Book_Convert_Move(buffer + i, book_movelist + listlen);
        listlen++;
    }

    return(listlen);
}

/*tries a normal lookup, and if that fails, a lookup with flipped colours.
  e.g. if white starts out with 1. e2-3, black may answer 1. ... e7-e5. if white
  now plays 2. e3-e4, then we have the normal king's pawn opening with flipped
  colours. So black can make the mirror moves of what white could do, which may
  give e.g. a flipped Ruy-Lopez. Sounds crazy because white gives up the advantage
  of the first move, but it is a way how to rely on one's solid opening book
  knowledge while throwing the machine out of its book.

  Well, unless the machine is smart enough to know that trick!*/
static int Book_Get_Position_Line(MOVE *restrict book_movelist)
{
    int book_line_len, i, j, flags;
    BPOS board_pos, mirror_pos;
    /*MAX_BOOK_MATCH is in moves, the line is in from/to bytes*/
    uint8_t book_line[MAX_BOOK_MATCH * 2];

    /*first build up an opening book format board from the actual board
      position.*/
    for (i = BP_A1; i <= BP_H8; i++)
    {
        /*the actual board position is in 12x10 while the opening book is
          in 8x8.*/
        board_pos.board[i] = board[board64[i]]->type;
    }

    /*get the castling flags. the make move routine for the main board has
      to match the handling of the book here: e.g. if the king moves, both
      rooks have to be marked as moved, too. if both rooks have moved, the
      king must be marked as moved.
      anyway, that is also needed for correct evaluation of threefold
      repetition.*/
    board_pos.board[BP_STATUS_FLAGS] = (gflags & CASTL_FLAGS);

    /*which side is to move. note that gflags denote who made the last move
      while the opening book expects the information who is to make the next
      move.*/
    if ((gflags & BLACK_MOVED) == 0)
        board_pos.board[BP_STATUS_FLAGS] |= BLACK_TO_MOVE;

    if (Book_Get_Position_Line_NormBoard(book_line, &book_line_len, &board_pos))
        return(Book_Convert_Movelist(book_line, book_movelist, book_line_len));

    /*no book hit. so let's try it with a reversed board.*/
    flags = board_pos.board[BP_STATUS_FLAGS];

    mirror_pos.board[BP_STATUS_FLAGS] = (flags & BLACK_TO_MOVE) ? FLAGRESET : BLACK_TO_MOVE;

    flags &= CASTL_FLAGS;
    /*swap the white and black castling flags*/
    mirror_pos.board[BP_STATUS_FLAGS] |= ((flags >> 3 ) | (flags  << 3));

    for (i = BP_A1; i <= BP_A8; i += BP_RANK_DIFF)
    {
        for (j = 0; j < BP_RANK_DIFF; j++)
        {
            /*swap the colours and mirror vertically*/
            int piece = board_pos.board[i + j];
            if (piece > NO_PIECE)
            {
                if (piece <= WKING)
                    piece += 10;
                else
                    piece -= 10;
            }
            mirror_pos.board[BP_A8 - i + j] = piece;
        }
    }

    if (Book_Get_Position_Line_NormBoard(book_line, &book_line_len, &mirror_pos))
    {
        /*mirror position found, i.e. with flipped colours. now convert the
          moves back to the board situation, i.e. mirror them.*/
        for (i = 0; i < book_line_len; i++)
        {
            uint8_t file, rank, square;

            square = book_line[i];
            file = square & BP_COL_MASK;
            rank = square & BP_RANK_MASK;
            rank = BP_RANK_MASK - rank; /*vertical mirror*/
            square = rank | file;
            book_line[i] = square;
        }
        return(Book_Convert_Movelist(book_line, book_movelist, book_line_len));
    }
    return(0);
}

/*checks whether the move is an EP move that only fails on the
  move order with regard to the EP square but would be legal otherwise.
  if the board movelist is empty, the first move will contain invalid
  data, but since no book move match can occur anyway, that doesn't
  matter. Besides, (stale)mate is checked before the book lookup.*/
static int Book_Maybe_EP_Move(MOVE move, const MOVE *restrict movelist)
{
    int to = move.m.to;

    if (board[to]->type == NO_PIECE) /*EP goes to empty square*/
    {
        int from = move.m.from;

        if (board[movelist->m.from]->type < BLACK) /*white to move*/
        {
            if ((board[from]->type == WPAWN) &&
                ((to == from + RANK_DIFF + FILE_DIFF) || (to == from + RANK_DIFF - FILE_DIFF)) &&
                (RowNum[to] == 6) &&
                (board[to - RANK_DIFF]->type == BPAWN) &&
                (board[to + RANK_DIFF]->type == NO_PIECE))
            {
                /*white EP capture found*/
                return(1);
            }
        } else /*black to move*/
        {
            if ((board[from]->type == BPAWN) &&
                ((to == from - RANK_DIFF + FILE_DIFF) || (to == from - RANK_DIFF - FILE_DIFF)) &&
                (RowNum[to] == 3) &&
                (board[to + RANK_DIFF]->type == WPAWN) &&
                (board[to - RANK_DIFF]->type == NO_PIECE))
            {
                /*black EP capture found*/
                return(1);
            }
        }
    }
    /*no possible EP capture*/
    return(0);
}


/* ---------- global functions ----------*/


/*we have our position in the regular board, and we have several suggested moves in
  movelist[] which are possible in our position. let's look whether one or
  more of the moves in movelist[] are in our book for the current position, and
  if so, return TRUE and also which move was chosen.*/
int Book_Is_Line(int *restrict book_movelist_index, const MOVE *restrict movelist,
                 int moves, int is_full_list)
{
    int i, book_movelist_len;
    unsigned matched = 0;
    MOVE book_movelist[MAX_BOOK_MATCH];
    uint8_t matching_book_moves[MAX_BOOK_MATCH];

    *book_movelist_index = -1;
    book_movelist_len = Book_Get_Position_Line(book_movelist);

    if (book_movelist_len) /*is the current board position in the opening book?*/
    {
        /*check whether moves from the book list are illegal on the board.
          an illegal EP move can happen because of EP in the book and
          transpositions, but otherwise, we probably have a CRC-40 collision
          and have to prevent an erroneous book hit. The remaining risk is
          only minimal.*/
        if (is_full_list)
        {
            for (i = 0; i < book_movelist_len; i++)
            {
                MOVE bmove = book_movelist[i];

                if (!(Play_Move_Is_Valid(bmove, movelist, moves) ||
                      Book_Maybe_EP_Move(bmove, movelist)))
                {
                    /*illegal move found - don't return a book match*/
                    return(0);
                }
            }
        }

        /*for every move possible in the board position*/
        for (i = 0; i < moves; i++)
        {
            if (Play_Move_Is_Valid(movelist[i], book_movelist, book_movelist_len)) /*is the move in the opening book move list?*/
            {
                if (LIKELY(matched < MAX_BOOK_MATCH)) /*is there room for some more moves in the response list?*/
                {
                    matching_book_moves[matched] = (uint8_t) i; /*record the board move list index of the move*/
                    matched++;
                }
            }
        }
    }

    if (matched == 0) /*we are out-of-book.*/
        return(0);

    /*make a random choice among the available response moves.*/
    i = Util_Rand(matched);

    /*tell the caller that the opening book choice has the index *book_movelist_index
      within the move list that was passed to this routine.*/
    *book_movelist_index = matching_book_moves[i];

    return(1);
}

/*returns a list of all moves that are in the book in the current position.*/
void Book_Get_Moves(MOVE *restrict result_movelist, int *result_moves, enum E_COLOUR colour)
{
    MOVE movelist[MAXMV]; /*what moves are possible on the board*/
    MOVE book_movelist[MAX_BOOK_MATCH]; /*what moves are in the book*/
    int i, move_cnt, raw_move_cnt, book_movelist_len;

    *result_moves = 0;

    book_movelist_len = Book_Get_Position_Line(book_movelist);
    if (book_movelist_len == 0) /*position not found in the book*/
        return;

    /*find all legal board moves*/
    raw_move_cnt = Mvgen_Find_All_Moves(movelist, NO_LEVEL, colour, UNDERPROM);
    move_cnt = 0;
    for (i = 0; i < raw_move_cnt; i++) {
        /*filter out all moves that would put or let our king in check*/
        Search_Push_Status();
        Search_Make_Move(movelist[i]);
        if (Mvgen_King_In_Check(colour)) {
            Search_Retract_Last_Move();
            Search_Pop_Status();
            movelist[i].m.mvv_lva = MVV_LVA_ILLEGAL;
            movelist[i].m.flag = 0;
            continue;
        }
        Search_Retract_Last_Move();
        Search_Pop_Status();
        move_cnt++;
    }
    if (move_cnt == 0) /*no legal moves possible*/
        return;

    Search_Do_Sort(movelist, raw_move_cnt);

    /*now match the legal board moves against the book moves. We can't just take
      the book list because the book may contain e.p. moves that are filtered out
      by matching the lists if the board position doesn't have that e.p. move
      anymore.*/

    for (i = 0; i < move_cnt; i++) /* for every move possible in the board position*/
    {
        if (Play_Move_Is_Valid(movelist[i], book_movelist, book_movelist_len)) /*is the move in the opening book move list?*/
        {
            result_movelist[*result_moves].u = movelist[i].u;
            (*result_moves)++;
        }
    }
}
