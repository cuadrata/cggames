/* cggames: SVG pictures
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

#ifndef GAMES_SVG_H
#define GAMES_SVG_H

#include <gdk/gdk.h>

/* An SVG file read once, to be drawn at any size, by the toolkit; it
 * knows paths, shapes, strokes, fills and gradients, but not text,
 * <use>, clipping, masks or filters. */
typedef GdkSvg GamesSvg;

/* NULL, with a message on stderr, if the file can't be read. */
GamesSvg *games_svg_load (const gchar *path);
void games_svg_free (GamesSvg *svg);

/* The same from SVG text held in memory; NULL if it isn't SVG. */
GamesSvg *games_svg_parse (const gchar *text);

/* Its size in its own units. */
void games_svg_size (GamesSvg *svg, gdouble *width, gdouble *height);

/* The whole picture drawn width by height: 8-bit RGBA, not
 * premultiplied, in rows of width * 4 bytes, to be freed with g_free. */
guchar *games_svg_render (GamesSvg *svg, gint width, gint height);

/* A width by height window on the picture drawn scale times its own
 * size, from (x, y) in its own units. */
guchar *games_svg_render_area (GamesSvg *svg, gdouble x, gdouble y,
                               gdouble scale, gint width, gint height);

#endif
