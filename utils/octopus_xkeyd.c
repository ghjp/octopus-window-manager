#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#define G_LOG_DOMAIN "octopus_xkeyd"
#include <glib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <sys/poll.h>
#include <errno.h>
#define USE_XLOOKUPSTRING
#undef  USE_XLOOKUPSTRING
#ifdef USE_XLOOKUPSTRING
#include <X11/Xutil.h>
#endif
#include <X11/XF86keysym.h>

#include "xkeyd_userconfig.h"

typedef struct {
  GSource gs;
  Display *display;
  gint xsrv_fd;
  GHashTable *keycode_action_hash;
  user_cfg_t ucfg;
} gs_ext_t;

static gint handle_xerror(Display *dsply, XErrorEvent *e)
{
  gchar errstr[128];

  XGetErrorText(dsply, e->error_code, errstr, sizeof(errstr));
  g_critical(
      "_X Error of failed request:  %s;"
      "  Major opcode of failed request: %3d;"
      "  Serial number of failed request:%5ld;",
      errstr, e->request_code, e->serial);
  return 0;
}

static gint handle_xioerror(Display *dsply)
{
  g_error("Got fatal Xserver IO error. Good bye!");
  return 0;
}

static gboolean _xsrv_prepare_fd_src(GSource *source, gint *timeout_)
{
  gs_ext_t *gse = (gs_ext_t *)source;
  /*g_message("%s fd=%d", __func__, gse->xsrv_fd); */
  *timeout_ = -1; /* Wait an infinite amount of time */
  /* For file descriptor sources, this function typically returns FALSE */
  return XPending(gse->display) ? TRUE: FALSE;
}

static gboolean _xsrv_check_fd_src(GSource *source)
{
  gs_ext_t *gse = (gs_ext_t *)source;
  struct pollfd ufds;
  gint pr;

  /*g_message("%s fd=%d", __func__, gse->xsrv_fd);*/
  ufds.fd = gse->xsrv_fd;
  ufds.events = POLLIN;
  if(0 > (pr = poll(&ufds, 1, 0)))
    g_error("%s poll failed: %s", __func__, g_strerror(errno));

  return pr ? TRUE: FALSE;
}

static gboolean _xsrv_dispatch_accept_src(GSource *source, GSourceFunc callback,
    gpointer user_data)
{
  gs_ext_t *gse = (gs_ext_t *)source;
  (void)gse; /* Keep compiler quiet */
  /*g_message("%s fd=%d", __func__, gse->xsrv_fd);*/
  return callback ? callback(user_data) : TRUE;
}

static void _xsrv_finalize_fd_src(GSource *source)
{
  gs_ext_t *gse = (gs_ext_t *)source;
  g_message("%s fd=%d", __func__, gse->xsrv_fd);
}

static gboolean _xsrv_event_cb(gpointer data)
{
  XEvent ev;
  gs_ext_t *gse = (gs_ext_t *)data;
  Display *dpy = gse->display;
  gchar *action;

  XNextEvent(dpy, &ev);
  if(KeyPress == ev.type) {
    GError *error = NULL;

    action = g_hash_table_lookup(gse->keycode_action_hash, GUINT_TO_POINTER(ev.xkey.keycode));
    if(action) {
      if(!g_spawn_command_line_async(action, &error)) {
        g_warning("%s", error->message);
        g_error_free(error);
      }
    }
    else
      g_warning("No action found for keycode %d", ev.xkey.keycode);
  }
  return TRUE; /* Keep this handler active */
}

static void x11_grab_keycode(Display *dpy, KeyCode kcode)
{
  gint i;
  for(i = 0; i < ScreenCount(dpy); i++)
    XGrabKey(dpy, kcode,
        AnyModifier, RootWindow(dpy,i),
        True, GrabModeAsync, GrabModeAsync);
}

