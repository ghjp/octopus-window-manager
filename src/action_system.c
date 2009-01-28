#define G_LOG_DOMAIN "action_system"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* Includes {{{1 */
#include "octopus.h"
#include "action_system.h"
#include "winactions.h"
#include "main.h"
#include "client.h"
#include "vdesk.h"
#include "input.h"
#include "winframe.h"

#include <X11/keysym.h> /* XK_x, ... */
#include <string.h> /* strcmp */
#include <stdlib.h> /* strtol */
#include <errno.h>
#include <signal.h> /* raise */

/* Typedefs {{{1 */
typedef struct {
  GMainLoop *gml;
  GHashTable *action_hash;
  GHashTable *user_action_hash;
  GHashTable *detect_recursion_hash;
} _as_user_data_t;

typedef void (*_action_func)(gswm_t *gsw);

typedef struct {
  gchar *name;
  _action_func func;
} action_table_t;

/* Action functions {{{1 */
/* Exec actions {{{2 */
static void _exec_command_line(gchar *cmdline)
{
  GError *error = NULL;
  if(!g_spawn_command_line_async(cmdline, &error)) {
    g_warning("%s %s", __func__, error->message);
    g_error_free(error);
  }
}

static void _action_xterm(gswm_t *gsw)
{
  _exec_command_line(gsw->ucfg.xterm_cmd);
}

static void _action_exec(gswm_t *gsw)
{
  GString *gs = gsw->action.line;

  /* FIXME Implement a better way to remove the "exec " string part */
  g_string_erase(gs, 0, 5);
  if(gs->len)
    _exec_command_line(gs->str);
}

static void _action_exec_term(gswm_t *gsw)
{
  GString *gs = gsw->action.line;

  /* FIXME Implement a better way to remove the "exec-term " string part */
  g_string_erase(gs, 0, 10);
  if(gs->len) {
    g_string_prepend_c(gs, ' ');
    g_string_prepend(gs, gsw->ucfg.xterm_terminal_cmd);
    _exec_command_line(gs->str);
  }
}

/* Global actions {{{2 */
static void _action_quit(gswm_t *gsw)
{
  _as_user_data_t *asud = gsw->action.user_data;

  if(asud->gml)
    g_main_loop_quit(asud->gml);
}

static void _action_restart(gswm_t *gsw)
{
  /* Send ourself a signal to restart */
  sig_main_loop_quit(SIGUSR1);
}

static void _action_rehash(gswm_t *gsw)
{
  rehash_executables(gsw);
}

/* Client actions {{{2 */
static void _action_kill_client(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    XKillClient(gsw->display, fc->win);
}

static void _action_delete_client(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_send_wm_delete(gsw, fc);
}

static void _action_attach_client(gswm_t *gsw)
{
  attach_first(gsw);
}

static void _action_attach_all(gswm_t *gsw)
{
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  while(scr->detached_list)
    attach_first(gsw);
}

static void _action_detach_client(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    detach(gsw, fc);
}

static void _action_toggle_sticky(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_sticky(gsw, fc, !fc->wstate.sticky);
}

static void _action_fit_to_workarea(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_fit_to_workarea(gsw, fc);
}

static void _action_raise(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_raise(gsw, fc);
}

static void _action_lower(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_lower(gsw, fc);
}

static void _action_client_next(gswm_t *gsw)
{
  client_t *next_c = get_next_focusable_client(gsw);

  if(next_c) {
    wa_raise(gsw, next_c);
    focus_client(gsw, next_c, FALSE);
  }
}

/* Frame actions {{{2 */
static void _action_frame_attach(gswm_t *gsw)
{
  attach_first2frame(gsw, NULL);
}

static void _action_frame_client_next(gswm_t *gsw)
{
  wframe_t *fframe = wframe_get_focused(gsw);

  if(fframe) {
    client_t *nextc = wframe_get_next_active_client(gsw, fframe);

    if(wframe_get_active_client(gsw, fframe) != nextc) {
      TRACE(("%s new nextc=%p (%s)", __func__, nextc, nextc->utf8_name));
      wa_raise(gsw, nextc);
      focus_client(gsw, nextc, FALSE);
    }
  }
}

