#ifndef _EWMH_H
#define _EWMH_H

gpointer get_ewmh_net_property_data(gswm_t *gsw, client_t *c, Atom prop, Atom type, gint *items);
void set_ewmh_workarea(gswm_t *gsw);
void init_ewmh_support(gswm_t *gsw);
void update_ewmh_net_client_list(gswm_t *gsw);
void set_ewmh_prop(gswm_t *gsw, client_t *c, Atom at, glong val);
gint get_ewmh_desktop(gswm_t *gsw, client_t *c);
void remove_ewmh_strut(gswm_t *gsw, client_t *c, gboolean add_strut_follows);
void add_ewmh_strut(gswm_t *gsw, client_t *c);
void fix_ewmh_position_based_on_struts(gswm_t *gsw, client_t *c);
void scan_ewmh_net_wm_states(gswm_t *gsw, client_t *c);
void set_ewmh_net_wm_states(gswm_t *gsw, client_t *c);
void send_ewmh_net_wm_state_add(gswm_t *gsw, client_t *c, Atom state);
void send_ewmh_net_wm_state_remove(gswm_t *gsw, client_t *c, Atom state);
void do_ewmh_net_wm_state_client_message(gswm_t *gsw, client_t *c, XClientMessageEvent *e);
gint get_ewmh_net_current_desktop(gswm_t *gsw);
void set_ewmh_net_wm_allowed_actions(gswm_t *gsw, client_t *c);
void set_ewmh_net_frame_extents(gswm_t *gsw, client_t *c);
void set_ewmh_net_wm_visible_name(gswm_t *gsw, client_t *c, GString *name);
void get_ewmh_net_wm_window_type(gswm_t *gsw, client_t *c);
void set_ewmh_net_desktop_names(gswm_t *gsw);

#endif
