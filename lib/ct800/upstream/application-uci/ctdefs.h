/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2015-2024, Rasmus Althoff <info@ct800.net>
 *  Copyright (C) 2010-2014, George Georgopoulos
 *
 *  This file is part of CT800/NGPlay (common definitions).
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

#define VERSION_INFO_DIALOGUE_LINE_1    "CT800 V1.46"
#define VERSION_INFO_DIALOGUE_LINE_2    "2016-2024 by Rasmus Althoff"
#define VERSION_INFO_DIALOGUE_LINE_3    "Free software under GPLv3+"

#define Abs(a)             (((a) >= 0) ? (a) : -(a))
#define Max(a,b)           (((a) >= (b)) ? (a) : (b))
#define MAXMV              300
#define MAXCAPTMV          64
#define CHECKLISTLEN       64
#define START_DEPTH        2
#define ID_WINDOW_DEPTH    (START_DEPTH+2)
#define PRE_DEPTH          1
#define NULL_START_DEPTH   2
/*2^11: history range is 0...2047*/
#define HIST_BITS          11U
#define HIST_MAX           (1U << HIST_BITS)
#define HIST_MVV_BITS      5U
#define HIST_MVV_MAX       (1U << HIST_MVV_BITS)
#define HIST_SHIFT_BITS    (HIST_BITS - HIST_MVV_BITS)
#define MAX_DEPTH          43
#if (MAX_DEPTH * MAX_DEPTH >= HIST_MAX)
    #error "History bit depth needs to be adapted."
#endif
#define MAX_QIESC_DEPTH    10
#define MAX_PLIES          801
#define MAX_STACK          (MAX_PLIES+MAX_DEPTH+MAX_QIESC_DEPTH)
#define IID_DEPTH          5
#define LMR_MOVES          4
#define LMR_DEPTH_LIMIT    3
#define LMP_DEPTH_LIMIT    2
#define CHECK_DEPTH        6
#define SORT_THRESHOLD     (EASY_THRESHOLD * 2)
#define EASY_THRESHOLD     200
#define EASY_MARGIN_DOWN   (-50)
#define EASY_MARGIN_UP     50
#define EASY_DEPTH         6
/*shifted 8 bit constants are better on ARM*/
#define INFINITY_          (16384 + 8192)
#define NO_RESIGN          (INFINITY_ + 1024)
#define INF_MATE_1         (INFINITY_ - 1)
#define MATE_CUTOFF        (INFINITY_ - 1024)
#define RESIGN_EVAL        1200
#define CONTEMPT_VAL       (-30)
#define CONTEMPT_END       70
#define TERMINAL_NODE      (-1)
#define LIGHT_SQ           1
#define DARK_SQ            2
#define TWO_COLOUR         3
#define PAWN_V             100
#define KNIGHT_V           320
#define BISHOP_V           325
#define ROOK_V             510
#define QUEEN_V            960
#define TUNE_PAWN_V        100
#define TUNE_KNIGHT_V      370
#define TUNE_BISHOP_V      380
#define TUNE_ROOK_V        600
#define TUNE_QUEEN_V      1200
#define QS_QUEEN_PAWN     1200
#define SIDE_TO_MOVE_V      10
#define EG_WINNING_MARGIN  450
#define PV_CHANGE_THRESH    50
#define EG_PIECES            6
/*15 book matches is the limit - the book format has only 4 bits for the
  number of match moves per position. currently, up to 8 are used.*/
#define MAX_BOOK_MATCH     15
#define NO_LEVEL           (-1)
#define QUEENING           0
#define UNDERPROM          1
#define QS_NO_CHECKS       0
#define QS_CHECKS          1
#define QS_CHECK_DEPTH     4
#define QS_RECAPT_DEPTH    5 /*must be greater than QS_CHECK_DEPTH*/
#define QS_DELTA_MARGIN    100
#define HIGH_EVAL_NOISE    30
#define MG_PST             0
#define EG_PST             1

/*approximate base Elo at 30 kNPS*/
#define BASE_ELO           2300
#define BASE_NODES         30000U
#define MIN_NODES          300U
/*how Elo goes down from 30 kNPS with half the speed.
  must be divisible by 8.*/
