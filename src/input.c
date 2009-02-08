#define G_LOG_DOMAIN "input"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "octopus.h"
#include "input.h"
#include "winframe.h"
#include "osd_cli.h"

#include <X11/keysym.h> /* XK_x, ... */
#ifdef HAVE_XF86VM
#include <X11/extensions/xf86vmode.h>
#endif
#include <xosd.h>
#include <cairo-xlib.h>

#define OSD_HEIGHT 2
#undef BG_OSD_WINDOW

void input_create(gswm_t *gsw)
{
  color_intensity_t *ci = &gsw->ucfg.focused_color;
  xosd *osd = xosd_create(OSD_HEIGHT);
  osd_color_t fgc, bgc;

  bgc.red = 1.0;
  bgc.green = .39;
  bgc.blue = 1.;

  fgc.red = .9;
  fgc.green = .9;
  fgc.blue = .98;

  /* Setup the OSD command line interface */
  gsw->osdcli = osd_cli_create(gsw, "Droid Sans", &fgc, &bgc);
  g_return_if_fail(gsw->osdcli);
  gsw->xosd = osd;
  g_return_if_fail(gsw->xosd);
  xosd_set_font(osd, gsw->ucfg.osd_font);
  xosd_set_colour(osd, gsw->ucfg.osd_color);
  xosd_set_outline_offset(osd, 1);
  /*xosd_set_shadow_offset (osd, 1);*/
  xosd_set_outline_colour(osd, ci->rgbi_str);
  /*xosd_set_shadow_colour(osd, gsw->ucfg.unfocused_color.rgbi_str);*/
  xosd_set_pos(osd, XOSD_bottom);
  xosd_set_align(osd, XOSD_left);
  xosd_hide(osd);
}

void input_destroy(gswm_t *gsw)
{
  g_return_if_fail(gsw->xosd);
  xosd_destroy(gsw->xosd);
  gsw->xosd = NULL;
  g_return_if_fail(gsw->osdcli);
  osd_cli_destroy(gsw->osdcli);
  gsw->osdcli = NULL;
}

