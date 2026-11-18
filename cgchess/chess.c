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

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chess.h"

static const gint knight_steps[8][2] = {
  { 1, 2 }, { 2, 1 }, { 2, -1 }, { 1, -2 },
  { -1, -2 }, { -2, -1 }, { -2, 1 }, { -1, 2 }
};
static const gint king_steps[8][2] = {
  { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 },
  { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }
};
static const gint rook_dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
static const gint bishop_dirs[4][2] = {
  { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
};

static gint8
piece (gint kind, gint side)
{
  return (gint8) (kind | (side ? CHESS_BLACK_PIECE : 0));
}

void
chess_start (ChessPosition *pos)
{
  static const gint back[8] = {
    CHESS_ROOK, CHESS_KNIGHT, CHESS_BISHOP, CHESS_QUEEN,
    CHESS_KING, CHESS_BISHOP, CHESS_KNIGHT, CHESS_ROOK
  };
  gint f;

  memset (pos, 0, sizeof *pos);
  for (f = 0; f < 8; f++)
    {
      pos->board[CHESS_SQUARE (f, 0)] = piece (back[f], CHESS_WHITE);
      pos->board[CHESS_SQUARE (f, 1)] = piece (CHESS_PAWN, CHESS_WHITE);
      pos->board[CHESS_SQUARE (f, 6)] = piece (CHESS_PAWN, CHESS_BLACK);
      pos->board[CHESS_SQUARE (f, 7)] = piece (back[f], CHESS_BLACK);
    }
  pos->side = CHESS_WHITE;
  pos->castling = CHESS_WHITE_SHORT | CHESS_WHITE_LONG
    | CHESS_BLACK_SHORT | CHESS_BLACK_LONG;
  pos->ep = -1;
  pos->fullmove = 1;
}

/* The square a step of (df, dr) from s, or -1 off the board. */
static gint
step (gint s, gint df, gint dr)
{
  gint f = CHESS_FILE (s) + df, r = CHESS_RANK (s) + dr;

  return f < 0 || f > 7 || r < 0 || r > 7 ? -1 : CHESS_SQUARE (f, r);
}

/* Whether side attacks square s. */
static gboolean
attacked (const ChessPosition *pos, gint s, gint side)
{
  gint i, t, p;

  /* Pawns attack from the rank behind, as side moves. */
  for (i = -1; i <= 1; i += 2)
    {
      t = step (s, i, side == CHESS_WHITE ? -1 : 1);
      if (t >= 0 && pos->board[t] == piece (CHESS_PAWN, side))
        return TRUE;
    }
  for (i = 0; i < 8; i++)
    {
      t = step (s, knight_steps[i][0], knight_steps[i][1]);
      if (t >= 0 && pos->board[t] == piece (CHESS_KNIGHT, side))
        return TRUE;
      t = step (s, king_steps[i][0], king_steps[i][1]);
      if (t >= 0 && pos->board[t] == piece (CHESS_KING, side))
        return TRUE;
    }
  for (i = 0; i < 8; i++)
    {
      const gint *d = i < 4 ? rook_dirs[i] : bishop_dirs[i - 4];
      gint slider = i < 4 ? CHESS_ROOK : CHESS_BISHOP;

      for (t = step (s, d[0], d[1]); t >= 0; t = step (t, d[0], d[1]))
        {
          p = pos->board[t];
          if (p == CHESS_EMPTY)
            continue;
          if (CHESS_SIDE (p) == side
              && (CHESS_KIND (p) == slider || CHESS_KIND (p) == CHESS_QUEEN))
            return TRUE;
          break;
        }
    }
  return FALSE;
}

gboolean
chess_in_check (const ChessPosition *pos, gint side)
{
  gint s;

  for (s = 0; s < 64; s++)
    if (pos->board[s] == piece (CHESS_KING, side))
      return attacked (pos, s, !side);
  return FALSE;
}

static void
add (ChessMove *moves, gint *n, gint from, gint to, gint promotion)
{
  moves[*n].from = (gint8) from;
  moves[*n].to = (gint8) to;
  moves[*n].promotion = (gint8) promotion;
  (*n)++;
}

/* A pawn's move to to, as four moves if it reaches the last rank. */
static void
add_pawn (ChessMove *moves, gint *n, gint from, gint to)
{
  if (CHESS_RANK (to) == 0 || CHESS_RANK (to) == 7)
    {
      add (moves, n, from, to, CHESS_QUEEN);
      add (moves, n, from, to, CHESS_ROOK);
      add (moves, n, from, to, CHESS_BISHOP);
      add (moves, n, from, to, CHESS_KNIGHT);
    }
  else
    add (moves, n, from, to, 0);
}

/* The moves of side's pieces as they move, but for the safety of the
 * king. */
static gint
pseudo_moves (const ChessPosition *pos, ChessMove *moves)
{
  gint side = pos->side, n = 0, s, t, i;
  gint forward = side == CHESS_WHITE ? 1 : -1;

  for (s = 0; s < 64; s++)
    {
      gint p = pos->board[s];

      if (p == CHESS_EMPTY || CHESS_SIDE (p) != side)
        continue;
      switch (CHESS_KIND (p))
        {
        case CHESS_PAWN:
          t = step (s, 0, forward);
          if (t >= 0 && pos->board[t] == CHESS_EMPTY)
            {
              add_pawn (moves, &n, s, t);
              if (CHESS_RANK (s) == (side == CHESS_WHITE ? 1 : 6))
                {
                  gint t2 = step (t, 0, forward);

                  if (pos->board[t2] == CHESS_EMPTY)
                    add (moves, &n, s, t2, 0);
                }
            }
          for (i = -1; i <= 1; i += 2)
            {
              t = step (s, i, forward);
              if (t < 0)
                continue;
              if ((pos->board[t] != CHESS_EMPTY
                   && CHESS_SIDE (pos->board[t]) != side) || t == pos->ep)
                add_pawn (moves, &n, s, t);
            }
          break;

        case CHESS_KNIGHT:
        case CHESS_KING:
          for (i = 0; i < 8; i++)
            {
              const gint *d = CHESS_KIND (p) == CHESS_KNIGHT
                ? knight_steps[i] : king_steps[i];

              t = step (s, d[0], d[1]);
              if (t >= 0 && (pos->board[t] == CHESS_EMPTY
                             || CHESS_SIDE (pos->board[t]) != side))
                add (moves, &n, s, t, 0);
            }
          break;

        default:
          for (i = 0; i < 8; i++)
            {
              const gint *d = i < 4 ? rook_dirs[i] : bishop_dirs[i - 4];

              if ((CHESS_KIND (p) == CHESS_ROOK && i >= 4)
                  || (CHESS_KIND (p) == CHESS_BISHOP && i < 4))
                continue;
              for (t = step (s, d[0], d[1]); t >= 0; t = step (t, d[0], d[1]))
                {
                  if (pos->board[t] != CHESS_EMPTY)
                    {
                      if (CHESS_SIDE (pos->board[t]) != side)
                        add (moves, &n, s, t, 0);
                      break;
                    }
                  add (moves, &n, s, t, 0);
                }
            }
          break;
        }
    }

  /* Castling: the king and rook unmoved, the squares between empty, and
   * the king not in check nor passing over an attacked square. */
  {
    gint home = side == CHESS_WHITE ? 0 : 56;
    gint shorts = side == CHESS_WHITE ? CHESS_WHITE_SHORT : CHESS_BLACK_SHORT;
    gint longs = side == CHESS_WHITE ? CHESS_WHITE_LONG : CHESS_BLACK_LONG;

    if (pos->board[home + 4] == piece (CHESS_KING, side)
        && !attacked (pos, home + 4, !side))
      {
        if ((pos->castling & shorts)
            && pos->board[home + 7] == piece (CHESS_ROOK, side)
            && pos->board[home + 5] == CHESS_EMPTY
            && pos->board[home + 6] == CHESS_EMPTY
            && !attacked (pos, home + 5, !side))
          add (moves, &n, home + 4, home + 6, 0);
        if ((pos->castling & longs)
            && pos->board[home] == piece (CHESS_ROOK, side)
            && pos->board[home + 1] == CHESS_EMPTY
            && pos->board[home + 2] == CHESS_EMPTY
            && pos->board[home + 3] == CHESS_EMPTY
            && !attacked (pos, home + 3, !side))
          add (moves, &n, home + 4, home + 2, 0);
      }
  }
  return n;
}

void
chess_play (ChessPosition *pos, ChessMove move)
{
  gint p = pos->board[move.from], side = pos->side;
  gint kind = CHESS_KIND (p);
  gboolean capture = pos->board[move.to] != CHESS_EMPTY;
  gint s;

  pos->board[move.to] = (gint8) p;
  pos->board[move.from] = CHESS_EMPTY;

  if (kind == CHESS_PAWN)
    {
      if (move.to == pos->ep)
        {
          /* The pawn taken en passant is beside the one that took it. */
          pos->board[CHESS_SQUARE (CHESS_FILE (move.to),
                                   CHESS_RANK (move.from))] = CHESS_EMPTY;
          capture = TRUE;
        }
      if (move.promotion)
        pos->board[move.to] = piece (move.promotion, side);
    }
  if (kind == CHESS_KING && ABS (move.to - move.from) == 2)
    {
      /* The rook goes over the king. */
      gint home = move.from - CHESS_FILE (move.from);
      gint rook = move.to > move.from ? home + 7 : home;
      gint over = move.to > move.from ? move.to - 1 : move.to + 1;

      pos->board[over] = pos->board[rook];
      pos->board[rook] = CHESS_EMPTY;
    }

  /* A pawn's double step can be taken en passant, if a pawn stands
   * beside it to take it. */
  pos->ep = -1;
  if (kind == CHESS_PAWN && ABS (move.to - move.from) == 16)
    for (s = -1; s <= 1; s += 2)
      {
        gint beside = step (move.to, s, 0);

        if (beside >= 0 && pos->board[beside] == piece (CHESS_PAWN, !side))
          pos->ep = (move.from + move.to) / 2;
      }

  /* A king or rook moving, or a rook taken, ends that castling. */
  for (s = 0; s < 2; s++)
    {
      gint sq = s ? move.to : move.from;

      if (sq == 4)
        pos->castling &= ~(CHESS_WHITE_SHORT | CHESS_WHITE_LONG);
      else if (sq == 60)
        pos->castling &= ~(CHESS_BLACK_SHORT | CHESS_BLACK_LONG);
      else if (sq == 7)
        pos->castling &= ~CHESS_WHITE_SHORT;
      else if (sq == 0)
        pos->castling &= ~CHESS_WHITE_LONG;
      else if (sq == 63)
        pos->castling &= ~CHESS_BLACK_SHORT;
      else if (sq == 56)
        pos->castling &= ~CHESS_BLACK_LONG;
    }

  pos->halfmoves = kind == CHESS_PAWN || capture ? 0 : pos->halfmoves + 1;
  if (side == CHESS_BLACK)
    pos->fullmove++;
  pos->side = !side;
}

gint
chess_moves (const ChessPosition *pos, ChessMove *moves)
{
  ChessMove all[CHESS_MAX_MOVES];
  gint n = pseudo_moves (pos, all), i, legal = 0;

  for (i = 0; i < n; i++)
    {
      ChessPosition after = *pos;

      chess_play (&after, all[i]);
      if (!chess_in_check (&after, pos->side))
        moves[legal++] = all[i];
    }
  return legal;
}

void
chess_coord (ChessMove move, gchar *out)
{
  static const gchar kinds[] = " pnbrqk";

  out[0] = (gchar) ('a' + CHESS_FILE (move.from));
  out[1] = (gchar) ('1' + CHESS_RANK (move.from));
  out[2] = (gchar) ('a' + CHESS_FILE (move.to));
  out[3] = (gchar) ('1' + CHESS_RANK (move.to));
  out[4] = move.promotion ? kinds[move.promotion] : '\0';
  out[5] = '\0';
}

gboolean
chess_parse_coord (const ChessPosition *pos, const gchar *text,
                   ChessMove *move)
{
  ChessMove moves[CHESS_MAX_MOVES];
  gchar coord[CHESS_COORD_LEN];
  gint n = chess_moves (pos, moves), i;

  for (i = 0; i < n; i++)
    {
      chess_coord (moves[i], coord);
      if (g_strncasecmp (text, coord, strlen (coord)) == 0
          && (text[strlen (coord)] == '\0'
              || isspace ((guchar) text[strlen (coord)])))
        {
          *move = moves[i];
          return TRUE;
        }
    }
  return FALSE;
}

gboolean
chess_parse_san (const ChessPosition *pos, const gchar *text,
                 ChessMove *move)
{
  static const gchar letters[] = "PNBRQK";
  ChessMove moves[CHESS_MAX_MOVES];
  gchar clean[16];
  gint len = 0, kind = CHESS_PAWN, promotion = 0, from_file = -1;
  gint from_rank = -1, to, n, i, found = -1;
  const gchar *t;

  /* The move without its marks: checks, captures, annotations. */
  for (t = text; *t && !isspace ((guchar) *t) && len < 15; t++)
    if (!strchr ("x:-=+#!?", *t))
      clean[len++] = *t == '0' ? 'O' : *t;
  clean[len] = '\0';
  if (len == 0)
    return FALSE;

  n = chess_moves (pos, moves);
  if (strcmp (clean, "OO") == 0 || strcmp (clean, "OOO") == 0)
    {
      for (i = 0; i < n; i++)
        if (CHESS_KIND (pos->board[moves[i].from]) == CHESS_KING
            && moves[i].to - moves[i].from == (len == 2 ? 2 : -2))
          {
            *move = moves[i];
            return TRUE;
          }
      return FALSE;
    }

  if (strchr (letters + 1, clean[0]) && len > 2)
    {
      kind = (gint) (strchr (letters, clean[0]) - letters) + 1;
      memmove (clean, clean + 1, (gsize) len--);
    }
  if (len >= 3 && strchr ("NBRQnbrq", clean[len - 1])
      && isdigit ((guchar) clean[len - 2]))
    {
      promotion = (gint) (strchr (letters, toupper ((guchar) clean[len - 1]))
                          - letters) + 1;
      clean[--len] = '\0';
    }
  if (len < 2 || clean[len - 2] < 'a' || clean[len - 2] > 'h'
      || clean[len - 1] < '1' || clean[len - 1] > '8')
    return chess_parse_coord (pos, text, move);
  to = CHESS_SQUARE (clean[len - 2] - 'a', clean[len - 1] - '1');
  for (i = 0; i < len - 2; i++)
    if (clean[i] >= 'a' && clean[i] <= 'h')
      from_file = clean[i] - 'a';
    else if (clean[i] >= '1' && clean[i] <= '8')
      from_rank = clean[i] - '1';
    else
      return FALSE;

  for (i = 0; i < n; i++)
    if (moves[i].to == to
        && CHESS_KIND (pos->board[moves[i].from]) == kind
        && moves[i].promotion == promotion
        && (from_file < 0 || CHESS_FILE (moves[i].from) == from_file)
        && (from_rank < 0 || CHESS_RANK (moves[i].from) == from_rank))
      {
        if (found >= 0)
          return FALSE;
        found = i;
      }
  if (found < 0)
    return chess_parse_coord (pos, text, move);
  *move = moves[found];
  return TRUE;
}

void
chess_san (const ChessPosition *pos, ChessMove move, gchar *out)
{
  static const gchar letters[] = "  NBRQK";
  ChessMove moves[CHESS_MAX_MOVES];
  ChessPosition after = *pos;
  gint kind = CHESS_KIND (pos->board[move.from]);
  gboolean capture = pos->board[move.to] != CHESS_EMPTY
    || (kind == CHESS_PAWN && move.to == pos->ep);
  gint n, i, len = 0;

  if (kind == CHESS_KING && ABS (move.to - move.from) == 2)
    len = g_snprintf (out, CHESS_SAN_LEN, "%s",
                      move.to > move.from ? "O-O" : "O-O-O");
  else
    {
      if (kind == CHESS_PAWN)
        {
          if (capture)
            out[len++] = (gchar) ('a' + CHESS_FILE (move.from));
        }
      else
        {
          gboolean other = FALSE, same_file = FALSE, same_rank = FALSE;

          out[len++] = letters[kind];
          /* Which piece of the kind, if another could go there too. */
          n = chess_moves (pos, moves);
          for (i = 0; i < n; i++)
            if (moves[i].to == move.to && moves[i].from != move.from
                && CHESS_KIND (pos->board[moves[i].from]) == kind)
              {
                other = TRUE;
                if (CHESS_FILE (moves[i].from) == CHESS_FILE (move.from))
                  same_file = TRUE;
                if (CHESS_RANK (moves[i].from) == CHESS_RANK (move.from))
                  same_rank = TRUE;
              }
          if (other && (!same_file || same_rank))
            out[len++] = (gchar) ('a' + CHESS_FILE (move.from));
          if (other && same_file)
            out[len++] = (gchar) ('1' + CHESS_RANK (move.from));
        }
      if (capture)
        out[len++] = 'x';
      out[len++] = (gchar) ('a' + CHESS_FILE (move.to));
      out[len++] = (gchar) ('1' + CHESS_RANK (move.to));
      if (move.promotion)
        {
          out[len++] = '=';
          out[len++] = letters[(gint) move.promotion];
        }
    }

  chess_play (&after, move);
  if (chess_in_check (&after, after.side))
    out[len++] = chess_moves (&after, moves) ? '+' : '#';
  out[len] = '\0';
}

void
chess_fen (const ChessPosition *pos, gchar *out)
{
  static const gchar letters[] = " PNBRQK  pnbrqk";
  gint r, f, len = 0;

  for (r = 7; r >= 0; r--)
    {
      gint empty = 0;

      for (f = 0; f < 8; f++)
        {
          gint p = pos->board[CHESS_SQUARE (f, r)];

          if (p == CHESS_EMPTY)
            empty++;
          else
            {
              if (empty)
                out[len++] = (gchar) ('0' + empty);
              empty = 0;
              out[len++] = letters[p];
            }
        }
      if (empty)
        out[len++] = (gchar) ('0' + empty);
      if (r)
        out[len++] = '/';
    }
  out[len++] = ' ';
  out[len++] = pos->side == CHESS_WHITE ? 'w' : 'b';
  out[len++] = ' ';
  if (!pos->castling)
    out[len++] = '-';
  if (pos->castling & CHESS_WHITE_SHORT)
    out[len++] = 'K';
  if (pos->castling & CHESS_WHITE_LONG)
    out[len++] = 'Q';
  if (pos->castling & CHESS_BLACK_SHORT)
    out[len++] = 'k';
  if (pos->castling & CHESS_BLACK_LONG)
    out[len++] = 'q';
  out[len++] = ' ';
  if (pos->ep >= 0)
    {
      out[len++] = (gchar) ('a' + CHESS_FILE (pos->ep));
      out[len++] = (gchar) ('1' + CHESS_RANK (pos->ep));
    }
  else
    out[len++] = '-';
  g_snprintf (out + len, (gulong) (CHESS_FEN_LEN - len), " %d %d",
              pos->halfmoves, pos->fullmove);
}

gboolean
chess_parse_fen (ChessPosition *pos, const gchar *fen)
{
  static const gchar letters[] = " PNBRQK  pnbrqk";
  gint r = 7, f = 0;
  const gchar *c = fen;

  memset (pos, 0, sizeof *pos);
  pos->ep = -1;
  for (; *c && *c != ' '; c++)
    {
      const gchar *l;

      if (*c == '/')
        {
          if (f != 8 || --r < 0)
            return FALSE;
          f = 0;
        }
      else if (*c >= '1' && *c <= '8')
        f += *c - '0';
      else if ((l = strchr (letters, *c)) != NULL && *c != ' ' && f < 8)
        pos->board[CHESS_SQUARE (f++, r)] = (gint8) (l - letters);
      else
        return FALSE;
      if (f > 8)
        return FALSE;
    }
  if (r != 0 || f != 8 || *c++ != ' ')
    return FALSE;
  if (*c != 'w' && *c != 'b')
    return FALSE;
  pos->side = *c++ == 'w' ? CHESS_WHITE : CHESS_BLACK;
  if (*c++ != ' ')
    return FALSE;
  for (; *c && *c != ' '; c++)
    switch (*c)
      {
      case 'K': pos->castling |= CHESS_WHITE_SHORT; break;
      case 'Q': pos->castling |= CHESS_WHITE_LONG; break;
      case 'k': pos->castling |= CHESS_BLACK_SHORT; break;
      case 'q': pos->castling |= CHESS_BLACK_LONG; break;
      case '-': break;
      default: return FALSE;
      }
  if (*c == ' ' && c[1] >= 'a' && c[1] <= 'h' && c[2] >= '1' && c[2] <= '8')
    pos->ep = CHESS_SQUARE (c[1] - 'a', c[2] - '1');
  if (*c == ' ')
    c = strchr (c + 1, ' ');
  if (c && *c == ' ')
    {
      pos->halfmoves = atoi (c + 1);
      c = strchr (c + 1, ' ');
      pos->fullmove = c ? MAX (atoi (c + 1), 1) : 1;
    }
  else
    pos->fullmove = 1;
  return TRUE;
}

/* Whether neither side can mate: kings alone, but for one knight or
 * bishop, or for bishops all on squares of one colour. */
static gboolean
no_material (const ChessPosition *pos)
{
  gint s, minors = 0, knights = 0, bishop_colours = 0;

  for (s = 0; s < 64; s++)
    switch (CHESS_KIND (pos->board[s]))
      {
      case CHESS_PAWN:
      case CHESS_ROOK:
      case CHESS_QUEEN:
        return FALSE;
      case CHESS_KNIGHT:
        knights++;
        minors++;
        break;
      case CHESS_BISHOP:
        bishop_colours |= 1 << ((CHESS_FILE (s) + CHESS_RANK (s)) & 1);
        minors++;
        break;
      default:
        break;
      }
  return minors <= 1 || (knights == 0 && bishop_colours != 3);
}

/* Whether two positions are the same for repetition: the same pieces on
 * the same squares, the same side to move, and the same moves open. */
static gboolean
same (const ChessPosition *a, const ChessPosition *b)
{
  return memcmp (a->board, b->board, sizeof a->board) == 0
    && a->side == b->side && a->castling == b->castling && a->ep == b->ep;
}

ChessState
chess_state (const ChessPosition *history, gint n)
{
  const ChessPosition *pos = &history[n - 1];
  ChessMove moves[CHESS_MAX_MOVES];
  gint i, seen = 0;

  if (chess_moves (pos, moves) == 0)
    return chess_in_check (pos, pos->side) ? CHESS_CHECKMATE
      : CHESS_STALEMATE;
  if (pos->halfmoves >= 100)
    return CHESS_FIFTY_MOVES;
  if (no_material (pos))
    return CHESS_NO_MATERIAL;
  for (i = 0; i < n; i++)
    if (same (&history[i], pos))
      seen++;
  return seen >= 3 ? CHESS_REPETITION : CHESS_PLAYING;
}
