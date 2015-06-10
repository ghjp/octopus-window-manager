#define G_LOG_DOMAIN "winaction"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "octopus.h"
#include "winactions.h"
#include "client.h"
#include "ewmh.h"
#include "winframe.h"
#include "events.h"
#include "xinerama.h"

#define MouseMask (ButtonMask|PointerMotionMask)

#define setmouse(w, x, y) XWarpPointer(dpy, None, w, 0, 0, 0, 0, x, y)
#define grab(w, mask, curs) (XGrabPointer(dpy, w, False, mask, \
      GrabModeAsync, GrabModeSync, None, curs, CurrentTime) == GrabSuccess)
#define ungrab() XUngrabPointer(dpy, CurrentTime)

/* During opaque move/resize mode we only redraw the clients after the given
   millisecond distance */
#define OPAQUE_CHECK_DISTANCE 100

/* Modes to call _get_incsize with */
enum {
  SIZE_PIXELS,
  SIZE_LOGICAL
};

/* If the window in question has a ResizeInc hint, then it wants to be
 * resized in multiples of some (x,y). If we are calculating a new
 * window size, we set mode == SIZE_PIXELS and get the correct
 * width and height back. If we are drawing a friendly label on the
 * screen for the user, we set mode == SIZE_LOGICAL so that they
 * see the geometry in human-readable form (80x25 for xterm, etc). */

static gint _get_incsize(gswm_t *gsw, client_t *c, gint *x_ret, gint *y_ret, gint mode)
{
  gint width_inc, height_inc;
  gint base_width, base_height;

  if(c->wframe->xsize.flags & PResizeInc) {
    width_inc = c->wframe->xsize.width_inc ? c->wframe->xsize.width_inc : 1;
    height_inc = c->wframe->xsize.height_inc ? c->wframe->xsize.height_inc : 1;

    if(PBaseSize & c->wframe->xsize.flags) {
      base_width = c->wframe->xsize.base_width;
      base_height = c->wframe->xsize.base_height;
    }
    else if(PMinSize & c->wframe->xsize.flags) {
      base_width = c->wframe->xsize.min_width;
      base_height = c->wframe->xsize.min_height;
    }
    else
      base_width = base_height = 0;

    switch(mode) {
      case SIZE_PIXELS:
        *x_ret = c->width - ((c->width - base_width) % width_inc);
        *y_ret = c->height - ((c->height - base_height) % height_inc);
        break;
      case SIZE_LOGICAL:
        *x_ret = (c->width - base_width) / width_inc;
        *y_ret = (c->height - base_height) / height_inc;
        break;
      default:
        g_return_val_if_reached(0);
        break;
    }
    return 1;
  }
  return 0;
}

static void _draw_outline(gswm_t *gsw, client_t *c, gboolean frameop)
{
  XRectangle rec[2];
  gchar buf[128];
  gint blen, txt_w, txt_h;
  Display *dpy = gsw->display;
  screen_t *scr = c->curr_screen;
  gint th = c->wframe->theight;
  gint bw2 = GET_BORDER_WIDTH(c) / 2;

  rec[0].x = c->x + bw2;
  rec[0].y = c->y - th + bw2;
  rec[0].width = c->width + 2*bw2;
  rec[0].height = c->height + th + 2*bw2;
  blen = 1;
  if(G_UNLIKELY(frameop)) {
    rec[1].x = c->x + c->width/4;
    rec[1].y = c->y + c->height/4;
    rec[1].width = c->width/2;
    rec[1].height = c->height/2;
    blen = 2;
  }
  XDrawRectangles(dpy, scr->rootwin, scr->fr_info.gc_invert, rec, blen);
  if(G_LIKELY(c->wstate.mwm_title && bw2)) /* MWM */
    XDrawLine(dpy, scr->rootwin, scr->fr_info.gc_invert, c->x + 2*bw2, c->y + bw2,
        c->x + c->width + 2*bw2, c->y + bw2);

  if(!_get_incsize(gsw, c, &txt_w, &txt_h, SIZE_LOGICAL)) {
    txt_w = c->width;
    txt_h = c->height;
  }

  gravitate(gsw, c, GRAV_UNDO);
  if(G_UNLIKELY(c->wstate.fullscreen))
    blen = g_snprintf(buf, sizeof(buf), "%dx%d%+d%+d (FULLSCREEN)", txt_w, txt_h, c->x, c->y);
  else
    blen = g_snprintf(buf, sizeof(buf), "%dx%d%+d%+d", txt_w, txt_h, c->x, c->y);
  gravitate(gsw, c, GRAV_APPLY);
  txt_w = (c->width - XTextWidth(gsw->font, buf, blen) - scr->fr_info.padding)/2;
  if(G_LIKELY(0 < txt_w))
    XDrawString(dpy, scr->rootwin, scr->fr_info.gc_invert,
        c->x + txt_w,
        c->y + c->height/2,
        buf, blen);
}

#define ABSMIN(a, b) (ABS(a) < ABS(b) ? (a): (b))

