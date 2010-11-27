#define G_LOG_DOMAIN "ewmh"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* Include headers {{{1 */
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h> /* memset */
#include <signal.h>

#include "octopus.h"
#include "ewmh.h"
#include "winactions.h"
#include "main.h"
#include "winactions.h"
#include "client.h"

#include <X11/cursorfont.h>
#include <X11/keysym.h> /* XK_Num_Lock */
#include <X11/Xatom.h> /* XA_CARDINAL, ... */

/* Defines {{{1 */
#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */

/* Functions {{{1 */
gpointer get_ewmh_net_property_data(gswm_t *gsw, client_t *c, Atom prop, Atom type, gint *items)
{
  Atom type_ret = None;
  gint format_ret = 0;
  gulong items_ret = 0;
  gulong after_ret = 0;
  guint8 *prop_data = NULL;

  XGetWindowProperty(gsw->display,
      c ? c->win: gsw->screen[gsw->i_curr_scr].rootwin,
      prop, 0, 0x7fffffff, False,
      type, &type_ret, &format_ret, &items_ret,
      &after_ret, &prop_data);
  if(items)
    *items = items_ret;

  return prop_data;
}

void set_ewmh_workarea(gswm_t *gsw)
{
  warea_t *workarea;
  gint i;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  /* Allocate on the stack. Very fast and no free necessary */
  workarea = g_newa(warea_t, scr->num_vdesk);

  for(i = 0; i < scr->num_vdesk; i++) {
    strut_t *cst = &scr->vdesk[i].master_strut;
    warea_t *wa  = &scr->vdesk[i].warea;

    wa->x = cst->west;
    wa->y = cst->north;
    wa->w = scr->dpy_width - cst->east - wa->x;
    wa->h = scr->dpy_height - cst->south - wa->y;

    workarea[i] = *wa;
  }

  XChangeProperty(gsw->display, scr->rootwin, gsw->xa.wm_net_workarea, XA_CARDINAL, 32,
      PropModeReplace, (guint8*)workarea, scr->num_vdesk*4);
}

#define ADD_NET_SUPPORT(val, name) val = XInternAtom(dpy, name, False);\
                                         g_array_append_val(net_supported_list, (val))

