#define G_LOG_DOMAIN "client"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h> /* BUFSIZ */

#include "octopus.h"
#include "client.h"
#include "winactions.h"
#include "ewmh.h"
#include "winframe.h"
#include "main.h"
#include "action_system.h"
#include "vdesk.h"
#include "mwm_stuff.h"

#include <X11/Xatom.h>
#ifdef HAVE_XSHAPE
#include <X11/extensions/shape.h> /* SHAPE */
#endif

/* This will need to be called whenever we update our client_t stuff.
 * Yeah, yeah, stop yelling at me about OO. */

void send_config(gswm_t *gsw, client_t *c)
{
  XConfigureEvent ce;

  ce.type = ConfigureNotify;
  ce.event = c->win;
  ce.window = c->win;
  ce.x = c->x;
  ce.y = c->y;
  ce.width = c->width;
  ce.height = c->height;
  ce.border_width = 0;
  ce.above = c->wframe->win;
  ce.override_redirect = 0;

  XSendEvent(gsw->display, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

gboolean client_win_is_alive(gswm_t *gsw, client_t *c)
{
  Window dummy_root, parent;
  Window *wins = NULL;
  guint count;
  Status test;

  XSync(gsw->display, False);
  test = XQueryTree(gsw->display, c->win, &dummy_root, &parent, &wins, &count);
  TRACE("%s count=%u", __func__, count);
  X_FREE(wins);
  return test && dummy_root == parent;
}

/* Attempt to follow the ICCCM by explicity specifying 32 bits for
    * this property. Does this goof up on 64 bit systems? */
void set_wm_state(gswm_t *gsw, client_t *c, gint state)
{
  glong data[2];

  data[0] = state;
  data[1] = None; /* Icon? We don't need no steenking icon. */

  XChangeProperty(gsw->display, c->win, gsw->xa.wm_state, gsw->xa.wm_state,
      32, PropModeReplace, (unsigned char *)data, 2);
  if(WithdrawnState == state) {
    XDeleteProperty(gsw->display, c->win, gsw->xa.wm_net_wm_desktop);
    XDeleteProperty(gsw->display, c->win, gsw->xa.wm_net_wm_state);
  }
}

/* If we can't find a WM_STATE we're going to have to assume
 * Withdrawn. This is not exactly optimal, since we can't really
 * distinguish between the case where no WM has run yet and when the
 * state was explicitly removed (clients are allowed to either set the
 * atom to Withdrawn or just remove it... yuck.) */

glong get_wm_state(gswm_t *gsw, client_t *c)
{
  Atom real_type; int real_format;
  glong state = WithdrawnState;
  gulong items_read, bytes_left;
  guchar *data;

  if(Success == XGetWindowProperty(gsw->display, c->win, gsw->xa.wm_state, 0L, 2L, False,
        gsw->xa.wm_state, &real_type, &real_format, &items_read, &bytes_left,
        &data) && items_read) {
    state = *(glong *)data;
    XFree(data);
  }
  return state;
}

static gchar *_get_ewmh_win_name(gswm_t *gsw, client_t *c)
{
  Atom actual_type;
  gint actual_format;
  gulong nitems;
  gulong leftover;
  guint8 *name;
  gchar *utf8_name;

  if(Success != XGetWindowProperty(gsw->display, c->win, gsw->xa.wm_net_wm_name,
        0L, (long)BUFSIZ, False, gsw->xa.wm_utf8_string, &actual_type, &actual_format,
        &nitems, &leftover, &name)) {
    TRACE("%s _NET_WM_NAME not found", __func__);
    c->wstate.has_net_wm_name = FALSE;
    return NULL;
  }

  if(G_LIKELY((actual_type == gsw->xa.wm_utf8_string) && (actual_format == 8))) {
    /* The data returned by XGetWindowProperty is guaranteed
       to contain one extra byte that is null terminated to
       make retrieving string properties easy  */
    utf8_name = g_strdup((gchar*)name);
    TRACE("%s _NET_WM_NAME ut8 name found and valid", __func__);
    c->wstate.has_net_wm_name = TRUE;
  }
  else {
    c->wstate.has_net_wm_name = FALSE;
    utf8_name = NULL;
  }
  XFree(name);
  return utf8_name;
}

static inline gchar *_read_property_xa_string(gswm_t *gsw, client_t *c, const XTextProperty *text_prop)
{
  gchar *xa_string = NULL;

  if(text_prop->value && 0 < text_prop->nitems && text_prop->format == 8) {
    if(XA_STRING == text_prop->encoding)
      xa_string = g_locale_to_utf8((gchar*)text_prop->value, -1, NULL, NULL, NULL);
    else {
      gint num;
      gchar **list;
      XmbTextPropertyToTextList(gsw->display, text_prop, &list, &num);
      if(num && *list) {
        xa_string = g_locale_to_utf8(*list, -1, NULL, NULL, NULL);
        XFreeStringList(list);
      }
    }
  }
  return xa_string;
}

void setup_client_name(gswm_t *gsw, client_t *c)
{
  XTextProperty text_prop;

  g_free(c->utf8_name);
  if((c->utf8_name = _get_ewmh_win_name(gsw, c)))
    return;

  text_prop.value = NULL;
  text_prop.nitems = 0;
  if(XGetWMName(gsw->display, c->win, &text_prop)) {
    c->utf8_name = _read_property_xa_string(gsw, c, &text_prop);
    XFree(text_prop.value);
  }
  else
    c->utf8_name = g_strdup("..?");
}

static void _free_client(gswm_t *gsw, client_t *c)
{
  g_free(c->utf8_name);
  X_FREE(c->cmap.windows);
  g_slice_free(client_t, c);
}

/* This one does -not- free the data coming back from Xlib; it just
    * sends back the pointer to what was allocated. */
static PropMwmHints *_get_mwm_hints(gswm_t *gsw, client_t *c) /* MWM */
{
  Atom real_type; int real_format;
  gulong items_read, bytes_left;
  guint8 *data;

  if(Success == XGetWindowProperty(gsw->display, c->win,
        gsw->xa.wm_mwm_hints, 0L, 20L, False,
        gsw->xa.wm_mwm_hints, &real_type, &real_format,
        &items_read, &bytes_left, &data) &&
      items_read >= PROP_MOTIF_WM_HINTS_ELEMENTS) {
    return (PropMwmHints *)data;
  }
  return NULL;
}

static Window _get_wm_client_leader(gswm_t *gsw, client_t *c)
{
  Atom real_type;
  gint real_format;
  gulong items_read, items_left;
  guint8 *data = NULL;
  Window value = 0;

  if(Success == XGetWindowProperty(gsw->display, c->win, gsw->xa.wm_client_leader,
        0L, 1L, False, XA_WINDOW, &real_type, &real_format, &items_read, &items_left,
        &data) && items_read) {
    value = *(Window*)data;
    XFree(data);
  }
  return value;
}

/* Set up a client structure for the new (not-yet-mapped) window. The
 * confusing bit is that we have to ignore 2 unmap events if the
 * client was already mapped but has IconicState set (for instance,
 * when we are the second window manager in a session).  That's
 * because there's one for the reparent (which happens on all viewable
 * windows) and then another for the unmapping itself. */

void create_new_client(gswm_t *gsw, Window w)
{
  client_t *c;
  glong dummy;
  gint state, frame_id;
  XWindowAttributes winattr;
  PropMwmHints *mhints;
  XWMHints *wmhints;
  GSList *win_group_list;
  Display *dpy = gsw->display;
  screen_t *scr = NULL;

  /* Window no longer exists */
  if(!XGetWindowAttributes(dpy, w, &winattr))
    return;

  for(state = 0; state < gsw->num_screens; state++) {
    if(winattr.root == gsw->screen[state].rootwin) {
      scr = gsw->screen + state;
      break;
    }
  }
  g_return_if_fail(NULL != scr);
  TRACE("%s: screen #%d", __func__, state);

  c = g_slice_new0(client_t);
  XGetTransientForHint(dpy, w, &c->trans);
  XGetWMNormalHints(dpy, w, &c->xsize, &dummy);
  c->win = w;
  c->x = c->x_old = winattr.x;
  c->y =  c->y_old = winattr.y;
  c->width = c->width_old = winattr.width;
  c->height = c->height_old = winattr.height;
  c->curr_screen = scr;
  c->cmap.dflt = winattr.colormap;
  if(!XGetWMColormapWindows(dpy, w, &c->cmap.windows, &c->cmap.num)) {
    c->cmap.num = 0;
    c->cmap.windows = NULL;
  }
  if(STICKY == (c->i_vdesk = get_ewmh_desktop(gsw, c)))
    c->wstate.sticky = TRUE;
  else if(c->i_vdesk >= scr->num_vdesk || 0 > c->i_vdesk)
    c->i_vdesk = scr->current_vdesk;
  setup_client_name(gsw, c);
  
  c->wstate.mwm_title = c->wstate.mwm_border = TRUE;
  if((mhints = _get_mwm_hints(gsw, c))) { /* MWM */
    if (mhints->flags & MWM_HINTS_DECORATIONS
        && !(mhints->decorations & MWM_DECOR_ALL)) {
      c->wstate.mwm_title  = mhints->decorations & MWM_DECOR_TITLE ? TRUE: FALSE;
      c->wstate.mwm_border = mhints->decorations & MWM_DECOR_BORDER ? TRUE: FALSE;
    }
    XFree(mhints);
  }
  TRACE("%s: window=%ld (%s) from screen %d: wstate.mwm_title=%d wstate.mwm_border=%d",
      __func__, c->win, c->utf8_name, scr->id, c->wstate.mwm_title, c->wstate.mwm_border);

  c->wstate.shaped = FALSE;
#ifdef HAVE_XSHAPE
  {
    Bool bounding_shaped = False, clip_shaped = False;
    gint idummy;
    guint udummy;

    XShapeQueryExtents(gsw->display, c->win, &bounding_shaped, &idummy,
        &idummy, &udummy, &udummy, &clip_shaped, &idummy, &idummy, &udummy, &udummy);
    /* A bound shaped client doesn't want to be decored by a visible frame */
    if(bounding_shaped) {
      TRACE("%s bounding_shaped client (%s)", __func__, c->utf8_name);
      c->wstate.shaped = TRUE;
    }
  }
#endif

  {
    Atom *protocols;
    gint n;

    if(XGetWMProtocols(dpy, c->win, &protocols, &n)) {
      gint i;
      for (i=0; i<n; i++)
        if(protocols[i] == gsw->xa.wm_delete)
          c->wstate.pr_delete = TRUE;
        else if(protocols[i] == gsw->xa.wm_take_focus)
          c->wstate.pr_take_focus = TRUE;
      XFree(protocols);
    }
  }
  /* Try to find the window group leader information and some MWM hints */
  c->window_group = _get_wm_client_leader(gsw, c);
  c->wstate.input_focus = TRUE;
  if((wmhints = XGetWMHints(dpy, c->win))) {
    if((StateHint & wmhints->flags))
      set_wm_state(gsw, c, wmhints->initial_state);
    if((InputHint & wmhints->flags))
      c->wstate.input_focus = wmhints->input ? TRUE: FALSE;
    if((WindowGroupHint & wmhints->flags) && !c->window_group)
      c->window_group = wmhints->window_group;
    XFree(wmhints);
  }
  if(!c->window_group) {
    client_t *master_c = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(c->trans));
    c->window_group = master_c ? master_c->window_group: c->win; /* Last resort to get a window group identifier */
  }

  win_group_list = g_hash_table_lookup(scr->win_group_hash, GUINT_TO_POINTER(c->window_group));
  win_group_list = g_slist_prepend(win_group_list, c);
  g_hash_table_replace(scr->win_group_hash, GUINT_TO_POINTER(c->window_group), win_group_list);
  TRACE("%s: client '%s' has a WindowGroupHint=0x%lx slist_len=%d",
        __FUNCTION__, c->utf8_name, c->window_group, g_slist_length(win_group_list));

  if(c->trans) {
    client_t *parent_c = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(c->trans));
    if(!parent_c) { /* Some trans entries don't point to a real client */
      GSList *another = g_slist_next(win_group_list);
      if(another)
        parent_c = another->data;
    }
    /* Transient windows are placed on the same desktop as the main application window */
    if(parent_c) {
      if(STICKY == (c->i_vdesk = parent_c->i_vdesk))
        c->wstate.sticky = TRUE;
    }
    else
      TRACE("%s transparent client (%s) doesn't possess a parent", __func__, c->utf8_name);
    /* Transient window should stay on top. So disable pending autoraise clients */
    signal_raise_window(gsw, FALSE);
  }

  scan_ewmh_net_wm_states(gsw, c);
  get_ewmh_net_wm_window_type(gsw, c);

  /* Create a new frame for the client or integrate it into an exisiting one */
  frame_id = get_frame_id_xprop(gsw, c->win);
  wframe_bind_client(gsw,
      g_hash_table_lookup(gsw->fid2frame_hash, GINT_TO_POINTER(frame_id)), c);

  state = get_wm_state(gsw, c);
  if(IsViewable == winattr.map_state) {
    if(IconicState == state) {
      TRACE("%s IsViewable && IconicState: detach w=%ld vdesk=%d",
          __func__, c->win, c->i_vdesk);
      detach(gsw, c);
    }
    else {
      TRACE("%s IsViewable && !IconicState: w=%ld, vdesk=%d",
          __func__, c->win, c->i_vdesk);
      attach(gsw, c);
    }
  }
  else {
    if(NormalState == state) {
      TRACE("%s !IsViewable && NormalState w=%ld vdesk=%d",
          __func__, c->win, c->i_vdesk);
      attach(gsw, c);
    }
    else {
      TRACE("%s !IsViewable && !NormalState detach w=%ld vdesk=%d",
          __func__, c->win, c->i_vdesk);
      detach(gsw, c);
    }
  }

  update_ewmh_net_client_list(gsw);
  set_ewmh_net_wm_allowed_actions(gsw, c);

  /* Restore old states */
  if(c->wstate.maxi_vert) {
    c->wstate.maxi_vert = FALSE; /* Force it */
    send_ewmh_net_wm_state_add(gsw, c, gsw->xa.wm_net_wm_state_maximized_vert);
  }
  if(c->wstate.maxi_horz) {
    c->wstate.maxi_horz = FALSE; /* Force it */
    send_ewmh_net_wm_state_add(gsw, c, gsw->xa.wm_net_wm_state_maximized_horz);
  }
  if(c->wstate.fullscreen) {
    c->wstate.fullscreen = FALSE; /* Force it */
    send_ewmh_net_wm_state_add(gsw, c, gsw->xa.wm_net_wm_state_fullscreen);
  }
  if(c->w_type.dialog || c->trans) {
    client_t *focused_clnt = scr->vdesk[scr->current_vdesk].focused;

    /* If the parent window has the focus then the dialog window inherits it */
    if(focused_clnt && focused_clnt->window_group == c->window_group) {
      TRACE("%s Dialog window appeared: parent=%p", __func__, focused_clnt);
      focus_client(gsw, c, FALSE);
    }
  }
  if(c->w_type.desktop)
    wa_lower(gsw, c);
}

