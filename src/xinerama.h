#ifndef XINERAMA_H
#define XINERAMA_H

/* some macros for getting at width and height */
#define RECTWIDTH(rect)		((rect)->x2 - (rect)->x1)
#define RECTHEIGHT(rect)	((rect)->y2 - (rect)->y1)

#ifdef HAVE_XINERAMA

#include <X11/extensions/Xinerama.h>

gboolean xinerama_init(Display *display);
void xinerama_shutdown(void);
gboolean xinerama_maximize(client_t *client);
void xinerama_correctloc(client_t *client);
void xinerama_scrdims(screen_t *screen, gint mon, rect_t *rect);
gint xinerama_current_mon(gswm_t *gsw);

#else

/*
 * remove our routines so the main code doesn't
 * get cluttered with #ifdefs
 */
#define xinerama_init(c)		(FALSE)
#define xinerama_shutdown()	G_STMT_START{}G_STMT_END
#define xinerama_maximize(c)	(FALSE)
#define xinerama_correctloc(c)	G_STMT_START{}G_STMT_END
#define xinerama_current_mon(c)	(0)

/*
 * just give the rect for the full screen, and on
 * the next call (*mon == 1) return 0.
 */
static inline gint xinerama_scrdims(screen_t *screen, gint mon, rect_t *rect)
{
  g_return_val_if_fail(0 == mon, 0);
  rect->x1 = 0;
  rect->y1 = 0;
  rect->x2 = screen->dpy_width;
  rect->y2 = screen->dpy_height;
  return 1;
}

#endif

#endif