#define ELO_HALF           72
/*how Elo goes up from 30 kNPS when doubling the speed.
  upwards is less than downwards because the strategic and
  endgame knowledge doesn't increase.
  must be divisible by 8.*/
#define ELO_DOUBLE         56

#define NO_ACTION_PLIES    40
#define FIFTY_MOVES_FULL   80

#define DEF_MAX_TT         0x8000U
#define PMAX_TT            0x2FFFU
#define CLUSTER_SIZE       3
#define MAX_AGE_CNT        3
#define HASH_DEFAULT       8    /*in MB*/
#define HASH_MIN           1    /*in MB*/
#define HASH_MAX           1024 /*in MB*/

/*13 kB ring buffer size. can hold more than three of the longest allowed
  UCI commands.*/
#define CMD_BUF_SIZE       (13L * 1024L)

/*input buffer in bytes*/
#define STDIN_BUF_SIZE     2048

/*maximum expected UCI command length is:
  "position fen r1n1b1q1/k1b1n1r1/p1p1p1p1/p1p1p1p1/P1P1P1P1/P1P1P1P1/R1N1B1Q1/K1B1N1R1 w KQkq f3 1000 100 moves \r\n"
  plus the move list which may take up to 801 plies plus 16 promotions plus
  16 bytes spare. multiple consecutive whitespaces are filtered out before
  putting a command into the buffer.*/
#define CMD_UCI_LEN        ((MAX_PLIES * 5L) + 113L + 32L)

#if (CMD_BUF_SIZE < CMD_UCI_LEN + 3)
    #error "CMD_BUZ_SIZE is too small for CMD_UCI_LEN."
#endif

/*states when filling the UCI command into the inter-thread ring buffer*/
enum E_CMD_CAN_WRITE {
    CMD_CAN_WRITE_WAIT,
    CMD_CAN_WRITE_OK,
    CMD_CAN_WRITE_FORCE
};

/*states for de-casing the "position" command*/
enum E_FEN_SCAN {
    FEN_SCAN_F,
    FEN_SCAN_E,
    FEN_SCAN_N,
    FEN_SCAN_MOVES,
    FEN_SCAN_DONE
};

#define PERFT_CHECK_NODES  100000U


/*now for some GCC specific attributes, builtins and pragmas. this is
  encapsulated via defines so that switching to another compiler will be
  easier if need be. works also with Clang.*/

/*alignment*/
#define ALIGN_4            __attribute__((aligned (4)))

/*telling LIKELY/UNLIKELY branches*/
#define LIKELY(x)          __builtin_expect(!!(x),1)
#define UNLIKELY(x)        __builtin_expect(!!(x),0)

/*for removing warnings of unused parameters that have to be there e.g. to
  have a function prototype match.*/
#define VAR_UNUSED         __attribute__((unused))

enum E_PROT_TYPE {PROT_NONE, PROT_UCI};

/*UCI time mode*/
enum E_TIME_CONTROL {
    TM_TIME_NONE,      /*none given*/
    TM_TIME_CONTROLS,  /*dynamic time per move*/
    TM_TIME_PER_MOVE   /*fixed time per move*/
};
/*infinite: ten years thinking time*/
#define INFINITE_TIME      (1000LL * 60LL * 60LL * 24LL * 365LL * 10LL)

/*UCI token parser for the GO command*/
enum E_TOKEN_TYPE {
    TOKEN_NONE,
    TOKEN_TEXT,
    TOKEN_TEXT_VALUE,
    TOKEN_MOVELIST
};

/*return codes from the FEN / movelist parser:
  useful for detailed error messages.*/
enum E_POS_VALID {
    POS_OK,
    POS_ERROR,
    POS_NO_FEN,
    POS_BAD_COORD,
    POS_BAD_PIECE,
    POS_IN_CHECK,
    POS_NO_SIDE,
    POS_OVERPROM,
    POS_TOO_MANY_PIECES,
    POS_TOO_MANY_PAWNS,
    POS_NO_KING,
    POS_KING_CLOSE,
    POS_PAWN_RANK,
    POS_BAD_MOVE_FORMAT,
    POS_ILLEGAL_MOVE,
    POS_TOO_MANY_MOVES
};

