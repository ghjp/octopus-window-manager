#define G_LOG_DOMAIN "osd_cli"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "octopus.h"
#include "osd_cli.h"
#include "client.h"
#include "xinerama.h"

#include <X11/extensions/shape.h>
#include <cairo-xlib.h>

osd_cli_t *osd_cli_create(gswm_t *gsw)
{
  Window rootwin;
  gint swidth, sheight;
  Display *dpy = gsw->display;
  screen_t *screen = gsw->screen + gsw->i_curr_scr;
  gint scr = screen->id;
  osd_cli_t *obj = g_new0(osd_cli_t, 1);

  obj->gsw = gsw;

  rootwin = screen->rootwin;
  swidth = screen->dpy_width;
  sheight = screen->dpy_height;
  obj->iw = swidth;
  obj->ih = gsw->ucfg.osd_height;
  TRACE("scr=%d swidth=%d sheight=%d", scr, swidth, sheight);

  if(TRUE) {
    XSetWindowAttributes xwinattr = {
      .override_redirect = True,
    };
    obj->win = XCreateWindow(dpy, rootwin, 0, 0 /*sheight - obj->ih*/, obj->iw, obj->ih, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect, &xwinattr);
  }
  else
    obj->win = XCreateSimpleWindow(dpy, rootwin, 0, sheight - obj->ih, obj->iw, obj->ih, 0,
        BlackPixel(dpy, scr), BlackPixel(dpy, scr));
  osd_cli_set_text(obj, "OSD CLI interface " PACKAGE_STRING);
  XStoreName(dpy, obj->win, "OSD CLI interface " PACKAGE_STRING);
  return obj;
}

void osd_cli_destroy(osd_cli_t *obj)
{
  g_return_if_fail(obj);
  XDestroyWindow(obj->gsw->display, obj->win);
  g_free(obj);
}

void osd_cli_show(osd_cli_t *obj)
{
  g_return_if_fail(obj);
  XMapRaised(obj->gsw->display, obj->win);
  //XFlush(obj->dpy);
}

void osd_cli_hide(osd_cli_t *obj)
{
  g_return_if_fail(obj);
  XUnmapWindow(obj->gsw->display, obj->win);
}

