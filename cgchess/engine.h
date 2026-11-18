/*
 * cgchess: the chess engine, a program spoken to in UCI
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

#ifndef ENGINE_H
#define ENGINE_H

#include <glib.h>

typedef struct _Engine Engine;

/* The move the engine chose, in coordinates ("e7e5"), for the last
 * position it was asked about; or NULL if the engine could not be
 * started, or has stopped. It must not free the engine. */
typedef void (*EngineFunc) (const gchar *move, gpointer data);

/* Starts command, as a shell would split it, say "gnuchess --uci"; if
 * its program isn't on the path, it is looked for in /usr/games too. */
Engine *engine_new (const gchar *command, EngineFunc func, gpointer data);

/* Asks for a move in position, a UCI "position" command's argument
 * ("startpos moves e2e4"), searched within limits, a "go" command's
 * ("movetime 1000"); a move asked for before is forgotten. */
void engine_think (Engine *engine, const gchar *position,
                   const gchar *limits);

/* How strongly the engine plays from the next move asked for, 0 the
 * weakest it offers and 100 its full strength, the first; an engine
 * that offers no way to play weaker plays at full strength. */
void engine_set_strength (Engine *engine, gint strength);

/* The engine's name for itself, "Stockfish 17" say, once it has said;
 * or NULL. */
const gchar *engine_name (Engine *engine);

/* Whether program can be run, from the path or from /usr/games. */
gboolean engine_installed (const gchar *program);

/* Forgets the move asked for, if any. */
void engine_cancel (Engine *engine);

/* Whether the engine is thinking about a move asked for. */
gboolean engine_thinking (Engine *engine);

/* Stops the engine. */
void engine_free (Engine *engine);

#endif
