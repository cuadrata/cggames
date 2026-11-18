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

#include "config.h"

#include <stdio.h>

#include <gdk/gdk.h>

#include "games.h"
#include "games-svg.h"

/* The toolkit reads and draws the pictures. The games mix their
 * pictures themselves, from pixels whose colours are not multiplied by
 * their alpha, so they ask it for those. */

GamesSvg *
games_svg_load (const gchar *path)
{
  GError *error = NULL;
  GamesSvg *svg = gdk_svg_load (path, &error);

  if (!svg)
    {
      /* the toolkit names a file it can't read, but not one it can't
       * make a picture of */
      if (error->domain == GDK_SVG_ERROR)
        fprintf (stderr, "%s: %s\n", path, error->message);
      else
        fprintf (stderr, "%s\n", error->message);
      g_error_free (error);
    }
  return svg;
}

GamesSvg *
games_svg_parse (const gchar *text)
{
  return gdk_svg_load_from_data (text, -1, NULL);
}

void
games_svg_free (GamesSvg *svg)
{
  gdk_svg_free (svg);
}

void
games_svg_size (GamesSvg *svg, gdouble *width, gdouble *height)
{
  gdk_svg_get_size (svg, width, height);
}

guchar *
games_svg_render (GamesSvg *svg, gint width, gint height)
{
  return gdk_svg_render_straight (svg, width, height);
}

guchar *
games_svg_render_area (GamesSvg *svg, gdouble x, gdouble y, gdouble scale,
                       gint width, gint height)
{
  return gdk_svg_render_area_straight (svg, x, y, scale, width, height);
}