static void _snap_to_clients(client_t *c, GList *cl, gint snap_val, gboolean *x_snapped, gboolean *y_snapped)
{
  register gint dx, dy;
  gint old_bwidth;
  gint th = c->wframe->theight;

  if(*x_snapped && *y_snapped)
    return;

  old_bwidth = c->wframe->bwidth;
  /* For borderless clients we simulate a border width of zero */
  if(TEST_BORDERLESS(c))
    c->wframe->bwidth = 0;
  for(dx = dy = snap_val; cl; cl = g_list_next(cl)) {
    register gint val, cp1, cp2, cip1, cip2, bdiff;
    client_t *ci = cl->data;
    gint bwidth_sav = ci->wframe->bwidth;

    if(ci->wframe == c->wframe || ci->wstate.below || ci->w_type.kde_override)
      continue;
    else if(TEST_BORDERLESS(ci)) /* Borderless client */
      ci->wframe->bwidth = 0;

    if(!*x_snapped && ci->y - ci->wframe->bwidth - c->wframe->bwidth - c->height - c->y <= snap_val &&
        c->y - c->wframe->bwidth - ci->wframe->bwidth - ci->height - ci->y <= snap_val) {
      cp1 = c->x - c->wframe->bwidth;
      cp2 = c->x + c->width + c->wframe->bwidth;
      cip1 = ci->x - ci->wframe->bwidth;
      cip2 = ci->x + ci->width + ci->wframe->bwidth;
      bdiff = ci->wframe->bwidth - c->wframe->bwidth;

      val = cip2 - cp1 + bdiff;
      dx = ABSMIN(dx, val);
      val = cip2 - cp2 + bdiff;
      dx = ABSMIN(dx, val);
      val = cip1 - cp2 + bdiff;
      dx = ABSMIN(dx, val);
      val = cip1 - cp1 + bdiff;
      dx = ABSMIN(dx, val);
    }
    if(!*y_snapped && ci->x - ci->wframe->bwidth - c->wframe->bwidth - c->width - c->x <= snap_val &&
        c->x - c->wframe->bwidth - ci->wframe->bwidth - ci->width - ci->x <= snap_val) {
      gint thdiff = ci->wframe->theight - th;
      cp1 = c->y - th - c->wframe->bwidth;
      cp2 = c->y + c->height + c->wframe->bwidth;
      cip1 = ci->y - th - ci->wframe->bwidth;
      cip2 = ci->y + ci->height + ci->wframe->bwidth;
      bdiff = ci->wframe->bwidth - c->wframe->bwidth;

      /*val = ci->y + ci->height - c->y + c->wframe->bwidth + ci->wframe->bwidth + th;*/
      val = cip2 - cp1 + bdiff;
      dy = ABSMIN(dy, val);
      /*val = ci->y + ci->height - c->y - c->height;*/
      val = cip2 - cp2 + bdiff;
      dy = ABSMIN(dy, val);
      /*val = ci->y - c->y - c->height - c->wframe->bwidth - ci->wframe->bwidth - th;*/
      val = cip1 - cp2 + bdiff - thdiff;
      dy = ABSMIN(dy, val);
      /*val = ci->y - c->y;*/
      val = cip1 - cp1 + bdiff - thdiff;
      dy = ABSMIN(dy, val);
    }
    ci->wframe->bwidth = bwidth_sav; /* Restore original border */
  }
  if(!*x_snapped && ABS(dx) < snap_val) {
    c->x += dx;
    *x_snapped = TRUE;
  }
  if(!*y_snapped && ABS(dy) < snap_val) {
    c->y += dy;
    *y_snapped = TRUE;
  }
  /* Restore original border width */
  c->wframe->bwidth = old_bwidth;
}

#undef ABSMIN

void wa_snap_to_borders(gswm_t *gsw, client_t *c, gboolean skip_client_snapping)
{
  gint snap_val;
  gint bw2 = 2 * GET_BORDER_WIDTH(c);
  screen_t *scr = c->curr_screen;
  vdesk_t *vd = scr->vdesk + scr->current_vdesk;
  strut_t *cst = &vd->master_strut;
  gint th = c->wframe->theight;
  gint x1 = c->x - cst->west;
  gint x2 = c->x + c->width + bw2 + cst->east - scr->dpy_width;
  gint y1 = c->y - th - cst->north;
  gint y2 = c->y + c->height + bw2 + cst->south - scr->dpy_height;
  gboolean x_snapped = FALSE, y_snapped = FALSE;

  if(0 < (snap_val = gsw->ucfg.border_snap)) { /* Border snapping turned on */
    /* Snap to workarea */
    if(ABS(x1) < snap_val) {
      c->x = cst->west;
      x_snapped = TRUE;
    }
    else if(ABS(x2) < snap_val) {
      c->x = scr->dpy_width - c->width - bw2 - cst->east;
      x_snapped = TRUE;
    }
    if(ABS(y1) < snap_val) {
      c->y = th + cst->north;
      y_snapped = TRUE;
    }
    else if(ABS(y2) < snap_val) {
      c->y = scr->dpy_height - c->height - bw2 - cst->south;
      y_snapped = TRUE;
    }

    /* Snap to xinerama borders */
    {
      rect_t mon_rect;
      gint mon = xinerama_monitor_count();
      while(--mon >= 0) {
        if(xinerama_scrdims(scr, mon, &mon_rect)) {
          if(!x_snapped) {
            x1 = c->x - mon_rect.x1;
            x2 = c->x + c->width + bw2 - mon_rect.x1;
            if(ABS(x1) < snap_val) {
              c->x = mon_rect.x1;
              x_snapped = TRUE;
            }
            else if(ABS(x2) < snap_val) {
              c->x = mon_rect.x1 - c->width - bw2;
              x_snapped = TRUE;
            }
          }
          if(!y_snapped) {
            y1 = c->y - th - mon_rect.y1;
            y2 = c->y + c->height + bw2 - mon_rect.y1;
            if(ABS(y1) < snap_val) {
              c->y = th + mon_rect.y1;
              y_snapped = TRUE;
            }
            else if(ABS(y2) < snap_val) {
              c->y = mon_rect.y1 - c->height - bw2;
              y_snapped = TRUE;
            }
          }
        }
      }
    }

    /* Snap to screen border */
    if(!x_snapped) {
      x1 = c->x;
      x2 = c->x + c->width + bw2 - scr->dpy_width;
      if(ABS(x1) < snap_val) {
        c->x = 0;
        x_snapped = TRUE;
      }
      else if(ABS(x2) < snap_val) {
        c->x = scr->dpy_width - c->width - bw2;
        x_snapped = TRUE;
      }
    }
    if(!y_snapped) {
      y1 = c->y - th;
      y2 = c->y + c->height + bw2 - scr->dpy_height;
      if(ABS(y1) < snap_val) {
        c->y = th;
        y_snapped = TRUE;
      }
      else if(ABS(y2) < snap_val) {
        c->y = scr->dpy_height - c->height - bw2;
        y_snapped = TRUE;
      }
    }

    if(x_snapped && y_snapped) /* All snapped already */
      return;
  }

  /* Client snapping desired */
  if(!skip_client_snapping && 0 < (snap_val = gsw->ucfg.client_snap)) {
    _snap_to_clients(c, vd->clnt_list,    snap_val, &x_snapped, &y_snapped);
    _snap_to_clients(c, scr->sticky_list, snap_val, &x_snapped, &y_snapped);
  }
}

