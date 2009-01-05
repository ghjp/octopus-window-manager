#define G_LOG_DOMAIN "events"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "octopus.h"
#include "events.h"
#include "winactions.h"
#include "input.h"
#include "action_system.h"
#include "client.h"
#include "vdesk.h"
#include "winframe.h"
#include "main.h"
#include "ewmh.h"

#include <X11/keysym.h> /* XK_x, ... */
#include <X11/Xatom.h>  /* XA_WM_NAME */
#ifdef HAVE_XSHAPE
#include <X11/extensions/shape.h> /* SHAPE */
#endif

#define _NET_WM_MOVERESIZE_SIZE_TOPLEFT      0
#define _NET_WM_MOVERESIZE_SIZE_TOP          1
#define _NET_WM_MOVERESIZE_SIZE_TOPRIGHT     2
#define _NET_WM_MOVERESIZE_SIZE_RIGHT        3
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT  4
#define _NET_WM_MOVERESIZE_SIZE_BOTTOM       5
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT   6
#define _NET_WM_MOVERESIZE_SIZE_LEFT         7
#define _NET_WM_MOVERESIZE_MOVE              8   /* movement only */
#define _NET_WM_MOVERESIZE_SIZE_KEYBOARD     9   /* size via keyboard */
#define _NET_WM_MOVERESIZE_MOVE_KEYBOARD    10   /* move via keyboard */

static void _handle_exec_key_event(gswm_t *gsw)
{
  GString *gs = gsw->cmd.line;

  input_loop(gsw, "Enter command: ", &gsw->cmd);
  if(gs->len) {
    g_string_prepend(gs, "exec ");
    action_system_interpret(gsw, gs->str);
  }
}

