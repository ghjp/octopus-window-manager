#define G_LOG_DOMAIN "vdesk"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "octopus.h"
#include "vdesk.h"
#include "winactions.h"
#include "main.h"
#include "client.h"
#include "winframe.h"

static void _hide_vdesk_client(gpointer data, gpointer user_data)
{
  client_t *c = (client_t*)data;
  gswm_t *gsw = (gswm_t *)user_data;
  wa_hide(gsw, c);
  wframe_set_type(gsw, c->wframe, FALSE);
}

static void _unhide_vdesk_client(gpointer data, gpointer user_data)
{
  client_t *c = (client_t*)data;
  gswm_t *gsw = (gswm_t *)user_data;
  wa_unhide(gsw, c, FALSE);
}

void switch_vdesk(gswm_t *gsw, gint v)
{
  GList *clist;
  client_t *old_focused_client;
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  if(scr->num_vdesk <= v)
    v = 0;
  else if(0 > v)
    v = scr->num_vdesk - 1;
  TRACE("%s current_vdesk=%d new_vdesk=%d", __func__, scr->current_vdesk, v);
	if(v == scr->current_vdesk)
    return;

  signal_raise_window(gsw, FALSE);
  /* Remember the focused client */
  scr->vdesk[scr->current_vdesk].focused = get_focused_client(gsw);
  /* Hide all windows on the actual workspace */
  clist = scr->vdesk[scr->current_vdesk].clnt_list;
  TRACE("%s hide_clist=%d", __func__, g_list_length(clist));
  // Move the input focus away from any window client
  //XSetInputFocus(gsw->display, None, RevertToPointerRoot, CurrentTime);
  //XSync(gsw->display, False);
  //XGrabServer(gsw->display);
  g_list_foreach(clist, _hide_vdesk_client, gsw);

	scr->current_vdesk = v;
  old_focused_client = scr->vdesk[scr->current_vdesk].focused;
  /* Show all windows on the new workspace */
  clist = scr->vdesk[v].clnt_list;
  TRACE("%s unhide_clist=%d", __func__, g_list_length(clist));
  g_list_foreach(clist, _unhide_vdesk_client, gsw);
  /*g_list_foreach(scr->sticky_list, _unhide_vdesk_client, gsw);*/
  {
    XEvent ev;
    gint ev_cnt = 0;
    XSync(gsw->display, False);
    while(XCheckMaskEvent(gsw->display, FocusChangeMask | EnterWindowMask | LeaveWindowMask, &ev))
      ++ev_cnt;
    TRACE("%s: ev_cnt=%d pending=%d", G_STRFUNC, ev_cnt, XPending(gsw->display));
  }
  //XSync(gsw->display, True); // Discard all events on the event queue
  //XUngrabServer(gsw->display);

  if(old_focused_client)
    focus_client(gsw, old_focused_client, TRUE);
  //else
    //XSetInputFocus(gsw->display, PointerRoot, RevertToPointerRoot, CurrentTime);
  set_root_prop_cardinal(gsw, gsw->xa.wm_net_current_desktop, v);
}

void switch_vdesk_up(gswm_t *gsw)
{
  screen_t *scr = gsw->screen + gsw->i_curr_scr;

  switch_vdesk(gsw, scr->current_vdesk + 1);
}

void switch_vdesk_down(gswm_t *gsw)
{
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  
  switch_vdesk(gsw, scr->current_vdesk - 1);
}

void clear_vdesk_focused_client(gswm_t *gsw, client_t *c)
{
  gint i;
  screen_t *scr = c->curr_screen;

  /* Check if the client was the focused one on any desktop */
  for(i = 0; i < scr->num_vdesk; i++)
    if(c == scr->vdesk[i].focused)
      scr->vdesk[i].focused = NULL;
}
