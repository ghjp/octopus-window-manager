#define G_LOG_DOMAIN "xinerama"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "octopus.h"
#include "xinerama.h"

/* xinerama support is optional */
#ifdef HAVE_XINERAMA

/* some macros for getting at width and height */
#define RECTWIDTH(rect)		((rect)->x2 - (rect)->x1)
#define RECTHEIGHT(rect)	((rect)->y2 - (rect)->y1)

#define SWAP(tmp, a, b)	G_STMT_START{	\
  (tmp) = (a);		\
  (a) = (b);		\
  (b) = (tmp);		\
}G_STMT_END

/* is xinerama active on the display */
static gboolean	xinerama_active		= FALSE;

/* xinerama screen information */
static gint		xinerama_count;
static rect_t		*xinerama_screens = NULL;

/* find intersection length of two lines */
static inline gint lineisect(gint p1, gint l1, gint p2, gint l2)
{
  /* make sure p1 <= p2 */
  if (p1 > p2) {
    gint tmpval;
    SWAP(tmpval, p1, p2);
    SWAP(tmpval, l1, l2);
  }

  /* calculate the intersection */
  if (p1 + l1 < p2)
    return 0;
  else if (p2 + l2 < p1 + l1)
    return l2;
  else
    return p1 + l1 - p2;
}

/*
 * find the area of the intersection of two
 * rectangles.
 */
gint rect_intersection(rect_t *r1, rect_t *r2)
{
  gint xsect, ysect;

  /*
   * find intersection of lines in the x and
   * y directions, multiply for the area of
   * the intersection rectangle.
   */
  xsect = lineisect(r1->x1, r1->x2 - r1->x1, r2->x1,
      r2->x2 - r2->x1);
  ysect = lineisect(r1->y1, r1->y2 - r1->y1, r2->y1,
      r2->y2 - r2->y1);

  return ysect * xsect;
}
/*
 * determine if the xinerama extension exists and if the display we
 * are on is using it.
 */
gboolean xinerama_init(Display *display)
{
  XineramaScreenInfo *sinfo;
  gint i;

  /* try for the xinerama extension */
  if (!XineramaQueryExtension(display, &i, &i))
    return FALSE;
  if (!XineramaIsActive(display))
    return FALSE;

  /* grab screen information structures */
  sinfo = XineramaQueryScreens(display, &xinerama_count);
  if (xinerama_count == 0 || !sinfo)
    return FALSE;

  /* mention that we are doing xinerama stuff */
  g_message("%d monitors detected", xinerama_count);
  /*
   * turn screen info structures to rectangles and
   * free the memory associated with them.
   */
  xinerama_screens = g_new0(rect_t, xinerama_count);
  for (i = 0; i < xinerama_count; i++) {
    xinerama_screens[i].x1 = sinfo[i].x_org;
    xinerama_screens[i].y1 = sinfo[i].y_org;
    xinerama_screens[i].x2 = sinfo[i].x_org + sinfo[i].width;
    xinerama_screens[i].y2 = sinfo[i].y_org + sinfo[i].height;
    g_message("  mon[%d]: x1=%4d y1=%4d x2=%4d y2=%4d",
        i,
        xinerama_screens[i].x1,
        xinerama_screens[i].y1,
        xinerama_screens[i].x2,
        xinerama_screens[i].y2);
  }
  X_FREE(sinfo);

  xinerama_active = TRUE;
  return xinerama_active;
}

/* free screen size rectangles */
void xinerama_shutdown(void)
{
  g_free(xinerama_screens);
}

/*
 * maximize a client; make it as large as the screen that it
 * is mostly on.
 */
gboolean xinerama_maximize(client_t *client)
{
  rect_t rect, *xir;
  gint most, mosti;
  gint area, i;
  warea_t *wa = &client->curr_screen->vdesk->warea;

  if(xinerama_active) {
    /* get a rect of client dimensions */
    rect.x1 = client->x;
    rect.y1 = client->y;
    rect.x2 = client->width + rect.x1;
    rect.y2 = client->height + rect.y1;

    /*
     * determine which screen this client is mostly
     * on by checking the area of intersection of
     * the client's rectangle and the xinerama
     * screen's rectangle.
     */
    most = mosti = 0;
    for (i = 0; i < xinerama_count; i++) {
      area = rect_intersection(&rect, &xinerama_screens[i]);
      if (area > most) {
        most = area;
        mosti = i;
      }
    }
    xir = xinerama_screens + mosti;

    TRACE(("%s: mosti=%d", __func__, mosti));
    /* zoom it on the screen it's mostly on */
    if(client->wstate.maxi_horz) {
      client->x = xir->x1 < wa->x ? wa->x: xir->x1;
      client->width = xir->x2 - client->x - 2 * client->wframe->bwidth;
      if(client->x + client->width > wa->x + wa->w)
        client->width = wa->x + wa->w - client->x - 2 * client->wframe->bwidth;
    }
    if(client->wstate.maxi_vert) {
      client->y = xir->y1 < wa->y ? wa->y: xir->y1;
      client->y += client->wframe->theight;
      client->height = xir->y2 - client->y - 2 * client->wframe->bwidth;
      if(client->y + client->height > wa->y + wa->h)
        client->height = wa->y + wa->h - client->y - 2 * client->wframe->bwidth;
    }
  }
  return xinerama_active;
}