static void _handle_key_event(gswm_t *gsw, XKeyEvent *e)
{
  client_t *clnt;
  KeySym ks = XLookupKeysym(e, 0);

  TRACE((__func__));
  switch(ks) {
    case XK_Tab:
      if(ShiftMask & e->state)
        action_system_interpret(gsw, "frame-client-next");
      else
        action_system_interpret(gsw, "frame-next");
      break;
    case XK_a:
      if(ShiftMask & e->state)
        attach_first2frame(gsw, NULL);
      else
        attach_first(gsw);
      break;
    case XK_d:
      clnt = wframe_lookup_client_for_window(gsw, e->window);
      if(clnt)
        detach(gsw, clnt);
      break;
    case XK_e:
      _handle_exec_key_event(gsw);
      break;
    case XK_i:
      action_system_input_loop(gsw);
      break;
    case XK_m:
      clnt = wframe_lookup_client_for_window(gsw, e->window);
      if(clnt)
        action_system_interpret(gsw,
            clnt->wstate.maxi_vert || clnt->wstate.maxi_horz ?
            "unmaximize-full" : "maximize-full");
      break;
    case XK_v:
      clnt = wframe_lookup_client_for_window(gsw, e->window);
      if(clnt)
        action_system_interpret(gsw,
            clnt->wstate.maxi_vert ? "unmaximize-vert" : "maximize-vert");
      break;
    case XK_h:
      clnt = wframe_lookup_client_for_window(gsw, e->window);
      if(clnt)
        action_system_interpret(gsw,
            clnt->wstate.maxi_horz ? "unmaximize-horz" : "maximize-horz");
      break;
    case XK_Return:
      action_system_interpret(gsw, "xterm");
      break;
    case XK_Escape:
      clnt = wframe_lookup_client_for_window(gsw, e->window);
      if(clnt)
        wa_send_wm_delete(gsw, clnt);
      break;
    case XK_Page_Up:
      switch_vdesk_down(gsw);
      break;
    case XK_Page_Down:
      switch_vdesk_up(gsw);
      break;
    case XK_F1 ... XK_F12:
      switch_vdesk(gsw, ks - XK_F1);
      break;
    case XK_1 ... XK_9:
      switch_vdesk(gsw, ks - XK_1);
      break;
    case XK_r:
      action_system_interpret(gsw, "restart");
      break;
    case XK_minus:
      clnt = wframe_lookup_client_for_window(gsw, e->window);

      if(clnt) {
        gint gravity = (clnt->xsize.flags & PWinGravity) ?
          clnt->xsize.win_gravity : NorthWestGravity;
        switch(gravity) {
          case NorthGravity:
          case NorthWestGravity:
            action_system_interpret(gsw, "shrink-south-east");
            break;
          case NorthEastGravity:
            action_system_interpret(gsw, "shrink-south-west");
            break;
          case SouthGravity:
          case SouthWestGravity:
            action_system_interpret(gsw, "shrink-north-east");
            break;
          case SouthEastGravity:
            action_system_interpret(gsw, "shrink-north-west");
            break;
          case CenterGravity:
            action_system_interpret(gsw, "shrink-north-west;shrink-south-east");
            break;
          default:
            /* TODO Check this logic */
            action_system_interpret(gsw, "shrink-south-east");
            break;
        }
      }
      break;
    case XK_plus:
      clnt = wframe_lookup_client_for_window(gsw, e->window);

      if(clnt) {
        gint gravity = (clnt->xsize.flags & PWinGravity) ?
          clnt->xsize.win_gravity : NorthWestGravity;
        switch(gravity) {
          case NorthGravity:
          case NorthWestGravity:
            action_system_interpret(gsw, "grow-south-east");
            break;
          case NorthEastGravity:
            action_system_interpret(gsw, "grow-south-west");
            break;
          case SouthGravity:
          case SouthWestGravity:
            action_system_interpret(gsw, "grow-north-east");
            break;
          case SouthEastGravity:
            action_system_interpret(gsw, "grow-north-west");
            break;
          case CenterGravity:
            action_system_interpret(gsw, "grow-north-west;grow-south-east");
            break;
          default:
            /* TODO Check this logic */
            action_system_interpret(gsw, "grow-south-east");
            break;
        }
      }
      break;
    case XK_Right:
      if((ShiftMask & e->state))
        action_system_interpret(gsw, "grow-east");
      else if((ControlMask & e->state))
        action_system_interpret(gsw, "shrink-west");
      else
        action_system_interpret(gsw, "move-east");
      break;
    case XK_Left:
      if((ShiftMask & e->state))
        action_system_interpret(gsw, "grow-west");
      else if((ControlMask & e->state))
        action_system_interpret(gsw, "shrink-east");
      else
        action_system_interpret(gsw, "move-west");
      break;
    case XK_Down:
      if((ShiftMask & e->state))
        action_system_interpret(gsw, "grow-south");
      else if((ControlMask & e->state))
        action_system_interpret(gsw, "shrink-north");
      else
        action_system_interpret(gsw, "move-south");
      break;
    case XK_Up:
      if((ShiftMask & e->state))
        action_system_interpret(gsw, "grow-north");
      else if((ControlMask & e->state))
        action_system_interpret(gsw, "shrink-south");
      else
        action_system_interpret(gsw, "move-north");
      break;
    case XK_Home:
      action_system_interpret(gsw, "fit-to-workarea");
      break;
    default:
      break;
  }
}

static void _handle_buttonpress_event(gswm_t *gsw, XButtonPressedEvent *e)
{
  client_t *clnt = wframe_lookup_client_for_window(gsw, e->window);

  if(clnt) {
    gint wf_btn_pressed = e->subwindow == clnt->wframe->btnwin;
    gint stat_btn_pressed = e->subwindow == clnt->wframe->statwin;
    TRACE(("%s subwin==titlewin=%d subwin==btnwin=%d", __func__,
          e->subwindow == clnt->wframe->titlewin, e->subwindow == clnt->wframe->btnwin));
    switch (e->button) {
      case Button1:
        if(wf_btn_pressed)
          wframe_tbar_button_set_pressed(gsw, clnt->wframe);
        else if(stat_btn_pressed)
          action_system_interpret(gsw, "detach");
        else
          wa_move_interactive(gsw, clnt);
        break;
      case Button2:
        if(wf_btn_pressed)
          wframe_tbar_button_set_pressed(gsw, clnt->wframe);
        else if(stat_btn_pressed)
          action_system_interpret(gsw, "attach");
        else
          wa_frameop_interactive(gsw, clnt);
        break;
      case Button3:
        if(wf_btn_pressed)
          wframe_tbar_button_set_pressed(gsw, clnt->wframe);
        else if(stat_btn_pressed)
          action_system_interpret(gsw, "frame-attach");
        else
          wa_resize_interactive(gsw, clnt);
        break;
      case Button4:
        if(stat_btn_pressed)
          action_system_interpret(gsw, "frame-client-next");
        else
          wa_lower(gsw, clnt);
        break;
      case Button5:
        if(stat_btn_pressed)
          action_system_interpret(gsw, "frame-client-next");
        else
          wa_raise(gsw, clnt);
        break;
    }
  }
}

