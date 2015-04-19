/* Compile with:
   gcc -Wall -o octopus-setbg octopus-setbg.c -L/usr/X11R6/lib -lX11  $(pkg-config --cflags --libs cairo glib-2.0)
   */
#define G_LOG_DOMAIN "octopus-setbg"
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlibrgb.h>
#include <unistd.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
/*#include <cairo-xlib.h>*/

static void set_prop(Display *dpy, Window win, Atom at, Atom type, long val)
{
  XChangeProperty(dpy, win, at, type, 32,
      PropModeReplace, (unsigned char *) &val, 1);
}

static Pixmap read_old_pixmap(Display *dpy, Window rootwin, Atom at)
{
  Atom real_type;
  gint real_format;
  gulong items_read, items_left;
  guint8 *data = NULL;
  Pixmap value = 0;

  if(Success == XGetWindowProperty(dpy, rootwin, at,
        0L, 1L, False, XA_PIXMAP, &real_type, &real_format, &items_read, &items_left,
        &data) && items_read) {
    value = *(Pixmap*)data;
    XFree(data);
  }
  return value;
}

static void show_help(gchar *prgname)
{
  g_printerr("Usage: %s [options] <img_filename>\n", prgname);
  g_printerr("Sets the workspace background to the specified image\n\n");
  g_printerr(" -c don't center image\n");
  g_printerr(" -s don't scale image if aspect ratio of image and screen matches\n");
  g_printerr(" -h show this help\n -? show this help\n");
  exit(EXIT_FAILURE);
}

static void install_new_bg_rootpixmap(Display *dpy, Window rootwin, Pixmap pmap)
{
  Atom x_rootpmap_id = XInternAtom(dpy, "_XROOTPMAP_ID", False);
  Pixmap old_pixmap = read_old_pixmap(dpy, rootwin, x_rootpmap_id);

  XSetWindowBackgroundPixmap(dpy, rootwin, pmap);
  XClearWindow(dpy, rootwin);
  XSync(dpy, False);
  XGrabServer(dpy);
  old_pixmap = read_old_pixmap(dpy, rootwin, x_rootpmap_id);
  g_message("old_pixmap=0x%lx is replaced by 0x%lx", old_pixmap, pmap);
  XKillClient(dpy, old_pixmap);
  XSync(dpy, False);
  set_prop(dpy, rootwin, x_rootpmap_id, XA_PIXMAP, pmap);
  XUngrabServer(dpy);
  XFlush(dpy);
  /* Don't delete the created background pixmap */
  XSetCloseDownMode(dpy, RetainPermanent);
}

