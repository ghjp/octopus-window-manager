#ifndef _WINACTIONS_H
#define _WINACTIONS_H

void wa_do_all_size_constraints(gswm_t *gsw, client_t *c);
void wa_hide(gswm_t *gsw, client_t *c);
void wa_unhide(gswm_t *gsw, client_t *c, gboolean raise);
void wa_iconify(gswm_t *gsw, client_t *c);
void wa_moveresize(gswm_t *gsw, client_t *c);
void wa_move_interactive(gswm_t *gsw, client_t *c);
client_t *wa_get_client_under_pointer(gswm_t *gsw);
void wa_frameop_interactive(gswm_t *gsw, client_t *c);
void wa_resize_interactive(gswm_t *gsw, client_t *c);
void wa_resize_north(gswm_t *gsw, client_t *c, gboolean grow);
void wa_resize_west(gswm_t *gsw, client_t *c, gboolean grow);
void wa_resize_east(gswm_t *gsw, client_t *c, gboolean grow);
void wa_resize_south(gswm_t *gsw, client_t *c, gboolean grow);
void wa_resize_north_east(gswm_t *gsw, client_t *c, gboolean grow);
void wa_resize_north_west(gswm_t *gsw, client_t *c, gboolean grow);
void wa_resize_south_west(gswm_t *gsw, client_t *c, gboolean grow);
void wa_resize_south_east(gswm_t *gsw, client_t *c, gboolean grow);
void wa_maximize_hv(gswm_t *gsw, client_t *c, gboolean horz, gboolean vert);
void wa_unmaximize_hv(gswm_t *gsw, client_t *c, gboolean horz, gboolean vert);
void wa_fullscreen(gswm_t *gsw, client_t *c, gboolean on);
void wa_send_xclimsg(gswm_t *gsw, client_t *c, Atom type,
    glong l0, glong l1, glong l2, glong l3, glong l4);
void wa_send_xclimsgwm(gswm_t *gsw, client_t *c, Atom type, Atom arg);
void wa_send_wm_delete(gswm_t *gsw, client_t *c);
void wa_sticky(gswm_t *gsw, client_t *c, gboolean on);
#define wa_raise(gsw, c) G_STMT_START{ XRaiseWindow((gsw)->display, (c)->wframe->win); XRaiseWindow((gsw)->display, (c)->win); }G_STMT_END
#define wa_lower(gsw, c) XLowerWindow((gsw)->display, (c)->wframe->win)
#define wa_set_input_focus(gsw, c) XSetInputFocus((gsw)->display, (c)->win, RevertToPointerRoot, CurrentTime)
void wa_move_north(gswm_t *gsw, client_t *c);
void wa_move_south(gswm_t *gsw, client_t *c);
void wa_move_east(gswm_t *gsw, client_t *c);
void wa_move_west(gswm_t *gsw, client_t *c);
void wa_fit_to_desktop(gswm_t *gsw, client_t *c);

#endif
