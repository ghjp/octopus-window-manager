#define G_LOG_DOMAIN "winframe"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "octopus.h"
#include "winframe.h"
#include "client.h"
#include "ewmh.h"
#include "winactions.h"
#include "vdesk.h"
#include <X11/Xatom.h>
#ifdef HAVE_XSHAPE
#include <X11/extensions/shape.h> /* SHAPE */
#endif
#include <cairo-xlib.h>
#include <string.h> /* memset */

#define DIM_FACTOR 3.333

static gint _find_new_frame_id(gswm_t *gsw)
{
  gint ret_id;

  if(gsw->unused_frame_id_list) {
    ret_id = GPOINTER_TO_INT(gsw->unused_frame_id_list->data);
    /* Remove first element */
    gsw->unused_frame_id_list = g_slist_delete_link(gsw->unused_frame_id_list, gsw->unused_frame_id_list);
  }
  else
    ret_id = 1 + g_hash_table_size(gsw->fid2frame_hash);
  return ret_id;
}

wframe_t *_wframe_new(gswm_t *gsw)
{
  XSetWindowAttributes pattr;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  wframe_t *fr = g_chunk_new0(wframe_t, gsw->memcache_frame);
  fr->title_str = g_string_new("");
  fr->id = _find_new_frame_id(gsw);
#ifdef SLOW_TITLE_TRANSPARENCY
  pattr.background_pixmap = ParentRelative;
#else
  pattr.background_pixmap = None;
#endif
  pattr.override_redirect = True;
  pattr.border_pixel = scr->color.unfocus.pixel;
  pattr.do_not_propagate_mask = ButtonMask | ButtonMotionMask;
  pattr.event_mask = FocusChangeMask | ChildMask | ButtonMask | EnterWindowMask;
  fr->win = XCreateWindow(gsw->display, scr->rootwin,
      -100, -100, 4, 4, 0, /* Size is initially irrelevant */
      CopyFromParent, InputOutput, CopyFromParent,
      CWOverrideRedirect|CWDontPropagate|CWEventMask|CWBorderPixel|CWBackPixmap, &pattr);

  pattr.override_redirect = False;
  fr->titlewin = XCreateWindow(gsw->display, fr->win,
      0, 0, 4, 4, 0, /* Size is initially irrelevant */
      CopyFromParent, InputOutput, CopyFromParent,
      CWOverrideRedirect|CWBackPixmap, &pattr);
  fr->btnwin = XCreateWindow(gsw->display, fr->win,
      0, 0, scr->fr_info.pm_width, scr->fr_info.pm_height, 0,
      CopyFromParent, InputOutput, CopyFromParent,
      CWOverrideRedirect, &pattr);
  fr->statwin = XCreateWindow(gsw->display, fr->win,
      scr->fr_info.padding, scr->fr_info.padding, scr->fr_info.pm_width, scr->fr_info.pm_height, 0,
      CopyFromParent, InputOutput, CopyFromParent,
      CWOverrideRedirect, &pattr);

  g_hash_table_insert(gsw->fid2frame_hash, GINT_TO_POINTER(fr->id), fr);
  g_hash_table_insert(gsw->win2frame_hash, GUINT_TO_POINTER(fr->win), fr);
  TRACE(("%s fr=%p fr_win=0x%lx fr_id=%d", __func__, fr, fr->win, fr->id));
  return fr;
}

static void _wframe_destroy(gswm_t *gsw, wframe_t *frame)
{
  if(frame->focused_tbar_pmap)
    XFreePixmap(gsw->display, frame->focused_tbar_pmap);
  if(frame->unfocused_tbar_pmap)
    XFreePixmap(gsw->display, frame->unfocused_tbar_pmap);
  g_string_free(frame->title_str, TRUE);
  gsw->unused_frame_id_list = g_slist_prepend(gsw->unused_frame_id_list, GINT_TO_POINTER(frame->id));
  if(!g_hash_table_remove(gsw->fid2frame_hash, GINT_TO_POINTER(frame->id)))
    g_critical("%s frame id %d not found in fid2frame_hash", __func__, frame->id);
  if(!g_hash_table_remove(gsw->win2frame_hash, GUINT_TO_POINTER(frame->win)))
    g_critical("%s client frame 0x%lx not found in win2frame_hash", __func__, frame->win);
  XDestroySubwindows(gsw->display, frame->win);
  XDestroyWindow(gsw->display, frame->win);
  g_chunk_free(frame, gsw->memcache_frame);
}

void wframe_nullify_pending_focus_events(gswm_t *gsw, wframe_t *frame)
{
  XEvent ev;
  while(XCheckWindowEvent(gsw->display, frame->win, FocusChangeMask, &ev))
    /* DO NOTHING */ ;
}

void wframe_list_add(gswm_t *gsw, wframe_t *frame)
{
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  client_t *c = wframe_get_active_client(gsw, frame);

  if(c->wstate.sticky) {
    if(!g_list_find(scr->sticky_frlist, frame))
      scr->sticky_frlist = g_list_append(scr->sticky_frlist, frame);
  }
  else
    if(!g_list_find(scr->vdesk[c->i_vdesk].frame_list, frame))
      scr->vdesk[c->i_vdesk].frame_list =
        g_list_append(scr->vdesk[c->i_vdesk].frame_list, frame);
}

void wframe_list_remove(gswm_t *gsw, wframe_t *frame)
{
  gint i;
  vdesk_t *vd;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  scr->sticky_frlist = g_list_remove(scr->sticky_frlist, frame);
  scr->detached_frlist = g_list_remove(scr->detached_frlist, frame);
  /* We have to scan all desktops. TODO Avoid this */
  vd = scr->vdesk;
  for(i = 0; i < scr->num_vdesk; i++, vd++)
    vd->frame_list = g_list_remove(vd->frame_list, frame);
}

static gint _intersect_area(client_t *c1, client_t *c2)
{
  register gint c1x1 = c1->x, c1y1 = c1->y - c1->wframe->theight;
  register gint c1x2 = c1->x+c1->width, c1y2 = c1->y + c1->height;
  register gint c2x1 = c2->x, c2y1 = c2->y - c2->wframe->theight;
  register gint c2x2 = c2->x+c2->width, c2y2 = c2->y + c2->height;

  if(c1x2 < c2x1 || c1y2 < c2y1 || c1x1 > c2x2 || c1y1 > c2y2)
    return 0;

  if(c2x2 > c1x1 && c2y2 > c1y1)
    return (c2x2-c1x1)*(c2y2-c1y1);
  else if(c1x2 > c2x1 && c2y2 > c1y1)
    return (c1x2-c2x1)*(c2y2-c1y1);
  else if(c2x2 > c1x1 && c1y2 > c2y1)
    return (c2x2-c1x1)*(c1y2-c2y1);
  else if(c1x2 > c2x1 && c1y2 > c2y1)
    return (c1x2-c2x1)*(c1y2-c2y1);
  g_return_val_if_reached(0);
  return 0;
}