static void _disintegrate_client(gswm_t *gsw, client_t *c)
{
  screen_t *scr = c->curr_screen;

  /* Cleanup the client lists. If a client isn't found in one list, this
     circumstance is silently ignored */
  if(c->window_group) {
    GSList *win_group_list = g_hash_table_lookup(scr->win_group_hash, GUINT_TO_POINTER(c->window_group));
    win_group_list = g_slist_remove(win_group_list, c);
    g_hash_table_replace(scr->win_group_hash, GUINT_TO_POINTER(c->window_group), win_group_list);
    TRACE("%s: client '%s' has a WindowGroupHint=0x%lx slist_len=%d",
        __FUNCTION__, c->utf8_name, c->window_group, g_slist_length(win_group_list));
  }
  scr->sticky_list = g_list_remove(scr->sticky_list, c);
  scr->detached_list = g_list_remove(scr->detached_list, c);
  scr->desktop_list = g_list_remove(scr->desktop_list, c);
  if(0 <= c->i_vdesk && c->i_vdesk < scr->num_vdesk)
    scr->vdesk[c->i_vdesk].clnt_list =
      g_list_remove(scr->vdesk[c->i_vdesk].clnt_list, c);

  if(!g_hash_table_remove(gsw->win2clnt_hash, GUINT_TO_POINTER(c->win)))
    g_critical("%s client 0x%lx (%s) not found in win2clnt_hash",
        __func__, c->win, c->utf8_name);
  remove_ewmh_strut(gsw, c, FALSE);
  update_ewmh_net_client_list(gsw);
  wframe_update_stat_indicator(gsw);
}

