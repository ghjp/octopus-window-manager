#ifndef _CLIENT_H
#define _CLIENT_H

/* Multipliers for calling gravitate */
enum {
  GRAV_APPLY,
  GRAV_UNDO
};

void setup_client_name(gswm_t *gsw, client_t *c);
void create_new_client(gswm_t *gsw, Window w);
void unmap_client(gswm_t *gsw, client_t *c);
void remap_client(gswm_t *gsw, client_t *c);
void destroy_client(gswm_t *gsw, client_t *c);
void get_mouse_position(gswm_t *gsw, gint *x, gint *y);
client_t *get_next_focusable_client(gswm_t *gsw);
void focus_client(gswm_t *gsw, client_t *c, gboolean raise);
void gravitate(gswm_t *gsw, client_t *c, gint mode);
void send_config(gswm_t *gsw, client_t *c);
gboolean client_win_is_alive(gswm_t *gsw, client_t *c);
void set_wm_state(gswm_t *gsw, client_t *c, gint state);
glong get_wm_state(gswm_t *gsw, client_t *c);
void attach(gswm_t *gsw, client_t *c);
void attach_first2frame(gswm_t *gsw, wframe_t *target_fr);
void detach(gswm_t *gsw, client_t *c);
void attach_first(gswm_t *gsw);
void install_colormaps(gswm_t *gsw, client_t *c);
void update_colormaps(gswm_t *gsw, client_t *c);
client_t *get_focused_client(gswm_t *gsw);
void set_frame_id_xprop(gswm_t *gsw, Window w, glong frame_id);
gint get_frame_id_xprop(gswm_t *gsw, Window w);

#endif
