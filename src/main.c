#define G_LOG_DOMAIN "main"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* Include headers {{{1 */
#include <sys/poll.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h> /* memset */
#include <signal.h>
#include <unistd.h> /* execvp */
#if HAVE_SYS_WAIT_H
#include <sys/wait.h> /* waitpid */
#endif
#include <locale.h> /* setlocale */

#include "octopus.h"
#include "main.h"
#include "winactions.h"
#include "events.h"
#include "client.h"
#include "ewmh.h"
#include "winframe.h"
#include "action_system.h"
#include "userconfig.h"
#include "input.h"
#include "xinerama.h"

#include <X11/cursorfont.h>
#include <X11/keysym.h> /* XK_Num_Lock */
#include <X11/Xatom.h> /* XA_CARDINAL, ... */
#ifdef HAVE_XSHAPE
#include <X11/extensions/shape.h> /* SHAPE */
#endif
#ifdef HAVE_XF86VM
#include <X11/extensions/xf86vmode.h>
#endif

/* Defines {{{1 */
#define MS_AUTORAISE 800

/* Typedefs {{{1 */
typedef struct {
  GHashTable *path_hash;
  GList *cmd_list;
  gswm_t *gsw;
  gint pidx;
  gchar **path_elems;
  gboolean running;
} _scan_exec_data_t;

/* Global privat data {{{1 */
static GMainLoop *gml = NULL;

/* X error handlers {{{1 */
static gint handle_init_xerror(Display *dsply, XErrorEvent *e)
{
  gchar errstr[128], number[32], request[128];

  if(X_ChangeWindowAttributes == e->request_code &&
      BadAccess == e->error_code) {
    g_critical("Seems that another window manager is running");
    exit(3);
  }

  XGetErrorText(dsply, e->error_code, errstr, sizeof(errstr));
  g_snprintf(number, sizeof(number), "%d", e->request_code);
  XGetErrorDatabaseText(dsply, "XRequest", number, "", request, sizeof(request));
  if(0 == request[0])
    g_snprintf(request, sizeof(request), "<request-code-%d>", e->request_code);
  g_critical("%s: %s (n=%d, XID=0x%lx): %s",
      __func__, request, e->request_code, e->resourceid, errstr);
  return 0;
}

static gint handle_xerror(Display *dsply, XErrorEvent *e)
{
  gchar errstr[128], number[32], request[128];

  /* If focused window is no longer viewable this error could
     happen. We ignore this silently */
  if(X_SetInputFocus == e->request_code &&
      BadMatch == e->error_code)
    return 0;

  XGetErrorText(dsply, e->error_code, errstr, sizeof(errstr));
  g_snprintf(number, sizeof(number), "%d", e->request_code);
  XGetErrorDatabaseText(dsply, "XRequest", number, "", request, sizeof(request));
  if(0 == request[0])
    g_snprintf(request, sizeof(request), "<request-code-%d>", e->request_code);
  g_critical("%s (n=%d, XID=0x%lx): %s",
      request, e->request_code, e->resourceid, errstr);
  return 0;
}

static gint handle_xioerror(Display *dsply)
{
  g_error("Got fatal Xserver IO error. Good bye!");
  return 0;
}

/* Glib2 event loop handling {{{1 */

static gboolean _xsrv_prepare_fd_src(GSource *source, gint *timeout_)
{
  gswm_t *gsw = (gswm_t *)source;
  /*TRACE(("%s fd=%d", __func__, gsw->fd_x)); */
  *timeout_ = -1; /* Wait an infinite amount of time */
  /* For file descriptor sources, this function typically returns FALSE */
  return G_LIKELY(XPending(gsw->display)) ? TRUE: FALSE;
}

static gboolean _xsrv_check_fd_src(GSource *source)
{
  gswm_t *gsw = (gswm_t *)source;
  struct pollfd ufds;
  gint pr;

  /* TRACE(("%s fd=%d", __func__, gsw->fd_x)); */
  ufds.fd = gsw->fd_x;
  ufds.events = POLLIN;
  if(0 > (pr = poll(&ufds, 1, 0)))
    g_error("%s poll failed: %s", __func__, g_strerror(errno));

  return (G_LIKELY(pr)) ? TRUE: FALSE;
}

static gboolean _xsrv_dispatch_accept_src(GSource *source, GSourceFunc callback,
    gpointer user_data)
{
  gswm_t *gsw = (gswm_t *)source;
  (void)gsw; /* Keep compiler quiet */
  /* TRACE(("%s fd=%d", __func__, gsw->fd_x)); */
  return G_LIKELY(callback) ? callback(user_data) : TRUE;
}