static void _focus_another_client(gswm_t *gsw, client_t *c)
{
  client_t *nextc;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  client_t *oldfc = scr->vdesk[scr->current_vdesk].focused;

  if(c->i_vdesk != scr->current_vdesk)
    clear_vdesk_focused_client(gsw, c);

  /* Select the next most suitable client */
  if(oldfc != c)
    return;
  else if(c->trans)
    nextc = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(c->trans));
  else
    nextc = NULL;
  if(!nextc) {
    nextc = wframe_get_active_client(gsw, c->wframe);
    if(nextc->wframe == c->wframe)
      if(!(nextc = wframe_get_previous_active_client(gsw, c->wframe)))
        nextc = wa_get_client_under_pointer(gsw);
  }
  if(!nextc || c == nextc) {
    /* Focus previously focused frame */
    screen_t *scr = gsw->screen + gsw->i_curr_scr;
    vdesk_t *vd = scr->vdesk + scr->current_vdesk;
    GList *cle = g_list_next(vd->frame_list);

    if(cle)
      nextc = wframe_get_active_client(gsw, cle->data);
  }
  if(nextc && nextc != c && nextc->wstate.input_focus)
    focus_client(gsw, nextc, TRUE);
}

static gint _ignore_xerror(G_GNUC_UNUSED Display *dsply, G_GNUC_UNUSED XErrorEvent *e)
{
  return 0;
}

