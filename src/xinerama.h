#ifndef XINERAMA_H
#define XINERAMA_H

#ifdef HAVE_XINERAMA

#include <X11/extensions/Xinerama.h>

gboolean xinerama_init(Display *display);
void xinerama_shutdown(void);
gint xinerama_zoom(client_t *client);
void xinerama_correctloc(client_t *client);
//int xinerama_scrdims(screen_t *screen, int *mon, rect_t *rect);
gint xinerama_current_mon(Display *display, screen_t *scr);

#else

/*
 * remove our routines so the main code doesn't
 * get cluttered with #ifdefs
 */
#define xinerama_init()		((void) 0)
#define xinerama_shutdown()	((void) 0)
#define xinerama_zoom(c)	(1)
#define xinerama_correctloc(c)	((void) 0)
#define xinerama_current_mon(c)	((int) 0)

/*
 * just give the rect for the full screen, and on
 * the next call (*mon == 1) return 0.
 */
static __inline int xinerama_scrdims(screen_t *screen, int *mon,
		rect_t *rect) {
	if (*mon)
		return 0;

	rect->x1 = 0;
	rect->y1 = 0;
	rect->x2 = screen->width;
	rect->y2 = screen->height;

	*mon = 1;
	return 1;
}

#endif

#endif