static void _xsrv_finalize_fd_src(GSource *source)
{
  gswm_t *gsw = (gswm_t *)source;
  g_message("%s fd=%d", __func__, gsw->fd_x);
}

static gboolean _xsrv_event_cb(gpointer data)
{
  process_xevent(data);
  return TRUE; /* Keep this handler active */
}

/* Initial display setup {{{1 */

static void assign_new_frame_id(gswm_t *gsw, Window child,
    GHashTable *frame_id_map, Window *childlist_wo_frame_id, gint *nchilds_wo_frame_id)
{
  static gint fr_id = 0;
  XWindowAttributes winattr;
  Display *dpy = gsw->display;

  if(XGetWindowAttributes(dpy, child, &winattr) &&
      !winattr.override_redirect && IsViewable == winattr.map_state) {
    gint fid = get_frame_id_xprop(gsw, child);

    if(fid) {
      gint new_fid = GPOINTER_TO_INT(g_hash_table_lookup(frame_id_map, GINT_TO_POINTER(fid)));

      if(!new_fid) {
        new_fid = ++fr_id;
        g_hash_table_insert(frame_id_map, GINT_TO_POINTER(fid), GINT_TO_POINTER(new_fid));
      }
      TRACE(("%s replace frame: %d -> %d", __func__, fid, new_fid));
      set_frame_id_xprop(gsw, child, new_fid);
    }
    else
      childlist_wo_frame_id[(*nchilds_wo_frame_id)++] = child;
  }
  else
    XDeleteProperty(dpy, child, gsw->xa.wm_octopus_frame_id);
}

static void _scan_clients_from_screen(gswm_t *gsw)
{
  XWindowAttributes winattr;
  Window rw, pw, *childlist;
  guint nchilds;
  Display *dpy = gsw->display;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  GHashTable *frame_id_map = g_hash_table_new(NULL, NULL);
  gint fr_id = 0;
  gint nchilds_wo_frame_id = 0;

  /* Needed to ensure that no window dies before we are ready */
  XGrabServer(dpy);
  XSync(dpy, False);
  if(XQueryTree(dpy, scr->rootwin, &rw, &pw, &childlist, &nchilds)) {
    Window *childlist_wo_frame_id, *trans_subwindows, *main_windows;
    gint i, trans_count, main_count;
    TRACE(("%d child windows found", nchilds));
    /* Split windows in two groups */
    trans_subwindows = g_newa(Window, nchilds);
    main_windows = g_newa(Window, nchilds);
    for(trans_count = main_count = i = 0; i < nchilds; i++) {
      Window trans_win;
      Window child = childlist[i];

      XGetTransientForHint(dpy, child, &trans_win);
      if(trans_win)
        trans_subwindows[trans_count++] = child;
      else
        main_windows[main_count++] = child;
    }
    /* Reassign new frame ids to existing ones. This is necessary because the existing frame ids
       are non successive */
    childlist_wo_frame_id = g_newa(Window, nchilds);
    /* First we inspect the main application windows */
    for(i = 0; i < main_count; i++)
      assign_new_frame_id(gsw, main_windows[i], frame_id_map, childlist_wo_frame_id, &nchilds_wo_frame_id);
    /* Next we inspect the dialog/transient windows */
    for(i = 0; i < trans_count; i++)
      assign_new_frame_id(gsw, trans_subwindows[i], frame_id_map, childlist_wo_frame_id, &nchilds_wo_frame_id);
    /* Clients which haven't a frame id yet get one now */
    for(i = 0; i < nchilds_wo_frame_id; i++) {
      set_frame_id_xprop(gsw, childlist_wo_frame_id[i], ++fr_id);
      TRACE(("%s assign new frame id: %d", __func__, fr_id));
    }
    /* Now we are ready to integrate the main application clients */
    for(i = 0; i < main_count; i++) {
      Window child = main_windows[i];

      if(XGetWindowAttributes(dpy, child, &winattr) &&
          !winattr.override_redirect && IsViewable == winattr.map_state)
        create_new_client(gsw, child);
    }
    /* Now we are ready to integrate the dialog/subwindow clients */
    for(i = 0; i < trans_count; i++) {
      Window child = trans_subwindows[i];

      if(XGetWindowAttributes(dpy, child, &winattr) &&
          !winattr.override_redirect && IsViewable == winattr.map_state)
        create_new_client(gsw, child);
    }
    X_FREE(childlist);
  }
  else
    g_error("XQueryTree failed");
  g_hash_table_destroy(frame_id_map);
  XUngrabServer(dpy);
}