void unmap_client(gswm_t *gsw, client_t *c)
{
  gint (*original_xerror)(Display *, XErrorEvent *);

  TRACE("%s (%s)", __func__, c->utf8_name);
  /* After pulling my hair out trying to find some way to tell if a
   * window is still valid, I've decided to instead carefully ignore any
   * errors raised by this function. We know that the X calls are, and
   * we know the only reason why they could fail -- a window has removed
   * itself completely before the Unmap events get through
   * the queue to us. It's not absolutely perfect, but it works.
   */
  original_xerror = XSetErrorHandler(_ignore_xerror);
  _focus_another_client(gsw, c);
  set_wm_state(gsw, c, WithdrawnState);
  XDeleteProperty(gsw->display, c->win, gsw->xa.wm_octopus_frame_id);
  wframe_unbind_client(gsw, c);
  wframe_update_stat_indicator(gsw);
  /* We need to call XSync to get all X errors happened unvisible */
  XSync(gsw->display, False);
  XSetErrorHandler(original_xerror);
  _disintegrate_client(gsw, c);
  _free_client(gsw, c);
}

void remap_client(gswm_t *gsw, client_t *c)
{
  set_wm_state(gsw, c, NormalState);
  wframe_unbind_client(gsw, c);
  wframe_update_stat_indicator(gsw);
  _disintegrate_client(gsw, c);
  XMapWindow(gsw->display, c->win);
  if(c->w_type.desktop)
    XLowerWindow(gsw->display, c->win);
  _free_client(gsw, c);
}