/*for showing current move*/
enum E_CURRMOVE {
    CURR_NEVER,
    CURR_UPDATE,
    CURR_ALWAYS
};

/*which kind of UCI command*/
enum E_CMD_TYPE {
    CMD_GENERIC,
    CMD_STOP,
    CMD_POSITION
};

/*for throttling depending on the cause*/
enum E_THROTTLE {
    THROTTLE_NONE,
    THROTTLE_CPU_PERCENT,
    THROTTLE_NPS_RATE
};

#define MIN_THROTTLE_KNPS  1L
#define MAX_THROTTLE_KNPS  32000L
/*NO_THROTTLE_NPS needs to have some headroom for a possible multiplication
  with up to 1000 without uint64_t overflow: search.c, Time_Calc_Throttle()*/
#define NO_THROTTLE_NPS    (1ULL << 52)

#define DEFAULT_MOVE_OVERHEAD 50LL

/*status flags for play and opening book (gflags)*/
#define WKMOVED            1
#define WRA1MOVED          2
#define WRH1MOVED          4
#define BKMOVED            8
#define BRA8MOVED          16
#define BRH8MOVED          32

/*status flags for play (gflags)*/
#define BLACK_MOVED        64
#define ALLFLAGS          (WKMOVED | WRA1MOVED | WRH1MOVED | BKMOVED | BRA8MOVED | BRH8MOVED | BLACK_MOVED)
#define FLAGRESET          0

/*status flags for the opening book (gflags)*/
#define CASTL_FLAGS        63
#define BLACK_TO_MOVE      64

/*board files definitions for the pawn masking*/
#define ALL_FILES_FREE     0x00U
#define NO_FILES_FREE      0xFFU
#define H_FILE             0x80U
#define G_FILE             0x40U
#define F_FILE             0x20U
#define E_FILE             0x10U
#define D_FILE             0x08U
#define C_FILE             0x04U
#define B_FILE             0x02U
#define A_FILE             0x01U

#define MIDDLE_FILES       (C_FILE | D_FILE | E_FILE | F_FILE)
#define CENTRE_FILES       (D_FILE | E_FILE)
#define KINGSIDE_FILES     (F_FILE | G_FILE | H_FILE)
#define QUEENSIDE_FILES    (A_FILE | B_FILE | C_FILE)
#define NOT_CENTRE_FILES   (KINGSIDE_FILES | QUEENSIDE_FILES)
#define EDGE_FILES         (A_FILE | H_FILE)
#define FLANK_FILES        (B_FILE | C_FILE | F_FILE | G_FILE)
#define QUEEN_SIDE         (A_FILE | B_FILE | C_FILE | D_FILE)
#define KING_SIDE          (E_FILE | F_FILE | G_FILE | H_FILE)

#define BOARD_A_FILE       1
#define BOARD_B_FILE       2
#define BOARD_C_FILE       3
#define BOARD_D_FILE       4
#define BOARD_E_FILE       5
#define BOARD_F_FILE       6
#define BOARD_G_FILE       7
#define BOARD_H_FILE       8

/*extra score in centipawns in middlegame for pawns on DE / FC files*/
#define PAWN_DE_VAL       10
#define PAWN_FC_VAL        5

enum E_COMP_RESULT {
    COMP_MOVE_FOUND,
    COMP_MATE,
    COMP_STALE,
    COMP_MAT_DRAW,
    COMP_RESIGN,
    COMP_NO_MOVE
};

/*for the mate searcher*/
enum E_MATE_CHECK {EXACT_CHECK_DEPTH, MIN_CHECK_DEPTH};

/*for the info dialogue with the position evaluation*/
enum E_POS_EVAL {EVAL_INVALID, EVAL_BOOK, EVAL_MOVE};

/*timeout state during search*/
enum E_TIMEOUT {
    TM_NO_TIMEOUT,
    TM_TIMEOUT,
    TM_NODES,
    TM_ABORT
};

#define CCM_RAM
#define FLASH_ROM
#define DATA_SECTION
#define VAR_SECTION(X)