static void _inst_root_kbd_shortcuts(gswm_t *gsw, guint mod_mask)
{
  Display *dpy = gsw->display;
  Window srwin = gsw->screen[gsw->i_curr_scr].rootwin;
  gint i, num_vd = MIN(12, gsw->screen[gsw->i_curr_scr].num_vdesk);

  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Return")),
      mod_mask, srwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Page_Up")),
      mod_mask, srwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Page_Down")),
      mod_mask, srwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("r")),
      ControlMask|mod_mask, srwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("a")),
      mod_mask, srwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("a")),
      ShiftMask|mod_mask, srwin, True, GrabModeAsync, GrabModeAsync);
  for(i = 1; i <= num_vd; i++) {
    gchar keysym_name[4];
    g_snprintf(keysym_name, sizeof(keysym_name), "F%d", i);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym(keysym_name)),
        mod_mask, srwin, True, GrabModeAsync, GrabModeAsync);
    if(10 > i) {
      g_snprintf(keysym_name, sizeof(keysym_name), "%d", i);
      XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym(keysym_name)),
          mod_mask, srwin, True, GrabModeAsync, GrabModeAsync);
    }
  }
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("e")),
      mod_mask, srwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("i")),
      mod_mask, srwin, True, GrabModeAsync, GrabModeAsync);
  XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Tab")),
      mod_mask, srwin, True, GrabModeAsync, GrabModeAsync);
}

void setup_root_kb_shortcuts(gswm_t *gsw)
{
  gint i, j;
  Display *dpy = gsw->display;
  XModifierKeymap *modmap = XGetModifierMapping(dpy);
  Window srwin = gsw->screen[gsw->i_curr_scr].rootwin;
  guint mod_mask = gsw->ucfg.modifier;

  /* Find out which modifier is NumLock - we'll use this when grabbing
   * every combination of modifiers we can think of */
  for(i = 0; i < 8; i++)
    for(j = 0; j < modmap->max_keypermod; j++)
      if(modmap->modifiermap[i*modmap->max_keypermod+j] == XKeysymToKeycode(dpy, XK_Num_Lock))
        gsw->numlockmask = (1<<i);
  XFreeModifiermap(modmap);

  XUngrabButton(dpy, AnyButton, AnyModifier, srwin);
  XUngrabKey(dpy, AnyKey, AnyModifier, srwin);
  /* TODO CFG Don't hardcode the key binding */
  _inst_root_kbd_shortcuts(gsw, mod_mask);
  _inst_root_kbd_shortcuts(gsw, mod_mask | gsw->numlockmask);
}

void set_root_prop_window(gswm_t *gsw, Atom at, glong val)
{
  XChangeProperty(gsw->display, gsw->screen[gsw->i_curr_scr].rootwin,
      at, XA_WINDOW, 32, PropModeReplace, (guint8 *) &val, 1);
}

void set_root_prop_cardinal(gswm_t *gsw, Atom at, glong val)
{
  XChangeProperty(gsw->display, gsw->screen[gsw->i_curr_scr].rootwin,
      at, XA_CARDINAL, 32, PropModeReplace, (guint8 *) &val, 1);
}

static void _init_cursors(gswm_t *gsw)
{
  Display *dpy = gsw->display;
  /* Cursors */
  gsw->curs.arrow = XCreateFontCursor(dpy, XC_top_left_arrow);
  gsw->curs.move  = XCreateFontCursor(dpy, XC_fleur);
  gsw->curs.framemove  = XCreateFontCursor(dpy, XC_hand1);
  gsw->curs.resize_nw  = XCreateFontCursor(dpy, XC_top_left_corner);
  gsw->curs.resize_ne  = XCreateFontCursor(dpy, XC_top_right_corner);
  gsw->curs.resize_sw  = XCreateFontCursor(dpy, XC_bottom_left_corner);
  gsw->curs.resize_se  = XCreateFontCursor(dpy, XC_bottom_right_corner);
  gsw->curs.resize_n   = XCreateFontCursor(dpy, XC_top_side);
  gsw->curs.resize_s   = XCreateFontCursor(dpy, XC_bottom_side);
  gsw->curs.resize_e   = XCreateFontCursor(dpy, XC_right_side);
  gsw->curs.resize_w   = XCreateFontCursor(dpy, XC_left_side);

}