void destroy_client(gswm_t *gsw, client_t *c)
{
  TRACE("%s (%s)", __func__, c->utf8_name);
  _focus_another_client(gsw, c);
  wframe_remove_client(gsw, c);
  _disintegrate_client(gsw, c);
  _free_client(gsw, c);
}

/* Window gravity is a mess to explain, but we don't need to do much
 * about it since we're using X borders. For NorthWest et al, the top
 * left corner of the window when there is no WM needs to match up
 * with the top left of our fram once we manage it, and likewise with
 * SouthWest and the bottom right (these are the only values I ever
 * use, but the others should be obvious.) Our titlebar is on the top
 * so we only have to adjust in the first case. */

void gravitate(gswm_t *gsw, client_t *c, gint mode)
{
  gint dx = 0, dy = 0; 
  gint bw = GET_BORDER_WIDTH(c);
  gint gravity = (c->xsize.flags & PWinGravity) ?
    c->xsize.win_gravity : NorthWestGravity;

  switch(gravity) {
    case NorthWestGravity:
      dy = c->wframe->theight;
      break;
    case NorthEastGravity:
      dx = -bw;
      dy = c->wframe->theight;
      break;
    case NorthGravity:
      dy = c->wframe->theight;
      break;
    case SouthWestGravity:
      dy = -bw;
      break;
    case SouthEastGravity:
      dx = dy = -bw;
      break;
    case SouthGravity:
      dy = -bw;
      break;
    case CenterGravity:
      dx = -bw;
      dy = -c->wframe->theight/2;
      break;
  }

  switch(mode) {
    case GRAV_APPLY:
      c->x += dx;
      c->y += dy;
      break;
    case GRAV_UNDO:
      c->x -= dx;
      c->y -= dy;
      break;
    default:
      g_return_if_reached();
      break;
  }
}

void get_mouse_position(gswm_t *gsw, gint *x, gint *y)
{
  Window mouse_root, mouse_win;  
  int win_x, win_y;
  unsigned int mask;        

  XQueryPointer(gsw->display, gsw->screen[gsw->i_curr_scr].rootwin,
      &mouse_root, &mouse_win, x, y, &win_x, &win_y, &mask);  
}