void init_ewmh_support(gswm_t *gsw)
{
  XSetWindowAttributes attr;
  Display *dpy = gsw->display;
  GArray *net_supported_list = g_array_new(FALSE, FALSE, sizeof(gsw->xa.wm_net_supported));
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  GString *wm_name = g_string_new(PACKAGE_STRING);

  /* Extended WM hints */
  /* Root Window Properties */
  ADD_NET_SUPPORT(gsw->xa.wm_net_supported,          "_NET_SUPPORTED");
  ADD_NET_SUPPORT(gsw->xa.wm_net_client_list,       "_NET_CLIENT_LIST");
  ADD_NET_SUPPORT(gsw->xa.wm_net_client_list_stacking, "_NET_CLIENT_LIST_STACKING");
  ADD_NET_SUPPORT(gsw->xa.wm_net_number_of_desktops, "_NET_NUMBER_OF_DESKTOPS");
  ADD_NET_SUPPORT(gsw->xa.wm_net_desktop_geometry,   "_NET_DESKTOP_GEOMETRY");
  ADD_NET_SUPPORT(gsw->xa.wm_net_desktop_viewport,   "_NET_DESKTOP_VIEWPORT");
  ADD_NET_SUPPORT(gsw->xa.wm_net_current_desktop,    "_NET_CURRENT_DESKTOP");
  ADD_NET_SUPPORT(gsw->xa.wm_net_desktop_names,      "_NET_DESKTOP_NAMES");
  ADD_NET_SUPPORT(gsw->xa.wm_net_active_window,      "_NET_ACTIVE_WINDOW");
  ADD_NET_SUPPORT(gsw->xa.wm_net_workarea,           "_NET_WORKAREA");
  ADD_NET_SUPPORT(gsw->xa.wm_net_supporting_wm_check,"_NET_SUPPORTING_WM_CHECK");
  /*ADD_NET_SUPPORT(gsw->xa.wm_net_virtual_roots,      "_NET_VIRTUAL_ROOTS");*/
  ADD_NET_SUPPORT(gsw->xa.wm_net_desktop_layout,     "_NET_DESKTOP_LAYOUT");
  /*ADD_NET_SUPPORT(gsw->xa.wm_net_showing_desktop,    "_NET_SHOWING_DESKTOP");*/
  /* Application Window Properties */
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_name,                 "_NET_WM_NAME");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_visible_name,         "_NET_WM_VISIBLE_NAME");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_desktop,              "_NET_WM_DESKTOP");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_window_type,          "_NET_WM_WINDOW_TYPE");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_allowed_actions,      "_NET_WM_ALLOWED_ACTIONS");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_pid,                  "_NET_WM_PID");
  ADD_NET_SUPPORT(gsw->xa.wm_net_frame_extents,           "_NET_FRAME_EXTENTS");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_visible_name,         "_NET_WM_VISIBLE_NAME");
  /* _NET_WM_WINDOW_TYPE atoms */
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_window_type_desktop,  "_NET_WM_WINDOW_TYPE_DESKTOP");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_window_type_dock,     "_NET_WM_WINDOW_TYPE_DOCK");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_window_type_toolbar,  "_NET_WM_WINDOW_TYPE_TOOLBAR");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_window_type_menu,     "_NET_WM_WINDOW_TYPE_MENU");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_window_type_utility,  "_NET_WM_WINDOW_TYPE_UTILITY");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_window_type_splash,   "_NET_WM_WINDOW_TYPE_SPLASH");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_window_type_dialog,   "_NET_WM_WINDOW_TYPE_DIALOG");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_window_type_normal,   "_NET_WM_WINDOW_TYPE_NORMAL");
  ADD_NET_SUPPORT(gsw->xa.wm_kde_net_wm_window_type_override, "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE");
  /* Other Root Window Messages */
  ADD_NET_SUPPORT(gsw->xa.wm_net_close_window,            "_NET_CLOSE_WINDOW");
  ADD_NET_SUPPORT(gsw->xa.wm_net_moveresize_window,       "_NET_MOVERESIZE_WINDOW");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_moveresize,           "_NET_WM_MOVERESIZE");
  ADD_NET_SUPPORT(gsw->xa.wm_net_request_frame_extents,   "_NET_REQUEST_FRAME_EXTENTS");
  /* Allowed application window actions */
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_action_move,          "_NET_WM_ACTION_MOVE");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_action_resize,        "_NET_WM_ACTION_RESIZE");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_action_minimize,      "_NET_WM_ACTION_MINIMIZE");
  /* TODO Not supported yet */
  /*ADD_NET_SUPPORT(gsw->xa.wm_net_wm_action_shade,         "_NET_WM_ACTION_SHADE");*/
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_action_stick,         "_NET_WM_ACTION_STICK");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_action_maximize_horz, "_NET_WM_ACTION_MAXIMIZE_HORZ");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_action_maximize_vert, "_NET_WM_ACTION_MAXIMIZE_VERT");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_action_fullscreen,    "_NET_WM_ACTION_FULLSCREEN");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_action_change_desktop,"_NET_WM_ACTION_CHANGE_DESKTOP");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_action_close,         "_NET_WM_ACTION_CLOSE");
  /* _NET_WM_STATE */
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state,                "_NET_WM_STATE");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state_modal,          "_NET_WM_STATE_MODAL");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state_sticky,         "_NET_WM_STATE_STICKY");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state_maximized_vert, "_NET_WM_STATE_MAXIMIZED_VERT");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state_maximized_horz, "_NET_WM_STATE_MAXIMIZED_HORZ");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state_shaded,         "_NET_WM_STATE_SHADED");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state_skip_taskbar,   "_NET_WM_STATE_SKIP_TASKBAR");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state_skip_pager,     "_NET_WM_STATE_SKIP_PAGER");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state_hidden,         "_NET_WM_STATE_HIDDEN");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state_fullscreen,     "_NET_WM_STATE_FULLSCREEN");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state_above,          "_NET_WM_STATE_ABOVE");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_state_below,          "_NET_WM_STATE_BELOW");
  ADD_NET_SUPPORT(gsw->xa.wm_net_wm_strut,                "_NET_WM_STRUT");

  TRACE("%s len=%d", __func__, net_supported_list->len);

  /* This window is does nothing more than store properties which let
     other apps know we are supporting the extended wm hints */
  scr->extended_hints_win = XCreateSimpleWindow(dpy, scr->rootwin, -200, -200, 5, 5, 0, 0, 0);

  attr.override_redirect = True;
  XChangeWindowAttributes(dpy, scr->extended_hints_win, CWOverrideRedirect, &attr);

  XChangeProperty(dpy, scr->extended_hints_win, gsw->xa.wm_net_wm_name, gsw->xa.wm_utf8_string, 8,
      PropModeReplace, (guint8*)wm_name->str, wm_name->len);

  XChangeProperty(dpy, scr->extended_hints_win, gsw->xa.wm_net_supporting_wm_check, XA_WINDOW, 32,
      PropModeReplace, (guint8*)&scr->extended_hints_win, 1);
  {
#define HNAME_MAXLEN 256
    gchar hname[HNAME_MAXLEN];
    CARD32 pid = getpid();
    Atom wm_client_machine = XInternAtom(dpy, "WM_CLIENT_MACHINE", False);

    XChangeProperty(dpy, scr->extended_hints_win, gsw->xa.wm_net_wm_pid, XA_CARDINAL, 32,
          PropModeReplace, (guint8*)&pid, 1);
#if HAVE_GETHOSTNAME
    if(!gethostname(hname, sizeof(hname))) {
      hname[HNAME_MAXLEN-1] = 0;
      XChangeProperty(dpy, scr->extended_hints_win, wm_client_machine, XA_STRING, 8,
          PropModeReplace, (guint8*)hname, strlen(hname));
    }
    else
      g_warning("%s gethostname call failed: %s", __func__, g_strerror(errno));
#endif
#undef HNAME_MAXLEN
    {
      XClassHint *wm_class_hint = XAllocClassHint();
      if(wm_class_hint) {
        wm_class_hint->res_name = PACKAGE_NAME;
        wm_class_hint->res_class = "Octopus";
        XSetClassHint(dpy, scr->extended_hints_win, wm_class_hint);
      }
      X_FREE(wm_class_hint);
    }
  }

  XChangeProperty(dpy, scr->rootwin, gsw->xa.wm_net_supporting_wm_check, XA_WINDOW, 32,
      PropModeReplace, (guint8*)&scr->extended_hints_win, 1);

  /* Set _NET_SUPPORTED hint */
  XChangeProperty(dpy, scr->rootwin, gsw->xa.wm_net_supported, XA_ATOM, 32,
      PropModeReplace, (guint8*)net_supported_list->data, net_supported_list->len);

  set_root_prop_cardinal(gsw, gsw->xa.wm_net_number_of_desktops, scr->num_vdesk);
  /*set_root_prop_cardinal(gsw, gsw->xa.wm_net_current_desktop, 0);*/
  set_root_prop_window(gsw, gsw->xa.wm_net_active_window, None);

  { /* Desktop geometry */
    CARD32 geometry[2];
    geometry[0] = scr->dpy_width;
    geometry[1] = scr->dpy_height;
    XChangeProperty(dpy, scr->rootwin, gsw->xa.wm_net_desktop_geometry, XA_CARDINAL, 32,
        PropModeReplace, (guint8 *)geometry, 2);
  }
  { /* Viewport */
    CARD32 *viewport = g_newa(CARD32, 2 * scr->num_vdesk);
    memset(viewport, 0, 2 * scr->num_vdesk * sizeof(*viewport));
    XChangeProperty(dpy, scr->rootwin, gsw->xa.wm_net_desktop_viewport, XA_CARDINAL, 32,
        PropModeReplace, (guint8 *)viewport, 2 * scr->num_vdesk);
  }
  set_ewmh_workarea(gsw);

  g_string_free(wm_name, TRUE);
  g_array_free(net_supported_list, TRUE);
}