static void _action_frame_next(gswm_t *gsw)
{
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  vdesk_t *vd = scr->vdesk + scr->current_vdesk;

  if(vd->frame_list) {
    GList *cle;
    for(cle = g_list_last(vd->frame_list); cle; cle = g_list_previous(cle)) {
      wframe_t *next_frame = cle->data;
      client_t *next_c = wframe_get_active_client(gsw, next_frame);
      if(next_c->wstate.input_focus) {
        wa_raise(gsw, next_c);
        focus_client(gsw, next_c, FALSE);
        break;
      }
    }
  }
}

/* Fullscreen actions {{{2 */
static void _action_fullscreen_on(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_fullscreen(gsw, fc, TRUE);
}

static void _action_fullscreen_off(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_fullscreen(gsw, fc, FALSE);
}

/* Maxi/Unmaxi actions {{{2 */
static void _action_maxi_full(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_maximize_hv(gsw, fc, TRUE, TRUE);
}

static void _action_maxi_horz(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_maximize_hv(gsw, fc, TRUE, FALSE);
}

static void _action_maxi_vert(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_maximize_hv(gsw, fc, FALSE, TRUE);
}

static void _action_unmaxi_full(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_unmaximize_hv(gsw, fc, TRUE, TRUE);
}

static void _action_unmaxi_horz(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_unmaximize_hv(gsw, fc, TRUE, FALSE);
}

static void _action_unmaxi_vert(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_unmaximize_hv(gsw, fc, FALSE, TRUE);
}

/* Grow actions {{{2 */
static void _action_grow_east(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_east(gsw, fc, TRUE);
}

static void _action_grow_north(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_north(gsw, fc, TRUE);
}

static void _action_grow_north_east(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_north_east(gsw, fc, TRUE);
}

static void _action_grow_north_west(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_north_west(gsw, fc, TRUE);
}

static void _action_grow_south(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_south(gsw, fc, TRUE);
}

static void _action_grow_south_east(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_south_east(gsw, fc, TRUE);
}

static void _action_grow_south_west(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_south_west(gsw, fc, TRUE);
}

static void _action_grow_west(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_west(gsw, fc, TRUE);
}

/* Shrink actions {{{2 */
static void _action_shrink_east(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_east(gsw, fc, FALSE);
}

static void _action_shrink_north(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_north(gsw, fc, FALSE);
}

static void _action_shrink_north_east(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_north_east(gsw, fc, FALSE);
}

static void _action_shrink_north_west(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_north_west(gsw, fc, FALSE);
}

static void _action_shrink_south(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_south(gsw, fc, FALSE);
}

static void _action_shrink_south_east(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_south_east(gsw, fc, FALSE);
}

static void _action_shrink_south_west(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_south_west(gsw, fc, FALSE);
}

static void _action_shrink_west(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);

  if(fc)
    wa_resize_west(gsw, fc, FALSE);
}

/* Move actions {{{2 */

static void _action_move_north(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_move_north(gsw, fc);
}

static void _action_move_south(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_move_south(gsw, fc);
}

static void _action_move_east(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_move_east(gsw, fc);
}

static void _action_move_west(gswm_t *gsw)
{
  client_t *fc = get_focused_client(gsw);
  
  if(fc)
    wa_move_west(gsw, fc);
}

/* Vdesk operations {{{2 */
static void _action_vdesk_next(gswm_t *gsw)
{
  switch_vdesk_up(gsw);
}

static void _action_vdesk_prev(gswm_t *gsw)
{
  switch_vdesk_down(gsw);
}

/* Debug actions {{{2 */
static void _calc_frame_clients(gpointer key, gpointer value, gpointer udata)
{
  wframe_t *frame = (wframe_t *)value;

  *(gint*)udata += g_list_length(frame->client_list);
}

