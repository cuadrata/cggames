/*
 * cgmahjong's maps: the layouts the tiles are laid out in
 * (C) 1998-1999 the Free Software Foundation
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
#include "maps.h"

#define APPNAME "cgmahjong"

/* Positions are in half tiles across and down, and layers up. Easy is
 * built in, so that there is always a map to play; the rest are read
 * from layouts.map. */
static const tilepos easy_map[MAX_TILES] = {
 {13, 7,  4}, {12, 8,  3}, {14, 8,  3}, {12, 6,  3},
 {14, 6,  3}, {10, 10,  2}, {12, 10,  2}, {14, 10,  2},
 {16, 10,  2}, {10, 8,  2}, {12, 8,  2}, {14, 8,  2},
 {16, 8,  2}, {10, 6,  2}, {12, 6,  2}, {14, 6,  2},
 {16, 6,  2}, {10, 4,  2}, {12, 4,  2}, {14, 4,  2},
 {16, 4,  2}, {8, 12,  1}, {10, 12,  1}, {12, 12,  1},
 {14, 12,  1}, {16, 12,  1}, {18, 12,  1}, {8, 10,  1},
 {10, 10,  1}, {12, 10,  1}, {14, 10,  1}, {16, 10,  1},
 {18, 10,  1}, {8, 8,  1}, {10, 8,  1}, {12, 8,  1},
 {14, 8,  1}, {16, 8,  1}, {18, 8,  1}, {8, 6,  1},
 {10, 6,  1}, {12, 6,  1}, {14, 6,  1}, {16, 6,  1},
 {18, 6,  1}, {8, 4,  1}, {10, 4,  1}, {12, 4,  1},
 {14, 4,  1}, {16, 4,  1}, {18, 4,  1}, {8, 2,  1},
 {10, 2,  1}, {12, 2,  1}, {14, 2,  1}, {16, 2,  1},
 {18, 2,  1}, {2, 14,  0}, {4, 14,  0}, {6, 14,  0},
 {8, 14,  0}, {10, 14,  0}, {12, 14,  0}, {14, 14,  0},
 {16, 14,  0}, {18, 14,  0}, {20, 14,  0}, {22, 14,  0},
 {24, 14,  0}, {6, 12,  0}, {8, 12,  0}, {10, 12,  0},
 {12, 12,  0}, {14, 12,  0}, {16, 12,  0}, {18, 12,  0},
 {20, 12,  0}, {4, 10,  0}, {6, 10,  0}, {8, 10,  0},
 {10, 10,  0}, {12, 10,  0}, {14, 10,  0}, {16, 10,  0},
 {18, 10,  0}, {20, 10,  0}, {22, 10,  0}, {0, 7,  0},
 {2, 8,  0}, {4, 8,  0}, {6, 8,  0}, {8, 8,  0},
 {10, 8,  0}, {12, 8,  0}, {14, 8,  0}, {16, 8,  0},
 {18, 8,  0}, {20, 8,  0}, {22, 8,  0}, {24, 8,  0},
 {2, 6,  0}, {4, 6,  0}, {6, 6,  0}, {8, 6,  0},
 {10, 6,  0}, {12, 6,  0}, {14, 6,  0}, {16, 6,  0},
 {18, 6,  0}, {20, 6,  0}, {22, 6,  0}, {24, 6,  0},
 {4, 4,  0}, {6, 4,  0}, {8, 4,  0}, {10, 4,  0},
 {12, 4,  0}, {14, 4,  0}, {16, 4,  0}, {18, 4,  0},
 {20, 4,  0}, {22, 4,  0}, {6, 2,  0}, {8, 2,  0},
 {10, 2,  0}, {12, 2,  0}, {14, 2,  0}, {16, 2,  0},
 {18, 2,  0}, {20, 2,  0}, {2, 0,  0}, {4, 0,  0},
 {6, 0,  0}, {8, 0,  0}, {10, 0,  0}, {12, 0,  0},
 {14, 0,  0}, {16, 0,  0}, {18, 0,  0}, {20, 0,  0},
 {22, 0,  0}, {24, 0,  0}, {26, 7, 0}, {28, 7, 0}
};

Map *maps;
gint n_maps;

/* layouts.map. Within <mahjongg>, each <map name="..."
 * scorename="..."> holds its tiles: <tile x y/>, <row y left right/>,
 * <column x top bottom/> and <block left right top bottom/>, in whole
 * and half tiles, each on the layer given by its own z or by the
 * <layer z> it is in. Comments are skipped; nothing else of XML is
 * needed. */