#undef ADD_NET_SUPPORT

static void _append_clients_from_framelist(GArray *arr, GList *fr_list)
{
  wframe_t *frame;
  register GList *fr, *cl;

  for(fr = g_list_last(fr_list); fr; fr = g_list_previous(fr)) {
    frame = fr->data;
    for(cl = g_list_last(frame->client_list); cl; cl = g_list_previous(cl))
      g_array_append_val(arr, ((client_t*)cl->data)->win);
  }
}

void update_ewmh_net_client_list(gswm_t *gsw)
{
  gint i;
  vdesk_t *vd;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  scr->extended_client_list = g_array_set_size(scr->extended_client_list, 0);
  _append_clients_from_framelist(scr->extended_client_list, scr->detached_frlist);
  _append_clients_from_framelist(scr->extended_client_list, scr->desktop_frlist);
  for(i = 0; i < scr->num_vdesk; i++) {
    if(i != scr->current_vdesk) {
      vd = scr->vdesk + i;
      _append_clients_from_framelist(scr->extended_client_list, vd->frame_list);
    }
  }
  _append_clients_from_framelist(scr->extended_client_list, scr->sticky_frlist);
  /* Most top one are the clients on the current desktop */
  vd = scr->vdesk + scr->current_vdesk;
  _append_clients_from_framelist(scr->extended_client_list, vd->frame_list);

  TRACE("%s len=%d", __func__, scr->extended_client_list->len);
  XChangeProperty(gsw->display, scr->rootwin, gsw->xa.wm_net_client_list,
      XA_WINDOW, 32, PropModeReplace, (guint8*)scr->extended_client_list->data, scr->extended_client_list->len);
  XChangeProperty(gsw->display, scr->rootwin, gsw->xa.wm_net_client_list_stacking,
      XA_WINDOW, 32, PropModeReplace, (guint8*)scr->extended_client_list->data, scr->extended_client_list->len);
}