client_t *get_next_focusable_client(gswm_t *gsw)
{
  GList *cle;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  vdesk_t *vd = scr->vdesk + scr->current_vdesk;
  client_t *sel_c, *fc;

  fc = get_focused_client(gsw);
  cle = g_list_find(vd->clnt_list, fc);
  if(cle)
    cle = g_list_previous(cle);
  if(!cle)
    cle = g_list_last(vd->clnt_list);
  /* If we have found a first next client */
  if(cle) {
    GList *cle_start = cle;

    sel_c = (client_t*)cle->data;
    /* Filter out clients which don't want input focus */
    while(!sel_c->wstate.input_focus) {

      cle = g_list_previous(cle);
      if(!cle)
        cle = g_list_last(vd->clnt_list);
      if(cle == cle_start) /* Already done a whole loop */
        return NULL;
      sel_c = cle->data;
    }
    TRACE("%s sel=(%s) first=(%s)", __func__,
        sel_c->utf8_name, wframe_get_active_client(gsw, sel_c->wframe)->utf8_name);
  }
  else
    sel_c = NULL;
  return sel_c;
}

typedef struct {
  gswm_t *gsw;
  client_t *focus_c;
} _check_trans_info_t;

static void _check_trans(gpointer data, gpointer user_data)
{
  client_t *c = (client_t*)data;
  _check_trans_info_t *cti = (_check_trans_info_t*)user_data;

  TRACE("%s: Inspecting '%s': trans=0x%lx", __FUNCTION__, c->utf8_name, c->trans);
  if(!c->wstate.below && 
      (c == cti->focus_c || c->trans == cti->focus_c->win || c->trans == cti->focus_c->window_group)) {
    TRACE("%s found trans (w=%ld) for client w=%ld", __func__, c->win, cti->focus_c->win);
    g_array_append_val(cti->gsw->auto_raise_frames, c->wframe->win);
  }
}

void focus_client(gswm_t *gsw, client_t *c, gboolean raise)
{
  gboolean recreate_pmap;
  screen_t *scr = c->curr_screen;
  _check_trans_info_t cti;
  GSList *win_grp_list = g_hash_table_lookup(scr->win_group_hash, GUINT_TO_POINTER(c->window_group));

  TRACE("%s w=%ld (%s)", __func__, c->win, c->utf8_name);
  /* First we try to find a transient window */
  g_array_set_size(gsw->auto_raise_frames, 0);
  cti.gsw = gsw;
  cti.focus_c = c;
  g_slist_foreach(win_grp_list, _check_trans, &cti);

  recreate_pmap = wframe_get_active_client(gsw, c->wframe) != c ? TRUE: FALSE;
  if(c->w_type.desktop) {
    scr->desktop_frlist = g_list_remove(scr->desktop_frlist, c->wframe);
    scr->desktop_frlist = g_list_prepend(scr->desktop_frlist, c->wframe);
    return;
  }
  else if(c->wstate.sticky) {
    scr->sticky_frlist = g_list_remove(scr->sticky_frlist, c->wframe);
    scr->sticky_frlist = g_list_prepend(scr->sticky_frlist, c->wframe);
  }
  else {
    vdesk_t *vd = scr->vdesk + c->i_vdesk;
    vd->frame_list = g_list_remove(vd->frame_list, c->wframe);
    vd->frame_list = g_list_prepend(vd->frame_list, c->wframe);
  }
  c->wframe->client_list = g_list_remove(c->wframe->client_list, c);
  c->wframe->client_list = g_list_prepend(c->wframe->client_list, c);
  signal_raise_window(gsw, TRUE);
  if(!raise)
    update_ewmh_net_client_list(gsw);
  set_root_prop_window(gsw, gsw->xa.wm_net_active_window, c->win);
  if(recreate_pmap) 
    wframe_tbar_pmap_recreate(gsw, c->wframe);
  scr->vdesk[scr->current_vdesk].focused = c;
  /* "Globally active" input model */
  if(G_UNLIKELY(!c->wstate.input_focus && c->wstate.pr_take_focus))
    wa_send_xclimsgwm(gsw, c, gsw->xa.wm_protocols, gsw->xa.wm_take_focus);
  else
    wa_set_input_focus(gsw, c);
}

static inline void _insert_client_into_list(screen_t *scr, client_t *c)
{
  if(G_UNLIKELY(c->w_type.desktop)) {
    scr->desktop_list = g_list_prepend(scr->desktop_list, c);
  }
  else if(G_UNLIKELY(c->wstate.sticky)) {
    c->i_vdesk = STICKY;
    if(!g_list_find(scr->sticky_list, c))
      scr->sticky_list = g_list_prepend(scr->sticky_list, c);
  }
  else {
    if(!g_list_find(scr->vdesk[c->i_vdesk].clnt_list, c))
      scr->vdesk[c->i_vdesk].clnt_list =
        g_list_prepend(scr->vdesk[c->i_vdesk].clnt_list, c);
  }
}