static void _drag(gswm_t *gsw, client_t *c, Cursor cursor)
{
  XEvent ev;
  gint x1, y1;
  gint orig_cx = c->x;
  gint orig_cy = c->y;
  gint x_last = c->x;
  gint y_last = c->y;
  gint w_last = c->width;
  gint h_last = c->height;
  screen_t *scr = c->curr_screen;
  Display *dpy = gsw->display;
  gboolean frameop = cursor == gsw->curs.framemove;
  Time lasttime = 0;

  if(!grab(scr->rootwin, MouseMask, cursor))
    return;
  if(!gsw->ucfg.move_opaque)
    XGrabServer(dpy);
  get_mouse_position(gsw, &x1, &y1);

  if(!gsw->ucfg.move_opaque)
    _draw_outline(gsw, c, frameop);
  while(TRUE) {
    XMaskEvent(dpy, MouseMask, &ev);
    switch (ev.type) {
      case MotionNotify:
        /* Compress move events */
        while(XCheckTypedEvent(dpy, MotionNotify, &ev))
          /* DO NOTHING */ ;
        if(!gsw->ucfg.move_opaque)
          _draw_outline(gsw, c, frameop); /* clear */
        c->x = orig_cx + (ev.xmotion.x - x1);
        c->y = orig_cy + (ev.xmotion.y - y1);
        wa_snap_to_borders(gsw, c, FALSE);
        if(gsw->ucfg.move_opaque) {
          if(OPAQUE_CHECK_DISTANCE < (ev.xmotion.time - lasttime)) {
            wa_moveresize(gsw, c);
            x_last = c->x;
            y_last = c->y;
            w_last = c->width;
            h_last = c->height;
          }
        }
        else
          _draw_outline(gsw, c, frameop);
        break;
      case ButtonRelease:
        if(!gsw->ucfg.move_opaque) {
          _draw_outline(gsw, c, frameop); /* clear */
          XUngrabServer(dpy);
        }
        else {
          c->x = x_last;
          c->y = y_last;
          c->width = w_last;
          c->height = h_last;
        }
        ungrab();
        return;
    }
  }
}

#define FLOOR(value, base)  ( ((gint) ((value) / (base))) * (base) )

static void _adapt_size_constraints(XSizeHints *xsh, gint *w, gint *h)
{
  if(xsh->flags & PMinSize) {
    if(*w < xsh->min_width) *w = xsh->min_width;
    if(*h < xsh->min_height) *h = xsh->min_height;
  }

  if(xsh->flags & PMaxSize) {
    if(xsh->max_width && *w > xsh->max_width) *w = xsh->max_width;
    if(xsh->max_height && *h > xsh->max_height) *h = xsh->max_height;
  }

  /* TODO Check the algorithm again */
  /* constrain aspect ratio, according to:
   *
   *                width     
   * min_aspect <= -------- <= max_aspect
   *                height    
   */
  if(xsh->flags & PAspect) {
    gint delta;
    gdouble min_aspect = xsh->min_aspect.x / (gdouble) xsh->min_aspect.y;
    gdouble max_aspect = xsh->max_aspect.x / (gdouble) xsh->max_aspect.y;
    gint height_inc = xsh->height_inc ? xsh->height_inc : 1;
    gint width_inc = xsh->width_inc ? xsh->width_inc : 1;

    if(min_aspect * *h > *w) {
      delta = FLOOR(*h - *w / min_aspect, height_inc);
      if(*h - delta >= xsh->min_height)
        *h -= delta;
      else {
        delta = FLOOR(*h * min_aspect - *w, width_inc);
        if(*w + delta <= xsh->max_width)
          *w += delta;
      }
    }
    if(max_aspect * *h < *w) {
      delta = FLOOR(*w - *h * max_aspect, width_inc);
      if(*w - delta >= xsh->min_width)
        *w -= delta;
      else {
        delta = FLOOR(*w / max_aspect - *h, height_inc);
        if(*h + delta <= xsh->max_height)
          *h += delta;
      }
    }
  }
}

#undef FLOOR

static void _recalculate_sweep(gswm_t *gsw, client_t *c,
    gint x1, gint y1, gint x2, gint y2, gboolean westwards, gboolean northwards)
{
	gint basex, basey, xsnap, ysnap, new_w, new_h;

  new_w = ABS(x1 - x2);
  new_h = ABS(y1 - y2);

  if(c->wframe->xsize.flags & PResizeInc) {
    gint width_inc = c->wframe->xsize.width_inc ? c->wframe->xsize.width_inc : 1;
    gint height_inc = c->wframe->xsize.height_inc ? c->wframe->xsize.height_inc : 1;

    if(PBaseSize & c->wframe->xsize.flags) {
      basex = c->wframe->xsize.base_width;
      basey = c->wframe->xsize.base_height;
    }
    else if(PMinSize & c->wframe->xsize.flags) {
      basex = c->wframe->xsize.min_width;
      basey = c->wframe->xsize.min_height;
    }
    else
      basex = basey = 0;

    xsnap = (new_w - basex) % width_inc;
    ysnap = (new_h - basey) % height_inc;
    new_w -= xsnap;
    new_h -= ysnap;
  }
  else
    xsnap = ysnap = 0;

  _adapt_size_constraints(&c->wframe->xsize, &new_w, &new_h);

  c->width = new_w;
  if(westwards)
    c->x = x1 < x2 ? x2 - new_w: x2;
  else
    c->x = x1 < x2 ? x1 : x2 + xsnap;

  c->height = new_h;
  if(northwards)
    c->y = y1 < y2 ? y2 - new_h : y2;
  else
    c->y = y1 < y2 ? y1 : y2 + ysnap;
}

