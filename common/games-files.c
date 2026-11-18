/* cggames: where the games keep their files
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "games.h"

#define SUBDIR "cggames"

/* An XDG base directory: the variable if it holds an absolute path,
 * else the default under home. */
static gchar *
xdg_dir (const gchar *variable, const gchar *fallback)
{
  const gchar *value = getenv (variable);

  if (value && value[0] == '/')
    return g_strdup (value);
  return g_strconcat (g_get_home_dir (), "/", fallback, NULL);
}

void
games_init_translation (void)
{
#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  textdomain (GETTEXT_PACKAGE);
#endif
}

gchar *
games_config_path (const gchar *game, const gchar *ext)
{
  gchar *dir = xdg_dir ("XDG_CONFIG_HOME", ".config");
  gchar *path = g_strconcat (dir, "/" SUBDIR "/", game, ".", ext, NULL);

  g_free (dir);
  return path;
}

gchar *
games_data_path (const gchar *game, const gchar *ext)
{
  gchar *dir = xdg_dir ("XDG_DATA_HOME", ".local/share");
  gchar *path = g_strconcat (dir, "/" SUBDIR "/", game, ".", ext, NULL);

  g_free (dir);
  return path;
}

gchar *
games_installed_path (const gchar *game, const gchar *file)
{
  return g_strconcat (GAMES_DATADIR "/", game, "/", file, NULL);
}

/* Creates the directories leading to path, mode 0700 for new ones as
 * the XDG spec asks. */
static gboolean
make_parents (const gchar *path)
{
  gchar *dir = g_strdup (path);
  gchar *p;

  for (p = strchr (dir + 1, '/'); p; p = strchr (p + 1, '/'))
    {
      *p = '\0';
      if (mkdir (dir, 0700) < 0 && errno != EEXIST)
        {
          fprintf (stderr, "%s: %s\n", dir, strerror (errno));
          g_free (dir);
          return FALSE;
        }
      *p = '/';
    }
  g_free (dir);
  return TRUE;
}

gboolean
games_write_file (const gchar *path, const gchar *contents)
{
  gchar *temp;
  FILE *file;
  gboolean ok;

  if (!make_parents (path))
    return FALSE;

  temp = g_strdup_printf ("%s.%ld.tmp", path, (long) getpid ());
  file = fopen (temp, "w");
  if (!file)
    {
      fprintf (stderr, "%s: %s\n", temp, strerror (errno));
      g_free (temp);
      return FALSE;
    }
  ok = fputs (contents, file) >= 0;
  ok = (fclose (file) == 0) && ok;
  if (ok && rename (temp, path) < 0)
    ok = FALSE;
  if (!ok)
    {
      fprintf (stderr, "%s: %s\n", path, strerror (errno));
      unlink (temp);
    }
  g_free (temp);
  return ok;
}