static void _handle_buttonrelease_event(gswm_t *gsw, XButtonReleasedEvent *e)
{
  client_t *clnt = wframe_lookup_client_for_window(gsw, e->window);

  if(clnt) {
    wframe_t *oldframe = clnt->wframe;

    if(e->subwindow == clnt->wframe->btnwin) { /* Button released on titlebar button */
      switch (e->button) {
        case Button1:
          wa_send_wm_delete(gsw, clnt);
          break;
        case Button2:
          if(!clnt->wstate.maxi_horz || !clnt->wstate.maxi_vert)
            wa_maximize_hv(gsw, clnt, !clnt->wstate.maxi_horz, !clnt->wstate.maxi_vert);
          else
            wa_unmaximize_hv(gsw, clnt, clnt->wstate.maxi_horz, clnt->wstate.maxi_vert);
          break;
        case Button3:
          wa_sticky(gsw, clnt, !clnt->wstate.sticky);
          break;
      }
    }
    wframe_tbar_button_set_normal(gsw, clnt->wframe);
    if(oldframe != clnt->wframe)
      wframe_tbar_button_set_normal(gsw, oldframe);
  }
  else if(gsw->screen[gsw->i_curr_scr].rootwin == e->window) {
    TRACE(("%s root button event", __func__));
    switch(e->button) {
      case Button4:
        switch_vdesk_down(gsw);
        break;
      case Button5:
        switch_vdesk_up(gsw);
        break;
    }
  }
}

/* This happens when a window is iconified and destroys itself. An
 * Unmap event wouldn't happen in that case because the window is
 * already unmapped. */

static void _handle_destroy_event(gswm_t *gsw, XDestroyWindowEvent *e)
{
  client_t *clnt = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(e->window));

  if(clnt)
    destroy_client(gsw, clnt);
}

static void _handle_unmap_event(gswm_t *gsw, XUnmapEvent *e)
{
  client_t *clnt = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(e->window));
  if(clnt) {
    /*
     * ICCCM spec states that a client wishing to switch
     * to WithdrawnState should send a synthetic UnmapNotify 
     * with the event field set to root if the client window 
     * is already unmapped.
     * Therefore, bypass the ignore_unmap counter and
     * unframe the client.
     */
    gint synthetic_unmap = e->send_event && e->event == clnt->curr_screen->rootwin;

    TRACE(("%s synthetic_unmap=%d event=%ld window=%ld from_configure=%d (%s)",
        __func__, synthetic_unmap, e->event, e->window, e->from_configure, clnt->utf8_name));
    if(clnt->ignore_unmap && !synthetic_unmap)
      clnt->ignore_unmap--;
    else
      unmap_client(gsw, clnt);
  }
}

static void _handle_map_event(gswm_t *gsw, XMapEvent *e)
{
  if(e->override_redirect)
    signal_raise_window(gsw, FALSE);
}

static void _handle_maprequest_event(gswm_t *gsw, XMapRequestEvent *e)
{
  client_t *clnt = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(e->window));

  if(!clnt)
    create_new_client(gsw, e->window);
  else {
    TRACE(("%s parent=%ld window=%ld c->win=%ld c->wframe->win=%ld",
        __func__, e->parent, e->window, clnt->win, clnt->wframe->win));
    /* If client is already framed we ignore this request
       TODO Check if this is really correct */
    if(e->parent != clnt->wframe->win)
      attach(gsw, clnt);
  }
}