void set_ewmh_prop(gswm_t *gsw, client_t *c, Atom at, glong val)
{
  XChangeProperty(gsw->display, c->win,
      at, XA_CARDINAL, 32, PropModeReplace, (guint8 *) &val, 1);
}

gint get_ewmh_desktop(gswm_t *gsw, client_t *c)
{
  Atom real_type;
  gint real_format;
  gulong items_read, items_left;
  guint8 *data = NULL;
  glong value = G_MAXINT;

  if(Success == XGetWindowProperty(gsw->display, c->win, gsw->xa.wm_net_wm_desktop,
        0L, 1L, False, XA_CARDINAL, &real_type, &real_format, &items_read, &items_left,
        &data) && items_read) {
    value = *(glong*)data;
    XFree(data);
  }
  return value;
}

static void _correct_master_strut(gpointer data, gpointer user_data)
{
  client_t *c = (client_t*)data;
  gint dnum = GPOINTER_TO_INT(user_data);
  strut_t *cst = &c->curr_screen->vdesk[dnum].master_strut;

  if(cst->east < c->cstrut.east)
    cst->east = c->cstrut.east;
  if(cst->west < c->cstrut.west)
    cst->west = c->cstrut.west;
  if(cst->north < c->cstrut.north)
    cst->north = c->cstrut.north;
  if(cst->south < c->cstrut.south)
    cst->south = c->cstrut.south;
}

static void _del_strut_from_desktop(client_t *c, screen_t *scr, gint desknum)
{
  strut_t *ms = &scr->vdesk[desknum].master_strut;
  GSList *cslist = scr->vdesk[desknum].cstrut_list;

  ms->east = ms->west = ms->north = ms->south = 0;
  cslist = g_slist_remove(cslist, c);
  g_slist_foreach(cslist, _correct_master_strut, GINT_TO_POINTER(desknum));
  scr->vdesk[desknum].cstrut_list = cslist;
  TRACE("%s vdesk=%d master_strut.east=%u master_strut.west=%u master_strut.north=%u master_strut.south=%u",
      __func__, desknum, (guint)ms->east, (guint)ms->west,
      (guint)ms->north, (guint)ms->south);
}

void remove_ewmh_strut(gswm_t *gsw, client_t *c, gboolean add_strut_follows)
{
  screen_t *scr = c->curr_screen;

  if(!c->wstate.strut_valid)
    return;
  if(c->wstate.sticky) {
    gint j;
    for(j = 0; j < scr->num_vdesk; j++)
      _del_strut_from_desktop(c, scr, j);
  }
  else
    _del_strut_from_desktop(c, scr, c->i_vdesk);
  if(!add_strut_follows)
    set_ewmh_workarea(gsw);
}

static void _add_strut_to_desktop(client_t *c, screen_t *scr, gint desknum)
{
  strut_t *ms;
  GSList *cslist = scr->vdesk[desknum].cstrut_list;

  cslist = g_slist_append(cslist, c);
  g_slist_foreach(cslist, _correct_master_strut, GINT_TO_POINTER(desknum));
  scr->vdesk[desknum].cstrut_list = cslist;
  ms = &scr->vdesk[desknum].master_strut;
  TRACE("%s vdesk=%d master_strut.east=%u master_strut.west=%u master_strut.north=%u master_strut.south=%u",
      __func__, desknum, (guint)ms->east, (guint)ms->west, (guint)ms->north, (guint)ms->south);
}