static void _action_dump_state(gswm_t *gsw)
{
  gint vi, frlen, len, count = 0;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  for(vi = 0; vi < scr->num_vdesk; vi++) {
    len = g_list_length(scr->vdesk[vi].clnt_list);
    frlen = g_list_length(scr->vdesk[vi].frame_list);
    g_message("scr=%d: frame/client list length: %d/%d (vdesk=%d)", scr->id, frlen, len, vi);
    count += len;
  }
  len = g_list_length(scr->detached_list);
  g_message("scr=%d: Detached frame/client list length: %u/%u",
      scr->id, g_list_length(scr->detached_frlist), len);
  count += len;
  len = g_list_length(scr->sticky_list);
  g_message("scr=%d: Sticky frame/client list length: %u/%u",
      scr->id, g_list_length(scr->sticky_frlist), len);
  count += len;
  len = g_list_length(scr->desktop_list);
  g_message("scr=%d: Desktop frame/client list length: %u/%u",
      scr->id, g_list_length(scr->desktop_frlist), len);
  count += len;

  len = g_hash_table_size(gsw->win2clnt_hash);
  g_message("scr=%d: win2hash length: %u", scr->id, len);
  if(len != count) {
    g_critical("scr=%d: size mismatch between win2clnt_hash and client number: %d != %d",
        scr->id, len, count);
    for(vi = 0; vi < scr->num_vdesk; vi++) {
      GList *cl = scr->vdesk[vi].clnt_list;
      for( ; cl; cl = g_list_next(cl)) {
        client_t *c = (client_t*)cl->data;
        g_message("vi=%d: c=%p w=%ld name='%s'", vi, c, c->win, c->utf8_name);
      }
    }
  }
  len = 0;
  g_hash_table_foreach(gsw->win2frame_hash, _calc_frame_clients, &len);
  g_message("scr=%d: fid2hash length: %u",
      scr->id, g_hash_table_size(gsw->fid2frame_hash));
  g_message("scr=%d: frame2hash length: %u (with %d clients)",
      scr->id, g_hash_table_size(gsw->win2frame_hash), len);
  if(len != count)
    g_critical("scr=%d: size mismatch between win2frame_hash and client number: %d != %d",
        scr->id, len, count);
}

typedef struct {
  gchar *name_pat;
  client_t *c_found;
} jump_search_t;

static void _compare_name_of_client(gpointer key, gpointer value, gpointer user_data)
{
  client_t *c = (client_t*)value;
  jump_search_t *js_info = (jump_search_t*)user_data;

  (void)key; /* Keep compiler quiet */
  if(!js_info->c_found) {
    GString *title_string = g_string_new(c->utf8_name);
    g_string_ascii_down(title_string);
    if(g_strstr_len(title_string->str, title_string->len, js_info->name_pat))
      js_info->c_found = c;
    g_string_free(title_string, TRUE);
  }
}

static void _action_jump_to(gswm_t *gsw, gchar *name_pattern)
{
  jump_search_t js_info;

  js_info.name_pat = name_pattern;
  js_info.c_found = NULL;
  g_hash_table_foreach(gsw->win2clnt_hash, _compare_name_of_client, &js_info);
  if(js_info.c_found) {
    if(STICKY !=  js_info.c_found->i_vdesk)
      switch_vdesk(gsw, js_info.c_found->i_vdesk);
    wa_raise(gsw, js_info.c_found);
    focus_client(gsw, js_info.c_found, FALSE);
    TRACE(("%s: %s", __func__, js_info.c_found->utf8_name));
  }
}

/* Action function table {{{1 */

