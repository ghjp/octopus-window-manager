#define G_LOG_DOMAIN "osd_cli"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "octopus.h"
#include "osd_cli.h"

#include <X11/extensions/shape.h>
#include <cairo-xlib.h>

Window osd_window = None;

gint osd_cli_create(gswm_t *gsw)
{
  Window rootwin;
  Display *dpy = gsw->display;
  screen_t *screen = gsw->screen + gsw->i_curr_scr;
  gint scr = screen->id;
  gint swidth, sheight, depth;
  gint iw, ih;
  GRand *rand = g_rand_new();

  if(!(dpy = XOpenDisplay(0))) {
    g_critical("XOpenDisplay failed");
    return 1;
  }
  rootwin = screen->rootwin;
  swidth = screen->dpy_width;
  sheight = screen->dpy_height;
  depth = DefaultDepth(dpy, scr);
  iw = swidth;
  ih = sheight * 50 / 1000;
  g_message("scr=%d swidth=%d sheight=%d", scr, swidth, sheight);

  if(TRUE) {
    XSetWindowAttributes xwinattr = {
      .override_redirect = True,
    };
    osd_window = XCreateWindow(dpy, rootwin, 0, sheight - ih, iw, ih, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect, &xwinattr);
  }
  else
    osd_window = XCreateSimpleWindow(dpy, rootwin, 0, sheight - ih, iw, ih, 0,
        BlackPixel(dpy, scr), BlackPixel(dpy, scr));
  if(osd_window) {
    Pixmap mask_bitmap, pmap = None;

    /* Construct window shape */
    if((mask_bitmap = XCreatePixmap(dpy, osd_window, iw, ih, 1))) {
      gdouble tw, th, tx, ty;
      cairo_text_extents_t extents;
      const gchar *mask_text = "Enter: Mask Queue text demo!!";
      const gchar *mask_font = "Sans";
      cairo_surface_t *mask_surface = cairo_xlib_surface_create_for_bitmap(
          dpy, mask_bitmap, ScreenOfDisplay(dpy, scr), iw, ih);
      cairo_t *cr_mask = cairo_create(mask_surface);

      cairo_surface_destroy(mask_surface);
      /* fill window bitmap with black */
      cairo_save(cr_mask);
      cairo_rectangle(cr_mask, 0, 0, iw, ih);
      cairo_set_operator(cr_mask, CAIRO_OPERATOR_CLEAR);
      cairo_fill(cr_mask);
      cairo_restore(cr_mask);

      cairo_select_font_face(cr_mask, mask_font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr_mask, ih);

      tw = iw;
      th = ih;
      cairo_text_extents(cr_mask, mask_text, &extents);
      g_message("e_w=%lf e_h=%lf e_xb=%lf e_yb=%lf e_xa=%lf e_ya=%lf",
          extents.width + extents.x_bearing, extents.height,
          extents.x_bearing, extents.y_bearing,
          extents.x_advance, extents.y_advance);

      tw = extents.x_advance;
      th = extents.height;
      tx = -extents.x_bearing;
      ty = -extents.y_bearing;
      g_message("tw=%lf th=%lf", tw, th);
      //XResizeWindow(dpy, osd_window, tw, th); iw = tw; ih = th;
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
      cairo_show_text(cr_mask, mask_text);
      cairo_destroy(cr_mask);

#if 0
      if(FALSE){
        GC mask_gc, mask_gc_back;
        mask_gc = XCreateGC(dpy, mask_bitmap, 0, NULL);
        XSetForeground(dpy, mask_gc, WhitePixel(dpy, scr));
        XSetBackground(dpy, mask_gc, BlackPixel(dpy, scr));
        mask_gc_back = XCreateGC(dpy, mask_bitmap, 0, NULL);
        XSetForeground(dpy, mask_gc_back, BlackPixel(dpy, scr));
        XSetBackground(dpy, mask_gc_back, WhitePixel(dpy, scr));
        XFillRectangle(dpy, mask_bitmap, mask_gc, 0, 0, iw, ih);
        /*XFillRectangle(dpy, mask_bitmap, mask_gc_back, 0*iw/4, 0*ih/4, iw*2/4, ih*2/4);*/
        XFillArc(dpy, mask_bitmap, mask_gc_back, -iw/4, -iw/4, iw*2/4, iw*2/4, 0, -90*64);
      }
#endif


      /* Create background relief */
      if((pmap = XCreatePixmap(dpy, DefaultRootWindow(dpy), iw, ih, depth))) {
        G_GNUC_UNUSED cairo_pattern_t *pat;
        cairo_surface_t *cr_xlib_surf = cairo_xlib_surface_create(dpy, pmap,
            DefaultVisual(dpy, scr), iw, ih);
        cairo_t *cr = cairo_create(cr_xlib_surf);

        g_message("Pixmap creation successful: pixmap = 0x%lx", pmap);
        cairo_surface_destroy(cr_xlib_surf); /* A reference is hold by cr */
        /**
          cairo_save(cr);
          cairo_rectangle(cr, 0, 0, iw, ih);
          cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
          cairo_fill(cr);
          cairo_restore(cr);
         **/

#if 0
        /* Draw image onto canvas */
        cairo_rectangle(cr, 0, 0, iw/2., ih/3.);
        pat = cairo_pattern_create_linear(0, 0, iw, ih);
        cairo_pattern_add_color_stop_rgba(pat, 0, 0.0, 0.0, 1.0, 0.9);
        cairo_pattern_add_color_stop_rgba(pat, 1, 0.0, 1.0, 0.0, 0.1);
        cairo_set_source(cr, pat);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, g_rand_double(rand), g_rand_double(rand), g_rand_double(rand), g_rand_double(rand));
        cairo_rectangle(cr, iw/10., ih/4., iw/2., ih/2.);
        cairo_fill(cr);
        /* Show some text */
        cairo_set_source_rgb(cr, g_rand_double(rand), g_rand_double(rand), g_rand_double(rand));
        cairo_move_to(cr, 0.1*iw, 0.5*ih);
        cairo_set_font_size(cr, 0.125*ih);
        cairo_show_text(cr, "Cairo OSD Demo");
        cairo_pattern_destroy(pat);

        /* Try some text path tricks */
        cairo_move_to(cr, 0.25*iw, ih);
        cairo_select_font_face(cr_mask, "Mercedes", CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 0.95*ih);
        cairo_text_path(cr, "Dr. Johann Pfefferl");
        cairo_set_source_rgb(cr, 0.5, 0.5, 1);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_set_line_width(cr, 0.025*ih);
        cairo_stroke(cr);
#endif
        /* Background color */
        //cairo_set_source_rgb(cr, .12, .39, 1.);
        cairo_set_source_rgb(cr, .9, .9, .98);
        cairo_rectangle(cr, 0, 0, iw, ih);
        cairo_fill(cr);
        /* Try some text path tricks */
        cairo_move_to(cr, tx, ty);
        cairo_select_font_face(cr, mask_font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, ih);
        cairo_text_path(cr, mask_text);
        /*
           pat = cairo_pattern_create_linear(0, 0, tw, 0);
           cairo_pattern_add_color_stop_rgb(pat, 0, .12, .39, 1.);
           cairo_pattern_add_color_stop_rgb(pat, 1, .9, .9, .98);
           cairo_set_source(cr, pat);
           cairo_fill_preserve(cr);
           */
        /*
           cairo_set_source_rgba(cr, .12, .39, 1., 1.);
           cairo_fill_preserve(cr);
           */
        //cairo_set_source_rgb(cr, .9, .9, .98);
        cairo_set_source_rgb(cr, .12, .39, 1.);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);

        cairo_destroy(cr);
      }
    }

    XShapeCombineMask(dpy, osd_window, ShapeBounding, 0, 0, mask_bitmap, ShapeSet);
    XFreePixmap(dpy, mask_bitmap);
    XSetWindowBackgroundPixmap(dpy, osd_window, pmap);
    XFreePixmap(dpy, pmap);
    XStoreName(dpy, osd_window, "Cairo OSD Demo");
    XMapWindow(dpy, osd_window);
    XFlush(dpy);
  }
  g_rand_free(rand);
  return TRUE;
}

gint osd_cli_destroy(gswm_t *gsw)
{
  g_message(__func__);
  XDestroyWindow(gsw->display, osd_window);
  return TRUE;
}
