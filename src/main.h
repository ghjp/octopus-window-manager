#ifndef _MAIN_H
#define _MAIN_H

void sig_main_loop_quit(int sig);
void rehash_executables(gswm_t *gsw);
void signal_raise_window(gswm_t *gsw, gboolean do_it);
void setup_root_kb_shortcuts(gswm_t *gsw);
void set_root_prop_window(gswm_t *gsw, Atom at, glong val);
void set_root_prop_cardinal(gswm_t *gsw, Atom at, glong val);

#endif