void add_ewmh_strut(gswm_t *gsw, client_t *c)
{
  gint num = 0;
  CARD32 *tmp_strut = get_ewmh_net_property_data(gsw, c,
      gsw->xa.wm_net_wm_strut, XA_CARDINAL, &num);

  if(tmp_strut) {
    screen_t *scr = c->curr_screen;

    TRACE("%s is given w=%ld", __func__, c->win);
    c->wstate.strut_valid = TRUE;
    c->cstrut.west = tmp_strut[0];
    c->cstrut.east = tmp_strut[1];
    c->cstrut.north = tmp_strut[2];
    c->cstrut.south = tmp_strut[3];

    if(c->wstate.sticky) {
      gint j;
      for(j = 0; j < scr->num_vdesk; j++)
        _add_strut_to_desktop(c, scr, j);
    }
    else
      _add_strut_to_desktop(c, scr, c->i_vdesk);
    set_ewmh_workarea(gsw);
    XFree(tmp_strut);
  }
  else
    c->wstate.strut_valid = FALSE;
}

void fix_ewmh_position_based_on_struts(gswm_t *gsw, client_t *c)
{
  screen_t *scr = c->curr_screen;
	gboolean size_changed = FALSE;
  strut_t *ms = &scr->vdesk[scr->current_vdesk].master_strut;
		
  if(!c->wstate.strut_valid)
    return;
	if(c->x <  ms->west)
		c->x = ms->west;
	else if((c->x + c->width) > scr->dpy_width ||
      (c->x + c->width) > (scr->dpy_width - ms->east))
		c->x = (scr->dpy_width - c->width) - ms->east;
	
	if(c->y <  ms->north)
		c->y = ms->north;
	else if((c->y + c->height) > scr->dpy_height ||
		(c->y + c->height) > (scr->dpy_height - ms->south))
		c->y = (scr->dpy_height - c->height) - ms->south;

	if(0 > c->x)
    c->x = 0;
	if(0 > c->y)
    c->y = 0;
	
	if(c->width > scr->dpy_width) {
		c->width = scr->dpy_width - ms->east - c->x;
		size_changed = TRUE;
	}
	
	if(c->height > scr->dpy_height) {
		c->height = scr->dpy_height - ms->south - c->y;
    size_changed = TRUE;
	}
	
  TRACE("%s x=%d y=%d width=%d height=%d", __func__, c->x, c->y, c->width, c->height);
	if(size_changed)
    send_config(gsw, c);
}

void scan_ewmh_net_wm_states(gswm_t *gsw, client_t *c)
{
  gint num;
  Atom *states = get_ewmh_net_property_data(gsw, c, gsw->xa.wm_net_wm_state, XA_ATOM, &num);

  if(states) {
    while(0 <= --num) {
      Atom as = states[num];

      if(as == gsw->xa.wm_net_wm_state_modal)
        c->wstate.modal = TRUE;
      else if(as == gsw->xa.wm_net_wm_state_sticky)
        c->wstate.sticky = TRUE;
      else if(as == gsw->xa.wm_net_wm_state_maximized_vert)
        c->wstate.maxi_vert = TRUE;
      else if(as == gsw->xa.wm_net_wm_state_maximized_horz)
        c->wstate.maxi_horz = TRUE;
      else if(as == gsw->xa.wm_net_wm_state_shaded)
        c->wstate.shaded = TRUE;
      else if(as == gsw->xa.wm_net_wm_state_skip_taskbar)
        c->wstate.skip_taskbar = TRUE;
      else if(as == gsw->xa.wm_net_wm_state_skip_pager)
        c->wstate.skip_pager = TRUE;
      else if(as == gsw->xa.wm_net_wm_state_hidden)
        c->wstate.hidden = TRUE;
      else if(as == gsw->xa.wm_net_wm_state_fullscreen)
        c->wstate.fullscreen = TRUE;
      else if(as == gsw->xa.wm_net_wm_state_above)
        c->wstate.above = TRUE;
      else if(as == gsw->xa.wm_net_wm_state_below)
        c->wstate.below = TRUE;
    }
    XFree(states);
  }
}

