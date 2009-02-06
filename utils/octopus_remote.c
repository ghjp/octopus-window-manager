#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#define G_LOG_DOMAIN "octopus_remote"
#include <glib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

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

static void wmcommand(Display *dpy, gchar *data)
{
  XEvent	ev;
  Atom cmd_prop;
  Window	root = DefaultRootWindow(dpy);

  cmd_prop = XInternAtom(dpy, "_OCTOPUS_COMMAND", False);
  XChangeProperty(dpy, root, cmd_prop, XA_STRING, 8,
      PropModeReplace, (guchar*)data, strlen(data));

  memset(&ev, 0, sizeof ev);
  ev.xclient.type = ClientMessage;
  ev.xclient.window = root; 	/* we send it _from_ root as we have no win  */
  ev.xclient.message_type = cmd_prop;
  ev.xclient.format = 8;

  ev.xclient.data.l[0] = cmd_prop;

  XSendEvent(dpy, root, False, 
      SubstructureRedirectMask|SubstructureNotifyMask, &ev);
  XSync(dpy, False);
}

static void show_help(gchar *prgname)
{
  g_printerr("Usage: %s cmd_string [cmd_string ...]\n", prgname);
  exit(EXIT_FAILURE);
}

gint main(gint argc, gchar **argv)
{
  Display *dpy;
  gint i;

  if(G_UNLIKELY(1 == argc))
    show_help(argv[0]);
  if(!(dpy = XOpenDisplay(0))) {
    g_critical("XOpenDisplay failed");
    return EXIT_FAILURE;
  }
  g_message("Running on display `%s'", DisplayString(dpy));
  XSetErrorHandler(handle_xerror);
  XSetIOErrorHandler(handle_xioerror); /* Fatal error handler */
  g_message("argc=%d", argc);
  for(i = 1; i < argc; i++) {
    g_message("c=%s", argv[i]);
    wmcommand(dpy, argv[i]);
  }
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}

