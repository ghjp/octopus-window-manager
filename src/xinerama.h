#ifndef XINERAMA_H
#define XINERAMA_H

#ifdef HAVE_XINERAMA

#include <X11/extensions/Xinerama.h>

gboolean xinerama_init(Display *display);
void xinerama_shutdown(void);
gboolean xinerama_maximize(client_t *client);
void xinerama_correctloc(client_t *client);
//int xinerama_scrdims(screen_t *screen, int *mon, rect_t *rect);
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
#define xinerama_current_mon(c)	G_STMT_START{}G_STMT_END

/*
 * just give the rect for the full screen, and on
 * the next call (*mon == 1) return 0.
 */
static __inline int xinerama_scrdims(screen_t *screen, int *mon, rect_t *rect)
{
  if (*mon)
    return 0;

  rect->x1 = 0;
  rect->y1 = 0;
  rect->x2 = screen->dpy_width;
  rect->y2 = screen->dpy_height;

  *mon = 1;
  return 1;
}

#endif

#endif