typedef struct {
        gboolean in_mahjongg;
        Map *map;               /* being read; NULL outside a <map> */
        gint count, layer;
        gboolean bad;
} Reader;

/* The value of attribute name in the tag's text, or NULL. */
static gchar *
attribute (const gchar *tag, const gchar *name)
{
        gsize n = strlen (name);
        const gchar *p = tag;

        while ((p = strstr (p, name)) != NULL) {
                const gchar *q = p + n;

                /* A whole name: not the end of a longer one. */
                if ((p == tag || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n'
                     || p[-1] == '\r')) {
                        while (*q == ' ' || *q == '\t')
                                q++;
                        if (*q == '=') {
                                gchar quote;
                                const gchar *end;

                                q++;
                                while (*q == ' ' || *q == '\t')
                                        q++;
                                quote = *q;
                                if (quote == '"' || quote == '\'') {
                                        end = strchr (q + 1, quote);
                                        if (end)
                                                return g_strndup (q + 1, end - q - 1);
                                }
                                return NULL;
                        }
                }
                p += n;
        }
        return NULL;
}

/* A number of whole and half tiles, as halves: "3.5" is 7. Read the
 * same in every locale. */
static gboolean
halves (const gchar *tag, const gchar *name, gint *value)
{
        gchar *text = attribute (tag, name);
        const gchar *p = text;
        gint whole = 0, sign = 1;
        gboolean digits = FALSE;

        if (!text)
                return FALSE;
        if (*p == '-') {
                sign = -1;
                p++;
        }
        for (; *p >= '0' && *p <= '9'; p++) {
                whole = whole * 10 + (*p - '0');
                digits = TRUE;
                if (whole > 1000)
                        break;
        }
        *value = whole * 2;
        if (*p == '.') {
                p++;
                if (*p >= '5' && *p <= '9')
                        *value += 1;
                while (*p >= '0' && *p <= '9') {
                        p++;
                        digits = TRUE;
                }
        }
        *value *= sign;
        digits = digits && *p == '\0';
        g_free (text);
        return digits;
}

static void
add_tile (Reader *r, gint x, gint y)
{
        if (r->count >= MAX_TILES || x < 0 || y < 0) {
                r->bad = TRUE;
                return;
        }
        r->map->pos[r->count].x = x;
        r->map->pos[r->count].y = y;
        r->map->pos[r->count].layer = r->layer;
        r->count++;
}

/* Bottom left first, and the top layer first, as the game draws and
 * checks them. */
static int
compare_tiles (const void *pa, const void *pb)
{
        const tilepos *a = pa, *b = pb;
        int dx = b->x - a->x, dy = b->y - a->y;

        if (b->layer != a->layer)
                return b->layer < a->layer ? -1 : 1;
        if (dx != dy)
                return dx > dy ? -1 : 1;
        return 0;
}

static void
end_map (Reader *r, GArray *list, const gchar *path)
{
        Map *m = r->map;
        gint i;

        r->map = NULL;
        /* A second map of a name already taken would share its scores. */
        for (i = 0; i < (gint) list->len && !r->bad; i++)
                if (g_strcasecmp (g_array_index (list, Map, i).key, m->key) == 0)
                        r->bad = TRUE;
        if (r->bad || r->count != MAX_TILES) {
                fprintf (stderr, "%s: %s: map \"%s\" left out: it should have "
                         "%d tiles, in places of its own\n",
                         APPNAME, path, m->name, MAX_TILES);
                g_free (m->name);
                g_free (m->key);
                g_free (m);
                return;
        }
        qsort (m->pos, MAX_TILES, sizeof (tilepos), compare_tiles);
        g_array_append_val (list, *m);
        g_free (m);
}