static void _init_icccm_and_misc_atoms(gswm_t *gsw)
{
  Display *dpy = gsw->display;

  /* ICCCM atoms */
  gsw->xa.wm_state = XInternAtom(dpy, "WM_STATE", False);
  gsw->xa.wm_change_state = XInternAtom(dpy, "WM_CHANGE_STATE", False);
  gsw->xa.wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
  gsw->xa.wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  gsw->xa.wm_colormap_windows = XInternAtom(dpy, "WM_COLORMAP_WINDOWS", False);
  gsw->xa.wm_take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
  gsw->xa.wm_client_leader = XInternAtom(dpy, "WM_CLIENT_LEADER", False);
  /* Motif atoms MWM */
  gsw->xa.wm_mwm_hints = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
  /* Octopus atoms */
  gsw->xa.wm_octopus_frame_id = XInternAtom(dpy, "_OCTOPUS_FRAME_ID", False);
  gsw->xa.wm_octopus_command = XInternAtom(dpy, "_OCTOPUS_COMMAND", False);
  /* Misc */
  gsw->xa.wm_utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
  gsw->xa.wm_xrootpmap_id = XInternAtom(dpy, "_XROOTPMAP_ID", False);
  gsw->xa.wm_xsetroot_id = XInternAtom(dpy, "_XSETROOT_ID", False);
}

static void _init_display(const gchar *dpyname, gswm_t *gsw)
{
  Display *dpy = XOpenDisplay(dpyname);
  XSetWindowAttributes attr;
  XColor dummyc;
  XGCValues gv;
  gint si;

  if(!dpy)
    g_error("Unable to connect to X server. Check DISPLAY environment variable");
  else
    TRACE(("Running on display `%s'", DisplayString(dpy)));
  XSetErrorHandler(handle_init_xerror);
  XSetIOErrorHandler(handle_xioerror); /* Fatal error handler */

  /* Check necessary fonts */
  gsw->font = XLoadQueryFont(dpy, gsw->ucfg.xosd_font);
  if(!gsw->font) {
    g_critical("Unable to load user OSD font `%s'", gsw->ucfg.xosd_font);
    /* Try to get the default font */
    g_free(gsw->ucfg.xosd_font);
    gsw->ucfg.xosd_font = g_strdup("fixed");
    gsw->font = XLoadQueryFont(dpy, gsw->ucfg.xosd_font);
    if(!gsw->font)
      g_error("Failed to load the default font 'fixed'");
  }

  gsw->fd_x = ConnectionNumber(dpy); /* Set the event loop file descriptor */
  gsw->display = dpy;
  gsw->win2clnt_hash = g_hash_table_new(NULL, NULL);
  gsw->win2frame_hash = g_hash_table_new(NULL, NULL);
  gsw->fid2frame_hash = g_hash_table_new(NULL, NULL);
  gsw->memcache_client = g_mem_chunk_create(client_t, 32, G_ALLOC_AND_FREE);
  gsw->memcache_frame = g_mem_chunk_create(wframe_t, 32, G_ALLOC_AND_FREE);
  gsw->autoraise_timer = g_timer_new();
  gsw->net_wm_states_array = g_array_new(FALSE, FALSE, sizeof(gsw->xa.wm_net_wm_state_sticky));

  /* SHAPE */
#ifdef HAVE_XSHAPE
  gsw->shape = XShapeQueryExtension(dpy, &gsw->shape_event, &si); 
  TRACE(("%s shape=%d shape_event=%d", __func__, gsw->shape, gsw->shape_event));
#endif
#ifdef HAVE_XF86VM
  gsw->xf86vm = XF86VidModeQueryExtension(dpy, &si, &si); 
#endif
  {
    gboolean xinerama_present = xinerama_init(dpy);
    g_message("Detected X extensions:%s%s%s",
        gsw->shape ? " SHAPE": "",
        xinerama_present ? " Xinerama": "",
        gsw->xf86vm ? " XFree86-VidModeExtension": "");
  }

  _init_cursors(gsw);

  /* Set up each screen */
  gsw->num_screens = ScreenCount(dpy);
  gsw->screen = g_new0(screen_t, gsw->num_screens);
  gsw->i_curr_scr = DefaultScreen(dpy);

  for(si = 0; si < gsw->num_screens; si++) {
    gint j;
    gint srwin = RootWindow(dpy, si);
    screen_t *scr = gsw->screen + si;

    scr->id = si;
    scr->dpy_width = DisplayWidth(dpy, si);
    scr->dpy_height = DisplayHeight(dpy, si);
    scr->rootwin = srwin;
    scr->num_vdesk = gsw->ucfg.vdesk_names->len;
    scr->vdesk = g_new0(vdesk_t, scr->num_vdesk);
    for(j = 0; j < scr->num_vdesk; j++) {
      scr->vdesk[j].warea.w = scr->dpy_width;
      scr->vdesk[j].warea.h = scr->dpy_height;
    }
    scr->win_group_hash = g_hash_table_new(NULL, NULL);
    scr->extended_client_list = g_array_new(FALSE, FALSE, sizeof(scr->rootwin));
    scr->fr_info.border_width = gsw->ucfg.border_width;
    scr->fr_info.padding = 1; /* TODO We don't need a variable to realize this */
    scr->fr_info.tbar_height = gsw->ucfg.titlebar_height;

    /* Try to get the window manager of the screen.
       If this call fails then another window manager is already running */
    attr.event_mask = ChildMask | PropertyChangeMask | EnterWindowMask | ButtonMask;
    XChangeWindowAttributes(dpy, srwin, CWEventMask, &attr);

    XDefineCursor(dpy, srwin, gsw->curs.arrow);
    gsw->ucfg.focused_color.rgbi_str = g_strdup_printf("rgbi:%.2lf/%.2lf/%.2lf",
        gsw->ucfg.focused_color.r, gsw->ucfg.focused_color.g,
        gsw->ucfg.focused_color.b);
    XAllocNamedColor(dpy, DefaultColormap(dpy, si),
        gsw->ucfg.focused_color.rgbi_str, &scr->color.focus, &dummyc);
    gsw->ucfg.unfocused_color.rgbi_str = g_strdup_printf("rgbi:%.2lf/%.2lf/%.2lf",
        gsw->ucfg.unfocused_color.r, gsw->ucfg.unfocused_color.g,
        gsw->ucfg.unfocused_color.b);
    XAllocNamedColor(dpy, DefaultColormap(dpy, si),
        gsw->ucfg.unfocused_color.rgbi_str, &scr->color.unfocus, &dummyc);
    /* Graphic context GC */
    gv.function = GXinvert;
    gv.subwindow_mode = IncludeInferiors;
    gv.line_width = scr->fr_info.border_width;
    gv.font = gsw->font->fid;
    scr->fr_info.gc_invert = XCreateGC(dpy, scr->rootwin, GCFunction|GCSubwindowMode|GCLineWidth|GCFont, &gv);

    gsw->i_curr_scr = si;
    _init_icccm_and_misc_atoms(gsw);
    init_ewmh_support(gsw);
    scr->current_vdesk = get_ewmh_net_current_desktop(gsw);
    if(scr->current_vdesk >= scr->num_vdesk)
      scr->current_vdesk = scr->num_vdesk - 1;
    setup_root_kb_shortcuts(gsw);
    wframe_button_init(gsw);
    _scan_clients_from_screen(gsw);
    set_ewmh_net_desktop_names(gsw);
  } /* end screen loop */
  gsw->i_curr_scr = DefaultScreen(dpy);
  set_root_prop_cardinal(gsw, gsw->xa.wm_net_current_desktop, gsw->screen[gsw->i_curr_scr].current_vdesk);
  g_message("Initialised %d screens", gsw->num_screens);
  XSetErrorHandler(handle_xerror);
  XSync(dpy, False);
}