void attach(gswm_t *gsw, client_t *c)
{
  screen_t *scr = c->curr_screen;
  client_t *cwh = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(c->win));

  if(!cwh) { /* Complete new client */
    TRACE("%s Complete new client", __func__);
    _insert_client_into_list(scr, c);
    g_hash_table_insert(gsw->win2clnt_hash, GUINT_TO_POINTER(c->win), c);
  }
  else if(cwh == c) { /* Client already exists (detached mode) */
    TRACE("%s Detached client", __func__);
    scr->detached_list = g_list_remove(scr->detached_list, c);
    scr->detached_frlist = g_list_remove(scr->detached_frlist, c->wframe);
    _insert_client_into_list(scr, c);
  }
  else
    g_return_if_reached();

  wframe_list_add(gsw, c->wframe);
  wa_unhide(gsw, c, TRUE);
  set_wm_state(gsw, c, NormalState);
  add_ewmh_strut(gsw, c);
  wframe_tbar_button_set_normal(gsw, c->wframe);
  if(c->wstate.below)
    wa_lower(gsw, c);
  if(!get_focused_client(gsw))
    focus_client(gsw, c, TRUE);
  wframe_update_stat_indicator(gsw);
}

void detach(gswm_t *gsw, client_t *c)
{
  screen_t *scr = c->curr_screen;
  client_t *cwh = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(c->win));

  /* Desktop type windows can not be detached */
  if(G_UNLIKELY(c->w_type.desktop))
    return;
  /* Due to race conditions under some conditions it could happen
     that this routine is called more than once for a client. E. g. More than one
     keyboard press is reported by the X server */
  if(g_list_find(scr->detached_list, c))
      return;

  _focus_another_client(gsw, c);
  if(!cwh) { /* Complete new client */
    g_hash_table_insert(gsw->win2clnt_hash, GUINT_TO_POINTER(c->win), c);
  }
  else {
    if(c->wstate.sticky)
      scr->sticky_list = g_list_remove(scr->sticky_list, c);
    else
      scr->vdesk[c->i_vdesk].clnt_list =
        g_list_remove(scr->vdesk[c->i_vdesk].clnt_list, c);
  }
  scr->detached_list = g_list_prepend(scr->detached_list, c);

  /* If we are detaching from a multi frame client we must disintegrate the client first */
  if(wframe_houses_multiple_clients(gsw, c->wframe)) {
    wframe_unbind_client(gsw, c);
    /* Create a new frame for the client */
    wframe_bind_client(gsw, NULL, c);
  }
  wframe_list_remove(gsw, c->wframe);
  scr->detached_frlist = g_list_prepend(scr->detached_frlist, c->wframe);
  wa_iconify(gsw, c);
  remove_ewmh_strut(gsw, c, FALSE);
  wframe_set_type(gsw, c->wframe, FALSE);
  /* Eliminate pending focus events */
  wframe_nullify_pending_focus_events(gsw, c->wframe);
  wframe_update_stat_indicator(gsw);
}

void attach_first(gswm_t *gsw)
{
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  GList *first = g_list_first(scr->detached_list);

  if(first) {
    client_t *c = first->data;
    c->i_vdesk = scr->current_vdesk;
    attach(gsw, c);
  }
}

static gboolean _check_frame_sizehints(wframe_t *frame, client_t *c)
{
  if((PMinSize & c->xsize.flags) && (PMaxSize & frame->xsize.flags)) {
    if(c->xsize.min_width > frame->xsize.max_width)
      return FALSE;
  }
  if((PMaxSize & c->xsize.flags) && (PMinSize & frame->xsize.flags)) {
    if(frame->xsize.min_width > c->xsize.max_width)
      return FALSE;
  }
  return TRUE;
}

#define CHECK_MULTIFRAME_CAP(c) (!(c)->wstate.sticky && !(c)->wstate.shaped && !(c)->wstate.below && !TEST_BORDERLESS(c) && !(PAspect & (c)->xsize.flags))