gint main(gint argc, gchar **argv)
{
  Display *dpy;
  Window rootwin;
  gint o, scr;
  Pixmap pmap;
  gint swidth, sheight, iw, ih;
  gchar *img_fname = NULL;
  GdkPixbuf *imgpb;
  GError *error = NULL;
  gboolean center = TRUE, scale = TRUE;

  g_type_init();
  while(0 <= (o = getopt(argc, argv, "csh?"))) {
    switch(o) {
      case 'c':
        center = FALSE;
        break;
      case 's':
        scale = FALSE;
        break;
      default:
        show_help(argv[0]);
        break;
    }
  }
  if(optind >= argc) /* An image filename must be given */
    show_help(argv[0]);
  else
    img_fname = argv[optind++];
  if(!(dpy = XOpenDisplay(0))) {
    g_critical("XOpenDisplay failed");
    return EXIT_FAILURE;
  }
  scr = DefaultScreen(dpy);
  rootwin = RootWindow(dpy, scr);
  swidth = DisplayWidth(dpy, scr);
  sheight = DisplayHeight(dpy, scr);
  xlib_rgb_init(dpy, ScreenOfDisplay(dpy, scr));
  g_message("%s: scr=%d swidth=%d sheight=%d", img_fname, scr, swidth, sheight);
  imgpb = gdk_pixbuf_new_from_file(img_fname, &error);
  if(error) {
    g_critical("%s", error->message);
    return EXIT_FAILURE;
  }
  iw = gdk_pixbuf_get_width(imgpb);
  ih = gdk_pixbuf_get_height(imgpb);
  g_message("has_alpha=%d nr_bits_per_pixel=%d row_stride=%d n_channels=%d w=%d h=%d",
      gdk_pixbuf_get_has_alpha(imgpb),
      gdk_pixbuf_get_bits_per_sample(imgpb),
      gdk_pixbuf_get_rowstride(imgpb),
      gdk_pixbuf_get_n_channels(imgpb),
      iw, ih);
  if(scale && (iw != swidth || ih != sheight) && iw * sheight == swidth * ih) { /* Aspect ratio is the same */
    GdkPixbuf *scaled_img = gdk_pixbuf_scale_simple(imgpb, swidth, sheight, GDK_INTERP_BILINEAR);
    if(scaled_img) {
      imgpb = scaled_img;
      iw = gdk_pixbuf_get_width(imgpb);
      ih = gdk_pixbuf_get_height(imgpb);
      g_message("Scaled: has_alpha=%d nr_bits_per_pixel=%d row_stride=%d n_channels=%d w=%d h=%d",
          gdk_pixbuf_get_has_alpha(imgpb),
          gdk_pixbuf_get_bits_per_sample(imgpb),
          gdk_pixbuf_get_rowstride(imgpb),
          gdk_pixbuf_get_n_channels(imgpb),
          iw, ih);
    }
  }

  pmap = XCreatePixmap(dpy, DefaultRootWindow(dpy), swidth, sheight, DefaultDepth(dpy, scr));
  if(pmap) {
    gboolean has_alpha;
    gint row_stride, nr_channels, xpos, ypos;
    guint8 *img_data = NULL, *pixels;
#if 0
    guint8 *rp, *p;
    gint r, c;
    guint32 *img_data_elem;
#endif

    g_message("Pixmap creation successful: pixmap id = 0x%lx", pmap);
    row_stride = gdk_pixbuf_get_rowstride(imgpb);
    nr_channels = gdk_pixbuf_get_n_channels(imgpb);
    has_alpha = gdk_pixbuf_get_has_alpha(imgpb);
    pixels = gdk_pixbuf_get_pixels(imgpb);
    g_message("has_alpha=%d nr_bits_per_pixel=%d row_stride=%d n_channels=%d",
        has_alpha,
        gdk_pixbuf_get_bits_per_sample(imgpb),
        row_stride, nr_channels);

    XFillRectangle(dpy, pmap, DefaultGC(dpy, scr), 0, 0, swidth, sheight);
    xpos = center && iw < swidth ?  (swidth-iw)/2  : 0;
    ypos = center && ih < sheight ? (sheight-ih)/2 : 0;
    if(G_UNLIKELY(has_alpha))
      xlib_draw_rgb_32_image(pmap, DefaultGC(dpy, scr),
          xpos, ypos, iw, ih,
          XLIB_RGB_DITHER_NONE, pixels, row_stride);
    else
      xlib_draw_rgb_image(pmap, DefaultGC(dpy, scr),
          xpos, ypos, iw, ih,
          XLIB_RGB_DITHER_NONE, pixels, row_stride);
#if 0
    img_data = g_malloc(swidth*sheight*sizeof(guint32));
    rp = pixels;
    for(r = 0; r < ih; r++, rp += row_stride) {
      img_data_elem = (guint32*)img_data + r * swidth;
      for(p = rp, c = 0; c < iw; c++) {
        guint32 alpha;
        guint32 red = *p++;
        guint32 green = *p++;
        guint32 blue = *p++;
        if(G_UNLIKELY(has_alpha))
          alpha = *p++;
        else
          alpha = 0xff;
        *img_data_elem++ = (alpha<<24) | (red<<16) | (green<<8) | blue;
      }
    }
#endif
    g_object_unref(imgpb);
#if 0
    {
      cairo_surface_t *img = cairo_image_surface_create_for_data(
          img_data, CAIRO_FORMAT_ARGB32,
          swidth, sheight, swidth * sizeof(guint32));
      cairo_pattern_t *pat = cairo_pattern_create_for_surface(img);
      cairo_surface_t *cr_xlib_surf = cairo_xlib_surface_create(dpy, pmap,
          DefaultVisual(dpy, scr), swidth, sheight);
      cairo_t *cr = cairo_create(cr_xlib_surf);

      cairo_surface_destroy(cr_xlib_surf); /* A reference is hold by cr */
      cairo_surface_destroy(img);
      /* Black background */
      cairo_set_source_rgba(cr, 0., 0., 0., 1.);
      cairo_rectangle(cr, 0, 0, swidth, sheight);
      cairo_fill(cr);
      /* Draw image onto canvas */
      cairo_rectangle(cr, 0, 0, swidth, sheight);
      cairo_set_source(cr, pat);
      cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
      cairo_fill(cr);
      cairo_pattern_destroy(pat);
      cairo_destroy(cr);
    }
#endif
    install_new_bg_rootpixmap(dpy, rootwin, pmap);
    g_free(img_data);
  }

  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