/* Signal handling {{{1 */

static gboolean restart_flag = FALSE;

void sig_main_loop_quit(int sig)
{
  if(gml) {
    g_warning("%s: signal (%s: %d) caught", __func__, g_strsignal(sig), sig);
    if(SIGUSR1 == sig)
      restart_flag = TRUE;
    g_main_loop_quit(gml);
  }
}

/* Main program {{{1 */

void signal_raise_window(gswm_t *gsw, gboolean do_it)
{
  if(!do_it)
    g_array_set_size(gsw->auto_raise_frames, 0);
  g_timer_start(gsw->autoraise_timer);
}

static gboolean _autoraise_cb(gpointer data)
{
  gswm_t *gsw = (gswm_t *)data;
  gboolean raise_done = FALSE;
  client_t *clnt;

  if(gsw->auto_raise_frames->len) {
    gint i;
    gdouble te = g_timer_elapsed(gsw->autoraise_timer, NULL);
    for(i = (gint)gsw->auto_raise_frames->len - 1; i >= 0; i--) {
      Window frame_win = g_array_index(gsw->auto_raise_frames, Window, i);

      if(((gdouble)MS_AUTORAISE/1000.) < te &&
          (clnt = wframe_lookup_client_for_window(gsw, frame_win))) {
        TRACE(("%s '%s'", __func__, clnt->utf8_name));
        wa_raise(gsw, clnt);
        raise_done = TRUE;
      }
    }
    if(raise_done)
      g_array_set_size(gsw->auto_raise_frames, 0);
    update_ewmh_net_client_list(gsw);
  }
  else
    g_timer_start(gsw->autoraise_timer);
  return TRUE; /* Keep handler running */
}

