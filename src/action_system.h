#ifndef _ACTION_SYSTEM_H
#define _ACTION_SYSTEM_H

void action_system_create(gswm_t *gsw, GMainLoop *gml);
void action_system_destroy(gswm_t *gsw);
void action_system_interpret(gswm_t *gsw, const gchar *action);
void action_system_input_loop(gswm_t *gsw);
void action_system_add(gswm_t *gsw, const gchar *name, const gchar *action_string);

#endif