static gint _calc_overlap(gswm_t *gsw, client_t *c)
{
  register GList *cl;
  screen_t *scr = c->curr_screen;
  gint dw = scr->dpy_width;
  gint dh = scr->dpy_height;
  register gint val = 0;

  for(cl = scr->vdesk[scr->current_vdesk].clnt_list ; cl; cl = g_list_next(cl)) {
    client_t *cl2 = (client_t*)cl->data;
    if(!cl2->wstate.below && cl2 != c)
      val += _intersect_area(c, cl2);
  }
  for(cl = scr->sticky_list; cl; cl = g_list_next(cl)) {
    client_t *cl2 = (client_t*)cl->data;
    if(!cl2->wstate.below && cl2 != c)
      val += _intersect_area(c, cl2);
  }

  /* penalize outside-of-window positions hard */
  if(c->x < 0)
    val += 32*ABS(c->x)*c->height;
  if(c->y < 0)
    val += 32*ABS(c->y)*c->width;
  if(c->x+c->width > dw)
    val += 32*(c->x+c->width-dw)*c->height;
  if(c->y+c->height > dh)
    val += 32*(c->y+c->height-dh)*c->width;

  return val;
}

G_GNUC_UNUSED static void _minoverlap_place_client(gswm_t *gsw, client_t *c)
{
  gint val, failures; 
  gint dw = c->curr_screen->dpy_width;
  gint dh = c->curr_screen->dpy_height;
  gint wi = dw / 16;
  gint hi = dh / MAX(16, c->wframe->theight);
  gint mlen = MAX(wi, hi);
  gint mlen2 = mlen/2;
  gint xmin = 0;
  gint ymin = 0;
  gint minval = G_MAXINT;

  /* Global placement strategy - the screen is divided into a grid,
     and the window's top left corner is placed where it causes the
     least overlap with existing windows */
  for (c->y = c->wframe->theight; c->y < dh; c->y += wi)
    for (c->x = 0; c->x < dw; c->x += hi) {
      val = _calc_overlap(gsw, c);
      if(!val) /* Already found a non overlapping place */
        return;
      else if(val < minval) {
        minval = val;
        xmin = c->x;
        ymin = c->y;
      }
    }

  /* Local placement strategy similar to the one suggested in the 
     specifications. Choose random displacement vector (manhattan 
     length = cellsize) and test, if the result is better than the current
     placement update current and continue. Stop after 5 failures. */

  c->x = xmin;
  c->y = ymin;

  for(failures = 0; failures < 5; ) {
    gint dx,dy;

    dx = g_random_int_range(0, mlen) - mlen2;
    dy = mlen-dx - mlen2;
    c->x += dx;
    c->y += dy;
    val = _calc_overlap(gsw, c);
    if(val < minval) {
      minval = val;
      xmin = c->x;
      ymin = c->y;
    }
    else
      failures++;
  }
  if(minval > c->width*c->height) {
    gint x, y;

    get_mouse_position(gsw, &x, &y);
    xmin = (x * (dw - c->wframe->bwidth - c->width)) / dw;
    ymin = (y * (dh - c->wframe->bwidth - c->height)) / dh;
  }
  c->x = xmin;
  c->y = ymin;
}

static void _mouse_place_client(gswm_t *gsw, client_t *c)
{
  gint mouse_x, mouse_y;
  gint xmax = c->curr_screen->dpy_width;
  gint ymax = c->curr_screen->dpy_height;
  gint th = c->wframe->theight;
  
  get_mouse_position(gsw, &mouse_x, &mouse_y);
  if(c->width < xmax)
    c->x = (mouse_x < xmax ?
        (mouse_x / (gdouble)xmax) : 1) * (xmax - c->width - 2*c->wframe->bwidth);
  if(c->height + th < ymax)
    c->y = (mouse_y < ymax ?
        (mouse_y / (gdouble)ymax) : 1) * (ymax - c->height - th - 2*c->wframe->bwidth);
  c->y += th;
}

/* Figure out where to map the window. c->x, c->y, c->width, and
 * c->height actually start out with values in them (whatever the
 * client passed to XCreateWindow).  Program-specified hints will
 * override these, but anything set by the program will be
 * sanity-checked before it is used. PSize is ignored completely,
 * because GTK sets it to 200x200 for almost everything. User-
 * specified hints will of course override anything the program says.
 *
 * If we can't find a reasonable position hint, we make up a position
 * using the mouse co-ordinates and window size. If the mouse is in
 * the center, we center the window; if it's at an edge, the window
 * goes on the edge. To account for window gravity while doing this,
 * we add theight into the calculation and then degravitate. Don't
 * think about it too hard, or your head will explode.
 */

static void _init_position(gswm_t *gsw, client_t *c)
{
  gint xmax = c->curr_screen->dpy_width;
  gint ymax = c->curr_screen->dpy_height;
  gint th = c->wframe->theight;

  fix_ewmh_position_based_on_struts(gsw, c);

  if(USSize & c->xsize.flags) {
    if(c->xsize.width)
      c->width = c->xsize.width;
    if(c->xsize.height)
      c->height = c->xsize.height;
  }
  else {
    /* make sure it's big enough to click at... this is somewhat
     * arbitrary */
    if(c->width < 2 * th) c->width = 2 * th;
    if(c->height < th) c->height = th;
  }

  if(c->xsize.flags & USPosition) {
    c->x = c->xsize.x;
    c->y = c->xsize.y;
    if(0 > c->x || c->x >= xmax)
      c->x = 0;
    if(0 > c->y || c->y >= ymax)
      c->y = 0;
    fix_ewmh_position_based_on_struts(gsw, c);
    TRACE(("%s USPosition: uspx=%d uspy=%d x=%d y=%d", __func__, c->xsize.x, c->xsize.y, c->x, c->y));
  }
  else if(c->xsize.flags & PPosition) {
    if(!c->x)
      c->x = c->xsize.x;
    if(!c->y)
      c->y = c->xsize.y;
    if(0 > c->x || c->x >= xmax)
      c->x = 0;
    if(0 > c->y || c->y >= ymax)
      c->y = 0;
    fix_ewmh_position_based_on_struts(gsw, c);
    TRACE(("%s PPosition: uspx=%d uspy=%d x=%d y=%d", __func__, c->xsize.x, c->xsize.y, c->x, c->y));
  }
  else {
    if(c->x < 0)
      c->x = 0;
    else if(c->x > xmax)
      c->x = xmax - th;

    if(c->y < 0)
      c->y = 0;
    else if(c->y > ymax)
      c->y = ymax - th;

    if(c->x == 0 && c->y == 0) {
      /* TODO CFG Make the placement policy configurable */
      if(c->trans)
        _mouse_place_client(gsw, c);
      else
        _minoverlap_place_client(gsw, c);
      gravitate(gsw, c, GRAV_UNDO);
    }
  }
}

/* Calculate the greatest common divisor of 2 numbers.
 * search www.wikipedia.org for "binary gcd algorithm" */

