## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = po

AM_CPPFLAGS = -I$(top_srcdir)/common \
        -DGAMES_DATADIR=\"$(pkgdatadir)\" \
        -DLOCALEDIR=\"$(localedir)\" \
        -DGAMES_ICONDIR=\"$(hicolordir)/48x48/apps\" \
        $(GTK_CFLAGS)

noinst_LIBRARIES = common/libgames.a
common_libgames_a_SOURCES = \
        common/games.h \
        common/games-cards.c \
        common/games-cards.h \
        common/games-config.c \
        common/games-dialogs.c \
        common/games-files.c \
        common/games-svg.c \
        common/games-svg.h \
        common/games-rand.c \
        common/games-scores.c

GAMES_LIBS = common/libgames.a $(GTK_LIBS) $(LIBINTL)

# The deck the card faces are drawn from, and the backs
cardsdir = $(pkgdatadir)/cards
dist_cards_DATA = common/cards/deck.svg
cards_backsdir = $(cardsdir)/backs
dist_cards_backs_DATA = \
        common/cards/backs/argyle.svg \
        common/cards/backs/art-deco.svg \
        common/cards/backs/black-vines.svg \
        common/cards/backs/blue-club.svg \
        common/cards/backs/castle.svg \
        common/cards/backs/clownfish.svg \
        common/cards/backs/island.svg \
        common/cards/backs/peacock.svg \
        common/cards/backs/red-club.svg \
        common/cards/backs/robot.svg \
        common/cards/backs/roses.svg \
        common/cards/backs/trumpet-snail.svg

bin_PROGRAMS = cgchess/cgchess cgfreecell/cgfreecell cghanafuda/cghanafuda \
        cghearts/cghearts cgmahjong/cgmahjong cgminesweep/cgminesweep \
        cgsolitaire/cgsolitaire
man_MANS = cgchess/cgchess.6 cgfreecell/cgfreecell.6 cghanafuda/cghanafuda.6 \
        cghearts/cghearts.6 cgmahjong/cgmahjong.6 cgminesweep/cgminesweep.6 \
        cgsolitaire/cgsolitaire.6

# cgchess

cgchess_cgchess_SOURCES = \
        cgchess/cgchess.c \
        cgchess/chess.c \
        cgchess/chess.h \
        cgchess/engine.c \
        cgchess/engine.h \
        cgchess/pieces.c \
        cgchess/pieces.h
cgchess_cgchess_LDADD = $(GAMES_LIBS)

# Chess pieces, GPL: Colin M. L. Burnett's, and Armando Hernandez
# Marroquin's Merida, a picture each
cgchess_piecesdir = $(pkgdatadir)/cgchess/pieces
dist_cgchess_pieces_DATA = \
        cgchess/pieces/cburnett.svg \
        cgchess/pieces/merida.svg

cgchess_desktopdir = $(datadir)/applications
dist_cgchess_desktop_DATA = cgchess/cgchess.desktop

# cgfreecell

cgfreecell_cgfreecell_SOURCES = cgfreecell/cgfreecell.c
cgfreecell_cgfreecell_LDADD = $(GAMES_LIBS)

cgfreecell_desktopdir = $(datadir)/applications
dist_cgfreecell_desktop_DATA = cgfreecell/cgfreecell.desktop

# cghanafuda

cghanafuda_cghanafuda_SOURCES = \
        cghanafuda/cghanafuda.c \
        cghanafuda/chart.c \
        cghanafuda/chart.h \
        cghanafuda/hanafuda.c \
        cghanafuda/hanafuda.h \
        cghanafuda/kabu-game.c \
        cghanafuda/kabu.c \
        cghanafuda/kabu.h \
        cghanafuda/koikoi-game.c \
        cghanafuda/koikoi.c \
        cghanafuda/koikoi.h \
        cghanafuda/table.c \
        cghanafuda/table.h \
        cghanafuda/tatami.c \
        cghanafuda/tatami.h
cghanafuda_cghanafuda_LDADD = $(GAMES_LIBS)

# Two sets of the cards, a picture each
cghanafuda_cardsdir = $(pkgdatadir)/cghanafuda/cards
dist_cghanafuda_cards_DATA = \
        cghanafuda/cards/light.svg \
        cghanafuda/cards/traditional.svg

cghanafuda_desktopdir = $(datadir)/applications
dist_cghanafuda_desktop_DATA = cghanafuda/cghanafuda.desktop

# cghearts

cghearts_cghearts_SOURCES = cghearts/cghearts.c
cghearts_cghearts_LDADD = $(GAMES_LIBS)

cghearts_desktopdir = $(datadir)/applications
dist_cghearts_desktop_DATA = cghearts/cghearts.desktop

# cgmahjong

cgmahjong_cgmahjong_SOURCES = \
        cgmahjong/cgmahjong.c \
        cgmahjong/cgmahjong.h \
        cgmahjong/maps.c \
        cgmahjong/maps.h \
        cgmahjong/solubility.c \
        cgmahjong/solubility.h \
        cgmahjong/tileset.c \
        cgmahjong/tileset.h
cgmahjong_cgmahjong_LDADD = $(GAMES_LIBS)

# The tiles' faces: public domain riichi tiles, and the flowers and
# seasons drawn for them
cgmahjong_tilesdir = $(pkgdatadir)/cgmahjong/tiles
dist_cgmahjong_tiles_DATA = cgmahjong/tiles/tiles.svg

# The maps after Easy, which is built in: GNOME Mahjongg's
cgmahjong_mapsdir = $(pkgdatadir)/cgmahjong
dist_cgmahjong_maps_DATA = cgmahjong/layouts.map

