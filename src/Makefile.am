octopus_CFLAGS = @GLIB_CFLAGS@ @X_CFLAGS@ @CAIRO_CFLAGS@ @LIBXOSD_CFLAGS@
#octopus_LDFLAGS = @GLIB_LIBS@ @X_LIBS@ @X_PRE_LIBS@ @X_EXTRA_LIBS@ -lX11
octopus_LDFLAGS = @GLIB_LIBS@ @X_LIBS@ @X_EXTRA_LIBS@ @CAIRO_LIBS@ @LIBXOSD_LIBS@ -lX11 
noinst_HEADERS = action_system.h client.h events.h ewmh.h glib_log_ext.h input.h main.h mwm_stuff.h octopus.h userconfig.h vdesk.h winactions.h winframe.h
octopus_SOURCES = main.c client.c events.c winactions.c vdesk.c ewmh.c winframe.c userconfig.c action_system.c input.c
bin_PROGRAMS = octopus