static void _handle_configurerequest_event(gswm_t *gsw, XConfigureRequestEvent *e)
{
  client_t *clnt;
  XWindowChanges wc;  
  XEvent next_ev;

  /* Compress events */
  while(XCheckTypedWindowEvent(gsw->display, e->window, ConfigureRequest,
        &next_ev)) {
    if(next_ev.xconfigurerequest.value_mask == e->value_mask) {
      TRACE(("%s compressed event", __func__));
      e = &next_ev.xconfigurerequest;
    }
    else {
      XPutBackEvent(gsw->display, &next_ev);
      break;
    }
  }

  TRACE(("ConfigureRequest ser=%lu send_event=%d parent=%lu window=%lu "
      "x=%d y=%d width=%d height=%d border_width=%d above=%lu detail=%d value_mask=%lu",
      e->serial, e->send_event, e->parent, e->window, e->x, e->y,
      e->width, e->height, e->border_width, e->above, e->detail, e->value_mask));

  clnt = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(e->window));
  if(clnt) {
    gint th = clnt->wframe->theight;

    gravitate(gsw, clnt, GRAV_UNDO);
    if((CWX & e->value_mask))
      clnt->x = e->x;
    if((CWY & e->value_mask))
      clnt->y = e->y;
    if((CWWidth & e->value_mask))
      clnt->width = e->width;
    if((CWHeight & e->value_mask))
      clnt->height = e->height;
    gravitate(gsw, clnt, GRAV_APPLY);

    if((CWX|CWY|CWWidth|CWHeight) & e->value_mask) {
      clnt->wstate.maxi_horz = clnt->wstate.maxi_vert = FALSE;
      wa_moveresize(gsw, clnt);
      /* We have already handled the size request */
      e->value_mask &= ~(CWX|CWY|CWWidth|CWHeight);
    }

    /* Deal with raising - needs work, not sure if anything really
     * relies on this / or how it fits with mb. 
     *
     * Wins can use _NET_ACTIVATE_WINDOW to raise themselse. 
     * Dialogs should be able to raise / lower theme selves ? 
     */
    if((CWSibling|CWStackMode) & e->value_mask) {
      /* e->above  is sibling 
       * e->detail is stack_mode 
       */

      client_t *sibling = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(e->above));

      TRACE(("%s value_mask & (CWSibling|CWStackMode) sibling is %s (%li vs %li)\n",
          __func__, sibling ? sibling->utf8_name : "unkown", sibling ? sibling->win: 0, clnt->win));

      if(sibling) {
        switch(e->detail) {
          case Above:
            TRACE(("%s (CWSibling|CWStackMode) above %s\n",
                __func__, sibling->utf8_name));

            /* Dialog 'above its self, call activate to raise it */
            if(sibling == clnt && clnt->w_type.dialog)
              wframe_set_client_active(gsw, clnt);
            break;
          case Below:
            TRACE(("%s (CWSibling|CWStackMode) below %s\n",
                __func__, sibling->utf8_name));
            /* TODO: Unsupported currently clear flags */
            e->value_mask &= ~(CWSibling|CWStackMode);
            break;
          case TopIf:      /* TODO: What do these mean ? */
          case BottomIf:
          case Opposite:
          default:
            TRACE(("%s (CWSibling|CWStackMode) uh? %s\n",
                __func__, sibling->utf8_name));
            /* Unsupported currently clear flags */
            e->value_mask &= ~(CWSibling|CWStackMode);
            break;
        }
      }
      else /* Just clear the flags now to be safe */
        e->value_mask &= ~(CWSibling|CWStackMode);
    }
#if 0
    if(e->value_mask) {
      wc.border_width = GET_BORDER_WIDTH(clnt);
      wc.sibling = e->above;
      wc.stack_mode = e->detail;
      XConfigureWindow(gsw->display, clnt->wframe->win,
          e->value_mask & ~CWSibling, &wc);
    }
#endif

    /* Start setting up the next call */
    wc.x = 0;
    wc.y = th;
  }
  else {
    wc.x = e->x;
    wc.y = e->y;
  }

  wc.width = e->width;
  wc.height = e->height;
  wc.sibling = e->above;
  wc.stack_mode = e->detail;
  XConfigureWindow(gsw->display, e->window, e->value_mask, &wc);
}

