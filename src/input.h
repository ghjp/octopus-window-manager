#ifndef _INPUT_H
#define _INPUT_H

void input_create(gswm_t *gsw);
void input_destroy(gswm_t *gsw);
void input_loop(gswm_t *gsw, const gchar *prompt, interaction_t *ia);

#endif
