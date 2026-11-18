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

/* The engine is a child process with a pipe each way. It is told
 * "uci", and asked nothing till it says "uciok"; each move is asked for
 * with "position" and "go", and comes back as "bestmove". A search no
 * longer wanted is stopped, and the question that replaces it waits for
 * the stopped search's answer, which is dropped, as UCI asks.
 *
 * An engine's strength can be lowered where it offers a way: Stockfish's
 * "Skill Level", or the standard UCI_LimitStrength and UCI_Elo. GNU
 * Chess offers neither, and plays at the depth asked. */

#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <gdk/gdk.h>

#include "engine.h"

struct _Engine
{
  EngineFunc func;
  gpointer data;
  pid_t pid;
  gint to, from;                /* the pipes' ends */
  gint tag;                     /* gdk_input_add's */
  GString *line;                /* what has come of a line */
  gboolean ready;               /* "uciok" said */
  gboolean searching;           /* a "go" not yet answered */
  gboolean wanted;              /* the search's answer is wanted */
  gchar *position, *limits;     /* a question waiting to be asked */
  gboolean dead;
  gchar *name;                  /* "id name"'s, if said */
  gint weaken;                  /* how it can be weakened, below */
  gint low, high;               /* the range of that option's values */
  gint strength, said;          /* wanted, and last set, 0 to 100 */
};

enum { WEAKEN_NONE, WEAKEN_SKILL, WEAKEN_ELO };

/* The engine running, for a signal that ends the program to end it too:
 * otherwise it would be left behind, thinking or waiting, with nobody
 * to quit it. */
static volatile sig_atomic_t live_pid;

static void
fatal_signal (int sig)
{
  if (live_pid > 0)
    kill ((pid_t) live_pid, SIGTERM);
  signal (sig, SIG_DFL);
  raise (sig);
}

/* Ends the engine with the program on a hang-up, an interrupt, a quit
 * or a termination, unless the program has set those signals itself. */
static void
watch_signals (void)
{
  static const int signals[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM };
  static gboolean done;
  gsize i;

  if (done)
    return;
  done = TRUE;
  for (i = 0; i < sizeof signals / sizeof signals[0]; i++)
    {
      struct sigaction action, old;

      if (sigaction (signals[i], NULL, &old) < 0
          || old.sa_handler != SIG_DFL)
        continue;
      memset (&action, 0, sizeof action);
      action.sa_handler = fatal_signal;
      sigemptyset (&action.sa_mask);
      sigaction (signals[i], &action, NULL);
    }
}

static void
say (Engine *engine, const gchar *text)
{
  gsize left = strlen (text);

  if (engine->dead)
    return;
  while (left > 0)
    {
      ssize_t n = write (engine->to, text, left);

      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        return;
      text += n;
      left -= (gsize) n;
    }
}

/* Asks the question waiting, if the engine can be asked. */
static void
ask (Engine *engine)
{
  gchar *text;

  if (!engine->ready || engine->searching || !engine->position)
    return;
  if (engine->weaken != WEAKEN_NONE && engine->strength != engine->said)
    {
      gint value = engine->low + (engine->high - engine->low)
        * engine->strength / 100;

      if (engine->weaken == WEAKEN_SKILL)
        text = g_strdup_printf ("setoption name Skill Level value %d\n",
                                value);
      else
        text = g_strdup_printf ("setoption name UCI_LimitStrength value %s\n"
                                "setoption name UCI_Elo value %d\n",
                                engine->strength < 100 ? "true" : "false",
                                value);
      say (engine, text);
      g_free (text);
      engine->said = engine->strength;
    }
  text = g_strdup_printf ("position %s\ngo %s\n", engine->position,
                          engine->limits);
  say (engine, text);
  g_free (text);
  g_free (engine->position);
  g_free (engine->limits);
  engine->position = engine->limits = NULL;
  engine->searching = TRUE;
  engine->wanted = TRUE;
}

static void
gone (Engine *engine)
{
  if (engine->dead)
    return;
  engine->dead = TRUE;
  engine->searching = FALSE;
  if (engine->tag)
    gdk_input_remove (engine->tag);
  engine->tag = 0;
  engine->func (NULL, engine->data);
}

/* The minimum and maximum of a spin option, from the "option" line that
 * offers it. */
static gboolean
spin_range (const gchar *line, gint *low, gint *high)
{
  const gchar *min = strstr (line, " min "), *max = strstr (line, " max ");

  if (!min || !max)
    return FALSE;
  *low = atoi (min + 5);
  *high = atoi (max + 5);
  return *high > *low;
}

static void
heard (Engine *engine, const gchar *line)
{
  gint low, high;

  if (strncmp (line, "id name ", 8) == 0 && !engine->name)
    engine->name = g_strdup (line + 8);
  else if (strncmp (line, "option name Skill Level type spin ", 34) == 0
           && spin_range (line, &low, &high))
    {
      /* Stockfish's: preferred, as it weakens to a beginner's play. */
      engine->weaken = WEAKEN_SKILL;
      engine->low = low;
      engine->high = high;
    }
  else if (strncmp (line, "option name UCI_Elo type spin ", 30) == 0
           && engine->weaken != WEAKEN_SKILL && spin_range (line, &low, &high))
    {
      engine->weaken = WEAKEN_ELO;
      engine->low = low;
      engine->high = high;
    }
  else if (strcmp (line, "uciok") == 0)
    {
      /* Thinking in the player's time would leave a search going. */
      say (engine, "setoption name Ponder value false\nucinewgame\n");
      engine->ready = TRUE;
      ask (engine);
    }
  else if (strncmp (line, "bestmove ", 9) == 0)
    {
      gchar move[8] = "";
      gboolean wanted = engine->wanted;

      sscanf (line + 9, "%7s", move);
      engine->searching = engine->wanted = FALSE;
      if (wanted)
        engine->func (move, engine->data);
      ask (engine);
    }
}