static void _sweep(gswm_t *gsw, client_t *c)
{
  XEvent ev;
  Cursor curs;
  gdouble win_px_n, win_py_n;
  gint root_px, root_py, x_last, y_last, w_last, h_last;
  gint x1 = c->x;
  gint y1 = c->y;
  gint x2 = c->x + c->width;
  gint y2 = c->y + c->height;
  Display *dpy = gsw->display;
  screen_t *scr = c->curr_screen;
  Time lasttime = 0;

  /* The mouse position determines the resize direction */
  get_mouse_position(gsw, &root_px, &root_py);
  win_px_n = (gdouble)(root_px - x1) / (gdouble)c->width;
  win_py_n = (gdouble)(root_py - y1) / (gdouble)c->height;
  if(PAspect & c->xsize.flags) {
    if(0.5 > win_py_n)
      curs = 0.5 < win_px_n ? gsw->curs.resize_ne: gsw->curs.resize_nw;
    else
      curs = 0.5 < win_px_n ? gsw->curs.resize_se: gsw->curs.resize_sw;
    c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
  }
  else if(0.333 > win_py_n) {
    if(0.333 > win_px_n) {
      curs = gsw->curs.resize_nw;
      c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
    }
    else if(0.666 < win_px_n) {
      c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
      curs = gsw->curs.resize_ne;
    }
    else {
      curs = gsw->curs.resize_n;
      c->wstate.maxi_vert = FALSE;
    }
  }
  else if(0.666 < win_py_n) {
    if(0.333 > win_px_n) {
      curs = gsw->curs.resize_sw;
      c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
    }
    else if(0.666 < win_px_n) {
      curs = gsw->curs.resize_se;
      c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
    }
    else {
      curs = gsw->curs.resize_s;
      c->wstate.maxi_vert = FALSE;
    }
  }
  else {
    if(0.5 > win_px_n) 
      curs = gsw->curs.resize_w;
    else
      curs = gsw->curs.resize_e;
    c->wstate.maxi_horz = FALSE;
  }

  if(!grab(scr->rootwin, MouseMask, curs))
    return;
  if(!gsw->ucfg.resize_opaque) {
    XGrabServer(dpy); 
    _draw_outline(gsw, c, FALSE);
  }
  x_last = c->x;
  y_last = c->y;
  w_last = c->width;
  h_last = c->height;
  while(TRUE) {
    gint dx, dy;
    XMaskEvent(dpy, MouseMask, &ev);
    switch(ev.type) {
      case MotionNotify:
        /* Compress move events */
        while(XCheckTypedEvent(dpy, MotionNotify, &ev))
          /* DO NOTHING */ ;
        dx = ev.xmotion.x_root - root_px;
        dy = ev.xmotion.y_root - root_py;
        if(c->wstate.fullscreen)
          dx = dy = 0;
        if(!gsw->ucfg.resize_opaque)
          _draw_outline(gsw, c, FALSE); /* clear */
        if(curs == gsw->curs.resize_nw) /* NorthWest */
           _recalculate_sweep(gsw, c, x1+dx, y1+dy, x2, y2, 1, 1);
        else if(curs == gsw->curs.resize_ne) /* NorthEast */
          _recalculate_sweep(gsw, c, x1, y1+dy, x2+dx, y2, 0, 1);
        else if(curs == gsw->curs.resize_sw) /* SouthWest */
          _recalculate_sweep(gsw, c, x1+dx, y1, x2, y2+dy, 1, 0);
        else if(curs == gsw->curs.resize_se) /* SouthEast */
          _recalculate_sweep(gsw, c, x1, y1, x2+dx, y2+dy, 0, 0);
        else if(curs == gsw->curs.resize_n) /* North */
          _recalculate_sweep(gsw, c, x1, y1+dy, x2, y2, 0, 1);
        else if(curs == gsw->curs.resize_s) /* South */
          _recalculate_sweep(gsw, c, x1, y1, x2, y2+dy, 0, 0);
        else if(curs == gsw->curs.resize_e) /* East */
          _recalculate_sweep(gsw, c, x1, y1, x2+dx, y2, 0, 0);
        else if(curs == gsw->curs.resize_w) /* West */
          _recalculate_sweep(gsw, c, x1+dx, y1, x2, y2, 1, 0);
        else
          g_assert_not_reached();
        if(gsw->ucfg.resize_opaque) {
          if(OPAQUE_CHECK_DISTANCE < (ev.xmotion.time - lasttime)) {
            wa_moveresize(gsw, c);
            if(gsw->shape && c->wstate.shaped)
              wframe_set_shape(gsw, c);
            x_last = c->x;
            y_last = c->y;
            w_last = c->width;
            h_last = c->height;
            lasttime = ev.xmotion.time;
          }
        }
        else
          _draw_outline(gsw, c, FALSE);
        break;
      case ButtonRelease:
        if(!gsw->ucfg.resize_opaque) {
          _draw_outline(gsw, c, FALSE); /* clear */
          XUngrabServer(dpy);
        }
        else {
            c->x = x_last;
            c->y = y_last;
            c->width = w_last;
            c->height = h_last;
        }
        ungrab();
        return;
    }
  }
}

void wa_do_all_size_constraints(gswm_t *gsw, client_t *c)
{
  /* Correct the new size by the resize increment */
  _get_incsize(gsw, c, &c->width, &c->height, SIZE_PIXELS);
  _adapt_size_constraints(&c->wframe->xsize, &c->width, &c->height);
}