void input_loop(gswm_t *gsw, const gchar *prompt, interaction_t *ia)
{
#ifdef BG_OSD_WINDOW
    Window bgw = None;
#endif
#ifdef HAVE_XF86VM
  XF86VidModeGamma old_gamma, new_gamma;
#endif
  gint cmpl_len;
  KeySym ks;
  gchar kstr_buf[64];
  Display *dpy = gsw->display;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  vdesk_t *vd = scr->vdesk + scr->current_vdesk;
  GString *avail_actions = g_string_new(NULL);
  gint off_cl = 0;
  GString *dest = ia->line;
  GCompletion *gcomp = ia->completion;
  xosd *osd = gsw->xosd;

  g_string_set_size(dest, 0);

  g_return_if_fail(osd);

  if(GrabSuccess != XGrabKeyboard(dpy, scr->rootwin,
        False, GrabModeSync, GrabModeAsync, CurrentTime))
    return;

#ifdef BG_OSD_WINDOW
  {
    Pixmap bg_pmap;
    cairo_t *cr;
    cairo_pattern_t *pat;
    XSetWindowAttributes attr;
    gint bgw_h, bgw_x, bgw_y, bgw_w;
    color_intensity_t *ciu = &gsw->ucfg.unfocused_color;
    color_intensity_t *ci = &gsw->ucfg.focused_color;

    if((cr = cairo_create()) {
      /* Create background window */
      bgw_h = OSD_HEIGHT * (gsw->font->max_bounds.ascent + gsw->font->max_bounds.descent);
      bgw_w = vd->warea.w - 2*scr->fr_info.border_width;
      bgw_x = vd->warea.x;
      bgw_y = vd->warea.y + vd->warea.h - bgw_h - 2*scr->fr_info.border_width;
      /*
         XCopyArea(dpy, scr->rootwin, bg_pmap, DefaultGC(dpy, scr->id),
         bgw_x, bgw_y, bgw_w, bgw_h, 0, 0);
       */
      bg_pmap = XCreatePixmap(dpy, scr->rootwin, bgw_w, bgw_h, DefaultDepth(dpy, scr->id));
      if(bg_pmap) {
        cairo_set_target_drawable(cr, dpy, bg_pmap);
        cairo_set_rgb_color(cr, ciu->r, ciu->g, ciu->b);
        cairo_rectangle(cr, 0, 0, bgw_w, bgw_h);
        cairo_fill(cr);
        cairo_stroke(cr);
        pat = cairo_pattern_create_linear(0., 0., bgw_w, 0);
        cairo_pattern_add_color_stop(pat, 1, ci->r, ci->g, ci->b, .1);
        cairo_pattern_add_color_stop(pat, 0, ci->r, ci->g, ci->b, .9);
        cairo_rectangle(cr, 0, 0, bgw_w, bgw_h);
        cairo_set_pattern(cr, pat);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
      }

      attr.border_pixel = scr->color.focus.pixel;
      attr.background_pixel = scr->color.unfocus.pixel;
      attr.background_pixmap = bg_pmap;
      attr.override_redirect = True;
      bgw = XCreateWindow(dpy, scr->rootwin, bgw_x, bgw_y, bgw_w, bgw_h,
          scr->fr_info.border_width, CopyFromParent, InputOutput, CopyFromParent,
          CWOverrideRedirect | CWBorderPixel | CWBackPixmap, &attr);

      /* We can free the pixmap again because the X server holds a reference to it */
      if(bg_pmap)
        XFreePixmap(dpy, bg_pmap);

      XMapWindow(dpy, bgw);
      cairo_destroy(cr);
    }
  }
#endif

#ifdef HAVE_XF86VM
  if(gsw->xf86vm) {
    XF86VidModeGetGamma(dpy, scr->id, &old_gamma);
    new_gamma = old_gamma;
    new_gamma.red *= 0.5;
    new_gamma.green *= 0.5;
    new_gamma.blue *= 0.5;
    XF86VidModeSetGamma(dpy, scr->id, &new_gamma);
  }
#endif
  XSync(dpy, False);
  xosd_set_horizontal_offset(osd, vd->warea.x);
  xosd_set_vertical_offset(osd, scr->dpy_height - vd->warea.h - vd->warea.y + 2*scr->fr_info.border_width);
  xosd_display(osd, OSD_HEIGHT-1, XOSD_string, "");
  xosd_display(osd, 0, XOSD_string, prompt);

  osd_cli_set_text(gsw->osdcli, (gchar*)prompt);
  osd_cli_show(gsw->osdcli);

  cmpl_len = scr->dpy_width / (gsw->font->max_bounds.rbearing - gsw->font->min_bounds.lbearing);
  while(TRUE) {
    XEvent ev;
    XMaskEvent(dpy, KeyPressMask, &ev);
    switch(ev.type) {
      case KeyPress:
        XLookupString(&ev.xkey, kstr_buf, sizeof(kstr_buf), &ks, 0);
        TRACE(("%s Key pressed: %s", __func__, kstr_buf));
        switch(ks) {
          case XK_Right:
            if(cmpl_len < avail_actions->len)
              off_cl += cmpl_len;
            break;
          case XK_Left:
            if(cmpl_len < avail_actions->len)
              off_cl -= cmpl_len;
            break;
          case XK_BackSpace:
            TRACE(("XK_BackSpace pressed"));
            if(0 < dest->len)
              g_string_truncate(dest, dest->len - 1);
            break;
          case XK_Escape:
            TRACE(("%s XK_Escape pressed", __func__));
            g_string_set_size(dest, 0);
            goto leave_loop;
            break;
          case XK_Return:
            TRACE(("%s XK_Return pressed", __func__));
            goto leave_loop;
            break;
          case XK_u:
            if(ControlMask == ev.xkey.state)
              g_string_set_size(dest, 0);
            else
              g_string_append_c(dest, 'u');
            break;
          case XK_Tab:
            if(gcomp) {
              gchar *new_prefix;
              GList *action_list = g_completion_complete(gcomp,
                  dest->str, &new_prefix);
              g_string_set_size(avail_actions, 0);
              while(action_list) {
                g_string_append(avail_actions, action_list->data);
                g_string_append_c(avail_actions, ' ');
                action_list = action_list->next;
              }
              if(new_prefix) {
                g_string_assign(dest, new_prefix);
                g_free(new_prefix);
              }
              off_cl = 0;
            }
            break;
          default:
            g_string_append(dest, kstr_buf);
            break;
        }
        xosd_display(osd, 0, XOSD_printf, "%s%s", prompt, dest->str);
        off_cl = CLAMP(off_cl, 0, (gint)avail_actions->len);
        xosd_display(osd, OSD_HEIGHT-1, XOSD_string, avail_actions->str + off_cl);
        osd_cli_printf(gsw->osdcli, "%s%s", prompt, dest->str);
        break;
    }
  }
leave_loop:
  osd_cli_hide(gsw->osdcli);
  xosd_hide(osd);
#ifdef HAVE_XF86VM
  if(gsw->xf86vm)
    XF86VidModeSetGamma(dpy, scr->id, &old_gamma);
#endif
  XUngrabKeyboard(dpy, CurrentTime);
#ifdef BG_OSD_WINDOW
  if(bgw)
    XDestroyWindow(dpy, bgw);
#endif
  g_string_free(avail_actions, TRUE);
}