static void _exec_scan_prepare(gswm_t *gsw, _scan_exec_data_t *sed)
{
  const gchar *env_path = g_getenv("PATH");

  g_completion_clear_items(gsw->cmd.completion);
  /* The completion strings are stored in the string chunk.
     So do a new setup of this stuff. This is necessary to make the
     whole scanning recallable.  We don't want to get memory leaks */
  if(gsw->cmd.str_chunk)
    g_string_chunk_free(gsw->cmd.str_chunk);
  gsw->cmd.str_chunk = g_string_chunk_new(32<<10);
  sed->path_hash = g_hash_table_new(g_str_hash, g_str_equal);
  sed->cmd_list = NULL;
  sed->gsw = gsw;

  sed->pidx = 0;
  if(env_path)
    sed->path_elems = g_strsplit(env_path, G_SEARCHPATH_SEPARATOR_S, 0);
  else
    sed->path_elems = NULL;
}

static void _exec_scan_finalize(gpointer data)
{
  _scan_exec_data_t *sed = (_scan_exec_data_t*)data;

  g_hash_table_destroy(sed->path_hash);
  g_list_free(sed->cmd_list);
  g_strfreev(sed->path_elems);
  sed->running = FALSE;
}

static gboolean _exec_scan_cb(gpointer data)
{
  GDir *dir;
  _scan_exec_data_t *sed = (_scan_exec_data_t*)data;
  gchar *dirpath = sed->path_elems[sed->pidx];

  /* Stop idle event loop handler and therefore calls _exec_scan_finalize */
  if(!dirpath) {
    sed->cmd_list = g_list_sort(sed->cmd_list, (gint(*)(gconstpointer, gconstpointer))strcmp);
    g_completion_add_items(sed->gsw->cmd.completion, sed->cmd_list);
    g_message("%s found %d executables", __func__, g_list_length(sed->cmd_list));
    return FALSE;
  }

  if((dir = g_dir_open(dirpath, 0, NULL))) {
    /* Filter out duplicates of path entries */
    if(!g_hash_table_lookup(sed->path_hash, dirpath)) {
      const gchar *dent;

      g_hash_table_insert(sed->path_hash, dirpath, GINT_TO_POINTER(1));
      while((dent = g_dir_read_name(dir))) {
        gchar *fname = g_build_filename(dirpath, dent, NULL);
        if(!g_file_test(fname, G_FILE_TEST_IS_DIR) &&
            g_file_test(fname, G_FILE_TEST_IS_EXECUTABLE)) {
          TRACE(("+x %s", fname));
          sed->cmd_list = g_list_prepend(sed->cmd_list,
              g_string_chunk_insert(sed->gsw->cmd.str_chunk, dent));
        }
        g_free(fname);
      }
    }
    g_dir_close(dir);
  }
  sed->pidx++;
  return TRUE; /* Keep on running */
}

static void _exec_scan_add_to_event_loop(_scan_exec_data_t *sed)
{
  GSource *idle_job = g_idle_source_new();

  g_source_set_callback(idle_job, _exec_scan_cb, sed, _exec_scan_finalize);
  g_source_attach(idle_job, sed->gsw->gmc);
  /* We want to destroy everything with the call g_main_loop_unref */
  g_source_unref(idle_job);
}

void rehash_executables(gswm_t *gsw)
{
  _scan_exec_data_t *sed = (_scan_exec_data_t*)gsw->cmd.user_data;

  if(!sed->running) {
    sed->running = TRUE;
    _exec_scan_prepare(gsw, sed);
    _exec_scan_add_to_event_loop(sed);
  }
  else
    g_warning("%s already running one instance", __func__);
}

static void _sig_child_handler(int sig)
{
  waitpid(-1, NULL, WNOHANG);
}