G_GNUC_UNUSED static guint _gcd(guint u, guint v)
{
  guint k = 0;

  if(!u)
    return v;
  else if(!v)
    return u;

  while ((u & 1) == 0  &&  (v & 1) == 0) { /* while both u and v are even */
    u >>= 1;   /* shift u right, dividing it by 2 */
    v >>= 1;   /* shift v right, dividing it by 2 */
    k++;       /* add a power of 2 to the final result */
  }
  /* At this point either u or v (or both) is odd */
  do {
    if ((u & 1) == 0)      /* if u is even */
      u >>= 1;           /* divide u by 2 */
    else if ((v & 1) == 0) /* else if v is even */
      v >>= 1;           /* divide v by 2 */
    else if (u >= v)       /* u and v are both odd */
      u = (u-v) >> 1;
    else                   /* u and v both odd, v > u */
      v = (v-u) >> 1;
  } while (u > 0);
  return v << k;  /* returns v * 2^k */
}

/* Least common multiple: gcd(a, b)·lcm(a, b) = a·b. */

G_GNUC_UNUSED static guint _lcm(guint u, guint v)
{
  if(u && v)
    return (u * v) / _gcd(u, v);
  return 0;
}

G_GNUC_UNUSED static void _calc_size_limits(gswm_t *gsw, client_t *c, gpointer udata)
{
  wframe_t *frame = (wframe_t*)udata;
  XSizeHints *sl = &frame->xsize;

  if(PMinSize & c->xsize.flags) {
    if(sl->min_width < c->xsize.min_width)
      sl->min_width = c->xsize.min_width;
    if(sl->min_height < c->xsize.min_height)
      sl->min_height = c->xsize.min_height;
    sl->flags |= PMinSize;
  }
  if(PMaxSize & c->xsize.flags) {
    if(sl->max_width > c->xsize.max_width)
      sl->max_width = c->xsize.max_width;
    if(sl->max_height > c->xsize.max_height)
      sl->max_height = c->xsize.max_height;
    sl->flags |= PMaxSize;
  }
  if(PResizeInc & c->xsize.flags) {
    sl->width_inc = _lcm(sl->width_inc, c->xsize.width_inc);
    sl->height_inc = _lcm(sl->height_inc, c->xsize.height_inc);
    sl->flags |= PResizeInc;
  }
  if(PBaseSize & c->xsize.flags) {
    sl->base_width = _lcm(sl->base_width, c->xsize.base_width);
    sl->base_height = _lcm(sl->base_height, c->xsize.base_height);
    sl->flags |= PBaseSize;
  }
  /* TODO At the moment clients with an aspect ratio can't be included in a multi frame */
  if(PAspect & c->xsize.flags) {
    sl->min_aspect = c->xsize.min_aspect;
    sl->max_aspect = c->xsize.max_aspect;
    sl->flags |= PAspect;
  }
  TRACE(("%s (%s): min_w=%d min_h=%d max_w=%d max_h=%d w_inc=%d h_inc=%d",
      __func__, c->utf8_name,
      sl->min_width, sl->min_height, sl->max_width,
      sl->max_height, sl->width_inc, sl->height_inc));
}

#define ROUND_UP(val, base, inc) G_STMT_START{ val = (base) + (((val) - (base) + (inc) - 1) / (inc)) * (inc); }G_STMT_END

void wframe_rebuild_xsizehints(gswm_t *gsw, wframe_t *frame, const gchar *context)
{
  memset(&frame->xsize, 0, sizeof(frame->xsize));
  frame->xsize.max_width = frame->xsize.max_height = G_MAXINT;
  frame->xsize.width_inc = frame->xsize.height_inc = 
    frame->xsize.base_width = frame->xsize.base_height = 1;
  wframe_foreach(gsw, frame, _calc_size_limits, frame);
  if(!(PBaseSize & frame->xsize.flags))
    frame->xsize.base_width = frame->xsize.base_height = 0;
  /* Round up the min/max size hints */
  /* FIXME The handling of the PBaseSize values isn't correct at the moment */
  if(PResizeInc & frame->xsize.flags) {
    if(PMinSize & frame->xsize.flags) {
      TRACE(("%s 1. min_width=%d min_height=%d", __func__,
            frame->xsize.min_width, frame->xsize.min_height));
      ROUND_UP(frame->xsize.min_width, frame->xsize.base_width, frame->xsize.width_inc);
      ROUND_UP(frame->xsize.min_height, frame->xsize.base_width, frame->xsize.height_inc);
      TRACE(("%s 2. min_width=%d min_height=%d", __func__,
            frame->xsize.min_width, frame->xsize.min_height));
    }
    if(PMaxSize & frame->xsize.flags) {
      TRACE(("%s 1. max_width=%d max_height=%d", __func__,
            frame->xsize.max_width, frame->xsize.max_height));
      ROUND_UP(frame->xsize.max_width, frame->xsize.base_width, frame->xsize.width_inc);
      ROUND_UP(frame->xsize.max_height, frame->xsize.base_width, frame->xsize.height_inc);
      TRACE(("%s 2. max_width=%d max_height=%d", __func__,
            frame->xsize.max_width, frame->xsize.max_height));
    }
  }
  TRACE(("%s.%s: min_w=%d min_h=%d max_w=%d max_h=%d w_inc=%d h_inc=%d",
      __func__, context,
      frame->xsize.min_width, frame->xsize.min_height, frame->xsize.max_width,
      frame->xsize.max_height, frame->xsize.width_inc, frame->xsize.height_inc));
}

void wframe_x_move_resize(gswm_t *gsw, wframe_t *frame)
{
  client_t *c = wframe_get_active_client(gsw, frame);

  XMoveResizeWindow(gsw->display, frame->win,
      c->x, c->y - frame->theight, c->width, c->height + frame->theight);
  if(frame->theight) {
    screen_t *scr = c->curr_screen;
    XResizeWindow(gsw->display, frame->titlewin, c->width, frame->theight);
    XMoveWindow(gsw->display, frame->btnwin,
        c->width - scr->fr_info.pm_width - scr->fr_info.padding, scr->fr_info.padding);
    /* The status window must not be moved because its placement is static on the left
       side of the frame */
  }
}

static void _adjust_client_stuff(gswm_t *gsw, client_t *cl, gpointer udata)
{
  client_t *ref_c = (client_t*)udata;

  TRACE(("%s ref_c=%p (%s)", __func__, ref_c, cl->utf8_name));
  cl->width = ref_c->width;
  cl->height = ref_c->height;
  XResizeWindow(gsw->display, cl->win, cl->width, cl->height);
  /*XMoveResizeWindow(gsw->display, cl->win, 0, cl->wframe->theight, cl->width, cl->height);*/
  send_config(gsw, cl);
}

#define CLNT_EV_MASK (EnterWindowMask|PropertyChangeMask|ColormapChangeMask)

