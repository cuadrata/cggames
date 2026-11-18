/*
 * cgchess: the rules of chess
 * Copyright (C) 2026 The CGTK authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef CHESS_H
#define CHESS_H

#include <glib.h>

/* The squares, a1 0 to h8 63, a rank at a time. */
#define CHESS_FILE(s) ((s) & 7)
#define CHESS_RANK(s) ((s) >> 3)
#define CHESS_SQUARE(file, rank) ((rank) * 8 + (file))

/* The pieces: a kind, and 8 for black. */
enum
{
  CHESS_EMPTY, CHESS_PAWN, CHESS_KNIGHT, CHESS_BISHOP, CHESS_ROOK,
  CHESS_QUEEN, CHESS_KING
};
#define CHESS_BLACK_PIECE 8
#define CHESS_KIND(p) ((p) & 7)
#define CHESS_SIDE(p) ((p) >> 3)        /* 0 white, 1 black */

#define CHESS_WHITE 0
#define CHESS_BLACK 1

/* The castling still allowed. */
#define CHESS_WHITE_SHORT 1
#define CHESS_WHITE_LONG 2
#define CHESS_BLACK_SHORT 4
#define CHESS_BLACK_LONG 8

typedef struct
{
  gint8 board[64];
  gint side;                    /* to move */
  gint castling;
  gint ep;                      /* the square a pawn may take en passant, or -1 */
  gint halfmoves;               /* since a capture or a pawn's move */
  gint fullmove;
} ChessPosition;

typedef struct
{
  gint8 from, to;
  gint8 promotion;              /* the kind a pawn becomes, or 0 */
} ChessMove;

#define CHESS_MAX_MOVES 256

void chess_start (ChessPosition *pos);

/* The legal moves in pos, into moves; how many. */
gint chess_moves (const ChessPosition *pos, ChessMove *moves);

/* Whether side's king is attacked. */
gboolean chess_in_check (const ChessPosition *pos, gint side);

/* Plays a legal move. */
void chess_play (ChessPosition *pos, ChessMove move);

/* A move in coordinates, as engines write them: "e2e4", "e7e8q". */
#define CHESS_COORD_LEN 6
void chess_coord (ChessMove move, gchar *out);

/* The legal move written in coordinates as text, into *move; FALSE if
 * it isn't one. */
gboolean chess_parse_coord (const ChessPosition *pos, const gchar *text,
                            ChessMove *move);

/* A legal move in standard algebraic notation: "Nf3", "exd8=Q+". */
#define CHESS_SAN_LEN 10
void chess_san (const ChessPosition *pos, ChessMove move, gchar *out);

/* The legal move written in standard algebraic notation, as PGN files
 * have it, into *move; checks, captures and annotations may be marked or
 * not, castling written with O or 0, and a move written in coordinates
 * is taken too. FALSE if it isn't one, or could be more than one. */
gboolean chess_parse_san (const ChessPosition *pos, const gchar *text,
                          ChessMove *move);

/* The position in Forsyth-Edwards notation. */
#define CHESS_FEN_LEN 100
void chess_fen (const ChessPosition *pos, gchar *out);

/* A position from Forsyth-Edwards notation; FALSE if fen isn't one. */
gboolean chess_parse_fen (ChessPosition *pos, const gchar *fen);

/* How the game stands after the positions in history, the last the
 * position now. */
typedef enum
{
  CHESS_PLAYING, CHESS_CHECKMATE, CHESS_STALEMATE, CHESS_FIFTY_MOVES,
  CHESS_NO_MATERIAL, CHESS_REPETITION
} ChessState;

ChessState chess_state (const ChessPosition *history, gint n);

#endif