#if 0
static inline void _x_warp_pointer_to_client(gswm_t *gsw, client_t *c)
{
  gint xm, ym, dmx, dmy;

  get_mouse_position(gsw, &xm, &ym);
  if(xm < c->x)
    dmx = c->x - xm;
  else if(xm > c->x+c->width)
    dmx = c->x+c->width - xm - 1;
  else
    dmx = 0;
  
  if(ym < c->y)
    dmy = c->y - ym;
  else if(ym > c->y+c->height)
    dmy = c->y+c->height - ym - 1;
  else
    dmy = 0;
  if(dmx || dmy)
    XWarpPointer(gsw->display, None, None, 0, 0, 0, 0, dmx, dmy);
}
#endif

typedef struct {
  gint x, y, w, h;
} _rec_t;

static void _apply_new_pos_client(gswm_t *gsw, client_t *cl, gpointer udata)
{
  _rec_t *r = (_rec_t*)udata;

  /* FIXME Handle win gravity */
  cl->x = r->x;
  cl->y = r->y;
  cl->width = r->w;
  cl->height = r->h;
}

static void _x_moveresize_client(gswm_t *gsw, client_t *cl, gpointer udata)
{
  TRACE("%s (%s)", __func__, cl->utf8_name);
  XMoveResizeWindow(gsw->display, cl->win, 0, cl->wframe->theight, cl->width, cl->height);
  send_config(gsw, cl);
}

static inline void _remove_all_enter_x_events(gswm_t *gsw)
{
  XEvent ev;

  XSync(gsw->display, False);
  while(XCheckTypedEvent(gsw->display, EnterNotify, &ev))
    /* Do nothing */ ;
}

static void _x_move_resize(gswm_t *gsw, client_t *c)
{
  _rec_t r;

  wframe_x_move_resize(gsw, c->wframe);
  r.x = c->x;
  r.y = c->y;
  r.w = c->width;
  r.h = c->height;
  wframe_foreach(gsw, c->wframe, _apply_new_pos_client, &r);
  wframe_foreach(gsw, c->wframe, _x_moveresize_client, NULL);
  wa_raise(gsw, c);
  _remove_all_enter_x_events(gsw);
}

void wa_hide(gswm_t *gsw, client_t *c)
{
  TRACE("%s w=%ld (%s)", __func__, c->win, c->utf8_name);
  c->ignore_unmap++;
  /*XUnmapWindow(gsw->display, c->win);*/
  XUnmapSubwindows(gsw->display, c->wframe->win);
  XUnmapWindow(gsw->display, c->wframe->win);
}

void wa_iconify(gswm_t *gsw, client_t *c)
{
  TRACE("%s w=%ld (%s)", __func__, c->win, c->utf8_name);
  wa_hide(gsw, c);
  c->wstate.hidden = TRUE;
  set_ewmh_net_wm_states(gsw, c);
  set_wm_state(gsw, c, IconicState);
}

void wa_unhide(gswm_t *gsw, client_t *c, gboolean raise)
{
  TRACE("%s w=%ld (%s)", __func__, c->win, c->utf8_name);
  if(c->wstate.sticky || c->i_vdesk == c->curr_screen->current_vdesk) {
    XMapSubwindows(gsw->display, c->wframe->win);
    raise ? XMapRaised(gsw->display, c->wframe->win): XMapWindow(gsw->display, c->wframe->win);
    c->wstate.hidden = FALSE;
  }
  else
    c->wstate.hidden = TRUE;
  set_ewmh_prop(gsw, c, gsw->xa.wm_net_wm_desktop, c->i_vdesk);
  set_ewmh_net_wm_states(gsw, c);
}

static void _apply_maxi_states(gswm_t *gsw, client_t *cl, gpointer udata)
{
  client_t *ref_c = (client_t*)udata;

  if((cl->wstate.maxi_vert = ref_c->wstate.maxi_vert)) {
    cl->y_old = ref_c->y_old;
    cl->height_old = ref_c->height_old;
    cl->height = ref_c->height;
  }
  if((cl->wstate.maxi_horz = ref_c->wstate.maxi_horz)) {
    cl->x_old = ref_c->x_old;
    cl->width_old = ref_c->width_old;
    cl->width = ref_c->width;
  }
  set_ewmh_net_wm_states(gsw, cl);
}