void wframe_bind_client(gswm_t *gsw, wframe_t *frame, client_t *c)
{
  XWindowAttributes winattr;
  XSetWindowAttributes pattr;
  Display *dpy = gsw->display;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  XGetWindowAttributes(dpy, c->win, &winattr);

  /* First of all the client gets a frame */
  c->wframe = frame ? frame: _wframe_new(gsw);
  /* Set properties of new frame */
  if(G_LIKELY(!frame)) {
    if(c->wstate.mwm_border && !c->wstate.shaped) {
      c->wframe->bwidth =  scr->fr_info.border_width;
      c->wframe->theight = scr->fr_info.tbar_height;
      XSetWindowBorderWidth(dpy, c->wframe->win, c->wframe->bwidth);
    }
    else {
      c->wframe->bwidth = G_MININT; /* Borderless client */
      c->wframe->theight = 0;
      XSetWindowBorderWidth(dpy, c->wframe->win, 0);
    }
  }
  c->wframe->client_list = g_list_prepend(c->wframe->client_list, c);

  if(!frame && IsViewable != winattr.map_state) {
    TRACE(("%s IsViewable != winattr.map_state (%s)", __func__, c->utf8_name));
    _init_position(gsw, c);
    set_wm_state(gsw, c, NormalState);
  }
  wframe_rebuild_xsizehints(gsw, c->wframe, __func__);
  if(G_UNLIKELY(frame))
    wa_do_all_size_constraints(gsw, c);
  gravitate(gsw, c, GRAV_APPLY);
  wframe_x_move_resize(gsw, c->wframe);

  TRACE(("%s frame=%ld w=%ld", __func__, c->wframe->win, c->win));

  /* We don't want these masks to be propagated down to the frame */
  pattr.do_not_propagate_mask = ButtonMask | ButtonMotionMask;
  XChangeWindowAttributes(dpy, c->win, CWDontPropagate, &pattr);

  /* make same shape of new parent as original */
  if(gsw->shape) { /* SHAPE */
    wframe_set_shape(gsw, c);
#ifdef HAVE_XSHAPE
    if(c->wstate.shaped)
      XShapeSelectInput(dpy, c->win, ShapeNotifyMask);
#endif
  }

  XAddToSaveSet(dpy, c->win);
  XSetWindowBorderWidth(dpy, c->win, 0);
  /*XResizeWindow(dpy, c->win, c->width, c->height);*/
  XSelectInput(dpy, c->win, NoEventMask);
  XReparentWindow(dpy, c->win, c->wframe->win, 0, c->wframe->theight);
  XSelectInput(dpy, c->win, CLNT_EV_MASK);
  /* If window is already mapped XReparentWindow generates an unmap event */
  if(IsViewable == winattr.map_state)
    ++c->ignore_unmap;

  wframe_install_kb_shortcuts(gsw, c->wframe);
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  set_ewmh_net_frame_extents(gsw, c);
  set_frame_id_xprop(gsw, c->win, c->wframe->id);
  wframe_foreach(gsw, c->wframe, _adjust_client_stuff, c);
}   

void wframe_unbind_client(gswm_t *gsw, client_t *c)
{
  XWindowAttributes winattr;
  Display *dpy = gsw->display;
  screen_t *scr = c->curr_screen;

  XGetWindowAttributes(dpy, c->win, &winattr);
  gravitate(gsw, c, GRAV_UNDO);
  XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
  XUngrabKey(dpy, AnyKey, AnyModifier, c->win);
  XReparentWindow(dpy, c->win, scr->rootwin, c->x, c->y);
  /* If window is already mapped XReparentWindow generates an unmap event */
  if(IsViewable == winattr.map_state)
    ++c->ignore_unmap;
  XRemoveFromSaveSet(dpy, c->win);
  /* Don't get events on not-managed windows */
  XSelectInput(dpy, c->win, NoEventMask);
#ifdef HAVE_XSHAPE
  if(gsw->shape)
    XShapeSelectInput(dpy, c->win, NoEventMask);
#endif
  wframe_remove_client(gsw, c);
}

void wframe_remove_client(gswm_t *gsw, client_t *c)
{
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  wframe_t *frame = c->wframe;

  if(!frame) /* Client wasn't constructed fully */
    return;
  frame->client_list = g_list_remove(frame->client_list, c);
  TRACE(("%s listlen=%d after g_list_remove",
        __func__, g_list_length(frame->client_list)));
  /* Last client has gone */
  if(G_LIKELY(!frame->client_list)) {
    /* Live of frame is over */
    wframe_list_remove(gsw, frame);
    _wframe_destroy(gsw, frame);
  }
  else {
    wframe_rebuild_xsizehints(gsw, frame, __func__);
    /* Frame was focused and another client is still present */
    if(get_focused_client(gsw) == c)
      scr->vdesk[scr->current_vdesk].focused = wframe_get_active_client(gsw, frame);
    wframe_tbar_pmap_recreate(gsw, frame);
  }
  c->wframe = NULL;
}

static void _inst_frame_shortcuts(Display *dpy, Window frwin, guint mod_mask)
{
  XGrabButton(dpy, AnyButton, mod_mask, frwin, True,
      ButtonPressMask|ButtonReleaseMask,
      GrabModeAsync, GrabModeAsync, None, None);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Escape")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("d")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("plus")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("minus")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Left")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Right")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Up")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Down")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Left")),
      ControlMask|mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Right")),
      ControlMask|mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Up")),
      ControlMask|mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Down")),
      ControlMask|mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Left")),
      ShiftMask|mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Right")),
      ShiftMask|mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Up")),
      ShiftMask|mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Down")),
      ShiftMask|mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Home")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("m")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("v")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("h")),
      mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Tab")),
      ShiftMask|mod_mask, frwin, True, GrabModeAsync, GrabModeAsync);
}

void wframe_install_kb_shortcuts(gswm_t *gsw, wframe_t *frame)
{
  Display *dpy = gsw->display;
  guint mod_mask = gsw->ucfg.modifier;

  XUngrabButton(dpy, AnyButton, AnyModifier, frame->win);
  XUngrabKey(dpy, AnyKey, AnyModifier, frame->win);
  _inst_frame_shortcuts(dpy, frame->win, mod_mask);
  _inst_frame_shortcuts(dpy, frame->win, mod_mask | gsw->numlockmask);
}

static void _truncate_title_string(gswm_t *gsw, client_t *c, cairo_t *cr, gboolean set_visible_name,
    gdouble max_width, GString *s)
{
  gdouble tw;
  cairo_text_extents_t extents;

  cairo_text_extents(cr, c->utf8_name, &extents);
  tw = extents.width + extents.x_bearing;
  if(G_UNLIKELY(tw > max_width)) {
    gchar *utf8_cut_start, *utf8_cut_end;
    glong utf8_len = g_utf8_strlen(c->utf8_name, -1);
    glong len = utf8_len * max_width / tw - 3;

    len /= 2;
    if(0 > len)
      len = 0;
    utf8_cut_start = g_utf8_offset_to_pointer(c->utf8_name, len);
    utf8_cut_end   = g_utf8_offset_to_pointer(c->utf8_name, utf8_len - len);
    g_string_set_size(s, 0);
    g_string_append_len(s, c->utf8_name, utf8_cut_start - c->utf8_name);
    g_string_append(s, "...");
    g_string_append(s, utf8_cut_end);
  }
  else
    g_string_assign(s, c->utf8_name);
  if(set_visible_name)
    set_ewmh_net_wm_visible_name(gsw, c, s);
}