gint main(gint argc, gchar **argv)
{
  gint s;
  GMainContext* gmc;
  GSourceFuncs xsrv_gsf;
  GPollFD xsrv_pfd;
  gswm_t *xsrv_source;
  struct sigaction sa;
  _scan_exec_data_t sed;

  g_message("!     Welcome to " PACKAGE_STRING);
  g_message("! (c) 2005-2009 Dr. Johann Pfefferl");
  g_message("! (c) Distributed under the terms and conditions of the GPL");

  if(!setlocale(LC_ALL, ""))
    g_warning("setlocale call failed");

  /* We need this because OSD library uses threads */
  if(!XInitThreads())
    g_critical("XInitThreads() failed");

  if(!XSetLocaleModifiers(""))
    g_warning("Cannot set locale modifiers for the X server");

  /* Setup signal handlers */
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sig_main_loop_quit;
  sigemptyset(&sa.sa_mask);
  if(0 > sigaction(SIGTERM, &sa, NULL))
    g_error("cannot install sighandler SIGTERM: %s", g_strerror(errno));
  if(0 > sigaction(SIGINT, &sa, NULL))
    g_error("cannot install sighandler SIGINT: %s", g_strerror(errno));
  if(0 > sigaction(SIGHUP, &sa, NULL))
    g_error("cannot install sighandler SIGHUP: %s", g_strerror(errno));
  if(0 > sigaction(SIGUSR1, &sa, NULL))
    g_error("cannot install sighandler SIGUSR1: %s", g_strerror(errno));
  sa.sa_handler = _sig_child_handler;
  if(0 > sigaction(SIGCHLD, &sa, NULL))
    g_error("cannot install sighandler SIGCHLD: %s", g_strerror(errno));

  /* Create main event loop */
  gmc = g_main_context_new();
  gml = g_main_loop_new(gmc, FALSE);
  /* We want to destroy everything with the call g_main_loop_unref */
  g_main_context_unref(gmc);

  /*
   * We create a "source" for reading the Xserver file descriptor
   */
  xsrv_gsf.prepare = _xsrv_prepare_fd_src;
  xsrv_gsf.check = _xsrv_check_fd_src;
  xsrv_gsf.dispatch = _xsrv_dispatch_accept_src;
  xsrv_gsf.finalize = _xsrv_finalize_fd_src;

  xsrv_source = (gswm_t *)g_source_new(&xsrv_gsf, sizeof(*xsrv_source));
  xsrv_source->gmc = gmc;
  xsrv_source->auto_raise_frames = g_array_new(FALSE, FALSE, sizeof(Window));

  /* This must be the first because read_xml_config calls
     action_system_add !! */
  action_system_create(xsrv_source, gml);
  init_xml_config(xsrv_source);
  read_xml_config(xsrv_source);
  if(1.0 != xsrv_source->ucfg.alpha_west || 1.0 != xsrv_source->ucfg.alpha_east)
    xsrv_source->alpha_processing = TRUE;

  /* Setup command completion search job into the event loop */
  xsrv_source->cmd.line = g_string_new(NULL);
  xsrv_source->cmd.completion = g_completion_new(NULL);
  xsrv_source->cmd.str_chunk = NULL;
  memset(&sed, 0, sizeof(sed));
  xsrv_source->cmd.user_data = &sed;
  rehash_executables(xsrv_source);

  _init_display(g_getenv("DISPLAY"), xsrv_source);
  input_create(xsrv_source);

  g_source_set_callback((GSource*)xsrv_source, _xsrv_event_cb, xsrv_source, NULL);
  g_source_attach((GSource *)xsrv_source, gmc);
  /* We want to destroy everything with the call g_main_loop_unref */
  g_source_unref((GSource *)xsrv_source);

  xsrv_pfd.fd = xsrv_source->fd_x;
  xsrv_pfd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
  TRACE(("xsrv_pfd.fd=%d", xsrv_pfd.fd));
  g_source_add_poll((GSource *)xsrv_source, &xsrv_pfd);

  /* Setup the autoraise function */
  {
    GSource *tsource = g_timeout_source_new((3*MS_AUTORAISE)/8);
    g_source_set_callback(tsource, _autoraise_cb, xsrv_source, NULL);
    g_source_attach(tsource, gmc);
    /* We want to destroy everything with the call g_main_loop_unref */
    g_source_unref(tsource);
  }
  /*
   * Now we are ready to enter the main event loop
   */
  g_message("%s event loop start", __func__);
  g_main_loop_run(gml);
  g_message("%s event loop stops", __func__);

  /* Must be the first */
  input_destroy(xsrv_source);
  /* Cleanup all screen relevant resources */
  for(s = 0; s < xsrv_source->num_screens; s++) {
    gint vi;
    screen_t *scr = xsrv_source->screen + s;

    for(vi = 0; vi < scr->num_vdesk; vi++) {
      GList **clp = &scr->vdesk[vi].clnt_list;
      /* The value *clp is modified inside remove_client() */
      while(*clp)
        remap_client(xsrv_source, (client_t*)((*clp)->data));
      g_list_free(*clp);
      g_list_free(scr->vdesk[vi].frame_list);
    }
    while(scr->detached_list)
      remap_client(xsrv_source, (client_t*)(scr->detached_list->data));
    g_list_free(scr->detached_list);
    g_list_free(scr->detached_frlist);
    while(scr->sticky_list)
      remap_client(xsrv_source, (client_t*)(scr->sticky_list->data));
    g_list_free(scr->sticky_list);
    g_list_free(scr->sticky_frlist);
    while(scr->desktop_list)
      remap_client(xsrv_source, (client_t*)(scr->desktop_list->data));
    g_list_free(scr->desktop_list);
    g_list_free(scr->desktop_frlist);
    g_free(scr->vdesk);
    g_array_free(scr->extended_client_list, TRUE);
    g_hash_table_destroy(scr->win_group_hash);

    XUndefineCursor(xsrv_source->display, scr->rootwin);
    XInstallColormap(xsrv_source->display, DefaultColormap(xsrv_source->display, s));
    XFreeGC(xsrv_source->display, scr->fr_info.gc_invert);
    XDestroyWindow(xsrv_source->display, scr->extended_hints_win);
    XFreePixmap(xsrv_source->display, scr->fr_info.btn_normal);
    XFreePixmap(xsrv_source->display, scr->fr_info.btn_sticky);
    XFreePixmap(xsrv_source->display, scr->fr_info.btn_normal_pressed);
    XFreePixmap(xsrv_source->display, scr->fr_info.btn_sticky_pressed);
  }
  /* Cleanup all global resources */
  if(xsrv_source->font)
    XFreeFont(xsrv_source->display, xsrv_source->font);
  XFreeCursor(xsrv_source->display, xsrv_source->curs.arrow);
  XFreeCursor(xsrv_source->display, xsrv_source->curs.move);
  XFreeCursor(xsrv_source->display, xsrv_source->curs.framemove);
  XFreeCursor(xsrv_source->display, xsrv_source->curs.resize_nw);
  XFreeCursor(xsrv_source->display, xsrv_source->curs.resize_ne);
  XFreeCursor(xsrv_source->display, xsrv_source->curs.resize_sw);
  XFreeCursor(xsrv_source->display, xsrv_source->curs.resize_se);
  XFreeCursor(xsrv_source->display, xsrv_source->curs.resize_n);
  XFreeCursor(xsrv_source->display, xsrv_source->curs.resize_s);
  XFreeCursor(xsrv_source->display, xsrv_source->curs.resize_e);
  XFreeCursor(xsrv_source->display, xsrv_source->curs.resize_w);
  g_free(xsrv_source->ucfg.titlebar_font_family);
  g_free(xsrv_source->ucfg.xterm_cmd);
  g_free(xsrv_source->ucfg.xterm_terminal_cmd);
  g_free(xsrv_source->ucfg.focused_color.rgbi_str);
  g_free(xsrv_source->ucfg.unfocused_color.rgbi_str);
  g_free(xsrv_source->ucfg.focused_text_color.rgbi_str);
  g_free(xsrv_source->ucfg.unfocused_text_color.rgbi_str);
  g_free(xsrv_source->ucfg.xosd_font);
  g_free(xsrv_source->ucfg.xosd_color);
  g_free(xsrv_source->screen);
  g_free(xsrv_source->ucfg.osd_font);
  g_free(xsrv_source->ucfg.osd_bgc.rgbi_str);
  g_free(xsrv_source->ucfg.osd_fgc.rgbi_str);
  g_array_free(xsrv_source->net_wm_states_array, TRUE);
  g_array_free(xsrv_source->auto_raise_frames, TRUE);
  g_hash_table_destroy(xsrv_source->win2clnt_hash);
  g_hash_table_destroy(xsrv_source->win2frame_hash);
  g_hash_table_destroy(xsrv_source->fid2frame_hash);
  g_mem_chunk_destroy(xsrv_source->memcache_client);
  g_mem_chunk_destroy(xsrv_source->memcache_frame);
  g_slist_free(xsrv_source->unused_frame_id_list);
  g_timer_destroy(xsrv_source->autoraise_timer);
  g_string_free(xsrv_source->cmd.line, TRUE);
  g_completion_free(xsrv_source->cmd.completion);
  g_string_chunk_free(xsrv_source->cmd.str_chunk);
  action_system_destroy(xsrv_source);
  g_ptr_array_free(xsrv_source->ucfg.vdesk_names, TRUE);
  xinerama_shutdown();

  XSetInputFocus(xsrv_source->display, PointerRoot, RevertToPointerRoot, CurrentTime);
  XCloseDisplay(xsrv_source->display);

  /* The next call frees also the context, removes the poll descriptors, sources, ... */
  g_main_loop_unref(gml);
  if(restart_flag) {
    execvp(argv[0], argv);
    g_error("Restart of window manager failed: %s", g_strerror(errno));
  }
  return 0;
}

