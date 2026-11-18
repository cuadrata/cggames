/* cggames: settings files
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "games.h"

/* A file of "key = value" lines. Blank lines and lines starting with
 * '#' are skipped on reading and not kept; keys are written back in the
 * order they were read or first set. */

typedef struct
{
  gchar *key;
  gchar *value;
} Entry;

struct _GamesConfig
{
  gchar *path;
  GSList *entries;
};

static gchar *
strip (gchar *s)
{
  gchar *end;

  while (*s == ' ' || *s == '\t')
    s++;
  end = s + strlen (s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t'
                     || end[-1] == '\n' || end[-1] == '\r'))
    end--;
  *end = '\0';
  return s;
}

static Entry *
find (GamesConfig *config, const gchar *key)
{
  GSList *l;

  for (l = config->entries; l; l = l->next)
    {
      Entry *entry = l->data;

      if (strcmp (entry->key, key) == 0)
        return entry;
    }
  return NULL;
}

GamesConfig *
games_config_load (const gchar *game)
{
  GamesConfig *config = g_new0 (GamesConfig, 1);
  FILE *file;
  char *line = NULL;
  size_t size = 0;

  config->path = games_config_path (game, "conf");
  file = fopen (config->path, "r");
  if (!file)
    return config;

  while (getline (&line, &size, file) >= 0)
    {
      gchar *equals = strchr (line, '=');
      gchar *key;

      if (!equals)
        continue;
      *equals = '\0';
      key = strip (line);
      if (key[0] == '\0' || key[0] == '#')
        continue;
      games_config_set_string (config, key, strip (equals + 1));
    }
  free (line);
  fclose (file);
  return config;
}

void
games_config_free (GamesConfig *config)
{
  GSList *l;

  for (l = config->entries; l; l = l->next)
    {
      Entry *entry = l->data;

      g_free (entry->key);
      g_free (entry->value);
      g_free (entry);
    }
  g_slist_free (config->entries);
  g_free (config->path);
  g_free (config);
}

const gchar *
games_config_get_string (GamesConfig *config, const gchar *key,
                         const gchar *fallback)
{
  Entry *entry = find (config, key);

  return entry ? entry->value : fallback;
}

gint
games_config_get_int (GamesConfig *config, const gchar *key, gint fallback)
{
  Entry *entry = find (config, key);
  char *end;
  long value;

  if (!entry)
    return fallback;
  value = strtol (entry->value, &end, 10);
  if (end == entry->value || *end != '\0' || value < G_MININT
      || value > G_MAXINT)
    return fallback;
  return (gint) value;
}

void
games_config_set_string (GamesConfig *config, const gchar *key,
                         const gchar *value)
{
  Entry *entry = find (config, key);

  if (!entry)
    {
      entry = g_new0 (Entry, 1);
      entry->key = g_strdup (key);
      config->entries = g_slist_append (config->entries, entry);
    }
  g_free (entry->value);
  entry->value = g_strdup (value);
}

void
games_config_set_int (GamesConfig *config, const gchar *key, gint value)
{
  gchar *text = g_strdup_printf ("%d", value);

  games_config_set_string (config, key, text);
  g_free (text);
}

gboolean
games_config_save (GamesConfig *config)
{
  GString *text = g_string_new (NULL);
  GSList *l;
  gboolean ok;

  for (l = config->entries; l; l = l->next)
    {
      Entry *entry = l->data;

      g_string_sprintfa (text, "%s = %s\n", entry->key, entry->value);
    }
  ok = games_write_file (config->path, text->str);
  g_string_free (text, TRUE);
  return ok;
}