static void _rebuild_wframe(gpointer key, gpointer value, gpointer user_data)
{
  TRACE(("%s fid=%d", __func__, GPOINTER_TO_INT(key)));
  wframe_tbar_pmap_recreate(user_data, value);
}

static void _handle_property_event(gswm_t *gsw, XPropertyEvent *e)
{
  screen_t *scr = gsw->screen + gsw->i_curr_scr;
  client_t *clnt = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(e->window));

  if(clnt) {
    if(gsw->xa.wm_net_wm_name == e->atom ||
        (XA_WM_NAME == e->atom && !clnt->wstate.has_net_wm_name)) {
      setup_client_name(gsw, clnt);
      wframe_tbar_pmap_recreate(gsw, clnt->wframe);
    }
    else if(XA_WM_NORMAL_HINTS == e->atom) {
      glong dummy;
      XGetWMNormalHints(gsw->display, clnt->win, &clnt->xsize, &dummy);
      wframe_rebuild_xsizehints(gsw, clnt->wframe, __func__);
    }
    else if(XA_WM_TRANSIENT_FOR == e->atom) {
        if(!XGetTransientForHint(gsw->display, clnt->win, &clnt->trans))
          clnt->trans = 0;
    }
    else if(gsw->xa.wm_net_wm_strut == e->atom) {
      remove_ewmh_strut(gsw, clnt, TRUE);
      add_ewmh_strut(gsw, clnt);
    }
    else if(gsw->xa.wm_colormap_windows == e->atom) {
      update_colormaps(gsw, clnt);
      if(get_focused_client(gsw) == clnt)
        install_colormaps(gsw, clnt);
    }
#if 0 /* TODO remove this code */
    else {
      gchar *an = XGetAtomName(gsw->display, e->atom);
      if(an) {
        g_message("%s unsupported atom %s", __func__, an);
        XFree(an);
      }
    }
#endif
  }
  else if(e->window == scr->rootwin) {
    if(gsw->xa.wm_xrootpmap_id == e->atom || gsw->xa.wm_xsetroot_id == e->atom)
      g_hash_table_foreach(gsw->fid2frame_hash, _rebuild_wframe, gsw);
#if 0
    else {
      gchar *an = XGetAtomName(gsw->display, e->atom);
      if(an) {
        g_message("%s on root window: %s", __func__, an);
        XFree(an);
      }
    }
#endif
  }
}

