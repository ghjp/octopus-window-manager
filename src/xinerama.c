#define G_LOG_DOMAIN "xinerama"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "octopus.h"
#include "xinerama.h"
#include "rect.h"

/* xinerama support is optional */
#ifdef HAVE_XINERAMA

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

static rect_t *_get_screen_rect_occupied_most_by_client_rect(rect_t *c_rect)
{
  gint i, area, most, mosti;

  /* This function is only valid on a xinerama setup */
  g_return_val_if_fail(xinerama_active, NULL);
  /*
   * determine which screen this client is mostly
   * on by checking the area of intersection of
   * the client's rectangle and the xinerama
   * screen's rectangle.
   */
  most = mosti = 0;
  for (i = 0; i < xinerama_count; i++) {
    area = rect_intersection(c_rect, &xinerama_screens[i]);
    if (area > most) {
      most = area;
      mosti = i;
    }
  }
  TRACE("%s: mosti=%d", __func__, mosti);
  return xinerama_screens + mosti;
}

#define FILL_RECT_STRUCT_FROM_CLIENT(cl_rect, client) \
  G_STMT_START { \
    (cl_rect).x1 = (cl_rect).x2 = (client)->x; \
    (cl_rect).y1 = (cl_rect).y2 = (client)->y; \
    (cl_rect).x2 += (client)->width; \
    (cl_rect).y2 += (client)->height; \
  } G_STMT_END

void xinerama_get_screensize_on_which_client_resides(client_t *client, gint *width, gint *height)
{
  if(G_UNLIKELY(xinerama_active)) {
    rect_t cl_rect, *xir;
    /* get a rect of client dimensions */
    FILL_RECT_STRUCT_FROM_CLIENT(cl_rect, client);

    xir = _get_screen_rect_occupied_most_by_client_rect(&cl_rect);
    *width = xir->x2 - xir->x1;
    *height = xir->y2 - xir->y1;
  }
  else {
    *width = client->curr_screen->dpy_width;
    *height = client->curr_screen->dpy_height;
  }
}

/*
 * maximize a client; make it as large as the screen that it
 * is mostly on.
 */
gboolean xinerama_maximize(client_t *client)
{
  rect_t cl_rect, *xir;
  warea_t *wa = &client->curr_screen->vdesk->warea;

  if(G_UNLIKELY(xinerama_active)) {
    /* get a rect of client dimensions */
    FILL_RECT_STRUCT_FROM_CLIENT(cl_rect, client);

    xir = _get_screen_rect_occupied_most_by_client_rect(&cl_rect);

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
  rect_t cl_rect, *xir;
  gint carea, i;
  gint *isect, isect_max_idx, isect_max;

  /* dont do anything if no xinerama */
  if (G_LIKELY(!xinerama_active))
    return;

  isect = g_newa(gint, xinerama_count);

  /* get client area and rectangle */
  FILL_RECT_STRUCT_FROM_CLIENT(cl_rect, client);
  carea = RECTWIDTH(&cl_rect) * RECTHEIGHT(&cl_rect);

  /*
   * determine the intersection of the client for each screen
   * and remember where the best one is.
   */
  for (i = isect_max_idx = 0; i < xinerama_count; i++) {
    isect[i] = rect_intersection(&cl_rect, &xinerama_screens[i]);
    if(isect[i] == carea) /* Client lies already completely on one screen */
      return;
    if (isect[i] > isect[isect_max_idx])
      isect_max_idx = i;
  }
  if(!isect[isect_max_idx]) /* Client is completely outside all screens */
    return;

  /*
   * we've found that the client lies across more than one
   * monitor, so try to push it into the one it is mostly
   * on.
   */
  TRACE("found a window \"%s\" that needs correction", client->utf8_name);
  xir = NULL;
  isect_max = 0;
  /* Search  for a screen where it lies at least partly and where it would fit completely */
  for (i = 0; i < xinerama_count; i++) {
    /* There is an intersection and the dimensions match */
    if(isect[i] && RECTWIDTH(&cl_rect) < RECTWIDTH(&xinerama_screens[i])
        && RECTHEIGHT(&cl_rect) < RECTHEIGHT(&xinerama_screens[i])) {
      if(isect[i] > isect_max) {
        xir = &xinerama_screens[i];
        isect_max = isect[i];
      }
    }
  }
  /* We have not found a screen where the client can be fully placed.
   * Last resort is to try the screen where the biggest area is located */
  if(!xir)
    xir = &xinerama_screens[isect_max_idx];

  /* push it into the screen */
  if (client->x < xir->x1)
    client->x = xir->x1;
  else if (client->x + client->width > xir->x2)
    client->x = xir->x2 - client->width;
  if (client->y < xir->y1)
    client->y = xir->y1;
  else if (client->y + client->height > xir->y2)
    client->y = xir->y2 - client->height;
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
gboolean xinerama_scrdims(screen_t *screen, gint mon, rect_t *rect)
{
  if (G_UNLIKELY(xinerama_active)) {
    g_return_val_if_fail(mon < xinerama_count, FALSE);
    rect->x1 = xinerama_screens[mon].x1;
    rect->y1 = xinerama_screens[mon].y1;
    rect->x2 = xinerama_screens[mon].x2;
    rect->y2 = xinerama_screens[mon].y2;
  } else {
    g_return_val_if_fail(0 == mon, FALSE);
    rect->x1 = 0;
    rect->y1 = 0;
    rect->x2 = screen->dpy_width;
    rect->y2 = screen->dpy_height;
  }
  return TRUE;
}

gint xinerama_current_mon(gswm_t *gsw)
{
  Window dumwin;
  gint dumint;
  guint mask;
  gint x,y;
  gint i;

  x = y = 0;
  XQueryPointer(gsw->display, gsw->screen[gsw->i_curr_scr].rootwin, &dumwin,
      &dumwin, &x, &y, &dumint, &dumint, &mask);

  for (i = 0; i < xinerama_count; i++)  {
    if ((xinerama_screens[i].x1<x) && (x < xinerama_screens[i].x2)) {
      if ((xinerama_screens[i].y1 < y ) && (y < xinerama_screens[i].y2)) {
        return i;
      }
    }
  }

  return 0;
}
#endif