static void _redraw_tbar_pixmap(gswm_t *gsw, client_t *c,
    Pixmap *pmap, color_intensity_t *ci, color_intensity_t *text_ci, gboolean set_visible_name)
{
  gint i;
  cairo_pattern_t *pat;
  gboolean fast_fill;
  gdouble alpha_east, alpha_west, wtab_width;
  Display *dpy = gsw->display;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  gint w = c->width;
  gint h = c->wframe->theight;
  gint nr_clnts = g_list_length(c->wframe->client_list);
  GString *s = c->wframe->title_str;

  if(G_LIKELY(*pmap))
    XFreePixmap(dpy, *pmap);
  if(G_UNLIKELY(0 >= h)) {
    c->wframe->pm_w = c->wframe->pm_h = 0;
    *pmap = None;
    return;
  }
  if(G_LIKELY((*pmap = XCreatePixmap(dpy, c->wframe->win,
          w, h, DefaultDepth(dpy, scr->id))))) {
    gdouble wtab_width_title;
    gdouble dx = 3. * scr->fr_info.padding;
    cairo_surface_t *cr_xlib_surf = cairo_xlib_surface_create(dpy, *pmap, DefaultVisual(dpy, scr->id), w, h);
    cairo_t *cr = cairo_create(cr_xlib_surf);
    cairo_surface_destroy(cr_xlib_surf); /* A reference is hold by cr */

    c->wframe->pm_w = w;
    c->wframe->pm_h = h;
    /*cairo_set_target_drawable(cr, dpy, *pmap);*/

    /* Transparency to the background desired */
    fast_fill = TRUE;
    alpha_east = alpha_west = 1.;
    if(gsw->alpha_processing) {
      Atom type_ret = None;
      gint format_ret = 0;
      gulong items_ret = 0;
      gulong after_ret = 0;
      guint8 *prop_data = NULL;

      if(Success == XGetWindowProperty(dpy, scr->rootwin, gsw->xa.wm_xrootpmap_id, 0, 1, False, XA_PIXMAP,
            &type_ret, &format_ret, &items_ret, &after_ret, &prop_data) && prop_data) {
        XCopyArea(dpy, *(Pixmap*)prop_data, *pmap, DefaultGC(dpy, scr->id),
            c->x + c->wframe->bwidth, c->y + c->wframe->bwidth - h, w, h, 0, 0);
        XFree(prop_data);
        alpha_east = gsw->ucfg.alpha_east;
        alpha_west = gsw->ucfg.alpha_west;
        fast_fill = FALSE;
      }
#if 0 /* TODO: Check if this method is applicable */
      else if(Success == XGetWindowProperty(dpy, scr->rootwin, gsw->xa.wm_xsetroot_id, 0, 1, False, XA_PIXMAP,
            &type_ret, &format_ret, &items_ret, &after_ret, &prop_data) && prop_data) {
        XImage *image = XGetImage(dpy, *(Pixmap*)prop_data,
            c->x + c->wframe->bwidth, c->y + c->wframe->bwidth - h, w, h, AllPlanes, ZPixmap);
        if(image) {
          XPutImage(dpy, *pmap, DefaultGC(dpy, scr->id), image, 0, 0, 0, 0, w, h);
          XDestroyImage(image);
          alpha_east = gsw->ucfg.alpha_east;
          alpha_west = gsw->ucfg.alpha_west;
          fast_fill = FALSE;
        }
        XFree(prop_data);
        TRACE(("%s wm_xsetroot_id image=%p", __func__, image));
      }
#endif
#ifdef SLOW_TITLE_TRANSPARENCY
      else {
        XSetWindowBackgroundPixmap(dpy, c->wframe->titlewin, ParentRelative);
        XClearWindow(dpy, c->wframe->titlewin);
        XCopyArea(dpy, c->wframe->titlewin, *pmap, DefaultGC(dpy, scr->id),
            0, 0, w, h, 0, 0);
        alpha_east = gsw->ucfg.alpha_east;
        alpha_west = gsw->ucfg.alpha_west;
        fast_fill = FALSE;
      }
#endif
    }

    if(fast_fill) { /* Perform a fast fill operation */
      cairo_set_source_rgb(cr, ci->r, ci->g, ci->b);
      cairo_rectangle(cr, 0, 0, w, h);
      cairo_fill(cr);
    }
    else {
      pat = cairo_pattern_create_linear(0., 0., w, 0);
      cairo_pattern_add_color_stop_rgba(pat, 1, ci->r, ci->g, ci->b, alpha_west);
      cairo_pattern_add_color_stop_rgba(pat, 0, ci->r, ci->g, ci->b, alpha_east);
      cairo_rectangle(cr, 0,0,w,h);
      cairo_set_source(cr, pat);
      cairo_fill(cr);
      cairo_pattern_destroy(pat);

      /*
         pat = cairo_pattern_create_linear(0., 0., 0., h/2.0);
         cairo_pattern_add_color_stop_rgba(pat, 1, ci->r, ci->g, ci->b, alpha_east);
         cairo_pattern_add_color_stop_rgba(pat, 0, ci->r, ci->g, ci->b, alpha_west);
         cairo_rectangle(cr, 0,0,w,h/2.0);
         cairo_set_source(cr, pat);
         cairo_fill(cr);
         cairo_pattern_destroy(pat);

         pat = cairo_pattern_create_linear(0., h/2.0, 0., h);
         cairo_pattern_add_color_stop_rgba(pat, 0, ci->r, ci->g, ci->b, alpha_east);
         cairo_pattern_add_color_stop_rgba(pat, 1, ci->r, ci->g, ci->b, alpha_west);
         cairo_rectangle(cr, 0,h/2.0,w,h/2.0);
         cairo_set_source(cr, pat);
         cairo_fill(cr);
         cairo_pattern_destroy(pat);
       */
    }

    /* Create tabbed window indicators and title strings */
    cairo_select_font_face(cr, gsw->ucfg.titlebar_font_family, CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 0.80*h);
    wtab_width = w - 2 * scr->fr_info.pm_width;
    if(0.0 > wtab_width)
      wtab_width = 0.;
    if(G_UNLIKELY(1 < nr_clnts)) {
      GList *cle;
      gdouble h1 = 0.9 * h;
      gdouble y = 0.1 *h;

      wtab_width = (0.9 * wtab_width) / nr_clnts;
      wtab_width_title = wtab_width - 2 * dx;
      if(0. > wtab_width_title)
        wtab_width_title = 0.;
      /*cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);*/
      cairo_set_line_width(cr, 1);
      for(i = 0, cle = g_list_first(c->wframe->client_list); i < nr_clnts;
          i++, cle = g_list_next(cle)) {
        gdouble x = 1.04 * i * wtab_width + 2*scr->fr_info.padding + scr->fr_info.pm_width;

        /* Set tab field */
        cairo_set_source_rgba(cr, ci->r, ci->g, ci->b, .3);
        cairo_rectangle(cr, x, y, wtab_width, h1);
        cairo_fill(cr);
        /* Set tab field outline */
        cairo_set_source_rgba(cr, text_ci->r, text_ci->g, text_ci->b, .75);
        cairo_move_to(cr, x, h);
        cairo_rel_line_to(cr, 0, -h1);
        cairo_rel_line_to(cr, wtab_width, 0);
        cairo_rel_line_to(cr, 0, h1);
        cairo_stroke(cr);
        /* Draw title */
        x += dx;
        cairo_set_source_rgb(cr, text_ci->r, text_ci->g, text_ci->b);
        cairo_move_to(cr, x, 0.8*h);
        _truncate_title_string(gsw, cle->data, cr, set_visible_name, wtab_width_title, s);
        cairo_show_text(cr, s->str);
      }
    }
    else {
      wtab_width_title = wtab_width - 2 * dx;
      if(0. > wtab_width_title)
        wtab_width_title = 0.;
      cairo_set_source_rgb(cr, text_ci->r, text_ci->g, text_ci->b);
      cairo_move_to(cr, scr->fr_info.pm_width + dx, 0.8*h);
      _truncate_title_string(gsw, c, cr, set_visible_name, wtab_width, s);
      cairo_show_text(cr, s->str);
    }
    cairo_destroy(cr);
  }
}