static action_table_t act_table[] = {
  { "attach",           _action_attach_client},
  { "attach-all",       _action_attach_all},
  { "client-next",      _action_client_next},
  { "delete-client",    _action_delete_client},
  { "detach",           _action_detach_client},
  { "dump-state",       _action_dump_state},
  { "exec",             NULL},
  { "exec-term",        NULL},
  { "fit-to-workarea",  _action_fit_to_workarea},
  { "frame-attach",     _action_frame_attach},
  { "frame-client-next",_action_frame_client_next},
  { "frame-next"       ,_action_frame_next},
  { "fullscreen-on",    _action_fullscreen_on},
  { "fullscreen-off",   _action_fullscreen_off},
  { "grow-east",        _action_grow_east},
  { "grow-north",       _action_grow_north},
  { "grow-north-east",  _action_grow_north_east},
  { "grow-north-west",  _action_grow_north_west},
  { "grow-south",       _action_grow_south},
  { "grow-south-east",  _action_grow_south_east},
  { "grow-south-west",  _action_grow_south_west},
  { "grow-west",        _action_grow_west},
  { "kill-client",      _action_kill_client},
  { "lower",            _action_lower},
  { "maximize-full",    _action_maxi_full},
  { "maximize-horz",    _action_maxi_horz},
  { "maximize-vert",    _action_maxi_vert},
  { "move-east",        _action_move_east},
  { "move-north",       _action_move_north},
  { "move-south",       _action_move_south},
  { "move-west",        _action_move_west},
  { "quit",             _action_quit},
  { "raise",            _action_raise},
  { "rehash",           _action_rehash},
  { "restart",          _action_restart},
  { "shrink-east",      _action_shrink_east},
  { "shrink-north",     _action_shrink_north},
  { "shrink-north-east",_action_shrink_north_east},
  { "shrink-north-west",_action_shrink_north_west},
  { "shrink-south",     _action_shrink_south},
  { "shrink-south-east",_action_shrink_south_east},
  { "shrink-south-west",_action_shrink_south_west},
  { "shrink-west",      _action_shrink_west},
  { "toggle-sticky",    _action_toggle_sticky},
  { "unmaximize-full",  _action_unmaxi_full},
  { "unmaximize-horz",  _action_unmaxi_horz},
  { "unmaximize-vert",  _action_unmaxi_vert},
  { "vdesk-next",       _action_vdesk_next},
  { "vdesk-prev",       _action_vdesk_prev},
  { "vdesk-goto ",       NULL}, /* Space at the end due to the need of an argument */
  { "xterm",            _action_xterm},
  { "jump ",            NULL}, /* Space at the end due to the need of an argument */
  { NULL, NULL}
};

static void _remove_leading_whitespace(GString *as)
{
  gint cnt = 0;
  while(as->str[cnt] == ' ' || as->str[cnt] == '\t')
    ++cnt;
  if(cnt)
    g_string_erase(as, 0, cnt);
}

/* Exported access functions {{{1 */
static void _action_system_perform(gswm_t *gsw)
{
  gint i;
  gchar *user_action, **actions;
  _action_func act_func;
  _as_user_data_t *asud = gsw->action.user_data;
  GString *as = gsw->action.line;

  TRACE(("%s cmd=%s", __func__, as->str));
  actions = g_strsplit(as->str, ";", -1);

  for(i = 0; actions[i]; i++) {
    g_strstrip(actions[i]); /* Remove leading and trailing whitespace */
    g_string_assign(as, actions[i]);
    if(!as->len)
      continue;

    TRACE(("%s %s", __func__, as->str));
    act_func = g_hash_table_lookup(asud->action_hash, as->str);
    if(act_func) /* Call action routine */
      act_func(gsw);
    else if((user_action = g_hash_table_lookup(asud->user_action_hash, as->str))) {
      /* Detect recursive calls of user actions */
      if(!g_hash_table_lookup(asud->detect_recursion_hash, user_action)) {
        g_hash_table_insert(asud->detect_recursion_hash,
            user_action, GINT_TO_POINTER(TRUE));
        action_system_interpret(gsw, user_action);
        g_hash_table_remove(asud->detect_recursion_hash, user_action);
      }
      else
        g_critical("%s Detected recursive call of user action `%s'",
            __func__, as->str);
    }
    else if(g_str_equal(as->str, "exec")) { /* without arguments */
      GString *gs = gsw->cmd.line;
      input_loop(gsw, "Enter command: ", &gsw->cmd);
      if(gs->len) {
        g_string_assign(gsw->action.line, "exec ");
        g_string_append(gsw->action.line, gs->str);
        _action_system_perform(gsw);
      }
    }
    else if(!g_ascii_strncasecmp(as->str, "exec ", 5))
      _action_exec(gsw);
    else if(g_str_equal(as->str, "exec-term")) { /* without arguments */
      GString *gs = gsw->cmd.line;
      input_loop(gsw, "Enter command: ", &gsw->cmd);
      if(gs->len) {
        g_string_assign(gsw->action.line, "exec-term ");
        g_string_append(gsw->action.line, gs->str);
        _action_system_perform(gsw);
      }
    }
    else if(!g_ascii_strncasecmp(as->str, "exec-term ", 10))
      _action_exec_term(gsw);
    else if(!g_ascii_strncasecmp(as->str, "vdesk-goto", 10)) {
      g_string_erase(as, 0, 10);
      _remove_leading_whitespace(as);
      if(as->len)
        switch_vdesk(gsw, strtol(as->str, NULL, 0));
      else
        g_warning("Command vdesk-goto has no argument");
    }
    else if(!g_ascii_strncasecmp(as->str, "jump", 4)) {
      g_string_erase(as, 0, 4);
      _remove_leading_whitespace(as);
      g_string_ascii_down(as);
      if(as->len)
        _action_jump_to(gsw, as->str);
      else
        g_warning("Command jump has no argument");
    }
    else
      g_warning("%s: Command `%s' not found", __func__, as->str);
  }
  g_strfreev(actions);
  TRACE(("%s detect_recursion_hash=%d",
      __func__, g_hash_table_size(asud->detect_recursion_hash)));
}