/*this is practically equivalent to using the sizeof operator during compile time
- a bit tricky because sizeof and #if cannot be combined directly. Taken from
the Linux kernel.
ISO C doesn't allow 0 length arrays, so the mapping goes to 1 and -1. Since
the array is declared as extern, it doesn't cost memory.*/
#define BUILD_ASSERT(condition, assertlabel, message) extern int VAR_UNUSED assertlabel[((!!(condition)) * 2) - 1]

/*piece types for the board and MVV/LVA*/
enum
{
    NO_PIECE,
    WPAWN =  2, WKNIGHT, WBISHOP, WROOK, WQUEEN, WKING,
    BPAWN = 12, BKNIGHT, BBISHOP, BROOK, BQUEEN, BKING,
    PIECEMAX
};

enum E_COLOUR {WHITE = 1, NONE = 3, BLACK = 10};

enum E_MOVESTATE {MOVE_SAFE, MOVE_UNSAFE, MOVE_UNKNOWN};

#define IS_NOT_ENDGAME      0
#define IS_ENDGAME          1U
#define IS_ENDGAME_WHITE    (1U << WHITE)
#define IS_ENDGAME_BLACK    (1U << BLACK)

#define WKING_CHAR         'K'
#define WQUEEN_CHAR        'Q'
#define WROOK_CHAR         'R'
#define WBISHOP_CHAR       'B'
#define WKNIGHT_CHAR       'N'
#define WPAWN_CHAR         'P'
#define BKING_CHAR         'k'
#define BQUEEN_CHAR        'q'
#define BROOK_CHAR         'r'
#define BBISHOP_CHAR       'b'
#define BKNIGHT_CHAR       'n'
#define BPAWN_CHAR         'p'

enum {NORMAL, CASTL, PROMOT};
enum {NO_FLAG, EXACT, CHECK_ALPHA, CHECK_BETA};
enum {CUT_NODE, PV_NODE};

enum
{
    A1=21, B1, C1, D1, E1, F1, G1, H1,
    A2=31, B2, C2, D2, E2, F2, G2, H2,
    A3=41, B3, C3, D3, E3, F3, G3, H3,
    A4=51, B4, C4, D4, E4, F4, G4, H4,
    A5=61, B5, C5, D5, E5, F5, G5, H5,
    A6=71, B6, C6, D6, E6, F6, G6, H6,
    A7=81, B7, C7, D7, E7, F7, G7, H7,
    A8=91, B8, C8, D8, E8, F8, G8, H8, ENDSQ
};

#define FILE_DIFF (B1 - A1)
#define RANK_DIFF (A2 - A1)

enum E_BP_SQUARE
{
    BP_A1, BP_B1, BP_C1, BP_D1, BP_E1, BP_F1, BP_G1, BP_H1,
    BP_A2, BP_B2, BP_C2, BP_D2, BP_E2, BP_F2, BP_G2, BP_H2,
    BP_A3, BP_B3, BP_C3, BP_D3, BP_E3, BP_F3, BP_G3, BP_H3,
    BP_A4, BP_B4, BP_C4, BP_D4, BP_E4, BP_F4, BP_G4, BP_H4,
    BP_A5, BP_B5, BP_C5, BP_D5, BP_E5, BP_F5, BP_G5, BP_H5,
    BP_A6, BP_B6, BP_C6, BP_D6, BP_E6, BP_F6, BP_G6, BP_H6,
    BP_A7, BP_B7, BP_C7, BP_D7, BP_E7, BP_F7, BP_G7, BP_H7,
    BP_A8, BP_B8, BP_C8, BP_D8, BP_E8, BP_F8, BP_G8, BP_H8, BP_NOSQUARE
};

enum {BP_FILE_A, BP_FILE_B, BP_FILE_C, BP_FILE_D, BP_FILE_E, BP_FILE_F, BP_FILE_G, BP_FILE_H};