void _redraw_tbar_button(gswm_t *gsw, wframe_t *frame)
{
  gint xb, yb, wb, hb;
  Display *dpy = gsw->display;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  wb = scr->fr_info.pm_width;
  hb = scr->fr_info.pm_height;
  xb = frame->pm_w - wb;
  yb = scr->fr_info.padding/2;
  if(!hb || !frame->actual_tbar_button) /* No Button drawing necessary */
    return;
  XSetWindowBackgroundPixmap(dpy, frame->btnwin, frame->actual_tbar_button);
  XClearWindow(dpy, frame->btnwin);
  /*
  GC border_gc = DefaultGC(dpy, scr->id);
  XCopyArea(dpy, frame->actual_tbar_button,
      frame->titlewin, border_gc, 0, 0, wb, hb, xb, yb);
      */
}

void wframe_tbar_button_set_pressed(gswm_t *gsw, wframe_t *frame)
{
  client_t *c;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  c = wframe_get_active_client(gsw, frame);
  frame->actual_tbar_button =
    c->wstate.sticky ? scr->fr_info.btn_sticky_pressed: scr->fr_info.btn_normal_pressed;
  _redraw_tbar_button(gsw, frame);
}

void wframe_tbar_button_set_normal(gswm_t *gsw, wframe_t *frame)
{
  client_t *c;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  c = wframe_get_active_client(gsw, frame);
  frame->actual_tbar_button =
    c->wstate.sticky ? scr->fr_info.btn_sticky: scr->fr_info.btn_normal;
  _redraw_tbar_button(gsw, frame);
}

void wframe_set_type(gswm_t *gsw, wframe_t *frame, gboolean focused)
{
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  /* Draw the correct pixmap onto the titlebar */
  if(focused) {
    XSetWindowBorder(gsw->display, frame->win, scr->color.focus.pixel);
    XSetWindowBackgroundPixmap(gsw->display, frame->titlewin, frame->focused_tbar_pmap);
  }
  else {
    XSetWindowBorder(gsw->display, frame->win, scr->color.unfocus.pixel);
    XSetWindowBackgroundPixmap(gsw->display, frame->titlewin, frame->unfocused_tbar_pmap);
  }
}

void wframe_expose(gswm_t *gsw, wframe_t *frame)
{
  Display *dpy = gsw->display;

  wframe_set_type(gsw, frame, frame->focus_expose_hint);
  XClearWindow(dpy, frame->titlewin);
  _redraw_tbar_button(gsw, frame);
}

void wframe_tbar_pmap_recreate(gswm_t *gsw, wframe_t *frame)
{
  client_t *c;

  c = wframe_get_active_client(gsw, frame);
  if(!c->wstate.mwm_title ||
      G_MININT == frame->bwidth) /* MWM */
    return;

  TRACE(("%s w=%ld cairo needed %d != %d", __func__, c->win, c->width, frame->pm_w));

  _redraw_tbar_pixmap(gsw, c, &frame->unfocused_tbar_pmap,
      &gsw->ucfg.unfocused_color, &gsw->ucfg.unfocused_text_color, TRUE);
  _redraw_tbar_pixmap(gsw, c, &frame->focused_tbar_pmap,
      &gsw->ucfg.focused_color, &gsw->ucfg.focused_text_color, FALSE);

  /* Draw the correct, full pixmap onto the titlebar */
  wframe_expose(gsw, frame);
}

static Pixmap _draw_stat_indicator(Display *dpy, screen_t *scr,
    gint w, gint h, color_intensity_t *foci, gdouble percent)
{
  cairo_pattern_t *pat;
  Pixmap pmap = XCreatePixmap(dpy, scr->rootwin, w, h, DefaultDepth(dpy, scr->id));

  if(pmap) {
    gdouble bg_r, bg_g, bg_b;
    cairo_t *cr;
    cairo_surface_t *cr_surface =
      cairo_xlib_surface_create(dpy, pmap, DefaultVisual(dpy, scr->id), w, h);
    cr = cairo_create(cr_surface);
    cairo_surface_destroy(cr_surface); /* A reference is hold by cr */

    if(0. == foci->r && 0. == foci->g && 0. == foci->b)
      bg_r = bg_g = bg_b  = 1.;
    else {
      bg_r = foci->r / DIM_FACTOR;
      bg_g = foci->g / DIM_FACTOR;
      bg_b = foci->b / DIM_FACTOR;
    }
    /** fill background **/
    cairo_set_source_rgb(cr, bg_r,bg_g,bg_b);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    if(percent) {
      gdouble sheight = (1. - percent) * h;
      pat = cairo_pattern_create_linear(0., 0., w, 0);
      cairo_pattern_add_color_stop_rgba(pat, 0, foci->r, foci->g, foci->b, 0.);
      cairo_pattern_add_color_stop_rgba(pat, .5, foci->r, foci->g, foci->b, 1.0);
      cairo_pattern_add_color_stop_rgba(pat, 1, foci->r, foci->g, foci->b, 0.);
      cairo_rectangle(cr, 0, sheight/2., w, h - sheight);
      cairo_set_source(cr, pat);
      cairo_fill(cr);
      cairo_pattern_destroy(pat);
    }
    else {
      pat = cairo_pattern_create_linear(0., 0., w, 0);
      cairo_pattern_add_color_stop_rgba(pat, 0, foci->r, foci->g, foci->b, 0.9);
      cairo_pattern_add_color_stop_rgba(pat, .5, foci->r, foci->g, foci->b, 0.0);
      cairo_pattern_add_color_stop_rgba(pat, 1, foci->r, foci->g, foci->b, 0.9);
      cairo_rectangle(cr, 1, h*(0.5-0.15), w-2, h*0.3);
      cairo_set_source(cr, pat);
      cairo_fill(cr);
      cairo_pattern_destroy(pat);
    }
    cairo_destroy(cr);
  }

  return pmap;
}