void action_system_create(gswm_t *gsw, GMainLoop *gml)
{
  action_table_t *atp;
  _as_user_data_t *asud;
  GList *aclist = NULL;
  interaction_t *ia = &gsw->action;

  TRACE((__func__));
  ia->line = g_string_new("");
  ia->completion = g_completion_new(NULL);
  ia->str_chunk = g_string_chunk_new(32<<10);
  asud = g_new0(_as_user_data_t, 1);
  asud->gml = gml;
  asud->action_hash = g_hash_table_new(g_str_hash, g_str_equal);
  asud->user_action_hash = g_hash_table_new(g_str_hash, g_str_equal);
  asud->detect_recursion_hash = g_hash_table_new(NULL, NULL);
  ia->user_data = asud;

  for(atp = act_table; atp->name; atp++) {
    TRACE(("%s n=%s f=%p", __func__, atp->name, atp->func));
    g_hash_table_insert(asud->action_hash, atp->name, atp->func);
    aclist = g_list_prepend(aclist, atp->name);
  }
  aclist = g_list_sort(aclist, (gint(*)(gconstpointer, gconstpointer))strcmp);
  g_completion_add_items(gsw->action.completion, aclist);
  g_list_free(aclist);
}

void action_system_destroy(gswm_t *gsw)
{
  interaction_t *ia = &gsw->action;
  _as_user_data_t *asu = ia->user_data;

  g_string_free(ia->line, TRUE);
  g_completion_free(ia->completion);
  g_string_chunk_free(ia->str_chunk);
  g_hash_table_destroy(asu->action_hash);
  g_hash_table_destroy(asu->user_action_hash);
  g_hash_table_destroy(asu->detect_recursion_hash);
  g_free(ia->user_data);
}

void action_system_input_loop(gswm_t *gsw)
{
  input_loop(gsw, "Enter action: ", &gsw->action);
  _action_system_perform(gsw);
}

void action_system_interpret(gswm_t *gsw, const gchar *action)
{
  g_string_assign(gsw->action.line, action);
  _action_system_perform(gsw);
}

void action_system_add(gswm_t *gsw, const gchar *name, const gchar *action_string)
{
  gchar *s_name, *s_action;
  GList *aclist = NULL;
  interaction_t *ia = &gsw->action;
  _as_user_data_t *asu = ia->user_data;

  g_return_if_fail(ia->line && ia->completion && ia->str_chunk && asu->user_action_hash);

  TRACE(("%s name=%s action=`%s'", __func__, name, action_string));
  s_name = g_string_chunk_insert(ia->str_chunk, name);
  s_action = g_string_chunk_insert(ia->str_chunk, action_string);
  g_hash_table_insert(asu->user_action_hash, s_name, s_action);
  aclist = g_list_prepend(aclist, s_name);
  g_completion_add_items(gsw->action.completion, aclist);
  g_list_free(aclist);
}