static void
readable (gpointer data, gint fd, GdkInputCondition condition)
{
  Engine *engine = data;
  gchar buf[4096];
  ssize_t n, i;

  (void) condition;
  n = read (fd, buf, sizeof buf);
  if (n < 0 && (errno == EINTR || errno == EAGAIN))
    return;
  if (n <= 0)
    {
      gone (engine);
      return;
    }
  for (i = 0; i < n && !engine->dead; i++)
    if (buf[i] == '\n')
      {
        gchar *line = g_strdup (engine->line->str);

        g_string_truncate (engine->line, 0);
        g_strchomp (line);
        heard (engine, line);
        g_free (line);
      }
    else
      g_string_append_c (engine->line, buf[i]);
}

Engine *
engine_new (const gchar *command, EngineFunc func, gpointer data)
{
  Engine *engine;
  gint to[2], from[2];
  gchar **argv;
  pid_t pid, parent;

  if (pipe (to) < 0)
    return NULL;
  if (pipe (from) < 0)
    {
      close (to[0]);
      close (to[1]);
      return NULL;
    }
  argv = g_strsplit (command, " ", 0);
  watch_signals ();
  parent = getpid ();
  pid = fork ();
  if (pid == 0)
    {
      gchar *games;

#ifdef __linux__
      /* Even a program killed outright takes the engine with it. */
      if (prctl (PR_SET_PDEATHSIG, SIGTERM) < 0 || getppid () != parent)
        _exit (127);
#endif
      dup2 (to[0], 0);
      dup2 (from[1], 1);
      close (to[0]);
      close (to[1]);
      close (from[0]);
      close (from[1]);
      /* Some engines leave files where they run. */
      if (chdir (g_get_tmp_dir ()) < 0)
        _exit (127);
      execvp (argv[0], argv);
      if (strchr (argv[0], '/') == NULL)
        {
          games = g_strconcat ("/usr/games/", argv[0], NULL);
          execv (games, argv);
        }
      _exit (127);
    }
  g_strfreev (argv);
  close (to[0]);
  close (from[1]);
  if (pid < 0)
    {
      close (to[1]);
      close (from[0]);
      return NULL;
    }

  /* A write to an engine that has gone is an error, not a signal. */
  signal (SIGPIPE, SIG_IGN);
  fcntl (from[0], F_SETFD, FD_CLOEXEC);
  fcntl (to[1], F_SETFD, FD_CLOEXEC);

  engine = g_new0 (Engine, 1);
  engine->func = func;
  engine->data = data;
  engine->pid = pid;
  live_pid = pid;
  engine->to = to[1];
  engine->from = from[0];
  engine->line = g_string_new (NULL);
  engine->strength = engine->said = 100;
  engine->tag = gdk_input_add (engine->from, GDK_INPUT_READ, readable,
                               engine);
  say (engine, "uci\n");
  return engine;
}

void
engine_think (Engine *engine, const gchar *position, const gchar *limits)
{
  engine_cancel (engine);
  engine->position = g_strdup (position);
  engine->limits = g_strdup (limits);
  ask (engine);
}

void
engine_set_strength (Engine *engine, gint strength)
{
  engine->strength = CLAMP (strength, 0, 100);
}

const gchar *
engine_name (Engine *engine)
{
  return engine->name;
}

gboolean
engine_installed (const gchar *program)
{
  const gchar *path = getenv ("PATH");
  gchar **dirs = g_strsplit (path ? path : "", ":", 0);
  gboolean found = FALSE;
  gint i;

  if (strchr (program, '/'))
    found = access (program, X_OK) == 0;
  for (i = 0; dirs[i] && !found; i++)
    {
      gchar *file = g_strconcat (*dirs[i] ? dirs[i] : ".", "/", program,
                                 NULL);

      found = access (file, X_OK) == 0;
      g_free (file);
    }
  g_strfreev (dirs);
  if (!found && !strchr (program, '/'))
    {
      gchar *file = g_strconcat ("/usr/games/", program, NULL);

      found = access (file, X_OK) == 0;
      g_free (file);
    }
  return found;
}

void
engine_cancel (Engine *engine)
{
  g_free (engine->position);
  g_free (engine->limits);
  engine->position = engine->limits = NULL;
  if (engine->searching && engine->wanted)
    {
      engine->wanted = FALSE;
      say (engine, "stop\n");
    }
}

gboolean
engine_thinking (Engine *engine)
{
  return (engine->searching && engine->wanted) || engine->position != NULL;
}

void
engine_free (Engine *engine)
{
  gint i;

  if (!engine)
    return;
  if (engine->tag)
    gdk_input_remove (engine->tag);
  if (live_pid == engine->pid)
    live_pid = 0;
  say (engine, "stop\nquit\n");
  engine->dead = TRUE;
  close (engine->to);
  close (engine->from);
  /* A moment to go of itself, then made to. */
  for (i = 0; i < 20; i++)
    {
      if (waitpid (engine->pid, NULL, WNOHANG) != 0)
        break;
      struct timespec moment = { 0, 10000000 };

      nanosleep (&moment, NULL);
    }
  if (i == 20)
    {
      kill (engine->pid, SIGTERM);
      waitpid (engine->pid, NULL, 0);
    }
  g_string_free (engine->line, TRUE);
  g_free (engine->position);
  g_free (engine->limits);
  g_free (engine->name);
  g_free (engine);
}