void wframe_button_init(gswm_t *gsw)
{
  cairo_t *cr;
  cairo_surface_t *cr_surface;
  gdouble cx, cy, bg_r, bg_g, bg_b;
  gint w, h;
  cairo_pattern_t *pat;
  Display *dpy = gsw->display;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  color_intensity_t *foci = &gsw->ucfg.focused_color;

  w = h = scr->fr_info.tbar_height - 2*scr->fr_info.padding;
  if(0 >= w)
    return;
  if(0. == foci->r && 0. == foci->g && 0. == foci->b)
    bg_r = bg_g = bg_b  = 1.;
  else {
    bg_r = foci->r / DIM_FACTOR;
    bg_g = foci->g / DIM_FACTOR;
    bg_b = foci->b / DIM_FACTOR;
  }

  /* Create normal button */
  scr->fr_info.btn_normal =
    XCreatePixmap(dpy, scr->rootwin, w, h, DefaultDepth(dpy, scr->id));
  cr_surface = cairo_xlib_surface_create(dpy, scr->fr_info.btn_normal, DefaultVisual(dpy, scr->id), w, h);
  cr = cairo_create(cr_surface);
  cairo_surface_destroy(cr_surface); /* A reference is hold by cr */
  scr->fr_info.pm_width = w;
  scr->fr_info.pm_height = h;
  /** fill background **/
  cairo_set_source_rgb(cr, bg_r,bg_g,bg_b);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);
  cx = w/2.;
  cy = h/2.;

  /** Button circle **/
  pat = cairo_pattern_create_radial(cx, cy, h/16., cx, cy, h/2.5);
  cairo_pattern_add_color_stop_rgba(pat, 1, bg_r,bg_g,bg_b, 1);
  cairo_pattern_add_color_stop_rgba(pat, 0, foci->r, foci->g, foci->b, 1);
  cairo_arc(cr, cx, cy, h/2., 0, 2*G_PI);
  cairo_set_source(cr, pat);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);
  cairo_destroy(cr);

#if 0
  /** outer border **/
  cairo_move_to(cr, 0, 0);
  cairo_set_source_rgb(cr, red/2, green/2, blue/2);
  cairo_rel_line_to(cr, w, 0);
  cairo_rel_line_to(cr, 0, h);
  cairo_rel_line_to(cr, -w, 0);
  cairo_close_path(cr);
  cairo_stroke(cr);
#endif

  /* Create normal pressed button */
  scr->fr_info.btn_normal_pressed =
    XCreatePixmap(dpy, scr->rootwin, w, h, DefaultDepth(dpy, scr->id));
  cr_surface = cairo_xlib_surface_create(dpy, scr->fr_info.btn_normal_pressed, DefaultVisual(dpy, scr->id), w, h);
  cr = cairo_create(cr_surface);
  cairo_surface_destroy(cr_surface); /* A reference is hold by cr */
  /** fill background **/
  cairo_set_source_rgb(cr, bg_r,bg_g,bg_b);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);
  cx = w/2.;
  cy = h/2.;

  /** Button circle **/
  pat = cairo_pattern_create_radial(cx, cy, h/16., cx, cy, h/2.5);
  cairo_pattern_add_color_stop_rgba(pat, 0, bg_r,bg_g,bg_b, 1);
  cairo_pattern_add_color_stop_rgba(pat, 1, foci->r, foci->g, foci->b, 1);
  cairo_arc(cr, cx, cy, h/2., 0, 2*G_PI);
  cairo_set_source(cr, pat);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);
  cairo_destroy(cr);

#if 0
  /** outer border **/
  cairo_move_to(cr, 0, 0);
  cairo_set_source_rgb(cr, red/2, green/2, blue/2);
  cairo_rel_line_to(cr, w, 0);
  cairo_rel_line_to(cr, 0, h);
  cairo_rel_line_to(cr, -w, 0);
  cairo_close_path(cr);
  cairo_stroke(cr);
#endif

  /* Create sticky button */
  scr->fr_info.btn_sticky = XCreatePixmap(dpy, scr->rootwin, w, h, DefaultDepth(dpy, scr->id));
  cr_surface = cairo_xlib_surface_create(dpy, scr->fr_info.btn_sticky, DefaultVisual(dpy, scr->id), w, h);
  cr = cairo_create(cr_surface);
  cairo_surface_destroy(cr_surface); /* A reference is hold by cr */
  /** fill background **/
  cairo_set_source_rgb(cr, bg_r,bg_g,bg_b);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);
  cx = w/2.;
  cy = h/2.;

  /** Button circle **/
  pat = cairo_pattern_create_radial(cx, cy, h/16., cx, cy, h/2.5);
  cairo_pattern_add_color_stop_rgba(pat, 1, bg_r,bg_g,bg_b, 1);
  cairo_pattern_add_color_stop_rgba(pat, 0, foci->r, foci->g, foci->b, 1);
  cairo_arc(cr, cx, cy, h/2., 0, 2*G_PI);
  cairo_set_source(cr, pat);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);
  {
    gint i;
    cairo_set_source_rgb(cr, foci->r, foci->g, foci->b);
    for(i = 1; i <= 3; i++) {
      cairo_move_to(cr, w/8., i*h/4.);
      cairo_rel_line_to(cr, w*6./8., 0);
    }
  }
  cairo_stroke(cr);

#if 0
  /** outer border **/
  cairo_move_to(cr, 0, 0);
  cairo_set_source_rgb(cr, bg_r,bg_g,bg_b);
  cairo_rel_line_to(cr, w, 0);
  cairo_rel_line_to(cr, 0, h);
  cairo_rel_line_to(cr, -w, 0);
  cairo_close_path(cr);
  cairo_stroke(cr);
