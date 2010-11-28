#ifndef _OCTOPUS_H
#define _OCTOPUS_H

#include <glib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h> /* XSizeHints */
#include <X11/Xproto.h>

#ifdef FULL_DEBUG
#include "glib_log_ext.h"
#define TRACE(args...) g_debug(args)
#else
#define TRACE(args...) G_STMT_START{}G_STMT_END
#endif

#define ChildMask (SubstructureRedirectMask|SubstructureNotifyMask)
#define ButtonMask  (ButtonPressMask|ButtonReleaseMask)

/* Convenience macro */
#define X_FREE(p) G_STMT_START{ if(p) XFree(p); }G_STMT_END

#define STICKY -1

/* Borderless client possess a border value of G_MININT */
#define GET_BORDER_WIDTH(cl) (G_MININT == (cl)->wframe->bwidth ? 0: (cl)->wframe->bwidth)
#define TEST_BORDERLESS(cl) (G_MININT == (cl)->wframe->bwidth)

typedef struct _gswm gswm_t;

typedef struct {
  XColor unfocus, focus;
} xcolor_t;

typedef struct _client client_t;

typedef struct {
  /* Size of strut per border */
  CARD32 east;
  CARD32 west;
  CARD32 north;
  CARD32 south;
} strut_t;

/* simple rectangle structure; defined by two x,y pairs */
typedef struct {
  gint	x1, y1;
  gint	x2, y2;
} rect_t;

typedef struct {
  gint x, y, w, h;
} warea_t;

typedef struct {
  client_t *focused;
  GList *clnt_list;
  GList *frame_list;
  GSList *cstrut_list;
  strut_t master_strut;
  warea_t warea;
} vdesk_t;

typedef struct {
  gdouble r, g, b;
  gchar *rgbi_str;
} color_intensity_t;

typedef struct {
  gint border_width, titlebar_height;
  gint border_snap, client_snap, resize_inc_fraction;
  gboolean resize_opaque, move_opaque;
  gchar *titlebar_font_family, *xterm_cmd, *xterm_terminal_cmd;
  color_intensity_t focused_color, unfocused_color;
  color_intensity_t focused_text_color, unfocused_text_color;
  gdouble alpha_east, alpha_west;
  guint modifier;
  GPtrArray *vdesk_names;
  color_intensity_t osd_fgc, osd_bgc;
  gchar *osd_font;
  gint osd_height;
} user_config_t;

typedef struct {
  gint tbar_height, border_width, padding;
  gint pm_width, pm_height;
  Pixmap btn_normal, btn_sticky;
  Pixmap btn_normal_pressed, btn_sticky_pressed;
  Pixmap stat[10];
  GC gc_invert;
} frame_info_t;

typedef struct {
  Window rootwin, extended_hints_win;
  GArray *extended_client_list;
  gint current_vdesk;
  gint num_vdesk;
  vdesk_t *vdesk;
  GList *sticky_list;
  GList *detached_list;
  GList *desktop_list;
  GList *sticky_frlist;
  GList *detached_frlist;
  GList *desktop_frlist;
  GHashTable *win_group_hash;
  gint id;
  gint dpy_width, dpy_height;
  xcolor_t color;
  frame_info_t fr_info;
} screen_t;

typedef struct {
  Window win, titlewin, btnwin, statwin;
  gboolean focus_expose_hint;
  GList *client_list;
  GString *title_str;
  gint   bwidth, theight;
  gint id;
  XSizeHints xsize;
  gint pm_w, pm_h;
  Pixmap focused_tbar_pmap;
  Pixmap unfocused_tbar_pmap;
  Pixmap actual_tbar_button;
} wframe_t;

typedef struct {
  /* _NET_WM_STATE */
  gboolean modal :1;
  gboolean sticky :1;
  gboolean maxi_vert :1;
  gboolean maxi_horz :1;
  gboolean shaded :1;
  gboolean skip_taskbar :1;
  gboolean skip_pager :1;
  gboolean hidden :1;
  gboolean fullscreen :1;
  gboolean above :1;
  gboolean below :1;
  /* WM_PROTOCOLS */
  gboolean pr_delete :1;
  gboolean pr_take_focus :1;
  /* Misc */
  gboolean mwm_title :1; /* MWM */
  gboolean mwm_border :1; /* MWM */
  gboolean strut_valid :1;
  gboolean input_focus :1;
  gboolean shaped :1;
  gboolean has_net_wm_name :1;
} state_info_t;

typedef struct {
  gint num;
  Window *windows;
  Colormap dflt;
} cmap_info_t;

typedef struct {
  gboolean desktop :1;
  gboolean dock :1;
  gboolean toolbar :1;
  gboolean menu :1;
  gboolean utility :1;
  gboolean splash :1;
  gboolean dialog :1;
  gboolean normal :1;
  gboolean kde_override :1;
} window_type_t;

typedef struct _client {
  Window win;
  gint i_vdesk;
  gint x, y, width, height;
  gint x_old, y_old, width_old, height_old;
  gchar *utf8_name;
  screen_t *curr_screen;
  guint ignore_unmap;
  wframe_t *wframe;
  Window trans;
  Window window_group;
  state_info_t wstate;
  strut_t cstrut;
  XSizeHints xsize;
  cmap_info_t cmap;
  window_type_t w_type;
} _client_t;