static void _handle_client_message(gswm_t *gsw, XClientMessageEvent *e)
{
  client_t *clnt = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(e->window));
  if(clnt && 32 == e->format) {
    if(e->message_type == gsw->xa.wm_change_state) {
      TRACE(("%s wm_change_state w=%ld", __func__, clnt->win));
      if(IconicState == e->data.l[0] && !clnt->wstate.hidden)
        detach(gsw, clnt);
    }
    else if(e->message_type == gsw->xa.wm_net_wm_state)
      do_ewmh_net_wm_state_client_message(gsw, clnt, e);
    else if(e->message_type == gsw->xa.wm_net_active_window) {
      switch(e->data.l[0]) { /* Source indication */
        case 2: /* Request from pager */
          TRACE(("%s _NET_ACTIVE_WINDOW request from pager", __func__));
          if(!clnt->wstate.sticky)
            switch_vdesk(gsw, clnt->i_vdesk);
          if(clnt->wstate.hidden)
            attach(gsw, clnt);
          focus_client(gsw, clnt, FALSE);
          wa_raise(gsw, clnt);
          break;
        case 1: /* Request from application */
          TRACE(("%s _NET_ACTIVE_WINDOW request from application", __func__));
          /* Fall through */
        default: /* Old style applications */
          TRACE(("%s _NET_ACTIVE_WINDOW Old style applications", __func__));
          if(!clnt->wstate.sticky)
            switch_vdesk(gsw, clnt->i_vdesk);
          if(clnt->wstate.hidden)
            attach(gsw, clnt);
          focus_client(gsw, clnt, TRUE);
          break;
      }
    }
    else if(e->message_type == gsw->xa.wm_net_wm_desktop) {
      glong new_desktop = e->data.l[0];
      TRACE(("%s valid desktop number: new_desktop=%ld", __func__, new_desktop));
      if(0 <= new_desktop && new_desktop < clnt->curr_screen->num_vdesk) {
        detach(gsw, clnt);
        clnt->i_vdesk = new_desktop;
        clnt->wstate.sticky = FALSE;
        attach(gsw, clnt);
      }
      else if(STICKY == new_desktop && !clnt->wstate.sticky) {
        detach(gsw, clnt);
        clnt->i_vdesk = STICKY;
        clnt->wstate.sticky = TRUE;
        attach(gsw, clnt);
      }
    }
    else if(e->message_type == gsw->xa.wm_net_close_window && 0 != e->data.l[0])
      wa_send_wm_delete(gsw, clnt);
    else if(e->message_type == gsw->xa.wm_protocols) {
#if 0
      Atom action = e->data.l[0];
      gchar *an = XGetAtomName(gsw->display, action);
      if(an) {
        g_message("%s WM_PROTO w=%ld: %s", __func__, clnt->win, an);
        XFree(an);
      }
#endif
    }
    else if(e->message_type == gsw->xa.wm_net_moveresize_window) {
      glong mr_info = e->data.l[0];
      G_GNUC_UNUSED glong gravity_to_use = mr_info & 0xff; /* Low Byte */
      gint xywh_present = (0xfL<<8) == (mr_info & (0xfL<<8));

      TRACE(("%s wm_net_moveresize_window: gravity_to_use=%ld xywh_present=%d",
            __func__, gravity_to_use, xywh_present));
      if(xywh_present) {
        /* TODO Use gravity_to_use information too */
        gravitate(gsw, clnt, GRAV_UNDO);
        clnt->x = (gint)e->data.l[1];
        clnt->y = (gint)e->data.l[2];
        clnt->width = (gint)e->data.l[3];
        clnt->height = (gint)e->data.l[4];
        gravitate(gsw, clnt, GRAV_APPLY);
        wa_moveresize(gsw, clnt);
      }
    }
    else if(e->message_type == gsw->xa.wm_net_wm_moveresize) {
      G_GNUC_UNUSED gint x_root = (gint)e->data.l[0];
      G_GNUC_UNUSED gint y_root = (gint)e->data.l[1];
      gint direction = (gint)e->data.l[2];
      G_GNUC_UNUSED gint button = (gint)e->data.l[3];

      TRACE(("%s _NET_WM_MOVERESIZE x_r=%d y_r=%d dir=%d button=%d",
            __func__, x_root, y_root, direction, button));
      /* TODO Check if for all situations wa_resize_interactive is the correct op */
      switch(direction) {
        case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
        case _NET_WM_MOVERESIZE_SIZE_TOP:
        case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
        case _NET_WM_MOVERESIZE_SIZE_RIGHT:
        case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
        case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
        case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
        case _NET_WM_MOVERESIZE_SIZE_LEFT:
          wa_resize_interactive(gsw, clnt);
          break;
        case _NET_WM_MOVERESIZE_MOVE:
          wa_move_interactive(gsw, clnt);
          break;
        case _NET_WM_MOVERESIZE_SIZE_KEYBOARD:
          g_message("%s _NET_WM_MOVERESIZE_SIZE_KEYBOARD not handled yet", __func__);
          break;
        case _NET_WM_MOVERESIZE_MOVE_KEYBOARD:
          {
            gint mx ,my;

            if(x_root < clnt->x || x_root > (clnt->x+clnt->width))
              mx = clnt->width/2;
            else
              mx = 0;
            if(y_root < clnt->y || y_root > (clnt->y+clnt->height))
              my = clnt->height/2;
            else
              my = 0;
            XWarpPointer(gsw->display, None, clnt->win, 0, 0, 0, 0, mx, my);
            wa_move_interactive(gsw, clnt);
          }
          break;
        default:
          g_warning("%s invalid direction indicator for _NET_WM_MOVERESIZE", __func__);
          break;
      }
    }
    else {
      gchar *an = XGetAtomName(gsw->display, e->message_type);
      if(an) {
        g_message("%s unhandled message w=%ld: %s", __func__, clnt->win, an);
        XFree(an);
      }
    }
  }
  else if(e->message_type == gsw->xa.wm_net_current_desktop) { /* Pagers use this */
    glong new_desktop = e->data.l[0];
    screen_t *scr = gsw->screen + gsw->i_curr_scr;
    if(0 <= new_desktop && new_desktop < scr->num_vdesk &&
        new_desktop != scr->current_vdesk) {
      switch_vdesk(gsw, new_desktop);
    }
  }
}