cgmahjong_desktopdir = $(datadir)/applications
dist_cgmahjong_desktop_DATA = cgmahjong/cgmahjong.desktop

# cgminesweep

cgminesweep_cgminesweep_SOURCES = \
        cgminesweep/cgminesweep.c \
        cgminesweep/minefield.c \
        cgminesweep/minefield.h
cgminesweep_cgminesweep_LDADD = $(GAMES_LIBS)

cgminesweep_desktopdir = $(datadir)/applications
dist_cgminesweep_desktop_DATA = cgminesweep/cgminesweep.desktop

# cgsolitaire

cgsolitaire_cgsolitaire_SOURCES = cgsolitaire/cgsolitaire.c
cgsolitaire_cgsolitaire_LDADD = $(GAMES_LIBS)

cgsolitaire_desktopdir = $(datadir)/applications
dist_cgsolitaire_desktop_DATA = cgsolitaire/cgsolitaire.desktop

# The icons: each game's one drawing, the sizes of it rendered
# beforehand by common/render-icons.sh, and an XPM, which is in the game
# too, for whatever still looks in pixmaps.
icongames = cgchess cgfreecell cghanafuda cghearts cgmahjong cgminesweep cgsolitaire
iconsizes = 16 22 24 32 48 64 128 256
hicolordir = $(datadir)/icons/hicolor
scalabledir = $(hicolordir)/scalable/apps
dist_scalable_DATA = \
        cgchess/icons/cgchess.svg \
        cgfreecell/icons/cgfreecell.svg \
        cghanafuda/icons/cghanafuda.svg \
        cghearts/icons/cghearts.svg \
        cgmahjong/icons/cgmahjong.svg \
        cgminesweep/icons/cgminesweep.svg \
        cgsolitaire/icons/cgsolitaire.svg
pixmapsdir = $(datadir)/pixmaps
dist_pixmaps_DATA = \
        cgchess/icons/cgchess.xpm \
        cgfreecell/icons/cgfreecell.xpm \
        cghanafuda/icons/cghanafuda.xpm \
        cghearts/icons/cghearts.xpm \
        cgmahjong/icons/cgmahjong.xpm \
        cgminesweep/icons/cgminesweep.xpm \
        cgsolitaire/icons/cgsolitaire.xpm

install-data-local:
	for g in $(icongames); do for s in $(iconsizes); do \
		$(MKDIR_P) "$(DESTDIR)$(hicolordir)/$${s}x$${s}/apps" && \
		$(INSTALL_DATA) "$(srcdir)/$$g/icons/$$g-$$s.png" \
			"$(DESTDIR)$(hicolordir)/$${s}x$${s}/apps/$$g.png" || exit 1; \
	done; done

uninstall-local:
	for g in $(icongames); do for s in $(iconsizes); do \
		rm -f "$(DESTDIR)$(hicolordir)/$${s}x$${s}/apps/$$g.png"; \
	done; done

EXTRA_DIST = $(man_MANS) history \
        common/render-icons.sh \
        cgchess/icons/cgchess-16.png cgchess/icons/cgchess-22.png cgchess/icons/cgchess-24.png cgchess/icons/cgchess-32.png \
        cgchess/icons/cgchess-48.png cgchess/icons/cgchess-64.png cgchess/icons/cgchess-128.png cgchess/icons/cgchess-256.png \
        cgfreecell/icons/cgfreecell-16.png cgfreecell/icons/cgfreecell-22.png cgfreecell/icons/cgfreecell-24.png cgfreecell/icons/cgfreecell-32.png \
        cgfreecell/icons/cgfreecell-48.png cgfreecell/icons/cgfreecell-64.png cgfreecell/icons/cgfreecell-128.png cgfreecell/icons/cgfreecell-256.png \
        cghanafuda/icons/cghanafuda-16.png cghanafuda/icons/cghanafuda-22.png cghanafuda/icons/cghanafuda-24.png cghanafuda/icons/cghanafuda-32.png \
        cghanafuda/icons/cghanafuda-48.png cghanafuda/icons/cghanafuda-64.png cghanafuda/icons/cghanafuda-128.png cghanafuda/icons/cghanafuda-256.png \
        cghearts/icons/cghearts-16.png cghearts/icons/cghearts-22.png cghearts/icons/cghearts-24.png cghearts/icons/cghearts-32.png \
        cghearts/icons/cghearts-48.png cghearts/icons/cghearts-64.png cghearts/icons/cghearts-128.png cghearts/icons/cghearts-256.png \
        cgmahjong/icons/cgmahjong-16.png cgmahjong/icons/cgmahjong-22.png cgmahjong/icons/cgmahjong-24.png cgmahjong/icons/cgmahjong-32.png \
        cgmahjong/icons/cgmahjong-48.png cgmahjong/icons/cgmahjong-64.png cgmahjong/icons/cgmahjong-128.png cgmahjong/icons/cgmahjong-256.png \
        cgminesweep/icons/cgminesweep-16.png cgminesweep/icons/cgminesweep-22.png cgminesweep/icons/cgminesweep-24.png cgminesweep/icons/cgminesweep-32.png \
        cgminesweep/icons/cgminesweep-48.png cgminesweep/icons/cgminesweep-64.png cgminesweep/icons/cgminesweep-128.png cgminesweep/icons/cgminesweep-256.png \
        cgsolitaire/icons/cgsolitaire-16.png cgsolitaire/icons/cgsolitaire-22.png cgsolitaire/icons/cgsolitaire-24.png cgsolitaire/icons/cgsolitaire-32.png \
        cgsolitaire/icons/cgsolitaire-48.png cgsolitaire/icons/cgsolitaire-64.png cgsolitaire/icons/cgsolitaire-128.png cgsolitaire/icons/cgsolitaire-256.png