void attach_first2frame(gswm_t *gsw, wframe_t *target_fr)
{
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  GList *first = g_list_first(scr->detached_list);

  if(!target_fr) /* Select currently focused frame */
    target_fr = wframe_get_focused(gsw);
  if(!target_fr)
    return;

  if(first) {
    client_t *fc;
    client_t *c = first->data;

    fc = wframe_get_active_client(gsw, target_fr);

    if(CHECK_MULTIFRAME_CAP(fc) && CHECK_MULTIFRAME_CAP(c) && _check_frame_sizehints(target_fr, c)) {

      TRACE("%s x=%d fx=%d y=%d fy=%d w=%d fw=%d h=%d fh=%d",
            __func__, c->x, fc->x, c->y, fc->y,c->width, fc->width, c->height, fc->height);
      c->i_vdesk = scr->current_vdesk;
      c->x = fc->x;
      c->y = fc->y;
      c->width = fc->width;
      c->height = fc->height;
      c->wstate.maxi_vert = fc->wstate.maxi_vert;
      c->wstate.maxi_horz = fc->wstate.maxi_horz;
      c->wstate.fullscreen = fc->wstate.fullscreen;
      set_ewmh_net_wm_states(gsw, c);
      wframe_unbind_client(gsw, c);
      wframe_bind_client(gsw, target_fr, c);
      attach(gsw, c);
      /*wa_set_input_focus(gsw, c);*/
      focus_client(gsw, c, FALSE);
      /* Simulate a pager */
      /*wa_send_xclimsg(gsw, c, gsw->xa.wm_net_active_window, 2, CurrentTime, 0, 0, 0);*/
    }
    else {
      wa_moveresize(gsw, c);
      attach_first(gsw);
    }
  }
}

void install_colormaps(gswm_t *gsw, client_t *c)
{
  XWindowAttributes attr;
  Display *dpy = gsw->display;
  gboolean installed = FALSE;

  TRACE("%s w=%ld", __func__, c->win);
  if(c->cmap.num) {
    gint i;
    for(i = c->cmap.num - 1; i >= 0; i--) {
      /* Some clients insert windows which no longer exist (e.g. xli) */
      if(client_win_is_alive(gsw, c)) {
        XGetWindowAttributes(dpy, c->cmap.windows[i], &attr);
        XInstallColormap(dpy, attr.colormap);
        gsw->installed_cmap = attr.colormap;
        if(c->cmap.windows[i] == c->win)
          installed = TRUE;
      }
    }
  }
  if(!installed && c->cmap.dflt && c->cmap.dflt != gsw->installed_cmap) {
    XInstallColormap(dpy, c->cmap.dflt);
    gsw->installed_cmap =  c->cmap.dflt;
    TRACE("%s w=%ld cmap=0x%x", __func__, c->win, (guint)c->cmap.dflt);
  }
}

void update_colormaps(gswm_t *gsw, client_t *c)
{
    XWindowAttributes attr;

    X_FREE(c->cmap.windows);
    if(!XGetWMColormapWindows(gsw->display, c->win, &c->cmap.windows, &c->cmap.num)) {
        c->cmap.num = 0;
        c->cmap.windows = NULL;
    }
    XGetWindowAttributes(gsw->display, c->win, &attr);
    c->cmap.dflt = attr.colormap;
}

client_t *get_focused_client(gswm_t *gsw)
{
  Window focus_ret;
  gint revert_to_ret;
  client_t *c = NULL;

  XGetInputFocus(gsw->display, &focus_ret, &revert_to_ret);
  if(G_LIKELY(PointerRoot != focus_ret && None != focus_ret)) {
    c = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(focus_ret));
    if(G_UNLIKELY(!c)) {
      // Some applications use a so called FocusProxy pseudo window to handle the input
      // focus. Java applications use this technique. The FocusProxy window is a child of
      // the real application window. So try to figure out the parent window.
      Window rw, pw, *childlist;
      guint nchilds;
      if(XQueryTree(gsw->display, focus_ret, &rw, &pw, &childlist, &nchilds)) {
        c = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(pw));
        X_FREE(childlist);
      }
      else
        g_critical("%s: XQueryTree failed", G_STRFUNC);
    }
  }
  return c;
}

void set_frame_id_xprop(gswm_t *gsw, Window w, glong frame_id)
{
  XChangeProperty(gsw->display, w, gsw->xa.wm_octopus_frame_id, XA_CARDINAL, 32,
      PropModeReplace, (guint8*)&frame_id, 1);
}

gint get_frame_id_xprop(gswm_t *gsw, Window w)
{
  Atom real_type;
  gint real_format;
  gulong items_read, items_left;
  guint8 *data = NULL;
  glong value = 0;

  if(Success == XGetWindowProperty(gsw->display, w, gsw->xa.wm_octopus_frame_id,
        0L, 1L, False, XA_CARDINAL, &real_type, &real_format, &items_read, &items_left,
        &data) && items_read) {
    value = *(glong*)data;
    XFree(data);
  }
  return value;
}