/* One tag, its text between < and >. */
static void
read_tag (Reader *r, const gchar *tag, GArray *list, const gchar *path)
{
        gchar name[16];
        gsize n = 0;
        gboolean closing = *tag == '/';
        gint x1 = 0, x2 = 0, y1 = 0, y2 = 0, z, x, y;

        if (closing)
                tag++;
        while (n < sizeof name - 1
               && ((tag[n] >= 'a' && tag[n] <= 'z')
                   || (tag[n] >= 'A' && tag[n] <= 'Z'))) {
                name[n] = g_ascii_tolower (tag[n]);
                n++;
        }
        name[n] = '\0';

        if (strcmp (name, "mahjongg") == 0) {
                r->in_mahjongg = !closing;
                return;
        }
        if (!r->in_mahjongg)
                return;

        if (strcmp (name, "map") == 0) {
                if (r->map)
                        end_map (r, list, path);
                if (closing)
                        return;
                r->map = g_new0 (Map, 1);
                r->map->name = attribute (tag, "name");
                r->map->key = attribute (tag, "scorename");
                if (!r->map->name)
                        r->map->name = g_strdup ("?");
                if (!r->map->key)
                        r->map->key = g_strdup (r->map->name);
                r->count = 0;
                r->layer = 0;
                r->bad = FALSE;
                return;
        }
        if (closing || !r->map)
                return;

        /* A tag's own z holds for it alone; a layer's for what follows. */
        z = r->layer;
        if (halves (tag, "z", &z))
                z /= 2;
        if (strcmp (name, "layer") == 0) {
                r->layer = z;
                return;
        }
        if (z < 0 || z > 255) {
                r->bad = TRUE;
                return;
        }

        {
                gint keep = r->layer;

                r->layer = z;
                if (strcmp (name, "tile") == 0) {
                        if (halves (tag, "x", &x1) && halves (tag, "y", &y1))
                                add_tile (r, x1, y1);
                        else
                                r->bad = TRUE;
                } else if (strcmp (name, "row") == 0) {
                        if (halves (tag, "y", &y1) && halves (tag, "left", &x1)
                            && halves (tag, "right", &x2))
                                for (x = x1; x <= x2; x += 2)
                                        add_tile (r, x, y1);
                        else
                                r->bad = TRUE;
                } else if (strcmp (name, "column") == 0) {
                        if (halves (tag, "x", &x1) && halves (tag, "top", &y1)
                            && halves (tag, "bottom", &y2))
                                for (y = y1; y <= y2; y += 2)
                                        add_tile (r, x1, y);
                        else
                                r->bad = TRUE;
                } else if (strcmp (name, "block") == 0) {
                        if (halves (tag, "left", &x1) && halves (tag, "right", &x2)
                            && halves (tag, "top", &y1)
                            && halves (tag, "bottom", &y2))
                                for (x = x1; x <= x2; x += 2)
                                        for (y = y1; y <= y2; y += 2)
                                                add_tile (r, x, y);
                        else
                                r->bad = TRUE;
                }
                r->layer = keep;
        }
}

static gchar *
read_file (const gchar *path)
{
        FILE *f = fopen (path, "rb");
        GString *s;
        gchar buffer[4096];
        gsize n;
        gchar *text;

        if (!f)
                return NULL;
        s = g_string_new (NULL);
        while ((n = fread (buffer, 1, sizeof buffer - 1, f)) > 0) {
                buffer[n] = '\0';
                g_string_append (s, buffer);
        }
        fclose (f);
        text = s->str;
        g_string_free (s, FALSE);
        return text;
}

static void
read_maps (const gchar *path, GArray *list)
{
        gchar *text = read_file (path), *p;
        Reader r = { FALSE, NULL, 0, 0, FALSE };

        if (!text) {
                fprintf (stderr, "%s: %s: the maps could not be read; "
                         "only Easy can be played\n", APPNAME, path);
                return;
        }
        p = text;
        while ((p = strchr (p, '<')) != NULL) {
                gchar *end;

                if (strncmp (p, "<!--", 4) == 0) {
                        end = strstr (p + 4, "-->");
                        if (!end)
                                break;
                        p = end + 3;
                        continue;
                }
                end = strchr (p, '>');
                if (!end)
                        break;
                *end = '\0';
                if (end > p + 1 && end[-1] == '/')
                        end[-1] = '\0';
                read_tag (&r, p + 1, list, path);
                p = end + 1;
        }
        if (r.map)
                end_map (&r, list, path);
        g_free (text);
}

void
maps_load (void)
{
        GArray *list = g_array_new (FALSE, FALSE, sizeof (Map));
        gchar *path;
        Map easy;

        easy.name = g_strdup ("Easy");
        easy.key = g_strdup ("easy");
        memcpy (easy.pos, easy_map, sizeof easy.pos);
        g_array_append_val (list, easy);

        path = games_installed_path (APPNAME, "layouts.map");
        read_maps (path, list);
        g_free (path);

        n_maps = (gint) list->len;
        maps = (Map *) list->data;
        g_array_free (list, FALSE);
}

Map *
maps_find (const gchar *key)
{
        gint i;

        for (i = 0; i < n_maps; i++)
                if (g_strcasecmp (maps[i].key, key) == 0)
                        return &maps[i];
        return NULL;
}