#define BP_FILE_DIFF       (BP_B1 - BP_A1)
#define BP_RANK_DIFF       (BP_A2 - BP_A1)
#define BP_UP_RIGHT        (BP_RANK_DIFF + BP_FILE_DIFF)
#define BP_UP_LEFT         (BP_RANK_DIFF - BP_FILE_DIFF)
#define BP_FILE(square)    (square % BP_RANK_DIFF)
#define BP_RANK(square)    (square / BP_RANK_DIFF)
#define BP_STATUS_FLAGS    64
#define BP_COL_MASK        (0x07U << 0)
#define BP_RANK_MASK       (0x07U << 3)
#define BP_MV_MASK         (BP_RANK_MASK | BP_COL_MASK)

#define MV_NO_MOVE_MASK    0
#define MV_NO_MOVE_CMASK   0
/*regular search*/
#define MVV_LVA_MATE_1     127
#define MVV_LVA_PV         126
#define MVV_LVA_HASH       125
#define MVV_LVA_CSTL_SHORT  91
#define MVV_LVA_CSTL_LONG   85
#define MVV_LVA_KILLER_0     2
#define MVV_LVA_KILLER_1     1
#define MVV_LVA_TACTICAL     0
#define MVV_LVA_ILLEGAL   -126
/*mate searcher*/
#define MVV_LVA_CHECK      126
/*50 moves resort*/
#define MVV_LVA_50_OK      125
#define MVV_LVA_50_NOK    -124
struct mvdata
{
    uint8_t flag;  /*0:NULL, 1:piece move, >1: pawn move  2=WPAWN or 12=BPAWN : simple pawn move, else stores promotion piece */
    uint8_t from;  /*in 10x12 mailbox format*/
    uint8_t to;    /*in 10x12 mailbox format*/
    int8_t mvv_lva; /* stores value for move ordering. >0 captures or pawn promotion,  <0 non captures */
};

/*for this union, u must have the same size as the struct!*/
typedef union
{
    struct mvdata m;
    uint32_t u;
} MOVE;

typedef uint16_t CMOVE; /*compressed move*/

/*for move lines like PV*/
typedef struct t_line
{
    int16_t line_len;               /*Number of moves in the line.*/
    CMOVE line_cmoves[MAX_DEPTH+2]; /*The line in compressed move format.*/
} LINE;

typedef struct piece_st
{
    struct piece_st *next;
    struct piece_st *prev;
    int8_t type;
    int8_t xy;
    /*mobility for pieces : number of moves - max_per piece/2.
               for pawns:   passed pawn evaluation.*/
    int8_t mobility;
} PIECE;

/*struct for the main move stack*/
typedef struct mvst
{
    uint64_t mv_pos_hash;
    uint64_t mv_pawn_hash;
    MOVE move;
    PIECE *captured;
    int16_t material;
    int8_t capt;
    uint8_t special;   /* values: NORMAL,CASTL,PROMOT */
} MVST;

/*general notes on the hash tables: of course, if would be nice to store the full 64bit
position hash and not just the upper 32bits, but with 4096*2+2048 entries, that would require
additional (!) 20kB of RAM. That isn't left over, so the hash table sizes would have to be reduced.
So only 32bit are saved, 6 more bits get fiddled into some free space within the entries, and the
index serves also as part of the hash sum. All in all, that gives around 48bits of hash without
wasting 20kB.*/

typedef struct tt_st
{
    /*if the cashed entry is not a terminal node, it is even more valuable because it represents a whole
    subtree. if it is a terminal node, then the move entry will be set to 0. so if the move entry
    is not zero, a hash collision would be more severe - that's why, if there is a stored move, it is
    checked for pseudo legality when looking up the entry. if we have a hash collision, it is not
    probable that the right kind of piece is at the from-square for that move, and that the to-square is
    either free or occupied by an enemy piece.*/
    CMOVE cmove;
    uint16_t pos_hash_upper_h;
    uint16_t pos_hash_upper_l;
    int16_t value;
    /*the flag holds the values 1, 2 or 3 in the lowest two bits, depending on whether the stored value
    is an exact evaluation, an alpha- or a beta bound. 0 is not used, so that these bit also work as validity
    bits since the table is cleared before starting the move calculation.
    that means the upper 6 bits are still free, so 6 more bits from the 64 bit position hash are stuffed
    in here. the lowest 12 bits work as index, with a cluster length of 3, so that effectively, 10 bits
    from the position hash are indirectly stored here via the index. All in all, that gives a stored
    position hash length of 32+6+10=48 bits.*/
    uint8_t flag;
    /*the lower 6 bits of depth contain the actual depth. The upper 2 bits contain the hash clear
    counter which is increased upon every search so that before each search, the oldest 25% of
    the entries are cleared.*/
    uint8_t depth;
#if (MAX_DEPTH > 63)
    #error "Review the TT_ST data structure with depth (too high) and clear counter!"
#endif
#if (MAX_AGE_CNT > 3)
    #error "Review the TT_ST data structure with clear counter (too high) and depth!"
#endif
} TT_ST;