#ifdef HAVE_XSHAPE
static void _handle_shape_event(gswm_t *gsw, XShapeEvent *e)
{
  client_t *clnt = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(e->window));
  if(clnt)
    wframe_set_shape(gsw, clnt);
}
#endif

static void _refresh_key_bindings(gpointer key, gpointer value, gpointer user_data)
{
  client_t *c = (client_t*)value;
  gswm_t *gsw = (gswm_t*)user_data;

  TRACE(("%s w=%ld %s", __func__, c->win, c->utf8_name));
  wframe_install_kb_shortcuts(gsw, c->wframe);
}

static void _handle_mapping_event(gswm_t *gsw, XMappingEvent *e)
{
  /* If more than one event happens only handle the last one */
  if(XPending(gsw->display)) {
    XEvent next_ev;

    XPeekEvent(gsw->display, &next_ev);
    if(MappingNotify == next_ev.type &&
        next_ev.xmapping.request == e->request)
      return;
  }
  if(MappingKeyboard == e->request) {
    /* Let XLib know that there is a new keyboard mapping.
     */
    XRefreshKeyboardMapping(e);
    setup_root_kb_shortcuts(gsw);
    g_hash_table_foreach(gsw->win2clnt_hash, _refresh_key_bindings, gsw);
  }
  else
    g_warning("%s Only MappingKeyboard supported at the moment", __func__);
  /* TODO Handle modifier remapping!!! */
}

static void _handle_enter_event(gswm_t *gsw, XCrossingEvent *e)
{
  client_t *clnt;
  XEvent ev;

  /* Compress Enter events to avoid "focus flicker during desktop switching */
  ev.xcrossing = *e;
  while(XCheckTypedEvent(gsw->display, EnterNotify, &ev))
    /* DO NOTHING */;
  TRACE(("%s window=%ld mode=%d detail=%d focus=%d",
        __func__, e->window, e->mode, e->detail, e->focus));
  /* Ignore internal application events */
  if(NotifyInferior == e->detail && ev.xcrossing.focus)
    return;
#if 0
  if(NotifyGrab == e->mode || NotifyUngrab == e->mode)
    return;
#endif
  clnt = wframe_lookup_client_for_window(gsw, ev.xcrossing.window);
  if(!clnt)
    clnt = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(ev.xcrossing.window));
  if(clnt) {
    TRACE(("EnterNotify frame=%ld w=%ld (%s)", clnt->wframe->win, clnt->win, clnt->utf8_name));
    focus_client(gsw, clnt, TRUE);
  }
}

#if 0
static void _handle_leave_event(gswm_t *gsw, XCrossingEvent *e)
{
  gint i;
  TRACE(("%s: w=0x%lx rwin=0x%lx same_screen=%d", __func__, e->window, e->root, e->same_screen));
  if(!e->same_screen) {
    for(i = 0; i < gsw->num_screens; i++) {
      if(e->root == gsw->screen[i].rootwin) {
        g_message("We entered screen number %d", i);
        gsw->i_curr_scr = i;
        break;
      }
    }
  }
}
#endif

static void _handle_focusin_event(gswm_t *gsw, XFocusInEvent *e)
{
  client_t *clnt;

  TRACE(("%s window=%ld mode=%d detail=%d", __func__, e->window, e->mode, e->detail));
  if(NotifyGrab == e->mode)
    return;
  clnt = wframe_lookup_client_for_window(gsw, e->window);
  TRACE(("%s window=%ld mode=%d detail=%d (%s)",
        __func__, e->window, e->mode, e->detail, clnt?clnt->utf8_name:""));
  if(clnt) { /*  (NotifyPointer != e->detail && NotifyAncestor != e->detail) */
    install_colormaps(gsw, clnt);
    clnt->wframe->focus_expose_hint = TRUE;
    wframe_expose(gsw, clnt->wframe);
  }
}

