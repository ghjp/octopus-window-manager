#ifndef _WINFRAME_H
#define _WINFRAME_H

typedef void (*wframe_func)(gswm_t *gsw, client_t *c, gpointer user_data);

void wframe_nullify_pending_focus_events(gswm_t *gsw, wframe_t *frame);
void wframe_rebuild_xsizehints(gswm_t *gsw, wframe_t *frame, const gchar *context);
void wframe_x_move_resize(gswm_t *gsw, wframe_t *frame);
void wframe_bind_client(gswm_t *gsw, wframe_t *frame, client_t *c);
void wframe_unbind_client(gswm_t *gsw, client_t *c);
void wframe_remove_client(gswm_t *gsw, client_t *c);
void wframe_install_kb_shortcuts(gswm_t *gsw, wframe_t *frame);
void wframe_set_type(gswm_t *gsw, wframe_t *frame, gboolean focused);
void wframe_expose(gswm_t *gsw, wframe_t *frame);
void wframe_tbar_pmap_recreate(gswm_t *gsw, wframe_t *frame);
void wframe_button_init(gswm_t *gsw);
void wframe_tbar_button_set_pressed(gswm_t *gsw, wframe_t *frame);
void wframe_tbar_button_set_normal(gswm_t *gsw, wframe_t *frame);
void wframe_set_shape(gswm_t *gsw, client_t *c);
client_t *wframe_lookup_client_for_window(gswm_t *gsw, Window w);
void wframe_foreach(gswm_t *gsw, wframe_t *frame, wframe_func func, gpointer user_data);
client_t *wframe_get_active_client(gswm_t *gsw, wframe_t *frame);
client_t *wframe_get_next_active_client(gswm_t *gsw, wframe_t *frame);
client_t *wframe_get_previous_active_client(gswm_t *gsw, wframe_t *frame);
void wframe_list_add(gswm_t *gsw, wframe_t *frame);
void wframe_list_remove(gswm_t *gsw, wframe_t *frame);
gboolean wframe_has_focus(gswm_t *gsw, wframe_t *frame);
wframe_t *wframe_get_focused(gswm_t *gsw);
gboolean wframe_houses_multiple_clients(gswm_t *gsw, wframe_t *frame);
void wframe_set_client_active(gswm_t *gsw, client_t *c);
void wframe_update_stat_indicator(gswm_t *gsw);
void wframe_events_disable(gswm_t *gsw, wframe_t *frame);
void wframe_events_enable(gswm_t *gsw, wframe_t *frame);

#endif