/*
 * correct a client's location if it falls across two or more
 * monitors so that it is, if possible, located on only one
 * monitor.
 */
void xinerama_correctloc(client_t *client)
{
  rect_t rect;
  gint isect, carea, i, cnt;
  gint most, mosti;

  /* dont do anything if no xinerama */
  if (!xinerama_active)
    return;

  /* get client area and rectangle */
  rect.x1 = client->x;
  rect.y1 = client->y;
  rect.x2 = client->width + rect.x1;
  rect.y2 = client->height + rect.y1;
  carea = RECTWIDTH(&rect) * RECTHEIGHT(&rect);

  /*
   * determine if the client is stretched across more
   * than one xinerama screen.
   */
  for (i = 0, cnt = 0; i < xinerama_count; i++) {
    isect = rect_intersection(&rect, &xinerama_screens[i]);
    if (isect) {
      if (isect == carea)
        return;
      if (++cnt >= 2)
        goto correct;
    }
  }
  return;

  /*
   * we've found that the client lies across more than one
   * monitor, so try to push it into the one it is mostly
   * on.
   */
correct:
  g_message("found a window \"%s\" that needs correction", client->utf8_name);
  most = mosti = 0;
  for (i = 0; i < xinerama_count; i++) {
    isect = rect_intersection(&rect, &xinerama_screens[i]);
    if (isect > most) {
      most = isect;
      mosti = i;
    }
  }

  /* make sure it is small enough to fit */
  if (RECTWIDTH(&rect) >= RECTWIDTH(&xinerama_screens[mosti]))
    return;
  if (RECTHEIGHT(&rect) >= RECTWIDTH(&xinerama_screens[mosti]))
    return;

  /* push it into the screen */
  if (client->x < xinerama_screens[mosti].x1)
    client->x = xinerama_screens[mosti].x1;
  else if (client->x + client->width > xinerama_screens[mosti].x2)
    client->x = xinerama_screens[mosti].x2 - client->width;
  if (client->y < xinerama_screens[mosti].y1)
    client->y = xinerama_screens[mosti].y1;
  else if (client->y + client->height > xinerama_screens[mosti].y2)
    client->y = xinerama_screens[mosti].y2 - client->height;
}

/*
 * used to fill rects with the dimensions of each xinerama
 * monitor in order.  the screen_t is provided so that if
 * xinerama is not active on the display we can just give a
 * rect of the size of that screen.  *mon is used as an index
 * to which monitor to get the dims of, and should be 0
 * on the first call to the function.  return 0 when there
 * aren't any more monitors.
 */
gint xinerama_scrdims(screen_t *screen, gint mon, rect_t *rect)
{
  if (xinerama_active) {
    g_return_val_if_fail(mon < xinerama_count, 0);
    rect->x1 = xinerama_screens[mon].x1;
    rect->y1 = xinerama_screens[mon].y1;
    rect->x2 = xinerama_screens[mon].x2;
    rect->y2 = xinerama_screens[mon].y2;
  } else {
    g_return_val_if_fail(0 == mon, 0);
    rect->x1 = 0;
    rect->y1 = 0;
    rect->x2 = screen->dpy_width;
    rect->y2 = screen->dpy_height;
  }
  return 1;
}


gint xinerama_current_mon(gswm_t *gsw)
{
  Window dumwin;
  gint dumint;
  guint mask;
  gint x,y;
  gint i;

  x=y=0;

  XQueryPointer(gsw->display, gsw->screen[gsw->i_curr_scr].rootwin, &dumwin,
      &dumwin, &x, &y, &dumint, &dumint, &mask);

  for (i = 0; i < xinerama_count; i++)  {
    if ((xinerama_screens[i].x1<x) && (x < xinerama_screens[i].x2)) {
      if ((xinerama_screens[i].y1 < y ) && (y < xinerama_screens[i].y2)) {
        return i;
      }
    }
  }

  return 1;
}
#endif