static void _handle_focusout_event(gswm_t *gsw, XFocusOutEvent *e)
{
  client_t *clnt;

  TRACE(("%s window=%ld mode=%d detail=%d", __func__, e->window, e->mode, e->detail));
  /* Get the last focus in event, no need to go through them all. */
  while(XCheckTypedEvent(gsw->display, FocusOut, (XEvent*)e))
    ;
  if(NotifyGrab == e->mode || NotifyUngrab == e->mode)
    return;
  clnt = wframe_lookup_client_for_window(gsw, e->window);
  TRACE(("%s window=%ld mode=%d detail=%d (%s)",
        __func__, e->window, e->mode, e->detail, clnt?clnt->utf8_name:""));
  if(clnt) {
    clnt->wframe->focus_expose_hint = FALSE;
    wframe_expose(gsw, clnt->wframe);
  }
  else {
    /* If no client is found we give focus to the root window
     * as we don't want to get left without focus.
     */
    if(XCheckTypedEvent(gsw->display, FocusIn, (XEvent*)e)) {
      /* We found a Event, put it back and handle it later on. */
      TRACE(("%s found a FocusIn event", __func__));
      XPutBackEvent(gsw->display, (XEvent *)e);
    }
    else {
      TRACE(("%s giving focus to root window", __func__));
      XSetInputFocus(gsw->display, gsw->screen[gsw->i_curr_scr].rootwin, RevertToPointerRoot, CurrentTime);
    }
  }
}

static void _handle_colormapchange_event(gswm_t *gsw, XColormapEvent *e)
{
  client_t *clnt = g_hash_table_lookup(gsw->win2clnt_hash, GUINT_TO_POINTER(e->window));
  
  if(clnt) {
    clnt->cmap.dflt = e->colormap;
    TRACE(("%s w=%ld cmap=0x%x", __func__, clnt->win, (guint)clnt->cmap.dflt));
    if(get_focused_client(gsw) == clnt)
      install_colormaps(gsw, clnt);
  }
}

void process_xevent(gswm_t *gsw)
{
  XEvent ev;
  Display *dpy = gsw->display;

  XNextEvent(dpy, &ev);
  switch(ev.type) {
    case KeyPress:
      _handle_key_event(gsw, &ev.xkey);
      break;
    case ButtonPress:
      _handle_buttonpress_event(gsw, &ev.xbutton);
      break;
    case ButtonRelease:
      _handle_buttonrelease_event(gsw, &ev.xbutton);
      break;
    case EnterNotify:
      _handle_enter_event(gsw, &ev.xcrossing);
      break;
#if 0
    case LeaveNotify:
      _handle_leave_event(gsw, &ev.xcrossing);
      break;
#endif
    case FocusIn:
      _handle_focusin_event(gsw, &ev.xfocus);
      break;
    case FocusOut:
      _handle_focusout_event(gsw, &ev.xfocus);
      break;
    case DestroyNotify:
      _handle_destroy_event(gsw, &ev.xdestroywindow);
      break;
    case UnmapNotify:
      _handle_unmap_event(gsw, &ev.xunmap);
      break;
    case MapNotify:
      _handle_map_event(gsw, &ev.xmap);
      break;
    case MapRequest:
      _handle_maprequest_event(gsw, &ev.xmaprequest);
      break;
    case ConfigureRequest:
      _handle_configurerequest_event(gsw, &ev.xconfigurerequest);
      break;
    case PropertyNotify:
      _handle_property_event(gsw, &ev.xproperty);
      break;
    case ClientMessage:
      _handle_client_message(gsw, &ev.xclient);
      break;
    case MappingNotify:
      _handle_mapping_event(gsw, &ev.xmapping);
      break;
    case ColormapNotify:
      _handle_colormapchange_event(gsw, &ev.xcolormap);
      break;
#if 0 /* TODO */
    case MotionNotify:
      break;
#endif
    default:
#ifdef HAVE_XSHAPE
      if(gsw->shape && ev.type == gsw->shape_event) /* SHAPE */
        _handle_shape_event(gsw, (XShapeEvent *)&ev);
#endif
      /* Everything else is ignored */
      break;
  }
}