static KeyCode grab_keysym(Display *dpy, GHashTable *ht, gchar *keystring, const gchar *action)
{
	KeySym sym;
	KeyCode code;

	if(NoSymbol == (sym = XStringToKeysym(keystring))) {
    g_warning("Symbol '%s' is unknown to X server", keystring);
		return 0;
  }
	if(0 == (code = XKeysymToKeycode(dpy, sym))) {
    g_warning("No keycode to symbol '%s' found", keystring);
		return 0;
  }

  x11_grab_keycode(dpy, code);
  g_hash_table_replace(ht, GUINT_TO_POINTER((guint)code), (gpointer)action);
  g_message("KeyCode %d (%s) grabbed", code, keystring);
	return code;
}

typedef struct {
  Display *dpy;
  GHashTable *target_hash;
} user_data_t;

static void try_to_grab_keysym(gpointer key, gpointer value, gpointer user_data)
{
  user_data_t *ud_p = user_data;
  grab_keysym(ud_p->dpy, ud_p->target_hash, key, value);
}

static void try_to_grab_keycode(gpointer key, gpointer value, gpointer user_data)
{
  user_data_t *ud_p = user_data;
  KeyCode kc = GPOINTER_TO_UINT(key);
  x11_grab_keycode(ud_p->dpy, kc);
  g_hash_table_replace(ud_p->target_hash, key, value);
  g_message("KeyCode %d grabbed", kc);
}

static void grab_keys(Display *dpy, GHashTable *ht, GHashTable *keysym_cmd_h, GHashTable *keycode_cmd_h)
{
  user_data_t ud;
  ud.dpy = dpy;
  ud.target_hash = ht;
  g_hash_table_foreach(keysym_cmd_h, try_to_grab_keysym, &ud);
  g_hash_table_foreach(keycode_cmd_h, try_to_grab_keycode, &ud);
}

gint main(void)
{
  Display *dpy;
  GMainLoop *gml = NULL;
  GMainContext* gmc;
  GSourceFuncs xsrv_gsf;
  GPollFD xsrv_pfd;
  gs_ext_t *xsrv_source;


  dpy = XOpenDisplay(0);
  g_return_val_if_fail(dpy, 1);
  g_message("Running on display `%s'", DisplayString(dpy));
  XSetErrorHandler(handle_xerror);
  XSetIOErrorHandler(handle_xioerror); /* Fatal error handler */

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

  xsrv_source = (gs_ext_t *)g_source_new(&xsrv_gsf, sizeof(*xsrv_source));
  xsrv_source->display = dpy;
  xsrv_source->xsrv_fd = ConnectionNumber(dpy);
  xsrv_source->keycode_action_hash = g_hash_table_new(NULL, NULL);
  g_source_set_callback((GSource*)xsrv_source, _xsrv_event_cb, xsrv_source, NULL);
  g_source_attach((GSource *)xsrv_source, gmc);
  /* We want to destroy everything with the call g_main_loop_unref */
  g_source_unref((GSource *)xsrv_source);

  xsrv_pfd.fd = xsrv_source->xsrv_fd;
  xsrv_pfd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
  g_message("xsrv_pfd.fd=%d", xsrv_pfd.fd);
  g_source_add_poll((GSource *)xsrv_source, &xsrv_pfd);

  init_xml_config(&xsrv_source->ucfg);
  read_xml_config(&xsrv_source->ucfg);
  grab_keys(dpy, xsrv_source->keycode_action_hash, xsrv_source->ucfg.keysym_command_hash, xsrv_source->ucfg.keycode_command_hash);

  /*
   * Now we are ready to enter the main event loop
   */
  g_message("%s event loop start", __func__);
  g_main_loop_run(gml);
  g_message("%s event loop stops", __func__);

  /* The next call frees also the context, removes the poll descriptors, ... */
  g_main_loop_unref(gml);

  XCloseDisplay(dpy);
  finalize_xml_config(&xsrv_source->ucfg);
  g_message("Good bye!!");
  return 0;
}