void set_ewmh_net_wm_states(gswm_t *gsw, client_t *c)
{
  gsw->net_wm_states_array = g_array_set_size(gsw->net_wm_states_array, 0);

  if(c->wstate.modal)
    g_array_append_val(gsw->net_wm_states_array, gsw->xa.wm_net_wm_state_modal);
  if(c->wstate.sticky)
    g_array_append_val(gsw->net_wm_states_array, gsw->xa.wm_net_wm_state_sticky);
  if(c->wstate.maxi_vert)
    g_array_append_val(gsw->net_wm_states_array, gsw->xa.wm_net_wm_state_maximized_vert);
  if(c->wstate.maxi_horz)
    g_array_append_val(gsw->net_wm_states_array, gsw->xa.wm_net_wm_state_maximized_horz);
  if(c->wstate.shaded)
    g_array_append_val(gsw->net_wm_states_array, gsw->xa.wm_net_wm_state_shaded);
  if(c->wstate.skip_taskbar)
    g_array_append_val(gsw->net_wm_states_array, gsw->xa.wm_net_wm_state_skip_taskbar);
  if(c->wstate.skip_pager)
    g_array_append_val(gsw->net_wm_states_array, gsw->xa.wm_net_wm_state_skip_pager);
  if(c->wstate.hidden)
    g_array_append_val(gsw->net_wm_states_array, gsw->xa.wm_net_wm_state_hidden);
  if(c->wstate.fullscreen)
    g_array_append_val(gsw->net_wm_states_array, gsw->xa.wm_net_wm_state_fullscreen);
  if(c->wstate.above)
    g_array_append_val(gsw->net_wm_states_array, gsw->xa.wm_net_wm_state_above);
  if(c->wstate.below)
    g_array_append_val(gsw->net_wm_states_array, gsw->xa.wm_net_wm_state_below);

  if(gsw->net_wm_states_array->len)
    XChangeProperty(gsw->display, c->win, gsw->xa.wm_net_wm_state, XA_ATOM, 32,
        PropModeReplace, (guint8*)gsw->net_wm_states_array->data, gsw->net_wm_states_array->len);
  else
    XDeleteProperty(gsw->display, c->win, gsw->xa.wm_net_wm_state);
}

void send_ewmh_net_wm_state_add(gswm_t *gsw, client_t *c, Atom state)
{
  wa_send_xclimsg(gsw, c, gsw->xa.wm_net_wm_state, _NET_WM_STATE_ADD, state, 0, 0, 0);
}

void send_ewmh_net_wm_state_remove(gswm_t *gsw, client_t *c, Atom state)
{
  wa_send_xclimsg(gsw, c, gsw->xa.wm_net_wm_state, _NET_WM_STATE_REMOVE, state, 0, 0, 0);
}