/* s:^\s\+ADD.*->xa\.\(wm_\S\+\),.*:Atom    \1; */
typedef struct {
  Atom    wm_state;
  Atom    wm_change_state;
  Atom    wm_protocols;
  Atom    wm_delete;
  Atom    wm_colormap_windows;
  Atom    wm_take_focus;
  Atom    wm_client_leader;
  /* MWM hints */
  Atom    wm_mwm_hints;
  /* Extended WM hints */
  /* Root Window Properties */
  Atom    wm_net_supported;
  Atom    wm_net_client_list;
  Atom    wm_net_client_list_stacking;
  Atom    wm_net_number_of_desktops;
  Atom    wm_net_desktop_geometry;
  Atom    wm_net_desktop_viewport;
  Atom    wm_net_current_desktop;
  Atom    wm_net_desktop_names;
  Atom    wm_net_active_window;
  Atom    wm_net_workarea;
  Atom    wm_net_supporting_wm_check;
  /*ADD_NET_SUPPORT(gsw->xa.wm_net_virtual_roots,      "_NET_VIRTUAL_ROOTS");*/
  Atom    wm_net_desktop_layout;
  /*ADD_NET_SUPPORT(gsw->xa.wm_net_showing_desktop,    "_NET_SHOWING_DESKTOP");*/
  /* Application Window Properties */
  Atom    wm_net_wm_name;
  Atom    wm_net_wm_visible_name;
  Atom    wm_net_wm_desktop;
  Atom    wm_net_wm_window_type;
  Atom    wm_net_wm_allowed_actions;
  Atom    wm_net_wm_pid;
  Atom    wm_net_frame_extents;
  /* _NET_WM_WINDOW_TYPE atoms */
  Atom    wm_net_wm_window_type_desktop;
  Atom    wm_net_wm_window_type_dock;
  Atom    wm_net_wm_window_type_toolbar;
  Atom    wm_net_wm_window_type_menu;
  Atom    wm_net_wm_window_type_utility;
  Atom    wm_net_wm_window_type_splash;
  Atom    wm_net_wm_window_type_dialog;
  Atom    wm_net_wm_window_type_normal;
  Atom    wm_kde_net_wm_window_type_override;
  /* Other Root Window Messages */
  Atom    wm_net_close_window;
  Atom    wm_net_moveresize_window;
  Atom    wm_net_wm_moveresize;
  Atom    wm_net_request_frame_extents;
  /* Allowed application window actions */
  Atom    wm_net_wm_action_move;
  Atom    wm_net_wm_action_resize;
  Atom    wm_net_wm_action_minimize;
  Atom    wm_net_wm_action_shade;
  Atom    wm_net_wm_action_stick;
  Atom    wm_net_wm_action_maximize_horz;
  Atom    wm_net_wm_action_maximize_vert;
  Atom    wm_net_wm_action_fullscreen;
  Atom    wm_net_wm_action_change_desktop;
  Atom    wm_net_wm_action_close;
  /* _NET_WM_STATE */
  Atom    wm_net_wm_state;
  Atom    wm_net_wm_state_modal;
  Atom    wm_net_wm_state_sticky;
  Atom    wm_net_wm_state_maximized_vert;
  Atom    wm_net_wm_state_maximized_horz;
  Atom    wm_net_wm_state_shaded;
  Atom    wm_net_wm_state_skip_taskbar;
  Atom    wm_net_wm_state_skip_pager;
  Atom    wm_net_wm_state_hidden;
  Atom    wm_net_wm_state_fullscreen;
  Atom    wm_net_wm_state_above;
  Atom    wm_net_wm_state_below;
  Atom    wm_net_wm_strut;
  /* Octopus atoms */
  Atom    wm_octopus_frame_id;
  Atom    wm_octopus_command;
  /* Misc */
  Atom    wm_utf8_string;
  Atom    wm_xrootpmap_id;
  Atom    wm_xsetroot_id;
} xa_wm_t;

typedef struct {
  Cursor arrow;
  Cursor move;
  Cursor framemove;
  Cursor resize_nw;
  Cursor resize_ne;
  Cursor resize_sw;
  Cursor resize_se;
  Cursor resize_n;
  Cursor resize_s;
  Cursor resize_e;
  Cursor resize_w;
} curs_t;

typedef struct {
  GString     *line;
  GCompletion  *completion;
  GStringChunk *str_chunk;
  gpointer user_data;
} interaction_t;

typedef struct {
  gswm_t *gsw;
  guint iw, ih, horiz_offset, vert_offset;
  Window win;
} osd_cli_t;

struct _gswm {
  GSource gs;
  GMainContext* gmc;
  Display *display;
  GArray *auto_raise_frames;
  GHashTable  *win2clnt_hash;
  GHashTable  *win2frame_hash;
  gboolean alpha_processing;
  GTimer *autoraise_timer;
  GSList *unused_frame_id_list;
  GHashTable  *fid2frame_hash;
  GArray *net_wm_states_array;
  guint num_screens;
  gint      i_curr_scr;
  screen_t *screen;
  xa_wm_t xa;
  curs_t  curs;
  XFontStruct *font;
  guint numlockmask;
  gint fd_x;
  gint shape_event, xf86vm;
  guint shape:1;
  guint xrandr:1;
  gint event_xrandr;
  user_config_t ucfg;
  interaction_t cmd, action;
  osd_cli_t *osd_cmd, *osd_info;
  Colormap installed_cmap;
};

#endif /* _OCTOPUS_H */
