#ifndef _VDESK_H
#define _VDESK_H

void switch_vdesk(gswm_t *gsw, gint v);
void switch_vdesk_up(gswm_t *gsw);
void switch_vdesk_down(gswm_t *gsw);
void clear_vdesk_focused_client(gswm_t *gsw, client_t *c);

#endif