void do_ewmh_net_wm_state_client_message(gswm_t *gsw, client_t *c, XClientMessageEvent *e)
{
  if(e->message_type == gsw->xa.wm_net_wm_state) {
    Atom a = e->data.l[1];
    TRACE("%s wm_net_wm_state", __func__);
redo_switch:
    switch(e->data.l[0]) {
      case _NET_WM_STATE_REMOVE:
        if(a == gsw->xa.wm_net_wm_state_fullscreen)
          wa_fullscreen(gsw, c, FALSE);
        if(a == gsw->xa.wm_net_wm_state_sticky)
          wa_sticky(gsw, c, FALSE);
        if(a == gsw->xa.wm_net_wm_state_above) {
          c->wstate.above = FALSE;
          set_ewmh_net_wm_states(gsw, c);
        }
        if(a == gsw->xa.wm_net_wm_state_below) {
          c->wstate.below = FALSE;
          set_ewmh_net_wm_states(gsw, c);
        }
        if(a == gsw->xa.wm_net_wm_state_maximized_horz)
          wa_unmaximize_hv(gsw, c, TRUE, e->data.l[2] == gsw->xa.wm_net_wm_state_maximized_vert);
        else if(a == gsw->xa.wm_net_wm_state_maximized_vert)
          wa_unmaximize_hv(gsw, c, e->data.l[2] == gsw->xa.wm_net_wm_state_maximized_horz, TRUE);
        break;
      case _NET_WM_STATE_ADD:
        if(a == gsw->xa.wm_net_wm_state_fullscreen)
          wa_fullscreen(gsw, c, TRUE);
        if(a == gsw->xa.wm_net_wm_state_sticky)
          wa_sticky(gsw, c, TRUE);
        if(a == gsw->xa.wm_net_wm_state_above) {
          c->wstate.above = TRUE;
          c->wstate.below = FALSE;
          wa_raise(gsw, c);
          set_ewmh_net_wm_states(gsw, c);
        }
        if(a == gsw->xa.wm_net_wm_state_below) {
          c->wstate.below = TRUE;
          c->wstate.above = FALSE;
          wa_lower(gsw, c);
          set_ewmh_net_wm_states(gsw, c);
        }
        if(a == gsw->xa.wm_net_wm_state_maximized_horz)
          wa_maximize_hv(gsw, c, TRUE, e->data.l[2] == gsw->xa.wm_net_wm_state_maximized_vert);
        else if(a == gsw->xa.wm_net_wm_state_maximized_vert)
          wa_maximize_hv(gsw, c, e->data.l[2] == gsw->xa.wm_net_wm_state_maximized_horz, TRUE);
        break;
      case _NET_WM_STATE_TOGGLE:
        if(a == gsw->xa.wm_net_wm_state_fullscreen)
          wa_fullscreen(gsw, c, !c->wstate.fullscreen);
        else if(a == gsw->xa.wm_net_wm_state_sticky)
          wa_sticky(gsw, c, !c->wstate.sticky);
        else if(a == gsw->xa.wm_net_wm_state_maximized_horz) {
          e->data.l[0] = c->wstate.maxi_horz ? _NET_WM_STATE_REMOVE: _NET_WM_STATE_ADD;
          goto redo_switch;
        }
        else if(a == gsw->xa.wm_net_wm_state_maximized_vert) {
          e->data.l[0] = c->wstate.maxi_vert ? _NET_WM_STATE_REMOVE: _NET_WM_STATE_ADD;
          goto redo_switch;
        }
        else if(a == gsw->xa.wm_net_wm_state_below) {
          e->data.l[0] = c->wstate.below ? _NET_WM_STATE_REMOVE: _NET_WM_STATE_ADD;
          goto redo_switch;
        }
        else if(a == gsw->xa.wm_net_wm_state_above) {
          e->data.l[0] = c->wstate.above ? _NET_WM_STATE_REMOVE: _NET_WM_STATE_ADD;
          goto redo_switch;
        }
#if 1
        else {
          gchar *an = XGetAtomName(gsw->display, a);
          if(an) {
            g_message("%s ignore _NET_WM_STATE_TOGGLE: %s", __func__, an);
            XFree(an);
          }
        }
#endif
        break;
      default:
        g_return_if_reached(); /* Never reached */
        break;
    }
  }
}

gint get_ewmh_net_current_desktop(gswm_t *gsw)
{
  gint num;
  CARD32 *ncd = get_ewmh_net_property_data(gsw, NULL, gsw->xa.wm_net_current_desktop, XA_CARDINAL, &num);
  
  if(ncd) {
    num = *ncd;
    XFree(ncd);
  }
  else
    num = 0;
  return num;
}

void set_ewmh_net_wm_allowed_actions(gswm_t *gsw, client_t *c)
{
  GArray *net_wm_allowed_actions_array =
    g_array_new(FALSE, FALSE, sizeof(gsw->xa.wm_net_wm_action_move));

  g_array_append_val(net_wm_allowed_actions_array, gsw->xa.wm_net_wm_action_minimize);
  g_array_append_val(net_wm_allowed_actions_array, gsw->xa.wm_net_wm_action_stick);
  g_array_append_val(net_wm_allowed_actions_array, gsw->xa.wm_net_wm_action_maximize_horz);
  g_array_append_val(net_wm_allowed_actions_array, gsw->xa.wm_net_wm_action_maximize_vert);
  g_array_append_val(net_wm_allowed_actions_array, gsw->xa.wm_net_wm_action_fullscreen);
  g_array_append_val(net_wm_allowed_actions_array, gsw->xa.wm_net_wm_action_change_desktop);
  g_array_append_val(net_wm_allowed_actions_array, gsw->xa.wm_net_wm_action_close);
  g_array_append_val(net_wm_allowed_actions_array, gsw->xa.wm_net_wm_action_resize);
  g_array_append_val(net_wm_allowed_actions_array, gsw->xa.wm_net_wm_action_move);

  XChangeProperty(gsw->display, c->win, gsw->xa.wm_net_wm_allowed_actions,
      XA_ATOM, 32, PropModeReplace,
      (guint8*)net_wm_allowed_actions_array->data, net_wm_allowed_actions_array->len);

  g_array_free(net_wm_allowed_actions_array, TRUE);
}