#endif
  cairo_destroy(cr);

  /* Create sticky pressed button */
  scr->fr_info.btn_sticky_pressed =
    XCreatePixmap(dpy, scr->rootwin, w, h, DefaultDepth(dpy, scr->id));
  cr_surface = cairo_xlib_surface_create(dpy, scr->fr_info.btn_sticky_pressed, DefaultVisual(dpy, scr->id), w, h);
  cr = cairo_create(cr_surface);
  cairo_surface_destroy(cr_surface); /* A reference is hold by cr */
  /** fill background **/
  cairo_set_source_rgb(cr, bg_r,bg_g,bg_b);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);
  cx = w/2.;
  cy = h/2.;

  /** Button circle **/
  pat = cairo_pattern_create_radial(cx, cy, h/16., cx, cy, h/2.5);
  cairo_pattern_add_color_stop_rgba(pat, 0, bg_r,bg_g,bg_b, 1);
  cairo_pattern_add_color_stop_rgba(pat, 1, foci->r, foci->g, foci->b, 1);
  cairo_arc(cr, cx, cy, h/2., 0, 2*G_PI);
  cairo_set_source(cr, pat);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);
  {
    gint i;
    cairo_set_source_rgb(cr, foci->r, foci->g, foci->b);
    for(i = 1; i <= 3; i++) {
      cairo_move_to(cr, w/8., i*h/4.);
      cairo_rel_line_to(cr, w*6./8., 0);
    }
  }
  cairo_stroke(cr);

#if 0
  /** outer border **/
  cairo_move_to(cr, 0, 0);
  cairo_set_source_rgb(cr, bg_r,bg_g,bg_b);
  cairo_rel_line_to(cr, w, 0);
  cairo_rel_line_to(cr, 0, h);
  cairo_rel_line_to(cr, -w, 0);
  cairo_close_path(cr);
  cairo_stroke(cr);
#endif
  cairo_destroy(cr);

  /* Status indicator */
  {
    gint i;
    gint n = G_N_ELEMENTS(scr->fr_info.stat);

    for(i = 0; i < n; i++)
      scr->fr_info.stat[i]  = _draw_stat_indicator(dpy, scr, w, h, foci,
          /*i && 3*i<n ? 0.333:*/ (gdouble)i/n);
  }
}

/* SHAPE */

void wframe_set_shape(gswm_t *gsw, client_t *c)
{
#ifdef HAVE_XSHAPE
  XRectangle temp, *rect;
  gint th = c->wframe->theight;
  gint n = 0, order = 0;

  TRACE(("%s w=%ld", __func__, c->win));
  rect = XShapeGetRectangles(gsw->display, c->win, ShapeBounding, &n, &order);
  if(n > 1) {
    XShapeCombineShape(gsw->display, c->wframe->win, ShapeBounding,
        0, th, c->win, ShapeBounding, ShapeSet);

    temp.x = -c->wframe->bwidth;
    temp.y = -c->wframe->bwidth;
    temp.width = c->width + 2 * c->wframe->bwidth;
    temp.height = th + c->wframe->bwidth;
    XShapeCombineRectangles(gsw->display, c->wframe->win, ShapeBounding,
        0, 0, &temp, 1, ShapeUnion, YXBanded);

    temp.x = 0;
    temp.y = 0;
    temp.width = c->width;
    temp.height = th - c->wframe->bwidth;
    XShapeCombineRectangles(gsw->display, c->wframe->win, ShapeClip,
        0, th, &temp, 1, ShapeUnion, YXBanded);
  }
  X_FREE(rect);
#endif
}

client_t *wframe_lookup_client_for_window(gswm_t *gsw, Window frame_win)
{
  wframe_t *frame = g_hash_table_lookup(gsw->win2frame_hash, GUINT_TO_POINTER(frame_win));

  TRACE(("%s called for frame=%p", __func__, frame));
  return frame ? wframe_get_active_client(gsw, frame): NULL;
}

void wframe_foreach(gswm_t *gsw, wframe_t *frame, wframe_func func, gpointer user_data)
{
  GList *cle;

  for(cle = g_list_first(frame->client_list); cle; cle = g_list_next(cle))
    func(gsw, cle->data, user_data);
}

client_t *wframe_get_active_client(gswm_t *gsw, wframe_t *frame)
{
  g_assert(frame->client_list);
  /* First client in list is active one */
  return frame->client_list->data;
}

client_t *wframe_get_next_active_client(gswm_t *gsw, wframe_t *frame)
{
  g_assert(frame->client_list);
  return g_list_last(frame->client_list)->data;
}

client_t *wframe_get_previous_active_client(gswm_t *gsw, wframe_t *frame)
{
  GList *cle;
  g_assert(frame->client_list);
  cle = g_list_next(frame->client_list);
  return cle ? cle->data: NULL;
}

gboolean wframe_has_focus(gswm_t *gsw, wframe_t *frame)
{
  Window focus_ret;
  gint revert_to_ret;
  client_t *fc;

  XGetInputFocus(gsw->display, &focus_ret, &revert_to_ret);
  fc = wframe_get_active_client(gsw, frame);
  return fc->win == focus_ret;
}

wframe_t *wframe_get_focused(gswm_t *gsw)
{
  Window focus_ret;
  gint revert_to_ret;
  client_t *fc;

  XGetInputFocus(gsw->display, &focus_ret, &revert_to_ret);
  fc = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(focus_ret));
  return fc ? fc->wframe: NULL;
}

gboolean wframe_houses_multiple_clients(gswm_t *gsw, wframe_t *frame)
{
  /* Fast method to check if more than one element is in a list */
  return NULL != g_list_next(frame->client_list);
}

void wframe_set_client_active(gswm_t *gsw, client_t *c)
{
  GList *cle;
  client_t *actcl = wframe_get_active_client(gsw, c->wframe);

  if(actcl != c) {
    gint i = g_list_length(c->wframe->client_list);
    Window *wl = g_newa(Window, i);

    clear_vdesk_focused_client(gsw, actcl);
    c->wframe->client_list = g_list_remove(c->wframe->client_list, c);
    c->wframe->client_list = g_list_prepend(c->wframe->client_list, c);
    for(i = 0, cle = g_list_first(c->wframe->client_list); cle; cle = g_list_next(cle))
      wl[i++] = ((client_t*)cle->data)->win;
    XRestackWindows(gsw->display, wl, i);
  }
}

typedef struct {
  gswm_t *gsw;
  screen_t *scr;
  gint size, dsize;
} _stat_indic_t;

static void _refresh_status_indicator(gpointer key, gpointer value, gpointer udata)
{
  gint idx;
  wframe_t *fr = (wframe_t*)value;
  _stat_indic_t *sti = (_stat_indic_t*)udata;

  if(sti->dsize < sti->size)
    idx = (G_N_ELEMENTS(sti->scr->fr_info.stat) * sti->dsize) / sti->size;
  else
    idx = G_N_ELEMENTS(sti->scr->fr_info.stat) - 1;
  if(!idx && sti->dsize)
    idx = 1;

  XSetWindowBackgroundPixmap(sti->gsw->display, fr->statwin, sti->scr->fr_info.stat[idx]);
  XClearWindow(sti->gsw->display, fr->statwin);
}

void wframe_update_stat_indicator(gswm_t *gsw)
{
  _stat_indic_t sti;

  sti.gsw = gsw;
  sti.scr = gsw->screen + gsw->i_curr_scr;
  sti.size = g_hash_table_size(gsw->fid2frame_hash);
  sti.dsize = g_list_length(sti.scr->detached_frlist);
  g_hash_table_foreach(gsw->fid2frame_hash, _refresh_status_indicator, &sti);
}