void wa_moveresize(gswm_t *gsw, client_t *c)
{
  wa_do_all_size_constraints(gsw, c);
  if(gsw->alpha_processing)
    wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

void wa_move_interactive(gswm_t *gsw, client_t *c)
{
  _rec_t r;
  gint x_orig, y_orig;

  if(c->w_type.kde_override)
    return;
  x_orig = c->x;
  y_orig = c->y;
  /*wa_iconify(gsw,c);*/
  _drag(gsw, c, gsw->curs.move);
  if(x_orig != c->x)
    c->wstate.fullscreen = c->wstate.maxi_horz = FALSE;
  if(y_orig != c->y)
    c->wstate.fullscreen = c->wstate.maxi_vert = FALSE;
  r.x = c->x;
  r.y = c->y;
  r.w = c->width;
  r.h = c->height;
  wframe_foreach(gsw, c->wframe, _apply_new_pos_client, &r);
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
  if(gsw->alpha_processing)
    wframe_tbar_pmap_recreate(gsw, c->wframe);
  XMoveWindow(gsw->display, c->wframe->win, c->x, c->y - c->wframe->theight);
  /*wa_unhide(gsw, c, TRUE);*/
  send_config(gsw, c);
}

client_t *wa_get_client_under_pointer(gswm_t *gsw)
{
  Window mouse_root, mouse_win;  
  gint root_x, root_y, win_x, win_y;
  guint mask;        
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  XQueryPointer(gsw->display, scr->rootwin,
      &mouse_root, &mouse_win, &root_x, &root_y, &win_x, &win_y, &mask);  
  return wframe_lookup_client_for_window(gsw, mouse_win);
}

void wa_frameop_interactive(gswm_t *gsw, client_t *c)
{
  gboolean move_opaque_save;
  client_t *c_target;
  gint x_o = c->x, y_o = c->y;

  if(c->wstate.fullscreen || c->w_type.kde_override)
    return;
  /* For frame operation we always use "wire" mode */
  move_opaque_save = gsw->ucfg.move_opaque;
  gsw->ucfg.move_opaque = FALSE;
  _drag(gsw, c, gsw->curs.framemove);
  gsw->ucfg.move_opaque = move_opaque_save;

  c_target = wa_get_client_under_pointer(gsw);
  if(c_target) { /* Attach to a selected frame */
    if(c_target != c) {
      detach(gsw, c);
      /* Restore old x,y position. If attach_first2frame failed it calls attach.
         So when attach is called, the old position is restored */
      attach_first2frame(gsw, c_target->wframe);
    }
    else {
      c->x = x_o;
      c->y = y_o;
    }
  }
  else { /* Detach from a client frame */
    if(wframe_houses_multiple_clients(gsw, c->wframe)) {
      detach(gsw, c);
      attach(gsw, c);
    }
    else
      wa_moveresize(gsw, c);
  }
}

static void _x_move(gswm_t *gsw, client_t *c)
{
  _rec_t r;

  r.x = c->x;
  r.y = c->y;
  r.w = c->width;
  r.h = c->height;
  wframe_foreach(gsw, c->wframe, _apply_new_pos_client, &r);
  if(gsw->alpha_processing)
    wframe_tbar_pmap_recreate(gsw, c->wframe);
  XMoveWindow(gsw->display, c->wframe->win, c->x, c->y - c->wframe->theight);
  send_config(gsw, c);
  _remove_all_enter_x_events(gsw);
}

void wa_move_north(gswm_t *gsw, client_t *c)
{
  screen_t *scr = c->curr_screen;
  
  if(c->wstate.fullscreen || c->w_type.kde_override)
    return;
  c->y -= (scr->dpy_height * gsw->ucfg.resize_inc_fraction) / 100;
  wa_snap_to_borders(gsw, c, FALSE);
  _x_move(gsw, c);
  if(c->wstate.maxi_vert) {
    c->wstate.maxi_vert = FALSE;
    wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
  }
}

void wa_move_south(gswm_t *gsw, client_t *c)
{
  screen_t *scr = c->curr_screen;
  
  if(c->wstate.fullscreen || c->w_type.kde_override)
    return;
  c->y += (scr->dpy_height * gsw->ucfg.resize_inc_fraction) / 100;
  wa_snap_to_borders(gsw, c, FALSE);
  _x_move(gsw, c);
  if(c->wstate.maxi_vert) {
    c->wstate.maxi_vert = FALSE;
    wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
  }
}

void wa_move_east(gswm_t *gsw, client_t *c)
{
  screen_t *scr = c->curr_screen;
  
  if(c->wstate.fullscreen || c->w_type.kde_override)
    return;
  c->x += (scr->dpy_width * gsw->ucfg.resize_inc_fraction) / 100;
  wa_snap_to_borders(gsw, c, FALSE);
  _x_move(gsw, c);
  if(c->wstate.maxi_horz) {
    c->wstate.maxi_horz = FALSE;
    wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
  }
}

void wa_move_west(gswm_t *gsw, client_t *c)
{
  screen_t *scr = c->curr_screen;
  
  if(c->wstate.fullscreen || c->w_type.kde_override)
    return;
  c->x -= (scr->dpy_width * gsw->ucfg.resize_inc_fraction) / 100;
  wa_snap_to_borders(gsw, c, FALSE);
  _x_move(gsw, c);
  if(c->wstate.maxi_horz) {
    c->wstate.maxi_horz = FALSE;
    wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
  }
}

void wa_fit_to_workarea(gswm_t *gsw, client_t *c)
{
  gint tmp_val, x2, y2;
  screen_t *scr = c->curr_screen;
  vdesk_t *vd = scr->vdesk + scr->current_vdesk;
  gint bw = GET_BORDER_WIDTH(c);

  xinerama_correctloc(c);
  x2 = c->x + c->width;
  y2 = c->y + c->height;

x_correct:
  if(vd->warea.x > c->x) {
    x2 -= c->x - vd->warea.x;
    c->x = vd->warea.x;
    if(x2 >= vd->warea.w)
      x2 = vd->warea.x + vd->warea.w - 2*bw;
  }
  else {
    tmp_val = vd->warea.x + vd->warea.w;
    if(x2 > tmp_val) {
      x2 = tmp_val - 2*bw;
      c->x = x2 - c->width;
      goto x_correct;
    }
  }
  c->width = x2 - c->x;

y_correct:
  tmp_val = vd->warea.y + c->wframe->theight;
  if(tmp_val > c->y) {
    c->y = tmp_val;
    y2 = c->y + c->height;

    tmp_val = vd->warea.y + vd->warea.h;
    if(y2 >= tmp_val)
      y2 = tmp_val - 2*bw;
  }
  else {
    tmp_val = vd->warea.y + vd->warea.h;
    if(y2 > tmp_val) {
      y2 = tmp_val - 2*bw;
      c->y = y2 - c->height - 2*bw;
      goto y_correct;
    }
  }
  c->height = y2 - c->y;

  wa_do_all_size_constraints(gsw, c);
  wa_snap_to_borders(gsw, c, FALSE);
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

void wa_resize_interactive(gswm_t *gsw, client_t *c)
{
  gint old_width = c->width;

  if(c->w_type.kde_override)
    return;
  _sweep(gsw, c);
  wa_do_all_size_constraints(gsw, c);
  if(G_LIKELY(c->width != old_width || gsw->alpha_processing))
    wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

static inline void _modify_size_east(gswm_t *gsw, client_t *c, gboolean grow)
{
  gint east_inc, new_width, sw;

  /* The argument east_inc is a dummy here. We don't need the height */
  xinerama_get_screensize_on_which_client_resides(c, &sw, &east_inc);
  east_inc = (sw * gsw->ucfg.resize_inc_fraction) / 100;
  if(!grow)
    east_inc = -east_inc;
  if(0 > (new_width = c->width + east_inc))
    return;
  c->width = new_width;
}

static inline void _modify_size_south(gswm_t *gsw, client_t *c, gboolean grow)
{
  gint south_inc, sh, new_height;

  /* The argument south_inc is a dummy here. We don't need the width */
  xinerama_get_screensize_on_which_client_resides(c, &south_inc, &sh);
  south_inc = (sh * gsw->ucfg.resize_inc_fraction) / 100;
  if(!grow)
    south_inc = -south_inc;
  if(0 > (new_height = c->height + south_inc))
    return;
  c->height = new_height;
}

static inline void _modify_size_north(gswm_t *gsw, client_t *c, gboolean grow)
{
  gint north_inc, sh, new_height;

  /* The argument north_inc is a dummy here. We don't need the width */
  xinerama_get_screensize_on_which_client_resides(c, &north_inc, &sh);
  north_inc = (sh * gsw->ucfg.resize_inc_fraction) / 100;
  if(!grow)
    north_inc = -north_inc;
  if(0 > (new_height = c->height + north_inc))
      return;
  c->height = new_height;
  c->y -= north_inc;
}

static inline void _modify_size_west(gswm_t *gsw, client_t *c, gboolean grow)
{
  gint west_inc, sw, new_width;

  /* The argument west_inc is a dummy here. We don't need the height */
  xinerama_get_screensize_on_which_client_resides(c, &sw, &west_inc);
  west_inc = (sw * gsw->ucfg.resize_inc_fraction) / 100;
  if(!grow)
    west_inc = -west_inc;
  if(0 > (new_width = c->width + west_inc))
    return;
  c->width = new_width;
  c->x -= west_inc;
}

void wa_resize_north(gswm_t *gsw, client_t *c, gboolean grow)
{
  gint y2old = c->y + c->height;

  _modify_size_north(gsw, c, grow);
  wa_do_all_size_constraints(gsw, c);
  /* If client has size constraints then stay on the south base line */
  c->y += y2old - (c->y + c->height);
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  /* No longer maximized */
  c->wstate.maxi_vert = FALSE;
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

void wa_resize_west(gswm_t *gsw, client_t *c, gboolean grow)
{
  gint x2old = c->x + c->width;

  _modify_size_west(gsw, c, grow);
  wa_do_all_size_constraints(gsw, c);
  /* If client has size constraints then stay on the east base line */
  c->x += x2old - (c->x + c->width);
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  c->wstate.maxi_horz = FALSE;
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

void wa_resize_east(gswm_t *gsw, client_t *c, gboolean grow)
{
  _modify_size_east(gsw, c, grow);
  wa_do_all_size_constraints(gsw, c);
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  c->wstate.maxi_horz = FALSE;
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

void wa_resize_south(gswm_t *gsw, client_t *c, gboolean grow)
{
  _modify_size_south(gsw, c, grow);
  wa_do_all_size_constraints(gsw, c);
  _x_move_resize(gsw, c);
  c->wstate.maxi_vert = FALSE;
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

void wa_resize_north_east(gswm_t *gsw, client_t *c, gboolean grow)
{
  gint y2old = c->y + c->height;

  _modify_size_north(gsw, c, grow);
  _modify_size_east(gsw, c, grow);
  wa_do_all_size_constraints(gsw, c);
  /* If client has size constraints then stay on the south base line */
  c->y += y2old - (c->y + c->height);
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

void wa_resize_north_west(gswm_t *gsw, client_t *c, gboolean grow)
{
  gint x2old = c->x + c->width;
  gint y2old = c->y + c->height;

  _modify_size_north(gsw, c, grow);
  _modify_size_west(gsw, c, grow);
  wa_do_all_size_constraints(gsw, c);
  /* If client has size constraints then stay on the east base line */
  c->x += x2old - (c->x + c->width);
  /* If client has size constraints then stay on the south base line */
  c->y += y2old - (c->y + c->height);
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

void wa_resize_south_west(gswm_t *gsw, client_t *c, gboolean grow)
{
  _modify_size_south(gsw, c, grow);
  _modify_size_west(gsw, c, grow);
  wa_do_all_size_constraints(gsw, c);
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

void wa_resize_south_east(gswm_t *gsw, client_t *c, gboolean grow)
{
  _modify_size_south(gsw, c, grow);
  _modify_size_east(gsw, c, grow);
  wa_do_all_size_constraints(gsw, c);
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  c->wstate.maxi_vert = c->wstate.maxi_horz = FALSE;
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

void _apply_maxi_horz(gswm_t *gsw, client_t *c, gboolean on)
{
  screen_t *scr = c->curr_screen;
  vdesk_t *vd = scr->vdesk + scr->current_vdesk;
  gint bw = GET_BORDER_WIDTH(c);

  if(c->wstate.fullscreen)
    return;
  if(on) {
    if(c->wstate.maxi_horz) /* Already maximized */
      return;
    c->x_old = c->x;
    c->width_old = c->width;
    c->wstate.maxi_horz = TRUE;
    if(!xinerama_maximize(c)) {
      c->x = vd->warea.x;
      c->width = vd->warea.w - 2 * bw;
    }
  }
  else {
    if(!c->wstate.maxi_horz)
      return;
    c->x = c->x_old;
    c->width = c->width_old;
    c->wstate.maxi_horz = FALSE;
  }
}

void _apply_maxi_vert(gswm_t *gsw, client_t *c, gboolean on)
{
  screen_t *scr = c->curr_screen;
  vdesk_t *vd = scr->vdesk + scr->current_vdesk;
  gint th = c->wframe->theight;
  gint bw = GET_BORDER_WIDTH(c);

  if(c->wstate.fullscreen)
    return;
  if(on) {
    if(c->wstate.maxi_vert) /* Already maximized */
      return;
    c->y_old = c->y;
    c->height_old = c->height;
    c->wstate.maxi_vert = TRUE;
    if(!xinerama_maximize(c)) {
      c->y = th + vd->warea.y;
      c->height = vd->warea.h - th - 2 * bw;
    }
  }
  else {
    if(!c->wstate.maxi_vert)
      return;
    c->y = c->y_old;
    c->height = c->height_old;
    c->wstate.maxi_vert = FALSE;
  }
}

void wa_maximize_hv(gswm_t *gsw, client_t *c, gboolean horz, gboolean vert)
{
  TRACE("%s horz=%d vert=%d", __func__, horz, vert);
  if(c->w_type.dock || c->wstate.fullscreen || c->w_type.kde_override)
    return;
  if(horz)
    _apply_maxi_horz(gsw, c, TRUE);
  if(vert)
    _apply_maxi_vert(gsw, c, TRUE);
  set_ewmh_net_wm_states(gsw, c);
  wa_do_all_size_constraints(gsw, c);
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

void wa_unmaximize_hv(gswm_t *gsw, client_t *c, gboolean horz, gboolean vert)
{
  TRACE("%s horz=%d vert=%d", __func__, horz, vert);
  if(horz)
    _apply_maxi_horz(gsw, c, FALSE);
  if(vert)
    _apply_maxi_vert(gsw, c, FALSE);
  set_ewmh_net_wm_states(gsw, c);
  wa_do_all_size_constraints(gsw, c);
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
}

static void _apply_fullscreen_state(gswm_t *gsw, client_t *cl, gpointer udata)
{
  client_t *ref_c = (client_t*)udata;

  cl->wstate.fullscreen = ref_c->wstate.fullscreen;
  set_ewmh_net_wm_states(gsw, cl);
}

void wa_fullscreen(gswm_t *gsw, client_t *c, gboolean on)
{
  screen_t *scr = c->curr_screen;
  gint bw = GET_BORDER_WIDTH(c);

  if(c->w_type.kde_override)
    return;
  if(on) { /* Switch on fullscreen mode */
    if(c->wstate.fullscreen)
      return;
    if(!(c->wstate.maxi_horz || c->wstate.maxi_vert)) {
      c->x_old = c->x;
      c->y_old = c->y;
      c->width_old = c->width;
      c->height_old = c->height;
    }
    else {
      c->wstate.maxi_horz = c->wstate.maxi_vert = FALSE;
      wframe_foreach(gsw, c->wframe, _apply_maxi_states, c);
    }
    c->x = c->y = -bw;
    c->width = scr->dpy_width;
    c->height = scr->dpy_height;
    c->wstate.fullscreen = TRUE;
  }
  else { /* Switch off fullscreen mode */
    if(!c->wstate.fullscreen)
      return;
    c->x = c->x_old;
    c->y = c->y_old;
    c->width = c->width_old;
    c->height = c->height_old;
    c->wstate.fullscreen = FALSE;
    wa_do_all_size_constraints(gsw, c);
  }
  wframe_tbar_pmap_recreate(gsw, c->wframe);
  _x_move_resize(gsw, c);
  wframe_foreach(gsw, c->wframe, _apply_fullscreen_state, c);
  if(on)
    focus_client(gsw, c, TRUE);
}

void wa_send_xclimsg(gswm_t *gsw, client_t *c, Atom type,
    glong l0, glong l1, glong l2, glong l3, glong l4)
{
  XClientMessageEvent e;

  e.type = ClientMessage;
  e.window = c->win;
  e.message_type = type;
  e.format = 32;
  e.data.l[0] = l0;
  e.data.l[1] = l1;
  e.data.l[2] = l2;
  e.data.l[3] = l3;
  e.data.l[4] = l4;

  XSendEvent(gsw->display, gsw->screen[gsw->i_curr_scr].rootwin,
      False, SubstructureNotifyMask|SubstructureRedirectMask, (XEvent *)&e);
}

void wa_send_xclimsgwm(gswm_t *gsw, client_t *c, Atom type, Atom arg)
{
  XClientMessageEvent e;

  e.type = ClientMessage;
  e.window = c->win;
  e.message_type = type;
  e.format = 32;
  e.data.l[0] = arg;
  e.data.l[1] = CurrentTime;

  XSendEvent(gsw->display, c->win, False, NoEventMask, (XEvent *)&e);
}

/* The name of this function is a bit misleading: if the client
 * doesn't listen to WM_DELETE then we just terminate it with extreme
 * prejudice. */

void wa_send_wm_delete(gswm_t *gsw, client_t *c)
{
  if(G_LIKELY(c->wstate.pr_delete))
    wa_send_xclimsgwm(gsw, c, gsw->xa.wm_protocols, gsw->xa.wm_delete);
  else
    XKillClient(gsw->display, c->win);
}

void wa_sticky(gswm_t *gsw, client_t *c, gboolean on)
{
  if(on) {
    if(!c->wstate.sticky) {
      TRACE("%s wm_net_wm_state sticky add", __func__);
      detach(gsw, c);
      c->i_vdesk = STICKY;
      c->wstate.sticky = TRUE;
      attach(gsw, c);
    }
  }
  else {
    if(c->wstate.sticky) {
      TRACE("%s wm_net_wm_state sticky remove", __func__);
      detach(gsw, c);
      c->wstate.sticky = FALSE;
      c->i_vdesk = c->curr_screen->current_vdesk;
      attach(gsw, c);
    }
  }
  set_ewmh_net_wm_states(gsw, c);
}

void wa_lower(gswm_t *gsw, client_t *fc)
{
  Window wl[2];
  GList *cl;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  wl[0] = fc->wframe->win;
  XLowerWindow(gsw->display, wl[0]);
  cl = g_list_first(scr->desktop_list);
  if(cl) {
    client_t *c_desk = (client_t*)cl->data;
    wl[1] = c_desk->wframe->win;
    XRestackWindows(gsw->display, wl, G_N_ELEMENTS(wl));
  }
}