void set_ewmh_net_frame_extents(gswm_t *gsw, client_t *c)
{
  CARD32 frame_extents[4];

  if(TEST_BORDERLESS(c)) /* Borderless client */
    frame_extents[0] = frame_extents[1] = frame_extents[2] = frame_extents[3] = 0;
  else {
    frame_extents[0] = frame_extents[1] = frame_extents[2] = frame_extents[3] = c->wframe->bwidth;
    frame_extents[2] += c->wframe->theight; /* top */
  }
  XChangeProperty(gsw->display, c->win, gsw->xa.wm_net_frame_extents,
      XA_CARDINAL, 32, PropModeReplace,
      (guint8*)frame_extents, G_N_ELEMENTS(frame_extents));
}

void set_ewmh_net_wm_visible_name(gswm_t *gsw, client_t *c, GString *name)
{
  XChangeProperty(gsw->display, c->win, gsw->xa.wm_net_wm_visible_name,
      gsw->xa.wm_utf8_string, 8, PropModeReplace,
      (guint8*)name->str, name->len);
}

void get_ewmh_net_wm_window_type(gswm_t *gsw, client_t *c)
{
  Atom *w_type_atoms;
  gint num = 0;

  memset(&c->w_type, 0, sizeof(c->w_type));
  w_type_atoms = get_ewmh_net_property_data(gsw, c, gsw->xa.wm_net_wm_window_type,
      XA_ATOM, &num);
  if(w_type_atoms) {
    gint i;
    for(i = 0; i < num; i++) {
      Atom wta = w_type_atoms[i];
      if(gsw->xa.wm_net_wm_window_type_desktop == wta) {
        TRACE("%s wm_net_wm_window_type_desktop", __func__);
        c->w_type.desktop = TRUE;
      }
      if(gsw->xa.wm_net_wm_window_type_dock == wta) {
        TRACE("%s wm_net_wm_window_type_dock", __func__);
        c->w_type.dock = TRUE;
      }
      if(gsw->xa.wm_net_wm_window_type_toolbar == wta) {
        TRACE("%s wm_net_wm_window_type_toolbar", __func__);
        c->w_type.toolbar = TRUE;
      }
      if(gsw->xa.wm_net_wm_window_type_menu == wta) {
        TRACE("%s wm_net_wm_window_type_menu", __func__);
        c->w_type.menu = TRUE;
      }
      if(gsw->xa.wm_net_wm_window_type_utility == wta) {
        TRACE("%s wm_net_wm_window_type_utility", __func__);
        c->w_type.utility = TRUE;
      }
      if(gsw->xa.wm_net_wm_window_type_splash == wta) {
        TRACE("%s wm_net_wm_window_type_splash", __func__);
        c->w_type.splash = TRUE;
      }
      if(gsw->xa.wm_net_wm_window_type_dialog == wta) {
        TRACE("%s wm_net_wm_window_type_dialog", __func__);
        c->w_type.dialog = TRUE;
      }
      if(gsw->xa.wm_net_wm_window_type_normal == wta) {
        TRACE("%s wm_net_wm_window_type_normal", __func__);
        c->w_type.normal = TRUE;
      }
      if(gsw->xa.wm_kde_net_wm_window_type_override == wta) {
        TRACE("%s wm_kde_net_wm_window_type_override", __func__);
        c->w_type.kde_override = TRUE;
      }
    }
    XFree(w_type_atoms);
  }
  else {
    TRACE("%s no _NET_WM_WINDOW_TYPE defined for client %s", __func__, c->utf8_name);
    c->w_type.normal = TRUE;
  }
}

void set_ewmh_net_desktop_names(gswm_t *gsw)
{
  gint i;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  GString *dsk_str = g_string_new(NULL);

  for(i = 0; i < gsw->ucfg.vdesk_names->len; i++) {
    gchar *str_i = g_ptr_array_index(gsw->ucfg.vdesk_names, i);
    g_string_append(dsk_str, str_i);
    g_string_append_c(dsk_str, 0);
  }
  if(dsk_str->len) {
    /* Remove the last zero byte */
    g_string_truncate(dsk_str, dsk_str->len - 1);
    XChangeProperty(gsw->display, scr->rootwin, gsw->xa.wm_net_desktop_names, gsw->xa.wm_utf8_string, 8,
        PropModeReplace, (guint8*)dsk_str->str, dsk_str->len);
  }
  g_string_free(dsk_str, TRUE);
}