typedef struct tt_ptt_st
{
    /*here, we use only 32bits from the position hash plus 11 bits indirectly from the index,
    so that the effective stored hash length becomes 43. 5 more hash bits get fiddled into the
    value variable, which is only used in the range of +/-511. Then we are at 48 bits of hash.*/
    uint32_t pawn_hash_upper;
#define PTT_VALUE_BITS     0x01FFU
#define PTT_SIGN_BIT       0x0200U
#define PTT_MG_BIT         0x0400U
#define PTT_HASH_BITS      0xF800U
    /*the lowest 9 bits of the value hold the absolute value, the 10th is for the sign.
    1 bit is for storing whether the pawn structure eval was only middlegame (MG) or
    with the endgame modifications.
    this gives 5 more bits to stuff in a bigger portion of the pawn hash value, as to
    minimise hash collisions.

    Of course, the bit twiddling could be done with a signed 16bit integer as well, but
    the C standard doesn't specify how a signed int is implemented. On ARM as well as
    on x86, it is the two's complement, but the unsigned int is specified (though not
    in the byte order, of course)*/
    uint16_t value;
    uint8_t w_pawn_mask; /*files with white pawns. LSB is the A file.*/
    uint8_t b_pawn_mask;
} TT_PTT_ST;

/*make this a separate table to avoid padding issues (or performance penalty).*/
typedef struct tt_ptt_rook_st
{
    uint8_t w_rook_files;
    uint8_t b_rook_files;
} TT_PTT_ROOK_ST;

typedef struct t_game_info
{
    int depth;
    int eval;
    int last_valid_eval;
    enum E_POS_EVAL valid;
} GAME_INFO;

/*opening book position keeping*/
typedef struct t_pos
{
    uint8_t board[65]; /*holds the piece standing on a square or 0. a1 = index 0, h1 = index 7, h8 = index 63.
                         index 64 is the flags for castling rights.*/
} BPOS;

/*the following typedefs are just for eval.c. their point is
to split the static eval function without having to pass around tons
of variables, just struct pointers.*/
typedef struct t_pawn_info
{
    int32_t w_pawns;
    int32_t b_pawns;
    int32_t all_pawns;
    int32_t w_isolani;
    int32_t b_isolani;
    int32_t w_outpassed;
    int32_t b_outpassed;
    int32_t deval_pawn_majority;
    uint32_t w_passed_rows[8];
    uint32_t b_passed_rows[8];
    uint32_t w_pawn_mask;
    uint32_t b_pawn_mask;
    uint32_t w_rook_files;
    uint32_t b_rook_files;
    uint32_t w_passed_pawns;
    uint32_t b_passed_pawns;
    uint32_t w_passed_mask;
    uint32_t b_passed_mask;
    uint32_t w_d_pawnmask;
    uint32_t b_d_pawnmask;
    int32_t w_passed_mobility;
    int32_t b_passed_mobility;
    int32_t extra_pawn_val;
} PAWN_INFO;

typedef struct t_piece_info
{
    int32_t white_pieces;
    int32_t black_pieces;
    int32_t all_pieces;
    int32_t all_queens;
    int32_t all_rooks;
    int32_t w_queens;
    int32_t w_rooks;
    int32_t w_bishops;
    int32_t w_knights;
    int32_t b_queens;
    int32_t b_rooks;
    int32_t b_bishops;
    int32_t b_knights;
    int32_t all_minor_pieces;
    int32_t w_bishop_colour;
    int32_t b_bishop_colour;
} PIECE_INFO;