void osd_cli_set_text(osd_cli_t *obj, gchar *text)
{
  Display *dpy;
  guint iw, ih;
  screen_t *screen;
  gint scr;
  Pixmap mask_bitmap, pmap = None;

  g_return_if_fail(obj && text);

  dpy = obj->gsw->display;
  iw = obj->iw;
  ih = obj->ih;
  screen = obj->gsw->screen + obj->gsw->i_curr_scr;
  scr = screen->id;

  /* Construct window shape */
  if(G_LIKELY((mask_bitmap = XCreatePixmap(dpy, obj->win, iw, ih, 1)))) {
    G_GNUC_UNUSED gdouble tw, th;
    gdouble tx, ty;
    cairo_text_extents_t extents;
    gchar *mask_font = obj->gsw->ucfg.osd_font;
    cairo_surface_t *mask_surface = cairo_xlib_surface_create_for_bitmap(
        dpy, mask_bitmap, ScreenOfDisplay(dpy, scr), iw, ih);
    cairo_t *cr_mask = cairo_create(mask_surface);

    cairo_surface_destroy(mask_surface);
    /* fill window bitmap with black */
    cairo_save(cr_mask);
    cairo_set_source_rgb(cr_mask, 0, 0, 0);
    //cairo_set_operator(cr_mask, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr_mask);
    cairo_restore(cr_mask);

    cairo_select_font_face(cr_mask, mask_font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr_mask, ih);

    tw = iw;
    th = ih;
    cairo_text_extents(cr_mask, text, &extents);
    TRACE("e_w=%lf e_h=%lf e_xb=%lf e_yb=%lf e_xa=%lf e_ya=%lf",
          extents.width + extents.x_bearing, extents.height,
          extents.x_bearing, extents.y_bearing,
          extents.x_advance, extents.y_advance);

    tw = extents.x_advance;
    th = extents.height;
    tx = -extents.x_bearing;
    ty = -extents.y_bearing;
    TRACE("tw=%lf th=%lf", tw, th);
    //XResizeWindow(dpy, obj->win, tw, th); iw = tw; ih = th;
    /*
       cairo_arc(cr_mask, iw / 2., ih / 2., iw * 0.25, 0, 2 * G_PI);
       cairo_set_source_rgb(cr_mask, 1, 1, 1);
       cairo_fill(cr_mask);
       */
    /* cairo_select_font_face(cr, mask_font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);*/
    cairo_move_to(cr_mask, tx, ty);
    /*
       cairo_text_path(cr_mask, "Hallo Hans");
       cairo_set_source_rgb(cr_mask, 1,0,0);
       cairo_fill(cr_mask);
       */
    cairo_show_text(cr_mask, text);
    cairo_destroy(cr_mask);

    /* Create background relief */
    if(G_LIKELY((pmap = XCreatePixmap(dpy, DefaultRootWindow(dpy), iw, ih, DefaultDepth(dpy, scr))))) {
      G_GNUC_UNUSED cairo_pattern_t *pat;
      cairo_surface_t *cr_xlib_surf = cairo_xlib_surface_create(dpy, pmap,
          DefaultVisual(dpy, scr), iw, ih);
      cairo_t *cr = cairo_create(cr_xlib_surf);

      TRACE("Pixmap creation successful: pixmap = 0x%lx", pmap);
      cairo_surface_destroy(cr_xlib_surf); /* A reference is hold by cr */

      /* Background color */
      //cairo_set_source_rgb(cr, .12, .39, 1.);
      cairo_set_source_rgb(cr, obj->gsw->ucfg.osd_fgc.r, obj->gsw->ucfg.osd_fgc.g, obj->gsw->ucfg.osd_fgc.b);
      cairo_paint(cr);
      cairo_move_to(cr, tx, ty);
      cairo_select_font_face(cr, mask_font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, ih);
      cairo_set_source_rgb(cr, obj->gsw->ucfg.osd_bgc.r, obj->gsw->ucfg.osd_bgc.g, obj->gsw->ucfg.osd_bgc.b);
      cairo_show_text(cr, text);
      cairo_destroy(cr);

      XShapeCombineMask(dpy, obj->win, ShapeBounding, 0, 0, mask_bitmap, ShapeSet);
      XSetWindowBackgroundPixmap(dpy, obj->win, pmap);
      XFreePixmap(dpy, pmap);
      XClearWindow(dpy, obj->win);
    }
    XFreePixmap(dpy, mask_bitmap);
  }
}

void osd_cli_printf(osd_cli_t *obj, const gchar *fmt, ...)
{
  gchar *buf;
  va_list ap;

  g_return_if_fail(obj);
  va_start(ap, fmt);
  buf = g_strdup_vprintf(fmt, ap);
  va_end(ap);
  osd_cli_set_text(obj, buf);
  g_free(buf);
}

void osd_cli_set_horizontal_offset(osd_cli_t *obj, gint offset)
{
  g_return_if_fail(obj);
  if(obj->horiz_offset != offset) {
    obj->horiz_offset = offset;
    XMoveWindow(obj->gsw->display, obj->win, obj->horiz_offset, obj->vert_offset);
  }
}

void osd_cli_set_vertical_offset(osd_cli_t *obj, gint offset)
{
  g_return_if_fail(obj);
  if(obj->vert_offset != offset) {
    obj->vert_offset = offset;
    XMoveWindow(obj->gsw->display, obj->win, obj->horiz_offset, obj->vert_offset);
  }
}

void osd_cli_get_bottom_location(osd_cli_t *obj, gint *x_offset, gint *y_offset, gint *len)
{
  rect_t rec;
  client_t *c = get_focused_client(obj->gsw);

  if(c)
    xinerama_get_scrdims_on_which_client_resides(c, &rec);
  else
    xinerama_scrdims(obj->gsw->screen, xinerama_current_mon(obj->gsw), &rec);
  *x_offset = rec.x1;
  *y_offset = rec.y2;
  *len = rec.x2 - rec.x1;
}

void osd_cli_set_width(osd_cli_t *obj, gint w)
{
  obj->iw = w;
}
