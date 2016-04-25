#include "e.h"

static int _e_client_hooks_delete = 0;
static int _e_client_hooks_walking = 0;

E_API int E_EVENT_CLIENT_ADD = -1;
E_API int E_EVENT_CLIENT_REMOVE = -1;
E_API int E_EVENT_CLIENT_ZONE_SET = -1;
E_API int E_EVENT_CLIENT_DESK_SET = -1;
E_API int E_EVENT_CLIENT_RESIZE = -1;
E_API int E_EVENT_CLIENT_MOVE = -1;
E_API int E_EVENT_CLIENT_SHOW = -1;
E_API int E_EVENT_CLIENT_HIDE = -1;
E_API int E_EVENT_CLIENT_ICONIFY = -1;
E_API int E_EVENT_CLIENT_UNICONIFY = -1;
E_API int E_EVENT_CLIENT_STACK = -1;
E_API int E_EVENT_CLIENT_FOCUS_IN = -1;
E_API int E_EVENT_CLIENT_FOCUS_OUT = -1;
E_API int E_EVENT_CLIENT_PROPERTY = -1;
E_API int E_EVENT_CLIENT_FULLSCREEN = -1;
E_API int E_EVENT_CLIENT_UNFULLSCREEN = -1;
#ifdef _F_ZONE_WINDOW_ROTATION_
E_API int E_EVENT_CLIENT_ROTATION_CHANGE_BEGIN = -1;
E_API int E_EVENT_CLIENT_ROTATION_CHANGE_CANCEL = -1;
E_API int E_EVENT_CLIENT_ROTATION_CHANGE_END = -1;
#endif
E_API int E_EVENT_CLIENT_VISIBILITY_CHANGE = -1;
#ifdef HAVE_WAYLAND_ONLY
E_API int E_EVENT_CLIENT_BUFFER_CHANGE = -1;
#endif

static Eina_Hash *clients_hash[2] = {NULL}; // pixmap->client

static unsigned int focus_track_frozen = 0;

static int warp_to = 0;
static int warp_to_x = 0;
static int warp_to_y = 0;
static int warp_x[2] = {0}; //{cur,prev}
static int warp_y[2] = {0}; //{cur,prev}
static Ecore_Timer *warp_timer = NULL;

static E_Client *focused = NULL;
static E_Client *warp_client = NULL;
static E_Client *ecmove = NULL;
static E_Client *ecresize = NULL;
static E_Client *action_client = NULL;
static E_Drag *client_drag = NULL;

static Eina_List *focus_stack = NULL;
static Eina_List *raise_stack = NULL;

static Eina_Bool comp_grabbed = EINA_FALSE;

static Eina_List *handlers = NULL;
//static Eina_Bool client_grabbed = EINA_FALSE;

static Ecore_Event_Handler *action_handler_key = NULL;
static Ecore_Event_Handler *action_handler_mouse = NULL;
static Ecore_Timer *action_timer = NULL;
static Eina_Rectangle action_orig = {0, 0, 0, 0};

static E_Client_Layout_Cb _e_client_layout_cb = NULL;

static Eina_Bool _e_calc_visibility = EINA_FALSE;

EINTERN void e_client_focused_set(E_Client *ec);

static Eina_Inlist *_e_client_hooks[] =
{
   [E_CLIENT_HOOK_EVAL_PRE_FETCH] = NULL,
   [E_CLIENT_HOOK_EVAL_FETCH] = NULL,
   [E_CLIENT_HOOK_EVAL_PRE_POST_FETCH] = NULL,
   [E_CLIENT_HOOK_EVAL_POST_FETCH] = NULL,
   [E_CLIENT_HOOK_EVAL_PRE_FRAME_ASSIGN] = NULL,
   [E_CLIENT_HOOK_EVAL_POST_FRAME_ASSIGN] = NULL,
   [E_CLIENT_HOOK_EVAL_PRE_NEW_CLIENT] = NULL,
   [E_CLIENT_HOOK_EVAL_POST_NEW_CLIENT] = NULL,
   [E_CLIENT_HOOK_EVAL_END] = NULL,
   [E_CLIENT_HOOK_FOCUS_SET] = NULL,
   [E_CLIENT_HOOK_FOCUS_UNSET] = NULL,
   [E_CLIENT_HOOK_NEW_CLIENT] = NULL,
   [E_CLIENT_HOOK_DESK_SET] = NULL,
   [E_CLIENT_HOOK_MOVE_BEGIN] = NULL,
   [E_CLIENT_HOOK_MOVE_UPDATE] = NULL,
   [E_CLIENT_HOOK_MOVE_END] = NULL,
   [E_CLIENT_HOOK_RESIZE_BEGIN] = NULL,
   [E_CLIENT_HOOK_RESIZE_UPDATE] = NULL,
   [E_CLIENT_HOOK_RESIZE_END] = NULL,
   [E_CLIENT_HOOK_FULLSCREEN_PRE] = NULL,
   [E_CLIENT_HOOK_DEL] = NULL,
   [E_CLIENT_HOOK_UNREDIRECT] = NULL,
   [E_CLIENT_HOOK_REDIRECT] = NULL,
#ifdef _F_E_CLIENT_NEW_CLIENT_POST_HOOK_
   [E_CLIENT_HOOK_NEW_CLIENT_POST] = NULL,
#endif
   [E_CLIENT_HOOK_EVAL_VISIBILITY] = NULL,
   [E_CLIENT_HOOK_ICONIFY] = NULL,
   [E_CLIENT_HOOK_UNICONIFY] = NULL,
   [E_CLIENT_HOOK_AUX_HINT_CHANGE] = NULL,
};

///////////////////////////////////////////

static Eina_Bool
_e_client_cb_config_mode(void *data EINA_UNUSED, int type EINA_UNUSED, void *ev EINA_UNUSED)
{
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_client_cb_pointer_warp(void *data EINA_UNUSED, int type EINA_UNUSED, E_Event_Pointer_Warp *ev)
{
   if (ecmove)
     evas_object_move(ecmove->frame, ecmove->x + (ev->curr.x - ev->prev.x), ecmove->y + (ev->curr.y - ev->prev.y));
   return ECORE_CALLBACK_RENEW;
}


static Eina_Bool
_e_client_cb_desk_window_profile_change(void *data EINA_UNUSED, int type EINA_UNUSED, E_Event_Desk_Window_Profile_Change *ev EINA_UNUSED)
{
   const Eina_List *l;
   E_Client *ec;

   EINA_LIST_FOREACH(e_comp->clients, l, ec)
     {
        if (e_object_is_del(E_OBJECT(ec))) continue;
        ec->e.fetch.profile = 1;
        EC_CHANGED(ec);
     }
   return ECORE_CALLBACK_RENEW;
}

static void
_e_client_cb_drag_finished(E_Drag *drag, int dropped EINA_UNUSED)
{
   E_Client *ec;

   ec = drag->data;
   UNREFD(ec, 1);
   e_object_unref(E_OBJECT(ec));
   client_drag = NULL;
}

static void
_e_client_desk_window_profile_wait_desk_delfn(void *data, void *obj)
{
   E_Client *ec = data;
   E_Desk *desk = obj, *new_desk;
   const char *p;
   int i;

   if (e_object_is_del(E_OBJECT(ec))) return;

   ec->e.state.profile.wait_desk_delfn = NULL;
   eina_stringshare_replace(&ec->e.state.profile.wait, NULL);
   if (ec->e.state.profile.wait_desk)
     e_object_unref(E_OBJECT(ec->e.state.profile.wait_desk));
   ec->e.state.profile.wait_desk = NULL;
   ec->e.state.profile.wait_for_done = 0;

   if (!ec->e.state.profile.use) return;

   new_desk = e_comp_desk_window_profile_get(desk->window_profile);
   if (new_desk)
     e_client_desk_set(ec, new_desk);
   else
     {
        for (i = 0; i < ec->e.state.profile.num; i++)
          {
             p = ec->e.state.profile.available_list[i];
             new_desk = e_comp_desk_window_profile_get(p);
             if (new_desk)
               {
                  e_client_desk_set(ec, new_desk);
                  break;
               }
          }
     }
}
////////////////////////////////////////////////


static Eina_Bool
_e_client_pointer_warp_to_center_timer(void *data EINA_UNUSED)
{
   if (warp_to && warp_client)
     {
        int x, y;
        double spd;

        ecore_evas_pointer_xy_get(e_comp->ee, &x, &y);
        /* move hasn't happened yet */
        if ((x == warp_x[1]) && (y == warp_y[1]))
           return EINA_TRUE;
        if ((abs(x - warp_x[0]) > 5) || (abs(y - warp_y[0]) > 5))
          {
             /* User moved the mouse, so stop warping */
             warp_to = 0;
             goto cleanup;
          }

        spd = 0.5;
        warp_x[1] = x = warp_x[0];
        warp_y[1] = y = warp_y[0];
        warp_x[0] = (x * (1.0 - spd)) + (warp_to_x * spd);
        warp_y[0] = (y * (1.0 - spd)) + (warp_to_y * spd);
        if ((warp_x[0] == x) && (warp_y[0] == y))
          {
             warp_x[0] = warp_to_x;
             warp_y[0] = warp_to_y;
             warp_to = 0;
             goto cleanup;
          }
        ecore_evas_pointer_warp(e_comp->ee, warp_x[0], warp_y[0]);
        return ECORE_CALLBACK_RENEW;
     }
cleanup:
   E_FREE_FUNC(warp_timer, ecore_timer_del);
   if (warp_client)
     {
        warp_x[0] = warp_x[1] = warp_y[0] = warp_y[1] = -1;
        if (warp_client->modal)
          {
             warp_client = NULL;
             return ECORE_CALLBACK_CANCEL;
          }
        e_focus_event_mouse_in(warp_client);
        if (warp_client->iconic)
          {
             if (!warp_client->lock_user_iconify)
               e_client_uniconify(warp_client);
          }
        if (warp_client->shaded)
          {
             if (!warp_client->lock_user_shade)
               e_client_unshade(warp_client, warp_client->shade_dir);
          }
        
        if (!warp_client->lock_focus_out)
          {
             evas_object_focus_set(warp_client->frame, 1);
             e_client_focus_latest_set(warp_client);
          }
        warp_client = NULL;
     }
   return ECORE_CALLBACK_CANCEL;
}

////////////////////////////////////////////////

static void
_e_client_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Client_Hook *ch;
   unsigned int x;

   for (x = 0; x < E_CLIENT_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_client_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_client_hooks[x] = eina_inlist_remove(_e_client_hooks[x], EINA_INLIST_GET(ch));
          free(ch);
       }
}

static Eina_Bool
_e_client_hook_call(E_Client_Hook_Point hookpoint, E_Client *ec)
{
   E_Client_Hook *ch;

   e_object_ref(E_OBJECT(ec));
   _e_client_hooks_walking++;
   EINA_INLIST_FOREACH(_e_client_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        ch->func(ch->data, ec);
        if ((hookpoint != E_CLIENT_HOOK_DEL) &&
          (hookpoint != E_CLIENT_HOOK_MOVE_END) &&
          (hookpoint != E_CLIENT_HOOK_RESIZE_END) &&
          (hookpoint != E_CLIENT_HOOK_FOCUS_UNSET) &&
          e_object_is_del(E_OBJECT(ec)))
          break;
     }
   _e_client_hooks_walking--;
   if ((_e_client_hooks_walking == 0) && (_e_client_hooks_delete > 0))
     _e_client_hooks_clean();
   return !!e_object_unref(E_OBJECT(ec));
}

///////////////////////////////////////////

static void
_e_client_event_simple_free(void *d EINA_UNUSED, E_Event_Client *ev)
{
   UNREFD(ev->ec, 3);
   e_object_unref(E_OBJECT(ev->ec));
   free(ev);
}

static void
_e_client_event_simple(E_Client *ec, int type)
{
   E_Event_Client *ev;

   ev = E_NEW(E_Event_Client, 1);
   if (!ev) return;
   ev->ec = ec;
   REFD(ec, 3);
   e_object_ref(E_OBJECT(ec));
   ecore_event_add(type, ev, (Ecore_End_Cb)_e_client_event_simple_free, NULL);
}

static void
_e_client_event_property(E_Client *ec, int prop)
{
   E_Event_Client_Property *ev;

   ev = E_NEW(E_Event_Client_Property, 1);
   if (!ev) return;
   ev->ec = ec;
   ev->property = prop;
   REFD(ec, 33);
   e_object_ref(E_OBJECT(ec));
   ecore_event_add(E_EVENT_CLIENT_PROPERTY, ev, (Ecore_End_Cb)_e_client_event_simple_free, NULL);
}

static void
_e_client_event_desk_set_free(void *d EINA_UNUSED, E_Event_Client_Desk_Set *ev)
{
   UNREFD(ev->ec, 4);
   e_object_unref(E_OBJECT(ev->ec));
   e_object_unref(E_OBJECT(ev->desk));
   free(ev);
}

static void
_e_client_event_zone_set_free(void *d EINA_UNUSED, E_Event_Client_Zone_Set *ev)
{
   UNREFD(ev->ec, 5);
   e_object_unref(E_OBJECT(ev->ec));
   e_object_unref(E_OBJECT(ev->zone));
   free(ev);
}

////////////////////////////////////////////////

static int
_e_client_action_input_win_del(void)
{
   if (!comp_grabbed) return 0;

   e_comp_ungrab_input(1, 1);
   comp_grabbed = 0;
   return 1;
}

static void
_e_client_action_finish(void)
{
   if (comp_grabbed)
     _e_client_action_input_win_del();

   if (action_handler_key && action_client)
     evas_object_freeze_events_set(action_client->frame, 0);
   E_FREE_FUNC(action_timer, ecore_timer_del);
   E_FREE_FUNC(action_handler_key,  ecore_event_handler_del);
   E_FREE_FUNC(action_handler_mouse, ecore_event_handler_del);
   if (action_client)
     {
        action_client->keyboard_resizing = 0;
        //if (action_client->internal_elm_win)
        //  ecore_event_window_ignore_events(elm_win_window_id_get(action_client->internal_elm_win), 0);
     }
   action_client = NULL;
}

static void _e_client_transform_point_transform(int cx, int cy, double angle,
                                                int x, int y, int *tx, int *ty)
{
   double s = sin(angle * M_PI / 180);
   double c = cos(angle * M_PI / 180);
   int rx, ry;

   x -= cx;
   y -= cy;

   rx = x * c + y * s;
   ry = - x * s + y * c;

   rx += cx;
   ry += cy;

   *tx = rx;
   *ty = ry;
}

static void _e_client_transform_geometry_save(E_Client *ec, Evas_Map *map)
{
   int i;

   if (!map) return;

   for (i = 0; i < 4; i ++)
     {
        evas_map_point_precise_coord_get(map, i,
                                         &ec->transform.saved[i].x,
                                         &ec->transform.saved[i].y,
                                         &ec->transform.saved[i].z);
     }
}

static void _e_client_transform_resize(E_Client *ec)
{
   Evas_Map *map;
   int cx, cy;
   double dx = 0, dy = 0;
   double px[4], py[4];
   int pw, ph;
   int i;

   if (!ec->transformed) return;

   if (!e_pixmap_size_get(ec->pixmap, &pw, &ph))
     {
        pw = ec->client.w;
        ph = ec->client.h;
     }

   cx = ec->client.x + pw / 2;
   cy = ec->client.y + ph / 2;

   /* step 1: Rotate resized object and get map points */
   map = evas_map_new(4);
   evas_map_util_points_populate_from_geometry(map,
                                               ec->client.x, ec->client.y,
                                               pw, ph,
                                               0);
   evas_map_util_rotate(map, ec->transform.angle, cx, cy);
   evas_map_util_zoom(map, ec->transform.zoom, ec->transform.zoom, cx, cy);

   for (i = 0; i < 4; i++)
     evas_map_point_precise_coord_get(map, i, &px[i], &py[i], NULL);

   evas_map_free(map);

   /* step 2: get adjusted values to keep up fixed position according
    * to resize mode */
   switch (ec->resize_mode)
     {
      case E_POINTER_RESIZE_R:
      case E_POINTER_RESIZE_BR:
         dx = ec->transform.saved[0].x - px[0];
         dy = ec->transform.saved[0].y - py[0];
         break;
      case E_POINTER_RESIZE_BL:
      case E_POINTER_RESIZE_B:
         dx = ec->transform.saved[1].x - px[1];
         dy = ec->transform.saved[1].y - py[1];
         break;
      case E_POINTER_RESIZE_TL:
      case E_POINTER_RESIZE_L:
         dx = ec->transform.saved[2].x - px[2];
         dy = ec->transform.saved[2].y - py[2];
         break;
      case E_POINTER_RESIZE_T:
      case E_POINTER_RESIZE_TR:
         dx = ec->transform.saved[3].x - px[3];
         dy = ec->transform.saved[3].y - py[3];
         break;
      default:
         break;
     }

   ec->transform.adjusted.x = dx;
   ec->transform.adjusted.y = dy;

   /* step 3: set each points of the quadrangle */
   map = evas_map_new(4);
   evas_map_util_points_populate_from_object_full(map, ec->frame, 0);

   for (i = 0; i < 4; i++)
      evas_map_point_precise_coord_set(map, i, px[i] + dx, py[i] + dy, 0);

   evas_object_map_set(ec->frame, map);
   evas_object_map_enable_set(ec->frame, EINA_TRUE);
   evas_map_free(map);
}

static void
_e_client_transform_resize_handle(E_Client *ec)
{

   int new_x, new_y, new_w, new_h;
   int org_w, org_h;
   int button_id;
   int cx, cy;
   Evas_Point current, moveinfo;

   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_client_util_ignored_get(ec)) return;
   if (!ec->transformed) return;

   button_id = ec->moveinfo.down.button;

   org_w = ec->mouse.last_down[button_id - 1].w;
   org_h = ec->mouse.last_down[button_id - 1].h;

   new_w = ec->client.w;
   new_h = ec->client.h;
   new_x = ec->client.x;
   new_y = ec->client.y;

   /* step 1: get center coordinate its' based on original object geometry*/
   cx = ec->client.x + org_w / 2;
   cy = ec->client.y + org_h / 2;

   /* step 2: transform coordinates of mouse position
    * subtract adjusted value from mouse position is needed */
   current.x = ec->mouse.current.mx - ec->transform.adjusted.x;
   current.y = ec->mouse.current.my - ec->transform.adjusted.y;
   moveinfo.x = ec->moveinfo.down.mx - ec->transform.adjusted.x;
   moveinfo.y = ec->moveinfo.down.my - ec->transform.adjusted.y;

   _e_client_transform_point_transform(cx, cy, ec->transform.angle,
                                       current.x, current.y,
                                       &current.x, &current.y);
   _e_client_transform_point_transform(cx, cy, ec->transform.angle,
                                       moveinfo.x, moveinfo.y,
                                       &moveinfo.x, &moveinfo.y);

   /* step 3: calculate new size */
   if ((ec->resize_mode == E_POINTER_RESIZE_TR) ||
       (ec->resize_mode == E_POINTER_RESIZE_R) ||
       (ec->resize_mode == E_POINTER_RESIZE_BR))
     {
        if ((button_id >= 1) && (button_id <= 3))
          new_w = org_w + (current.x - moveinfo.x);
        else
          new_w = ec->moveinfo.down.w + (current.x - moveinfo.x);
     }
   else if ((ec->resize_mode == E_POINTER_RESIZE_TL) ||
            (ec->resize_mode == E_POINTER_RESIZE_L) ||
            (ec->resize_mode == E_POINTER_RESIZE_BL))
     {
        if ((button_id >= 1) && (button_id <= 3))
          new_w = org_w - (current.x - moveinfo.x);
        else
          new_w = ec->moveinfo.down.w - (current.x - moveinfo.x);
     }

   if ((ec->resize_mode == E_POINTER_RESIZE_TL) ||
       (ec->resize_mode == E_POINTER_RESIZE_T) ||
       (ec->resize_mode == E_POINTER_RESIZE_TR))
     {
        if ((button_id >= 1) && (button_id <= 3))
          new_h = org_h - (current.y - moveinfo.y);
        else
          new_h = ec->moveinfo.down.h - (current.y - moveinfo.y);
     }
   else if ((ec->resize_mode == E_POINTER_RESIZE_BL) ||
            (ec->resize_mode == E_POINTER_RESIZE_B) ||
            (ec->resize_mode == E_POINTER_RESIZE_BR))
     {
        if ((button_id >= 1) && (button_id <= 3))
          new_h = org_h + (current.y - moveinfo.y);
        else
          new_h = ec->moveinfo.down.h + (current.y - moveinfo.y);
     }

   new_w = MIN(new_w, ec->zone->w);
   new_h = MIN(new_h, ec->zone->h);

   /* step 4: move to new position */
   if ((ec->resize_mode == E_POINTER_RESIZE_TL) ||
       (ec->resize_mode == E_POINTER_RESIZE_L) ||
       (ec->resize_mode == E_POINTER_RESIZE_BL))
     new_x += (new_w - org_w);
   if ((ec->resize_mode == E_POINTER_RESIZE_TL) ||
       (ec->resize_mode == E_POINTER_RESIZE_T) ||
       (ec->resize_mode == E_POINTER_RESIZE_TR))
     new_y += (new_h - org_h);

   /* step 5: set geometry to new value */
   evas_object_geometry_set(ec->frame, new_x, new_y, new_w, new_h);
}

void
_e_client_transform_resize_begin(E_Client *ec)
{
   if (!ec->transformed) return;

   _e_client_transform_geometry_save(ec, (Evas_Map *)evas_object_map_get(ec->frame));
}

void
_e_client_transform_resize_end(E_Client *ec)
{
   Evas_Map *map;
   int new_x = 0, new_y = 0;
   int cx, cy, pw, ph;

   if (!ec->transformed) return;

   map = (Evas_Map*)evas_object_map_get(ec->frame);
   if (!map) return;

   if (!e_pixmap_size_get(ec->pixmap, &pw, &ph))
     {
        pw = ec->client.w;
        ph = ec->client.h;
     }

   cx = ec->client.x + pw / 2 + ec->transform.adjusted.x;
   cy = ec->client.y + ph / 2 + ec->transform.adjusted.y;

   if (ec->transform.zoom != 1.0)
     {
        Evas_Map *tmp_map;

        tmp_map = evas_map_dup(map);
        evas_map_util_zoom(tmp_map,
                           1 / ec->transform.zoom,
                           1 / ec->transform.zoom,
                           cx, cy);

        _e_client_transform_geometry_save(ec, tmp_map);
        evas_map_free(tmp_map);
     }
   else
     {
        _e_client_transform_geometry_save(ec, map);
     }

   /* move original object to adjusted position after resizing */
   _e_client_transform_point_transform(cx, cy,
                                       ec->transform.angle,
                                       ec->transform.saved[0].x,
                                       ec->transform.saved[0].y,
                                       &new_x, &new_y);
   e_client_util_move_without_frame(ec, new_x, new_y);
   evas_map_util_object_move_sync_set(map, EINA_TRUE);
}

static void
_e_client_transform_move_end(E_Client *ec)
{

   int i;
   double dx, dy, px, py;
   Evas_Map *map;

   if (!ec->transformed) return;

   map = (Evas_Map*)evas_object_map_get(ec->frame);
   if (!map) return;

   if (ec->transform.zoom != 1.0)
     {
        evas_map_point_precise_coord_get(map, 0, &px, &py, NULL);

        dx = px - ec->transform.saved[0].x;
        dy = py - ec->transform.saved[0].y;

        for (i = 0; i < 4; i++)
          {
             ec->transform.saved[i].x += dx;
             ec->transform.saved[i].y += dy;
          }
     }
   else
     _e_client_transform_geometry_save(ec, map);
}

static void
_e_client_revert_focus(E_Client *ec)
{
   E_Client *pec;
   E_Desk *desk;

   if (stopping) return;
   if (!ec->focused) return;
   if (!ec->zone) return;
   desk = e_desk_current_get(ec->zone);
   if (!desk) return;
   if (ec->desk == desk)
     evas_object_focus_set(ec->frame, 0);

   if ((ec->parent) &&
       (ec->parent->desk == desk) && (ec->parent->modal == ec))
     {
        evas_object_focus_set(ec->parent->frame, 1);
        if (e_config->raise_on_revert_focus)
          evas_object_raise(ec->parent->frame);
     }
   else if (e_config->focus_policy == E_FOCUS_MOUSE)
     {
        pec = e_client_under_pointer_get(desk, ec);
        if (pec)
          evas_object_focus_set(pec->frame, 1);
        /* no autoraise/revert here because it's probably annoying */
     }
}

static void
_e_client_free(E_Client *ec)
{
   e_comp_object_redirected_set(ec->frame, 0);
   e_comp_object_render_update_del(ec->frame);

   E_OBJECT(ec)->references++;
   if (ec->fullscreen)
     {
        ec->desk->fullscreen_clients = eina_list_remove(ec->desk->fullscreen_clients, ec);
        if (!ec->desk->fullscreen_clients)
          e_comp_render_queue();
     }
   if (ec->new_client)
     e_comp->new_clients--;
   if (ec->e.state.profile.use)
     {
        e_client_desk_window_profile_wait_desk_set(ec, NULL);

        if (ec->e.state.profile.available_list)
          {
             int i;
             for (i = 0; i < ec->e.state.profile.num; i++)
               eina_stringshare_replace(&ec->e.state.profile.available_list[i], NULL);
             E_FREE(ec->e.state.profile.available_list);
          }

        ec->e.state.profile.num = 0;

        eina_stringshare_replace(&ec->e.state.profile.set, NULL);
        eina_stringshare_replace(&ec->e.state.profile.wait, NULL);
        eina_stringshare_replace(&ec->e.state.profile.name, NULL);
        ec->e.state.profile.wait_for_done = 0;
        ec->e.state.profile.use = 0;
     }

   if (ec->e.state.video_parent && ec->e.state.video_parent_client)
     {
        ec->e.state.video_parent_client->e.state.video_child =
          eina_list_remove(ec->e.state.video_parent_client->e.state.video_child, ec);
     }
   if (ec->e.state.video_child)
     {
        E_Client *tmp;

        EINA_LIST_FREE(ec->e.state.video_child, tmp)
          tmp->e.state.video_parent_client = NULL;
     }
   E_FREE_FUNC(ec->internal_elm_win, evas_object_del);
   E_FREE_FUNC(ec->post_job, ecore_idle_enterer_del);

   E_FREE_FUNC(ec->kill_timer, ecore_timer_del);
   E_FREE_LIST(ec->pending_resize, free);

   E_FREE_FUNC(ec->map_timer, ecore_timer_del);

   if (ec->remember)
     {
        E_Remember *rem;

        rem = ec->remember;
        ec->remember = NULL;
        e_remember_unuse(rem);
     }
   ec->group = eina_list_free(ec->group);
   ec->transients = eina_list_free(ec->transients);
   ec->stick_desks = eina_list_free(ec->stick_desks);
   if (ec->netwm.icons)
     {
        int i;
        for (i = 0; i < ec->netwm.num_icons; i++)
          free(ec->netwm.icons[i].data);
        E_FREE(ec->netwm.icons);
     }
   E_FREE(ec->netwm.extra_types);
   eina_stringshare_replace(&ec->border.name, NULL);
   eina_stringshare_replace(&ec->bordername, NULL);
   eina_stringshare_replace(&ec->icccm.name, NULL);
   eina_stringshare_replace(&ec->icccm.class, NULL);
   eina_stringshare_replace(&ec->icccm.title, NULL);
   eina_stringshare_replace(&ec->icccm.icon_name, NULL);
   eina_stringshare_replace(&ec->icccm.machine, NULL);
   eina_stringshare_replace(&ec->icccm.window_role, NULL);
   if ((ec->icccm.command.argc > 0) && (ec->icccm.command.argv))
     {
        int i;

        for (i = 0; i < ec->icccm.command.argc; i++)
          free(ec->icccm.command.argv[i]);
        E_FREE(ec->icccm.command.argv);
     }
   eina_stringshare_replace(&ec->netwm.name, NULL);
   eina_stringshare_replace(&ec->netwm.icon_name, NULL);
   eina_stringshare_replace(&ec->internal_icon, NULL);
   eina_stringshare_replace(&ec->internal_icon_key, NULL);

   focus_stack = eina_list_remove(focus_stack, ec);
   raise_stack = eina_list_remove(raise_stack, ec);

   if (ec->e.state.profile.wait_desk)
     {
        e_object_delfn_del(E_OBJECT(ec->e.state.profile.wait_desk),
                           ec->e.state.profile.wait_desk_delfn);
        ec->e.state.profile.wait_desk_delfn = NULL;
        e_object_unref(E_OBJECT(ec->e.state.profile.wait_desk));
     }
   ec->e.state.profile.wait_desk = NULL;
   evas_object_del(ec->frame);
   E_OBJECT(ec)->references--;
   ELOG("CLIENT FREE", ec->pixmap, ec);

#ifdef HAVE_WAYLAND
   e_uuid_store_entry_del(ec->uuid);
#endif

   free(ec);
}

static void
_e_client_del(E_Client *ec)
{
   E_Client *child;

   ec->changed = 0;
   focus_stack = eina_list_remove(focus_stack, ec);
   raise_stack = eina_list_remove(raise_stack, ec);

   if (ec->cur_mouse_action)
     {
        if (ec->cur_mouse_action->func.end)
          ec->cur_mouse_action->func.end(E_OBJECT(ec), "");
     }
   if (action_client == ec) _e_client_action_finish();
   e_pointer_type_pop(e_comp->pointer, ec, NULL);

   if (warp_client == ec)
     {
        E_FREE_FUNC(warp_timer, ecore_timer_del);
        warp_client = NULL;
     }

   if ((client_drag) && (client_drag->data == ec))
     {
        e_object_del(E_OBJECT(client_drag));
        client_drag = NULL;
     }
   if (!stopping)
     {
        e_client_comp_hidden_set(ec, 1);
        evas_object_pass_events_set(ec->frame, 1);
     }

   E_FREE_FUNC(ec->border_prop_dialog, e_object_del);
   E_FREE_FUNC(ec->color_editor, evas_object_del);

   if (ec->internal_elm_win)
     evas_object_hide(ec->internal_elm_win);

   if (ec->focused)
     _e_client_revert_focus(ec);
   evas_object_focus_set(ec->frame, 0);

   E_FREE_FUNC(ec->ping_poller, ecore_poller_del);
   /* must be called before parent/child clear */
   _e_client_hook_call(E_CLIENT_HOOK_DEL, ec);
   E_FREE(ec->comp_data);

   if ((!ec->new_client) && (!stopping))
     _e_client_event_simple(ec, E_EVENT_CLIENT_REMOVE);

   ELOG("CLIENT DEL", ec->pixmap, ec);

   if (ec->parent)
     {
        ec->parent->transients = eina_list_remove(ec->parent->transients, ec);
        ec->parent = NULL;
     }
   EINA_LIST_FREE(ec->transients, child)
     child->parent = NULL;

   if (ec->leader)
     {
        ec->leader->group = eina_list_remove(ec->leader->group, ec);
        if (ec->leader->modal == ec)
          ec->leader->modal = NULL;
        ec->leader = NULL;
     }
   EINA_LIST_FREE(ec->group, child)
     child->leader = NULL;

   eina_hash_del_by_key(clients_hash[e_pixmap_type_get(ec->pixmap)], &ec->pixmap);
   e_comp->clients = eina_list_remove(e_comp->clients, ec);
   e_comp_object_render_update_del(ec->frame);
   e_comp_post_update_purge(ec);
   if (e_pixmap_free(ec->pixmap))
     e_pixmap_client_set(ec->pixmap, NULL);
   ec->pixmap = NULL;

   if (ec->transform_core.transform_list)
     {
        E_Util_Transform *transform;

        EINA_LIST_FREE(ec->transform_core.transform_list, transform)
          {
             e_util_transform_unref(transform);
          }
     }

   ec->transform_core.result.enable = EINA_FALSE;

   e_client_visibility_calculate();
}

///////////////////////////////////////////

static Eina_Bool
_e_client_cb_kill_timer(void *data)
{
   E_Client *ec = data;

// dont wait until it's hung -
//   if (ec->hung)
//     {
   if (ec->netwm.pid > 1)
     kill(ec->netwm.pid, SIGKILL);
//     }
   ec->kill_timer = NULL;
   return ECORE_CALLBACK_CANCEL;
}

static Eina_Bool
_e_client_cb_ping_poller(void *data)
{
   E_Client *ec;

   ec = data;
   if (ec->ping_ok)
     {
        if (ec->hung)
          {
             ec->hung = 0;
             evas_object_smart_callback_call(ec->frame, "unhung", NULL);
             E_FREE_FUNC(ec->kill_timer, ecore_timer_del);
          }
     }
   else
     {
        /* if time between last ping and now is greater
         * than half the ping interval... */
        if ((ecore_loop_time_get() - ec->ping) >
            ((e_config->ping_clients_interval *
              ecore_poller_poll_interval_get(ECORE_POLLER_CORE)) / 2.0))
          {
             if (!ec->hung)
               {
                  ec->hung = 1;
                  evas_object_smart_callback_call(ec->frame, "hung", NULL);
                  /* FIXME: if below dialog is up - hide it now */
               }
             if (ec->delete_requested)
               {
                  /* FIXME: pop up dialog saying app is hung - kill client, or pid */
                  e_client_act_kill_begin(ec);
               }
          }
     }
   ec->ping_poller = NULL;
   e_client_ping(ec);
   return ECORE_CALLBACK_CANCEL;
}

///////////////////////////////////////////

static int
_e_client_action_input_win_new(void)
{
   if (comp_grabbed)
     {
        CRI("DOUBLE COMP GRAB! ACK!!!!");
        return 1;
     }
   comp_grabbed = e_comp_grab_input(1, 1);
   if (!comp_grabbed) _e_client_action_input_win_del();
   return comp_grabbed;
}

static void
_e_client_action_init(E_Client *ec)
{
   action_orig.x = ec->x;
   action_orig.y = ec->y;
   action_orig.w = ec->w;
   action_orig.h = ec->h;

   if (action_client)
     {
        action_client->keyboard_resizing = 0;
        //if (action_client->internal_elm_win)
        //  ecore_event_window_ignore_events(elm_win_window_id_get(action_client->internal_elm_win), 0);
     }
   action_client = ec;
   //if (ec->internal_elm_win)
   //  ecore_event_window_ignore_events(elm_win_window_id_get(ec->internal_elm_win), 1);
}

static void
_e_client_action_restore_orig(E_Client *ec)
{
   if (action_client != ec)
     return;

   evas_object_geometry_set(ec->frame, action_orig.x, action_orig.y, action_orig.w, action_orig.h);
}

static int
_e_client_key_down_modifier_apply(int modifier, int value)
{
   if (modifier & ECORE_EVENT_MODIFIER_CTRL)
     return value * 2;
   else if (modifier & ECORE_EVENT_MODIFIER_ALT)
     {
        value /= 2;
        if (value)
          return value;
        else
          return 1;
     }

   return value;
}


static int
_e_client_move_begin(E_Client *ec)
{
   if ((ec->fullscreen) || (ec->lock_user_location))
     return 0;

/*
   if (client_grabbed && !e_grabinput_get(e_client_util_pwin_get(ec), 0, e_client_util_pwin_get(ec)))
     {
        client_grabbed = 0;
        return 0;
     }
*/
   if (!_e_client_action_input_win_new()) return 0;
   ec->moving = 1;
   ecmove = ec;
   _e_client_hook_call(E_CLIENT_HOOK_MOVE_BEGIN, ec);
   if (!ec->moving)
     {
        if (ecmove == ec) ecmove = NULL;
        _e_client_action_input_win_del();
        return 0;
     }
   if (!ec->lock_user_stacking)
     {
        if (e_config->border_raise_on_mouse_action)
          evas_object_raise(ec->frame);
     }
   return 1;
}

static int
_e_client_move_end(E_Client *ec)
{
   //if (client_grabbed)
     //{
        //e_grabinput_release(e_client_util_pwin_get(ec), e_client_util_pwin_get(ec));
        //client_grabbed = 0;
     //}
   _e_client_action_input_win_del();
   e_pointer_mode_pop(ec, E_POINTER_MOVE);
   ec->moving = 0;
   _e_client_hook_call(E_CLIENT_HOOK_MOVE_END, ec);

   if (ec->transformed)
     _e_client_transform_move_end(ec);

   ecmove = NULL;
   return 1;
}

static Eina_Bool
_e_client_action_move_timeout(void *data EINA_UNUSED)
{
   _e_client_move_end(action_client);
   _e_client_action_finish();
   return ECORE_CALLBACK_CANCEL;
}

static void
_e_client_action_move_timeout_add(void)
{
   E_FREE_FUNC(action_timer, ecore_timer_del);
   if (e_config->border_keyboard.timeout)
     action_timer = ecore_timer_add(e_config->border_keyboard.timeout, _e_client_action_move_timeout, NULL);
}

static Eina_Bool
_e_client_move_key_down(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Event_Key *ev = event;
   int x, y;

   if (!comp_grabbed) return ECORE_CALLBACK_RENEW;
   if (!action_client)
     {
        ERR("no action_client!");
        goto stop;
     }

   x = action_client->x;
   y = action_client->y;

   if ((strcmp(ev->key, "Up") == 0) || (strcmp(ev->key, "k") == 0))
     y -= _e_client_key_down_modifier_apply(ev->modifiers, MAX(e_config->border_keyboard.move.dy, 1));
   else if ((strcmp(ev->key, "Down") == 0) || (strcmp(ev->key, "j") == 0))
     y += _e_client_key_down_modifier_apply(ev->modifiers, MAX(e_config->border_keyboard.move.dy, 1));
   else if ((strcmp(ev->key, "Left") == 0) || (strcmp(ev->key, "h") == 0))
     x -= _e_client_key_down_modifier_apply(ev->modifiers, MAX(e_config->border_keyboard.move.dx, 1));
   else if ((strcmp(ev->key, "Right") == 0) || (strcmp(ev->key, "l") == 0))
     x += _e_client_key_down_modifier_apply(ev->modifiers, MAX(e_config->border_keyboard.move.dx, 1));
   else if (strcmp(ev->key, "Return") == 0)
     goto stop;
   else if (strcmp(ev->key, "Escape") == 0)
     {
        _e_client_action_restore_orig(action_client);
        goto stop;
     }
   else if ((strncmp(ev->key, "Control", sizeof("Control") - 1) != 0) &&
            (strncmp(ev->key, "Alt", sizeof("Alt") - 1) != 0))
     goto stop;

   evas_object_move(action_client->frame, x, y);
   _e_client_action_move_timeout_add();

   return ECORE_CALLBACK_PASS_ON;

stop:
   if (action_client) _e_client_move_end(action_client);
   _e_client_action_finish();
   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_e_client_move_mouse_down(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   if (!comp_grabbed) return ECORE_CALLBACK_RENEW;

   if (!action_client)
     ERR("no action_client!");

   if (action_client) _e_client_move_end(action_client);
   _e_client_action_finish();
   return ECORE_CALLBACK_DONE;
}

static void
_e_client_moveinfo_gather(E_Client *ec, const char *source)
{
   if (e_util_glob_match(source, "mouse,*,1"))
     ec->moveinfo.down.button = 1;
   else if (e_util_glob_match(source, "mouse,*,2"))
     ec->moveinfo.down.button = 2;
   else if (e_util_glob_match(source, "mouse,*,3"))
     ec->moveinfo.down.button = 3;
   else
     ec->moveinfo.down.button = 0;
   if ((ec->moveinfo.down.button >= 1) && (ec->moveinfo.down.button <= 3))
     {
        ec->moveinfo.down.mx = ec->mouse.last_down[ec->moveinfo.down.button - 1].mx;
        ec->moveinfo.down.my = ec->mouse.last_down[ec->moveinfo.down.button - 1].my;
     }
   else
     {
        ec->moveinfo.down.mx = ec->mouse.current.mx;
        ec->moveinfo.down.my = ec->mouse.current.my;
     }
}

static void
_e_client_resize_handle(E_Client *ec)
{
   int x, y, w, h;
   int new_x, new_y, new_w, new_h;
   int tw, th;
   Eina_List *skiplist = NULL;

   if (ec->transformed)
     {
        _e_client_transform_resize_handle(ec);
        return;
     }

   x = ec->x;
   y = ec->y;
   w = ec->w;
   h = ec->h;

   if ((ec->resize_mode == E_POINTER_RESIZE_TR) ||
       (ec->resize_mode == E_POINTER_RESIZE_R) ||
       (ec->resize_mode == E_POINTER_RESIZE_BR))
     {
        if ((ec->moveinfo.down.button >= 1) &&
            (ec->moveinfo.down.button <= 3))
          w = ec->mouse.last_down[ec->moveinfo.down.button - 1].w +
            (ec->mouse.current.mx - ec->moveinfo.down.mx);
        else
          w = ec->moveinfo.down.w + (ec->mouse.current.mx - ec->moveinfo.down.mx);
     }
   else if ((ec->resize_mode == E_POINTER_RESIZE_TL) ||
            (ec->resize_mode == E_POINTER_RESIZE_L) ||
            (ec->resize_mode == E_POINTER_RESIZE_BL))
     {
        if ((ec->moveinfo.down.button >= 1) &&
            (ec->moveinfo.down.button <= 3))
          w = ec->mouse.last_down[ec->moveinfo.down.button - 1].w -
            (ec->mouse.current.mx - ec->moveinfo.down.mx);
        else
          w = ec->moveinfo.down.w - (ec->mouse.current.mx - ec->moveinfo.down.mx);
     }

   if ((ec->resize_mode == E_POINTER_RESIZE_TL) ||
       (ec->resize_mode == E_POINTER_RESIZE_T) ||
       (ec->resize_mode == E_POINTER_RESIZE_TR))
     {
        if ((ec->moveinfo.down.button >= 1) &&
            (ec->moveinfo.down.button <= 3))
          h = ec->mouse.last_down[ec->moveinfo.down.button - 1].h -
            (ec->mouse.current.my - ec->moveinfo.down.my);
        else
          h = ec->moveinfo.down.h - (ec->mouse.current.my - ec->moveinfo.down.my);
     }
   else if ((ec->resize_mode == E_POINTER_RESIZE_BL) ||
            (ec->resize_mode == E_POINTER_RESIZE_B) ||
            (ec->resize_mode == E_POINTER_RESIZE_BR))
     {
        if ((ec->moveinfo.down.button >= 1) &&
            (ec->moveinfo.down.button <= 3))
          h = ec->mouse.last_down[ec->moveinfo.down.button - 1].h +
            (ec->mouse.current.my - ec->moveinfo.down.my);
        else
          h = ec->moveinfo.down.h + (ec->mouse.current.my - ec->moveinfo.down.my);
     }

   tw = ec->w;
   th = ec->h;

   if ((ec->resize_mode == E_POINTER_RESIZE_TL) ||
       (ec->resize_mode == E_POINTER_RESIZE_L) ||
       (ec->resize_mode == E_POINTER_RESIZE_BL))
     x += (tw - w);
   if ((ec->resize_mode == E_POINTER_RESIZE_TL) ||
       (ec->resize_mode == E_POINTER_RESIZE_T) ||
       (ec->resize_mode == E_POINTER_RESIZE_TR))
     y += (th - h);

   skiplist = eina_list_append(skiplist, ec);
   e_resist_client_position(skiplist,
                                      ec->x, ec->y, ec->w, ec->h,
                                      x, y, w, h,
                                      &new_x, &new_y, &new_w, &new_h);
   eina_list_free(skiplist);

   w = new_w;
   h = new_h;
   if (e_config->screen_limits == E_CLIENT_OFFSCREEN_LIMIT_ALLOW_NONE)
     {
        if (ec->zone)
          {
             w = MIN(w, ec->zone->w);
             h = MIN(h, ec->zone->h);
          }
     }
   e_client_resize_limit(ec, &new_w, &new_h);
   if ((ec->resize_mode == E_POINTER_RESIZE_TL) ||
       (ec->resize_mode == E_POINTER_RESIZE_L) ||
       (ec->resize_mode == E_POINTER_RESIZE_BL))
     new_x += (w - new_w);
   if ((ec->resize_mode == E_POINTER_RESIZE_TL) ||
       (ec->resize_mode == E_POINTER_RESIZE_T) ||
       (ec->resize_mode == E_POINTER_RESIZE_TR))
     new_y += (h - new_h);

   evas_object_geometry_set(ec->frame, new_x, new_y, new_w, new_h);
}

static int
_e_client_resize_end(E_Client *ec)
{
   _e_client_action_input_win_del();
   e_pointer_mode_pop(ec, ec->resize_mode);
   ec->resize_mode = E_POINTER_RESIZE_NONE;

   /* If this border was maximized, we need to unset Maximized state or
    * on restart, E still thinks it's maximized */
   if (ec->maximized != E_MAXIMIZE_NONE)
     e_hints_window_maximized_set(ec, ec->maximized & E_MAXIMIZE_NONE,
                                  ec->maximized & E_MAXIMIZE_NONE);

   _e_client_hook_call(E_CLIENT_HOOK_RESIZE_END, ec);

   if (ec->transformed)
     _e_client_transform_resize_end(ec);

   ecresize = NULL;

   return 1;
}


static Eina_Bool
_e_client_action_resize_timeout(void *data EINA_UNUSED)
{
   _e_client_resize_end(action_client);
   _e_client_action_finish();
   return ECORE_CALLBACK_CANCEL;
}

static void
_e_client_action_resize_timeout_add(void)
{
   E_FREE_FUNC(action_timer, ecore_timer_del);
   if (e_config->border_keyboard.timeout)
     action_timer = ecore_timer_add(e_config->border_keyboard.timeout, _e_client_action_resize_timeout, NULL);
}

static Eina_Bool
_e_client_resize_key_down(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Event_Key *ev = event;
   int w, h, dx, dy;

   if (!comp_grabbed) return ECORE_CALLBACK_RENEW;
   if (!action_client)
     {
        ERR("no action_client!");
        goto stop;
     }

   w = action_client->w;
   h = action_client->h;

   dx = e_config->border_keyboard.resize.dx;
   if (dx < action_client->icccm.step_w)
     dx = action_client->icccm.step_w;
   dx = _e_client_key_down_modifier_apply(ev->modifiers, dx);
   if (dx < action_client->icccm.step_w)
     dx = action_client->icccm.step_w;

   dy = e_config->border_keyboard.resize.dy;
   if (dy < action_client->icccm.step_h)
     dy = action_client->icccm.step_h;
   dy = _e_client_key_down_modifier_apply(ev->modifiers, dy);
   if (dy < action_client->icccm.step_h)
     dy = action_client->icccm.step_h;

   if ((strcmp(ev->key, "Up") == 0) || (strcmp(ev->key, "k") == 0))
     h -= dy;
   else if ((strcmp(ev->key, "Down") == 0) || (strcmp(ev->key, "j") == 0))
     h += dy;
   else if ((strcmp(ev->key, "Left") == 0) || (strcmp(ev->key, "h") == 0))
     w -= dx;
   else if ((strcmp(ev->key, "Right") == 0) || (strcmp(ev->key, "l") == 0))
     w += dx;
   else if (strcmp(ev->key, "Return") == 0)
     goto stop;
   else if (strcmp(ev->key, "Escape") == 0)
     {
        _e_client_action_restore_orig(action_client);
        goto stop;
     }
   else if ((strncmp(ev->key, "Control", sizeof("Control") - 1) != 0) &&
            (strncmp(ev->key, "Alt", sizeof("Alt") - 1) != 0))
     goto stop;
   if (e_config->screen_limits == E_CLIENT_OFFSCREEN_LIMIT_ALLOW_NONE)
     {
        if (action_client->zone)
          {
             w = MIN(w, action_client->zone->w);
             h = MIN(h, action_client->zone->h);
          }
     }
   e_client_resize_limit(action_client, &w, &h);
   evas_object_resize(action_client->frame, w, h);
   _e_client_action_resize_timeout_add();

   return ECORE_CALLBACK_PASS_ON;

stop:
   if (action_client) _e_client_resize_end(action_client);
   _e_client_action_finish();
   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_e_client_resize_mouse_down(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   if (!comp_grabbed) return ECORE_CALLBACK_RENEW;

   if (!action_client)
     ERR("no action_client!");

   if (action_client) _e_client_resize_end(action_client);
   _e_client_action_finish();
   return ECORE_CALLBACK_DONE;
}

////////////////////////////////////////////////

static E_Client *
_e_client_under_pointer_helper(E_Desk *desk, E_Client *exclude, int x, int y)
{
   E_Client *ec = NULL, *cec;

   E_CLIENT_REVERSE_FOREACH(cec)
     {
        /* If a border was specified which should be excluded from the list
         * (because it will be closed shortly for example), skip */
        if (e_client_util_ignored_get(cec) || (!e_client_util_desk_visible(cec, desk))) continue;
        if (!evas_object_visible_get(cec->frame)) continue;
        if ((exclude) && (cec == exclude)) continue;
        if (!E_INSIDE(x, y, cec->x, cec->y, cec->w, cec->h))
          continue;
        /* If the layer is higher, the position of the window is higher
         * (always on top vs always below) */
        if (!ec || (cec->layer > ec->layer))
          ec = cec;
     }
   return ec;
}

////////////////////////////////////////////////

static void
_e_client_zones_layout_calc(E_Client *ec, int *zx, int *zy, int *zw, int *zh)
{
   int x, y, w, h;
   E_Zone *zone_above, *zone_below, *zone_left, *zone_right;

   if (!ec->zone) return;
   x = ec->zone->x;
   y = ec->zone->y;
   w = ec->zone->w;
   h = ec->zone->h;

   if (eina_list_count(e_comp->zones) == 1)
     {
        if (zx) *zx = x;
        if (zy) *zy = y;
        if (zw) *zw = w;
        if (zh) *zh = h;
        return;
     }

   zone_left = e_comp_zone_xy_get((x - w + 5), y);
   zone_right = e_comp_zone_xy_get((x + w + 5), y);
   zone_above = e_comp_zone_xy_get(x, (y - h + 5));
   zone_below = e_comp_zone_xy_get(x, (y + h + 5));

   if (!(zone_above) && (y))
     zone_above = e_comp_zone_xy_get(x, (h - 5));

   if (!(zone_left) && (x))
     zone_left = e_comp_zone_xy_get((x - 5), y);

   if (zone_right)
     w = zone_right->x + zone_right->w;

   if (zone_left)
     w = ec->zone->x + ec->zone->w;

   if (zone_below)
     h = zone_below->y + zone_below->h;

   if (zone_above)
     h = ec->zone->y + ec->zone->h;

   if ((zone_left) && (zone_right))
     w = ec->zone->w + zone_right->x;

   if ((zone_above) && (zone_below))
     h = ec->zone->h + zone_below->y;

   if (x) x -= ec->zone->w;
   if (y) y -= ec->zone->h;

   if (zx) *zx = x > 0 ? x : 0;
   if (zy) *zy = y > 0 ? y : 0;
   if (zw) *zw = w;
   if (zh) *zh = h;
}

static void
_e_client_stay_within_canvas(E_Client *ec, int x, int y, int *new_x, int *new_y)
{
   int new_x_max, new_y_max;
   int zw, zh;
   Eina_Bool lw, lh;

   if (!ec->zone)
     {
        if (new_x) *new_x = x;
        if (new_y) *new_y = y;
        return;
     }

   _e_client_zones_layout_calc(ec, NULL, NULL, &zw, &zh);

   new_x_max = zw - ec->w;
   new_y_max = zh - ec->h;
   lw = ec->w > zw ? EINA_TRUE : EINA_FALSE;
   lh = ec->h > zh ? EINA_TRUE : EINA_FALSE;

   if (lw)
     {
        if (x <= new_x_max)
          *new_x = new_x_max;
        else if (x >= 0)
          *new_x = 0;
     }
   else
     {
        if (x >= new_x_max)
          *new_x = new_x_max;
        else if (x <= 0)
          *new_x = 0;
     }

   if (lh)
     {
        if (y <= new_y_max)
          *new_y = new_y_max;
        else if (y >= 0)
          *new_y = 0;
     }
   else
     {
        if (y >= new_y_max)
          *new_y = new_y_max;
        else if (y <= 0)
          *new_y = 0;
     }
}

static void
_e_client_reset_lost_window(E_Client *ec)
{
   E_OBJECT_CHECK(ec);

   if (ec->during_lost) return;
   ec->during_lost = EINA_TRUE;

   if (ec->iconic) e_client_uniconify(ec);
   if (!ec->moving) e_comp_object_util_center(ec->frame);

   evas_object_raise(ec->frame);
   if (!ec->lock_focus_out)
     evas_object_focus_set(ec->frame, 1);

   e_client_pointer_warp_to_center(ec);
   ec->during_lost = EINA_FALSE;
}

static void
_e_client_move_lost_window_to_center(E_Client *ec)
{
   int loss_overlap = 5;
   int zw, zh, zx, zy;

   if (ec->during_lost) return;
   if (!ec->zone) return;

   _e_client_zones_layout_calc(ec, &zx, &zy, &zw, &zh);

   if (!E_INTERSECTS(zx + loss_overlap,
                     zy + loss_overlap,
                     zw - (2 * loss_overlap),
                     zh - (2 * loss_overlap),
                     ec->x, ec->y, ec->w, ec->h))
     {
        _e_client_reset_lost_window(ec);
     }
}

////////////////////////////////////////////////
static void
_e_client_zone_update(E_Client *ec)
{
   Eina_List *l;
   E_Zone *zone;

   /* still within old zone - leave it there */
   if (ec->zone && E_INTERSECTS(ec->x, ec->y, ec->w, ec->h,
                    ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h))
     return;
   /* find a new zone */
   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        if (E_INTERSECTS(ec->x, ec->y, ec->w, ec->h,
                         zone->x, zone->y, zone->w, zone->h))
          {
             e_client_zone_set(ec, zone);
             return;
          }
     }
}

////////////////////////////////////////////////

static void
_e_client_cb_evas_hide(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Client *ec = data;

   if (stopping) return; //ignore all of this if we're shutting down!
   if (e_object_is_del(data)) return; //client is about to die
   if (ec->cur_mouse_action)
     {
        if (ec->cur_mouse_action->func.end_mouse)
          ec->cur_mouse_action->func.end_mouse(E_OBJECT(ec), "", NULL);
        else if (ec->cur_mouse_action->func.end)
          ec->cur_mouse_action->func.end(E_OBJECT(ec), "");
        E_FREE_FUNC(ec->cur_mouse_action, e_object_unref);
     }
   if (action_client == ec) _e_client_action_finish();
   e_pointer_type_pop(e_comp->pointer, ec, NULL);

   if (!ec->hidden)
     {
        if (ec->focused)
          _e_client_revert_focus(ec);
     }
   ec->want_focus = ec->take_focus = 0;

   ec->post_show = 0;

   if (ec->new_client || ec->iconic) return;
   _e_client_event_simple(ec, E_EVENT_CLIENT_HIDE);

   EC_CHANGED(ec);
}

static void
_e_client_cb_evas_shade_done(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Client *ec = data;

   if (e_object_is_del(data)) return;

   ec->shading = 0;
   ec->shaded = !(ec->shaded);
   ec->changes.shaded = 1;
   ec->changes.shading = 1;
   e_client_comp_hidden_set(ec, ec->shaded);
}

static void
_e_client_cb_evas_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Client *ec = data;
   Evas_Coord x, y;

   if (e_object_is_del(data)) return;

   ec->pre_res_change.valid = 0;
   if (ec->internal_elm_win)
     {
        EC_CHANGED(ec);
        ec->changes.pos = 1;
     }

   _e_client_event_simple(ec, E_EVENT_CLIENT_MOVE);

   _e_client_zone_update(ec);
   evas_object_geometry_get(ec->frame, &x, &y, NULL, NULL);
   if ((e_config->transient.move) && (ec->transients))
     {
        Eina_List *list = eina_list_clone(ec->transients);
        E_Client *child;

        EINA_LIST_FREE(list, child)
          {
             evas_object_move(child->frame,
                              child->x + x - ec->pre_cb.x,
                              child->y + y - ec->pre_cb.y);
          }
     }
   if (ec->moving || (ecmove == ec))
     _e_client_hook_call(E_CLIENT_HOOK_MOVE_UPDATE, ec);

   if ((!ec->moving) && (ec->transformed))
     _e_client_transform_geometry_save(ec,
                                       (Evas_Map *)evas_object_map_get(ec->frame));

   e_remember_update(ec);
   ec->pre_cb.x = x; ec->pre_cb.y = y;

   e_client_visibility_calculate();
}

static void
_e_client_cb_evas_resize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Client *ec = data;
   Evas_Coord x, y, w, h;

   if (e_object_is_del(data)) return;

   ec->pre_res_change.valid = 0;

   _e_client_event_simple(ec, E_EVENT_CLIENT_RESIZE);

   _e_client_zone_update(ec);
   evas_object_geometry_get(ec->frame, &x, &y, &w, &h);
   if ((e_config->transient.resize) && (ec->transients))
     {
        Eina_List *list = eina_list_clone(ec->transients);
        E_Client *child;

        EINA_LIST_FREE(list, child)
          {
             Evas_Coord nx, ny, nw, nh;

             if ((ec->pre_cb.w > 0) && (ec->pre_cb.h > 0))
               {
                  nx = x + (((child->x - x) * w) / ec->pre_cb.w);
                  ny = y + (((child->y - y) * h) / ec->pre_cb.h);
                  nw = (child->w * w) / ec->pre_cb.w;
                  nh = (child->h * h) / ec->pre_cb.h;
                  nx += ((nw - child->w) / 2);
                  ny += ((nh - child->h) / 2);
                  evas_object_move(child->frame, nx, ny);
               }
          }
     }

   if (e_client_util_resizing_get(ec) || (ecresize == ec))
     _e_client_hook_call(E_CLIENT_HOOK_RESIZE_UPDATE, ec);
   e_remember_update(ec);
   ec->pre_cb.w = w; ec->pre_cb.h = h;

   e_client_visibility_calculate();
}

static void
_e_client_cb_evas_show(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Client *ec = data;

   if (e_object_is_del(data)) return;

   _e_client_event_simple(data, E_EVENT_CLIENT_SHOW);
   EC_CHANGED(ec);
}

static void
_e_client_cb_evas_restack(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Client *ec = data;

   if (e_object_is_del(data)) return;
   if (ec->layer_block) return;
   if (e_config->transient.raise && ec->transients)
     {
        Eina_List *list = eina_list_clone(ec->transients);
        E_Client *child, *below = NULL, *above = NULL;

        E_LIST_REVERSE_FREE(list, child)
          {
             /* Don't stack iconic transients. If the user wants these shown,
              * that's another option.
              */
             if (child->iconic) continue;
             if (child->transient_policy == E_TRANSIENT_ABOVE)
               {
                  if (below)
                    evas_object_stack_below(child->frame, below->frame);
                  else
                    evas_object_stack_above(child->frame, ec->frame);
                  below = child;
               }
             else if (child->transient_policy == E_TRANSIENT_BELOW)
               {
                  if (above)
                    evas_object_stack_above(child->frame, above->frame);
                  else
                    evas_object_stack_below(child->frame, ec->frame);
                  above = child;
               }

          }
     }
   if (ec->unredirected_single) return;
   e_remember_update(ec);
   _e_client_event_simple(ec, E_EVENT_CLIENT_STACK);

   e_client_visibility_calculate();
}

////////////////////////////////////////////////

static void
_e_client_maximize(E_Client *ec, E_Maximize max)
{
   int x1, yy1, x2, y2;
   int w, h, pw, ph;
   int zx, zy, zw, zh;
   int ecx, ecy, ecw, ech;
   Eina_Bool override = ec->maximize_override;

   if (!ec->zone) return;
   zx = zy = zw = zh = 0;
   ec->maximize_override = 1;

   switch (max & E_MAXIMIZE_TYPE)
     {
      case E_MAXIMIZE_NONE:
        /* Ignore */
        break;

      case E_MAXIMIZE_FULLSCREEN:
        w = ec->zone->w;
        h = ec->zone->h;

        evas_object_smart_callback_call(ec->frame, "fullscreen", NULL);
        e_client_resize_limit(ec, &w, &h);
        /* center x-direction */
        x1 = ec->zone->x + (ec->zone->w - w) / 2;
        /* center y-direction */
        yy1 = ec->zone->y + (ec->zone->h - h) / 2;

        switch (max & E_MAXIMIZE_DIRECTION)
          {
           case E_MAXIMIZE_BOTH:
             evas_object_geometry_set(ec->frame, x1, yy1, w, h);
             break;

           case E_MAXIMIZE_VERTICAL:
             evas_object_geometry_set(ec->frame, ec->x, yy1, ec->w, h);
             break;

           case E_MAXIMIZE_HORIZONTAL:
             evas_object_geometry_set(ec->frame, x1, ec->y, w, ec->h);
             break;

           case E_MAXIMIZE_LEFT:
             evas_object_geometry_set(ec->frame, ec->zone->x, ec->zone->y, w / 2, h);
             break;

           case E_MAXIMIZE_RIGHT:
             evas_object_geometry_set(ec->frame, x1, ec->zone->y, w / 2, h);
             break;
          }
        break;

      case E_MAXIMIZE_SMART:
      case E_MAXIMIZE_EXPAND:
        if (ec->desk->visible)
          e_zone_useful_geometry_get(ec->zone, &zx, &zy, &zw, &zh);
        else
          {
             x1 = ec->zone->x;
             yy1 = ec->zone->y;
             x2 = ec->zone->x + ec->zone->w;
             y2 = ec->zone->y + ec->zone->h;
             e_maximize_client_shelf_fill(ec, &x1, &yy1, &x2, &y2, max);
             zx = x1, zy = yy1;
             zw = x2 - x1;
             zh = y2 - yy1;
          }
        w = zw, h = zh;

        evas_object_smart_callback_call(ec->frame, "maximize", NULL);
        e_comp_object_frame_xy_unadjust(ec->frame, ec->x, ec->y, &ecx, &ecy);
        e_comp_object_frame_wh_unadjust(ec->frame, ec->w, ec->h, &ecw, &ech);

        if (ecw < zw)
          w = ecw;

        if (ech < zh)
          h = ech;

        if (ecx < zx) // window left not useful coordinates
          x1 = zx;
        else if (ecx + ecw > zx + zw) // window right not useful coordinates
          x1 = zx + zw - ecw;
        else // window normal position
          x1 = ecx;

        if (ecy < zy) // window top not useful coordinates
          yy1 = zy;
        else if (ecy + ech > zy + zh) // window bottom not useful coordinates
          yy1 = zy + zh - ech;
        else // window normal position
          yy1 = ecy;

        switch (max & E_MAXIMIZE_DIRECTION)
          {
           case E_MAXIMIZE_BOTH:
             evas_object_geometry_set(ec->frame, zx, zy, zw, zh);
             break;

           case E_MAXIMIZE_VERTICAL:
             evas_object_geometry_set(ec->frame, ec->x, zy, ec->w, zh);
             break;

           case E_MAXIMIZE_HORIZONTAL:
             evas_object_geometry_set(ec->frame, zx, ec->y, zw, ec->h);
             break;

           case E_MAXIMIZE_LEFT:
             evas_object_geometry_set(ec->frame, zx, zy, zw / 2, zh);
             break;

           case E_MAXIMIZE_RIGHT:
             evas_object_geometry_set(ec->frame, zx + zw / 2, zy, zw / 2, zh);
             break;
          }

        break;

      case E_MAXIMIZE_FILL:
        x1 = ec->zone->x;
        yy1 = ec->zone->y;
        x2 = ec->zone->x + ec->zone->w;
        y2 = ec->zone->y + ec->zone->h;

        /* walk through all shelves */
        e_maximize_client_shelf_fill(ec, &x1, &yy1, &x2, &y2, max);

        /* walk through all windows */
        e_maximize_client_client_fill(ec, &x1, &yy1, &x2, &y2, max);

        w = x2 - x1;
        h = y2 - yy1;
        pw = w;
        ph = h;
        e_client_resize_limit(ec, &w, &h);
        /* center x-direction */
        x1 = x1 + (pw - w) / 2;
        /* center y-direction */
        yy1 = yy1 + (ph - h) / 2;

        switch (max & E_MAXIMIZE_DIRECTION)
          {
           case E_MAXIMIZE_BOTH:
             evas_object_geometry_set(ec->frame, x1, yy1, w, h);
             break;

           case E_MAXIMIZE_VERTICAL:
             evas_object_geometry_set(ec->frame, ec->x, yy1, ec->w, h);
             break;

           case E_MAXIMIZE_HORIZONTAL:
             evas_object_geometry_set(ec->frame, x1, ec->y, w, ec->h);
             break;

           case E_MAXIMIZE_LEFT:
             evas_object_geometry_set(ec->frame, ec->zone->x, ec->zone->y, w / 2, h);
             break;

           case E_MAXIMIZE_RIGHT:
             evas_object_geometry_set(ec->frame, x1, ec->zone->y, w / 2, h);
             break;
          }
        break;
     }
   if (ec->maximize_override)
     ec->maximize_override = override;
}

////////////////////////////////////////////////

static void
_e_client_eval(E_Client *ec)
{
   int rem_change = 0;
   int send_event = 1;
   unsigned int prop = 0;

   if (e_object_is_del(E_OBJECT(ec)))
     {
        CRI("_e_client_eval(%p) with deleted border! - %d\n", ec, ec->new_client);
        ec->changed = 0;
        return;
     }

   TRACE_DS_BEGIN(CLIENT:EVAL);

   if (!_e_client_hook_call(E_CLIENT_HOOK_EVAL_PRE_NEW_CLIENT, ec))
     {
        TRACE_DS_END();
        return;
     }

   if ((ec->new_client) && (!e_client_util_ignored_get(ec)) && (ec->zone))
     {
        int zx = 0, zy = 0, zw = 0, zh = 0;

        e_zone_useful_geometry_get(ec->zone, &zx, &zy, &zw, &zh);
        /* enforce wm size hints for initial sizing */
        if (e_config->screen_limits == E_CLIENT_OFFSCREEN_LIMIT_ALLOW_NONE)
          {
             ec->w = MIN(ec->w, ec->zone->w);
             ec->h = MIN(ec->h, ec->zone->h);
          }
        e_client_resize_limit(ec, &ec->w, &ec->h);

        if (ec->re_manage)
          {
             int x = ec->x, y = ec->y;
             if (ec->x) e_comp_object_frame_xy_adjust(ec->frame, ec->x, 0, &ec->x, NULL);
             if (ec->y) e_comp_object_frame_xy_adjust(ec->frame, 0, ec->y, NULL, &ec->y);
             if ((x != ec->x) || (y != ec->y)) ec->changes.pos = 1;
             ec->placed = 1;
             ec->pre_cb.x = ec->x; ec->pre_cb.y = ec->y;
          }
        if (!ec->placed)
          {
             if (ec->parent)
               {
                  if (ec->parent->zone != e_zone_current_get())
                    {
                       e_client_zone_set(ec, ec->parent->zone);
                       e_zone_useful_geometry_get(ec->zone, &zx, &zy, &zw, &zh);
                    }

                  if (evas_object_visible_get(ec->parent->frame))
                    {
                       if ((!E_CONTAINS(ec->x, ec->y, ec->w, ec->h, zx, zy, zw, zh)) ||
                          (!E_CONTAINS(ec->x, ec->y, ec->w, ec->h, ec->parent->x, ec->parent->y, ec->parent->w, ec->parent->h)))
                         {
                            int x, y;

                            e_comp_object_util_center_pos_get(ec->parent->frame, &x, &y);
                            if (E_CONTAINS(x, y, ec->w, ec->h, zx, zy, zw, zh))
                              {
                                 ec->x = x, ec->y = y;
                              }
                            else
                              {
                                 x = ec->parent->x;
                                 y = ec->parent->y;
                                 if (!E_CONTAINS(x, y, ec->w, ec->h, zx, zy, zw, zh))
                                   {
                                      e_comp_object_util_center_on(ec->frame,
                                                                   ec->parent->frame);
                                   }
                              }
                            ec->changes.pos = 1;
                         }
                    }
                  else
                    {
                       e_comp_object_util_center_on(ec->frame,
                                                    ec->parent->frame);
                       ec->changes.pos = 1;
                    }
                  ec->placed = 1;
                  ec->pre_cb.x = ec->x; ec->pre_cb.y = ec->y;
               }
#if 0
             else if ((ec->leader) && (ec->dialog))
               {
                  /* TODO: Place in center of group */
               }
#endif
             else if (ec->dialog)
               {
                  E_Client *trans_ec = NULL;

                  if (ec->icccm.transient_for)
                    trans_ec = e_pixmap_find_client(E_PIXMAP_TYPE_X, ec->icccm.transient_for);
                  if (trans_ec)
                    {
                       // if transient for a window and not placed, center on
                       // transient parent if found
                       ec->x = trans_ec->x + ((trans_ec->w - ec->w) / 2);
                       ec->y = trans_ec->y + ((trans_ec->h - ec->h) / 2);
                    }
                  else
                    {
                       ec->x = zx + ((zw - ec->w) / 2);
                       ec->y = zy + ((zh - ec->h) / 2);
                    }
                  ec->changes.pos = 1;
                  ec->placed = 1;
                  ec->pre_cb.x = ec->x; ec->pre_cb.y = ec->y;
               }
          }

        if (!ec->placed)
          {
             Eina_List *skiplist = NULL;
             int new_x, new_y, t = 0;
             E_Client *trans_ec = NULL;

             if (zw > ec->w)
               new_x = zx + (rand() % (zw - ec->w));
             else
               new_x = zx;
             if (zh > ec->h)
               new_y = zy + (rand() % (zh - ec->h));
             else
               new_y = zy;

             e_comp_object_frame_geometry_get(ec->frame, NULL, NULL, &t, NULL);

             if (ec->icccm.transient_for)
               trans_ec = e_pixmap_find_client(E_PIXMAP_TYPE_X, ec->icccm.transient_for);
             if (trans_ec)
               {
                  // if transient for a window and not placed, center on
                  // transient parent if found
                  new_x = trans_ec->x + ((trans_ec->w - ec->w) / 2);
                  new_y = trans_ec->y + ((trans_ec->h - ec->h) / 2);
               }
             else if ((e_config->window_placement_policy == E_WINDOW_PLACEMENT_SMART) || (e_config->window_placement_policy == E_WINDOW_PLACEMENT_ANTIGADGET))
               {
                  skiplist = eina_list_append(skiplist, ec);
                  if (ec->desk)
                    e_place_desk_region_smart(ec->desk, skiplist,
                                              ec->x, ec->y, ec->w, ec->h,
                                              &new_x, &new_y);
                  else
                    e_place_zone_region_smart(ec->zone, skiplist,
                                              ec->x, ec->y, ec->w, ec->h,
                                              &new_x, &new_y);
                  eina_list_free(skiplist);
               }
             else if (e_config->window_placement_policy == E_WINDOW_PLACEMENT_MANUAL)
               {
                  e_place_zone_manual(ec->zone, ec->w, t, &new_x, &new_y);
               }
             else
               {
                  e_place_zone_cursor(ec->zone, ec->x, ec->y, ec->w, ec->h,
                                      t, &new_x, &new_y);
               }
             ec->x = new_x;
             ec->y = new_y;
             ec->changes.pos = 1;
             ec->placed = 1;
             ec->pre_cb.x = ec->x; ec->pre_cb.y = ec->y;
          }
        else if (!E_INSIDE(ec->x, ec->y, zx, zy, zw, zh))
          {
             /* If an ec is placed out of bound, fix it! */
             ec->x = zx + ((zw - ec->w) / 2);
             ec->y = zy + ((zh - ec->h) / 2);
             ec->changes.pos = 1;
          }

        /* Recreate state */
        if (!ec->override)
          e_hints_window_init(ec);
        if ((ec->e.state.centered) &&
            ((!ec->remember) ||
             ((ec->remember) && (!(ec->remember->apply & E_REMEMBER_APPLY_POS)))))
          {
             ec->x = zx + (zw - ec->w) / 2;
             ec->y = zy + (zh - ec->h) / 2;
             ec->changes.pos = 1;
          }

        /* if the explicit geometry request asks for the app to be
         * in another zone - well move it there */
        {
           E_Zone *zone = NULL;
           int x, y;

           x = MAX(ec->x, 0);
           y = MAX(ec->y, 0);
           if ((!ec->re_manage) && ((ec->x != x) || (ec->y != y)))
             zone = e_comp_zone_xy_get(x, y);

           if (!zone)
             {
                zone = e_comp_zone_xy_get(ec->x + (ec->w / 2), ec->y + (ec->h / 2));
                if (zone)
                  {
                     E_Zone *z2 = e_comp_zone_xy_get(ec->x, ec->y);

                     if (z2 && (z2 != zone))
                       {
                          size_t psz = 0;
                          E_Zone *zf = z2;
                          Eina_List *l;

                          EINA_LIST_FOREACH(e_comp->zones, l, z2)
                            {
                                int w, h;

                                x = ec->x, y = ec->y, w = ec->w, h = ec->h;
                                E_RECTS_CLIP_TO_RECT(x, y, w, h, z2->x, z2->y, z2->w, z2->h);
                                if (w * h == z2->w * z2->h)
                                  {
                                     /* client fully covering zone */
                                     zf = z2;
                                     break;
                                  }
                                if ((unsigned)(w * h) > psz)
                                  {
                                     psz = w * h;
                                     zf = z2;
                                  }
                            }
                          zone = zf;
                       }
                  }
             }
           if (!zone)
             zone = e_comp_zone_xy_get(ec->x, ec->y);
           if (!zone)
             zone = e_comp_zone_xy_get(ec->x + ec->w - 1, ec->y);
           if (!zone)
             zone = e_comp_zone_xy_get(ec->x + ec->w - 1, ec->y + ec->h - 1);
           if (!zone)
             zone = e_comp_zone_xy_get(ec->x, ec->y + ec->h - 1);
           if ((zone) && (zone != ec->zone))
             e_client_zone_set(ec, zone);
        }
     }

   if (!_e_client_hook_call(E_CLIENT_HOOK_EVAL_POST_NEW_CLIENT, ec))
     {
        TRACE_DS_END();
        return;
     }

   /* effect changes to the window border itself */
   if ((ec->changes.shading))
     {
        /*  show at start of unshade (but don't hide until end of shade) */
        //if (ec->shaded)
          //ecore_x_window_raise(ec->win);
        ec->changes.shading = 0;
        send_event = 0;
        rem_change = 1;
     }
   if (ec->changes.shaded) send_event = 0;
   if ((ec->changes.shaded) && (ec->changes.pos) && (ec->changes.size))
     {
        //if (ec->shaded)
          //ecore_x_window_lower(ec->win);
        //else
          //ecore_x_window_raise(ec->win);
        ec->changes.shaded = 0;
        rem_change = 1;
     }
   else if ((ec->changes.shaded) && (ec->changes.pos))
     {
        //if (ec->shaded)
          //ecore_x_window_lower(ec->win);
        //else
          //ecore_x_window_raise(ec->win);
        ec->changes.size = 1;
        ec->changes.shaded = 0;
        rem_change = 1;
     }
   else if ((ec->changes.shaded) && (ec->changes.size))
     {
        //if (ec->shaded)
          //ecore_x_window_lower(ec->win);
        //else
          //ecore_x_window_raise(ec->win);
        ec->changes.shaded = 0;
        rem_change = 1;
     }
   else if (ec->changes.shaded)
     {
        //if (ec->shaded)
          //ecore_x_window_lower(ec->win);
        //else
          //ecore_x_window_raise(ec->win);
        ec->changes.shaded = 0;
        rem_change = 1;
     }

   if (ec->changes.size)
     {
        ec->changes.size = 0;
        if ((!ec->shaded) && (!ec->shading))
          evas_object_resize(ec->frame, ec->w, ec->h);

        rem_change = 1;
        prop |= E_CLIENT_PROPERTY_SIZE;
     }
   if (ec->changes.pos)
     {
        ec->changes.pos = 0;
        evas_object_move(ec->frame, ec->x, ec->y);
        rem_change = 1;
        prop |= E_CLIENT_PROPERTY_POS;
     }

   if (ec->changes.reset_gravity)
     {
        ec->changes.reset_gravity = 0;
        rem_change = 1;
        prop |= E_CLIENT_PROPERTY_GRAVITY;
     }

   if ((ec->changes.visible) && (ec->visible) && (ec->new_client) && (!ec->iconic))
     {
        int x, y;

        ecore_evas_pointer_xy_get(e_comp->ee, &x, &y);
        if ((!ec->placed) && (!ec->re_manage) &&
            (e_config->window_placement_policy == E_WINDOW_PLACEMENT_MANUAL) &&
            (!((ec->icccm.transient_for != 0) ||
               (ec->dialog))) &&
            (!ecmove) && (!ecresize))
          {
             /* Set this window into moving state */

             ec->cur_mouse_action = e_action_find("window_move");
             if (ec->cur_mouse_action)
               {
                  if ((!ec->cur_mouse_action->func.end_mouse) &&
                      (!ec->cur_mouse_action->func.end))
                    ec->cur_mouse_action = NULL;
                  if (ec->cur_mouse_action)
                    {
                       int t;
                       ec->x = x - (ec->w >> 1);
                       e_comp_object_frame_geometry_get(ec->frame, NULL, NULL, &t, NULL);
                       ec->y = y - (t >> 1);
                       EC_CHANGED(ec);
                       ec->changes.pos = 1;
                    }
               }
          }

        evas_object_show(ec->frame);
        if (evas_object_visible_get(ec->frame))
          {
             if (ec->cur_mouse_action)
               {
                  ec->moveinfo.down.x = ec->x;
                  ec->moveinfo.down.y = ec->y;
                  ec->moveinfo.down.w = ec->w;
                  ec->moveinfo.down.h = ec->h;
                  ec->mouse.current.mx = x;
                  ec->mouse.current.my = y;
                  ec->moveinfo.down.button = 0;
                  ec->moveinfo.down.mx = x;
                  ec->moveinfo.down.my = y;

                  e_object_ref(E_OBJECT(ec->cur_mouse_action));
                  ec->cur_mouse_action->func.go(E_OBJECT(ec), NULL);
                  if (e_config->border_raise_on_mouse_action)
                    evas_object_raise(ec->frame);
                  evas_object_focus_set(ec->frame, 1);
               }
             ec->changes.visible = 0;
             rem_change = 1;
             _e_client_event_simple(ec, E_EVENT_CLIENT_SHOW);
          }
     }
   else if ((ec->changes.visible) && (ec->new_client))
     {
        ec->changes.visible = 0;
        if (!ec->iconic)
          _e_client_event_simple(ec, E_EVENT_CLIENT_HIDE);
     }

   if (ec->changes.icon)
     {
        ec->changes.icon = 0;
     }

   ec->new_client = 0;
   e_comp->new_clients--;
   ec->changed = ec->changes.pos || ec->changes.size ||
                 ec->changes.stack || ec->changes.prop || ec->changes.border ||
                 ec->changes.reset_gravity || ec->changes.shading || ec->changes.shaded ||
                 ec->changes.shape || ec->changes.shape_input || ec->changes.icon ||
                 ec->changes.internal_state ||
                 ec->changes.need_maximize || ec->changes.need_unmaximize;
   ec->changes.stack = 0;

   if ((!ec->input_only) && (!ec->iconic) &&
       ((!ec->zone) || e_client_util_desk_visible(ec, e_desk_current_get(ec->zone))) &&
       ((ec->take_focus) || (ec->want_focus)))
     {
        ec->take_focus = 0;
        if ((e_config->focus_setting == E_FOCUS_NEW_WINDOW) || (ec->want_focus))
          {
             ec->want_focus = 0;
             e_client_focus_set_with_pointer(ec);
          }
        else if (ec->dialog)
          {
             if ((e_config->focus_setting == E_FOCUS_NEW_DIALOG) ||
                 ((e_config->focus_setting == E_FOCUS_NEW_DIALOG_IF_OWNER_FOCUSED) &&
                  (ec->parent == e_client_focused_get())))
               {
                  e_client_focus_set_with_pointer(ec);
               }
          }
        else
          {
             /* focus window by default when it is the only one on desk */
             E_Client *ec2 = NULL;
             Eina_List *l;
             EINA_LIST_FOREACH(focus_stack, l, ec2)
               {
                  if (ec == ec2) continue;
                  if ((!ec2->iconic) && (ec2->visible) &&
                      ((ec->desk == ec2->desk) || ec2->sticky))
                    break;
               }

             if (!ec2)
               {
                  e_client_focus_set_with_pointer(ec);
               }
          }
     }
   else
     ec->take_focus = ec->want_focus = 0;

   if (ec->changes.need_maximize)
     {
        E_Maximize max = ec->maximized;
        ec->maximized = E_MAXIMIZE_NONE;
        e_client_maximize(ec, max);
        ec->changes.need_maximize = 0;
     }
   else if (ec->changes.need_unmaximize)
     {
        e_client_unmaximize(ec, ec->maximized);
        ec->changes.need_unmaximize = 0;
     }

   if (ec->need_fullscreen)
     {
        e_client_fullscreen(ec, e_config->fullscreen_policy);
        ec->need_fullscreen = 0;
     }

   if (rem_change)
     e_remember_update(ec);

   if (send_event && rem_change && prop)
     {
        _e_client_event_property(ec, prop);
     }

#ifdef HAVE_WAYLAND_ONLY
     {
        E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;

        if (cdata && cdata->aux_hint.changed)
          {
             _e_client_hook_call(E_CLIENT_HOOK_AUX_HINT_CHANGE, ec);
             cdata->aux_hint.changed = 0;
          }
     }
#endif

   e_client_transform_core_update(ec);
   _e_client_hook_call(E_CLIENT_HOOK_EVAL_END, ec);

   TRACE_DS_END();
}

static void
_e_client_frame_update(E_Client *ec)
{
   const char *bordername;

   ec->border.changed = 0;
   if (!e_comp_object_frame_allowed(ec->frame)) return;
   if (ec->fullscreen || ec->borderless)
     bordername = "borderless";
   else if (ec->bordername)
     bordername = ec->bordername;
   else if (ec->mwm.borderless)
     bordername = "borderless";
   else if (((ec->icccm.transient_for != 0) || (ec->dialog)) &&
            (ec->icccm.min_w == ec->icccm.max_w) &&
            (ec->icccm.min_h == ec->icccm.max_h))
     bordername = "noresize_dialog";
   else if ((ec->icccm.min_w == ec->icccm.max_w) &&
            (ec->icccm.min_h == ec->icccm.max_h))
     bordername = "noresize";
   else if (ec->shaped)
     bordername = "shaped";
   else if (e_pixmap_is_x(ec->pixmap) &&
            ((!ec->icccm.accepts_focus) &&
            (!ec->icccm.take_focus)))
     bordername = "nofocus";
   else if (ec->urgent)
     bordername = "urgent";
   else if (((ec->icccm.transient_for != 0) || (ec->dialog)) && 
            (e_pixmap_is_x(ec->pixmap)))
     bordername = "dialog";
   else if (ec->netwm.state.modal)
     bordername = "modal";
   else if ((ec->netwm.state.skip_taskbar) ||
            (ec->netwm.state.skip_pager))
     bordername = "skipped";
  /*
   else if ((ec->internal) && (ec->icccm.class) &&
            (!strncmp(ec->icccm.class, "e_fwin", 6)))
     bordername = "internal_fileman";
  */
   else
     bordername = e_config->theme_default_border_style;
   if (!bordername) bordername = "default";

   e_client_border_set(ec, bordername);
}

static Eina_Bool
_e_client_type_match(E_Client *ec, E_Config_Client_Type *m)
{
   if (!ec || !m) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   if ((int)ec->netwm.type != m->window_type)
     return EINA_FALSE;

   if (((m->clas) && (!ec->icccm.class)) ||
       ((ec->icccm.class) && (m->clas) && (!e_util_glob_match(ec->icccm.class, m->clas))))
     return EINA_FALSE;

   if (((m->name) && (!ec->icccm.name)) ||
       ((ec->icccm.name) && (m->name) && (!e_util_glob_match(ec->icccm.name, m->name))))
     return EINA_FALSE;

   return EINA_TRUE;
}

static int
_e_client_type_get(E_Client *ec)
{
   E_Config_Client_Type *m;
   Eina_List *l;
   int type = 0;

   if (!e_config->client_types) return 0;

   EINA_LIST_FOREACH(e_config->client_types, l, m)
     {
        if (!_e_client_type_match(ec, m)) continue;
        else
          {
             type = m->client_type;
             break;
          }
     }

   ec->client_type = type;

   return ec->client_type;
}

#ifdef HAVE_WAYLAND_ONLY
static void
_e_client_transform_sub_apply(E_Client *ec, E_Client *epc, double zoom)
{
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   E_Comp_Wl_Subsurf_Data *sdata = cdata->sub.data;
   E_Client *subc;
   Eina_List *l;
   int px, py;
   int ox, oy, ow, oh;
   int mx, my, mw, mh;
   Evas_Map *map;

   EINA_SAFETY_ON_NULL_RETURN(sdata);

   ox = sdata->position.x;
   oy = sdata->position.y;
   ow = cdata->width_from_viewport;
   oh = cdata->height_from_viewport;

   map = (Evas_Map*)evas_object_map_get(epc->frame);
   evas_map_point_coord_get(map, 0, &px, &py, 0);

   mx = ox * zoom + px;
   my = oy * zoom + py;
   mw = ow * zoom;
   mh = oh * zoom;

   map = evas_map_new(4);
   evas_map_util_points_populate_from_geometry(map, mx, my, mw, mh, 0);
   evas_map_util_object_move_sync_set(map, EINA_TRUE);
   evas_object_map_set(ec->frame, map);
   evas_object_map_enable_set(ec->frame, EINA_TRUE);

   EINA_LIST_FOREACH(cdata->sub.list, l, subc)
     _e_client_transform_sub_apply(subc, ec, zoom);
   EINA_LIST_REVERSE_FOREACH(cdata->sub.below_list, l, subc)
     _e_client_transform_sub_apply(subc, ec, zoom);

   evas_map_free(map);
}
#endif

static void
_e_client_visibility_zone_calculate(E_Zone *zone)
{
   E_Client *ec;
   E_Client *focus_ec = NULL;
   Evas_Object *o;
   Eina_Tiler *t;
   Eina_Rectangle r, *_r;
   Eina_Iterator *it;
   Eina_Bool canvas_vis = EINA_TRUE;
   Eina_Bool ec_vis, ec_opaque, changed, calc_region;
   int x = 0;
   int y = 0;
   int w = 0;
   int h = 0;
   const int edge = 1;
#ifdef HAVE_WAYLAND_ONLY
   E_Comp_Wl_Client_Data *cdata;
#endif
   Eina_List *changed_list = NULL;
   Eina_List *l = NULL;

   if (!zone) return;

   TRACE_DS_BEGIN(CLIENT:VISIBILITY CALCULATE);

   t = eina_tiler_new(zone->w + edge, zone->h + edge);
   eina_tiler_tile_size_set(t, 1, 1);

   if (zone->display_state != E_ZONE_DISPLAY_STATE_OFF)
     {
        EINA_RECTANGLE_SET(&r, zone->x, zone->y, zone->w, zone->h);
        eina_tiler_rect_add(t, &r);
     }

   o = evas_object_top_get(e_comp->evas);
   for (; o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");

        /* check e_client and skip e_clients not intersects with zone */
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (e_client_util_ignored_get(ec)) continue;
        if (ec->zone != zone) continue;
        if (!ec->frame) continue;
#ifdef HAVE_WAYLAND_ONLY
        /* if ec is subsurface, skip this */
        cdata = (E_Comp_Wl_Client_Data *)ec->comp_data;
        if (cdata && cdata->sub.data) continue;
#endif

        /* TODO: need to check whether window intersects with entire screen, not zone. */
        /* if (!E_INTERSECTS(ec->x, ec->y, ec->w, ec->h, zone->x, zone->y, zone->w, zone->h)) continue; */
#if 0 // TODO: How do we handle the window when it is running animation & effect
        /* check if internal animation is running */
        if (e_comp_object_is_animating(ec->frame)) continue;
        /* check if external animation is running */
        if (evas_object_data_get(ec->frame, "effect_running")) continue;
#endif
        e_client_geometry_get(ec, &x, &y, &w, &h);
        ec_vis = ec_opaque = changed = EINA_FALSE;
        calc_region = EINA_TRUE;

        if (!ec->visible)
          {

             if (ec->visibility.obscured == E_VISIBILITY_UNOBSCURED)
               calc_region = EINA_FALSE;
             else
               continue;
          }
        else
          {
             if (!evas_object_visible_get(ec->frame))
               {
                  if (cdata && !cdata->mapped)
                    {
                       calc_region = EINA_FALSE;
                       if (ec->visibility.obscured == E_VISIBILITY_UNOBSCURED)
                         calc_region = EINA_FALSE;
                       else
                         continue;
                    }

                  if (!ec->iconic)
                    {
                       calc_region = EINA_FALSE;
                       if (ec->visibility.obscured == E_VISIBILITY_UNOBSCURED)
                         calc_region = EINA_FALSE;
                       else
                         continue;
                    }
                  else
                    {
                       if (ec->exp_iconify.by_client)
                         {
                            if (ec->visibility.obscured == E_VISIBILITY_UNOBSCURED)
                              calc_region = EINA_FALSE;
                            else
                              continue;
                         }
                    }
               }
          }

        if (canvas_vis && calc_region)
          {
             it = eina_tiler_iterator_new(t);
             EINA_ITERATOR_FOREACH(it, _r)
               {
                  if (E_INTERSECTS(x, y, w, h,
                                   _r->x, _r->y, _r->w, _r->h))
                    {
                       ec_vis = EINA_TRUE;
                       break;
                    }
               }
             eina_iterator_free(it);
          }

        if (ec_vis)
          {
             /* unobscured case */
             if (ec->visibility.obscured != E_VISIBILITY_UNOBSCURED)
               {
                  /* previous state is obscured: -1 or 1 */
                  ec->visibility.obscured = E_VISIBILITY_UNOBSCURED;
                  ec->visibility.changed = 1;
                  changed = EINA_TRUE;
                  ELOG("CLIENT VIS ON", ec->pixmap, ec);
               }

             /* subtract window region from canvas region */
             if (canvas_vis)
               {
                  /* check alpha window is opaque or not. */
                  if ((ec->visibility.opaque > 0) && (ec->argb))
                    ec_opaque = EINA_TRUE;

                  /* if e_client is not alpha or opaque then delete intersect rect */
                  if ((!ec->argb) || (ec_opaque))
                    {
                       EINA_RECTANGLE_SET(&r,
                                          x,
                                          y,
                                          w + edge,
                                          h + edge);
                       eina_tiler_rect_del(t, &r);

                       if (eina_tiler_empty(t))
                         canvas_vis = EINA_FALSE;
                    }
               }
          }
        else
          {
             /* obscured case */
             if (ec->visibility.obscured != E_VISIBILITY_FULLY_OBSCURED)
               {
                  /* previous state is unobscured: -1 or 0 */
                  ec->visibility.obscured = E_VISIBILITY_FULLY_OBSCURED;
                  ec->visibility.changed = 1;
                  changed = EINA_TRUE;
                  ELOG("CLIENT VIS OFF", ec->pixmap, ec);
               }
          }

        if (e_config->focus_policy_ext == E_FOCUS_EXT_TOP_STACK)
          {
             if ((ec_vis) && (!focus_ec))
               {
                  if ((ec->icccm.accepts_focus) || (ec->icccm.take_focus))
                    {
                       focus_ec = ec;
                    }
               }
          }

        changed_list = eina_list_append(changed_list, ec);
     }

   if (changed_list)
     {
        EINA_LIST_FOREACH(changed_list, l, ec)
          {
             if (changed)
               _e_client_event_simple(ec, E_EVENT_CLIENT_VISIBILITY_CHANGE);

             _e_client_hook_call(E_CLIENT_HOOK_EVAL_VISIBILITY, ec);
             ec->visibility.changed = 0;
          }
        eina_list_free(changed_list);
        changed_list = NULL;
     }

   eina_tiler_free(t);

   if (focus_ec)
     {
        ec = e_client_focused_get();
        if (ec != focus_ec)
          {
             if (!focus_ec->floating)
               {
                  e_client_focused_set(focus_ec);
                  evas_object_focus_set(focus_ec->frame, 1);
               }
          }
     }

   TRACE_DS_END();
}


static Eina_Bool
_e_client_transform_core_check_change(E_Client *ec)
{
   Eina_Bool check = EINA_FALSE;

   if (!ec) return EINA_FALSE;

   // check client position or size change
   if (ec->x != ec->transform_core.backup.client_x ||
       ec->y != ec->transform_core.backup.client_y ||
       ec->w != ec->transform_core.backup.client_w ||
       ec->h != ec->transform_core.backup.client_h)
     {
        check = EINA_TRUE;
        ec->transform_core.backup.client_x = ec->x;
        ec->transform_core.backup.client_y = ec->y;
        ec->transform_core.backup.client_w = ec->w;
        ec->transform_core.backup.client_h = ec->h;
     }

   // check new transform or del transform
   if (ec->transform_core.changed)
     {
        check = EINA_TRUE;
        ec->transform_core.changed = EINA_FALSE;
     }

   // check each transform change
   if (ec->transform_core.transform_list)
     {
        Eina_List *l;
        Eina_List *l_next;
        E_Util_Transform *transform;

        EINA_LIST_FOREACH_SAFE(ec->transform_core.transform_list, l, l_next, transform)
          {
             // del transform check
             if (e_util_transform_ref_count_get(transform) <= 1)
               {
                  ec->transform_core.transform_list = eina_list_remove(ec->transform_core.transform_list, transform);
                  e_util_transform_unref(transform);
                  check = EINA_TRUE;
                  continue;
               }

             // transform change test
             if (e_util_transform_change_get(transform))
               {
                  check = EINA_TRUE;
                  e_util_transform_change_unset(transform);
               }
          }
     }

   // check parent matrix change
#ifdef HAVE_WAYLAND_ONLY
   if (ec->comp_data)
     {
        E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;

        if (cdata->sub.data)
          {
             E_Client *parent = cdata->sub.data->parent;

             if (parent && parent->transform_core.result.enable)
               {
                  ec->transform_core.parent.enable = EINA_TRUE;

                  if (!e_util_transform_matrix_equal_check(&ec->transform_core.parent.matrix,
                                                           &parent->transform_core.result.matrix))
                    {
                       check = EINA_TRUE;
                       ec->transform_core.parent.matrix = parent->transform_core.result.matrix;
                    }
               }
             else if (ec->transform_core.parent.enable)
               {
                  ec->transform_core.parent.enable = EINA_FALSE;
                  e_util_transform_matrix_load_identity(&ec->transform_core.parent.matrix);
                  check = EINA_TRUE;
               }
          }
     }
#endif

   return check;
}

static void
_e_client_transform_core_boundary_update(E_Client *ec,  E_Util_Transform_Rect_Vertex *vertices)
{
   double minx, miny;
   double maxx, maxy;
   double initValue = 99999;
   int i;

   if (!ec) return;
   if (!ec->frame) return;
   if (!ec->transform_core.result.enable) return;
   if (!vertices) return;

   minx = initValue;
   miny = initValue;
   maxx = -initValue;
   maxy = -initValue;

   for (i = 0 ; i < 4 ; ++i)
     {
        double x = 0.0;
        double y = 0.0;
        e_util_transform_vertices_pos_get(vertices, i, &x, &y, 0, 0);

        if (x < minx) minx = x;
        if (y < miny) miny = y;
        if (x > maxx) maxx = x;
        if (y > maxy) maxy = y;
     }

   ec->transform_core.result.boundary.x = (int)(minx + 0.5);
   ec->transform_core.result.boundary.y = (int)(miny + 0.5);
   ec->transform_core.result.boundary.w = (int)(maxx - minx + 0.5);
   ec->transform_core.result.boundary.h = (int)(maxy - miny + 0.5);

   ELOGF("COMP", "[Transform][boundary][%d %d %d %d]", ec->pixmap, ec,
         ec->transform_core.result.boundary.x, ec->transform_core.result.boundary.y,
         ec->transform_core.result.boundary.w, ec->transform_core.result.boundary.h);
}

static void
_e_client_transform_core_vertices_apply(E_Client *ec EINA_UNUSED, Evas_Object *obj, E_Util_Transform_Rect_Vertex *vertices)
{
   if (!obj) return;

   if (vertices)
     {
        Evas_Map *map = evas_map_new(4);

        if (map)
          {
             int i;
             evas_map_util_points_populate_from_object_full(map, obj, 0);
             evas_map_util_points_color_set(map, 255, 255, 255, 255);

             for (i = 0 ; i < 4 ; ++i)
               {
                  double dx = 0.0;
                  double dy = 0.0;
                  int x = 0;
                  int y = 0;

                  e_util_transform_vertices_pos_get(vertices, i, &dx, &dy, 0, 0);

                  x = (int)(dx + 0.5);
                  y = (int)(dy + 0.5);

                  evas_map_point_coord_set(map, i, x, y, 1.0);
               }

             evas_object_map_set(obj, map);
             evas_object_map_enable_set(obj, EINA_TRUE);

             evas_map_free(map);
          }
     }
   else
      evas_object_map_enable_set(obj, EINA_FALSE);
}

static void
_e_client_transform_core_sub_update(E_Client *ec, E_Util_Transform_Rect_Vertex *vertices)
{
#ifdef HAVE_WAYLAND_ONLY
   Eina_List *l;
   E_Client *subc;
   E_Comp_Wl_Client_Data *cdata;

   if (!ec) return;
   if (!ec->comp_data) return;

   cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;

   if (cdata->sub.below_obj)
      _e_client_transform_core_vertices_apply(ec, cdata->sub.below_obj, vertices);

   EINA_LIST_FOREACH(cdata->sub.list, l, subc)
      e_client_transform_core_update(subc);

   EINA_LIST_FOREACH(cdata->sub.below_list, l, subc)
      e_client_transform_core_update(subc);
#endif
}

E_API void
e_client_visibility_calculate(void)
{
   _e_calc_visibility = EINA_TRUE;
}

////////////////////////////////////////////////
EINTERN void
e_client_idler_before(void)
{
   const Eina_List *l;
   E_Client *ec;

   if ((!eina_hash_population(clients_hash[0])) && (!eina_hash_population(clients_hash[1]))) return;

   TRACE_DS_BEGIN(CLIENT:IDLE BEFORE);

   EINA_LIST_FOREACH(e_comp->clients, l, ec)
     {
        Eina_Stringshare *title;
        int client_type;

        // pass 1 - eval0. fetch properties on new or on change and
        // call hooks to decide what to do - maybe move/resize
        if (ec->ignored || (!ec->changed)) continue;

        if (!_e_client_hook_call(E_CLIENT_HOOK_EVAL_PRE_FETCH, ec)) continue;
        /* FETCH is hooked by the compositor to get client hints */
        title = e_client_util_name_get(ec);
        if (!_e_client_hook_call(E_CLIENT_HOOK_EVAL_FETCH, ec)) continue;
        if (title != e_client_util_name_get(ec))
          _e_client_event_property(ec, E_CLIENT_PROPERTY_TITLE);

        client_type = ec->client_type;
        if (client_type != _e_client_type_get(ec))
           _e_client_event_property(ec, E_CLIENT_PROPERTY_CLIENT_TYPE);

        /* PRE_POST_FETCH calls e_remember apply for new client */
        if (!_e_client_hook_call(E_CLIENT_HOOK_EVAL_PRE_POST_FETCH, ec)) continue;
        if (!_e_client_hook_call(E_CLIENT_HOOK_EVAL_POST_FETCH, ec)) continue;
        if (!_e_client_hook_call(E_CLIENT_HOOK_EVAL_PRE_FRAME_ASSIGN, ec)) continue;

        if ((ec->border.changed) && (!ec->shaded) && (!e_client_is_stacking(ec)) &&
            ((!ec->override) || ec->internal) &&
            (!(((ec->maximized & E_MAXIMIZE_TYPE) == E_MAXIMIZE_FULLSCREEN))))
          _e_client_frame_update(ec);
        ec->border.changed = 0;
        _e_client_hook_call(E_CLIENT_HOOK_EVAL_POST_FRAME_ASSIGN, ec);
     }

   E_CLIENT_FOREACH(ec)
     {
        if (ec->ignored) continue;
        // pass 2 - show windows needing show
        if ((ec->changes.visible) && (ec->visible) &&
            (!ec->new_client) && (!ec->changes.pos) &&
            (!ec->changes.size))
          {
             evas_object_show(ec->frame);
             ec->changes.visible = !evas_object_visible_get(ec->frame);
          }

        if ((!ec->new_client) && (!e_client_util_ignored_get(ec)) &&
            (!E_INSIDE(ec->x, ec->y, 0, 0, e_comp->w - 5, e_comp->h - 5)) &&
            (!E_INSIDE(ec->x, ec->y, 0 - ec->w + 5, 0 - ec->h + 5, e_comp->w - 5, e_comp->h - 5))
            )
          {
             if (e_config->screen_limits != E_CLIENT_OFFSCREEN_LIMIT_ALLOW_FULL)
               _e_client_move_lost_window_to_center(ec);
          }
     }

   if (_e_client_layout_cb)
     _e_client_layout_cb();

   // pass 3 - hide windows needing hide and eval (main eval)
   E_CLIENT_FOREACH(ec)
     {
        if (ec->ignored || e_object_is_del(E_OBJECT(ec))) continue;

        if ((ec->changes.visible) && (!ec->visible))
          {
             evas_object_hide(ec->frame);
             ec->changes.visible = 0;
          }

        if (ec->changed)
          {
             _e_client_eval(ec);
             e_client_visibility_calculate();
          }

        if ((ec->changes.visible) && (ec->visible) && (!ec->changed))
          {
             evas_object_show(ec->frame);
             ec->changes.visible = !evas_object_visible_get(ec->frame);
             ec->changed = ec->changes.visible;
             e_client_visibility_calculate();
          }
     }

   if (_e_calc_visibility)
     {
        E_Zone *zone;
        Eina_List *zl;
        EINA_LIST_FOREACH(e_comp->zones, zl, zone)
          {
             _e_client_visibility_zone_calculate(zone);
          }
        _e_calc_visibility = EINA_FALSE;
     }

   TRACE_DS_END();
}


EINTERN Eina_Bool
e_client_init(void)
{
   clients_hash[0] = eina_hash_pointer_new(NULL);
   clients_hash[1] = eina_hash_pointer_new(NULL);

   E_LIST_HANDLER_APPEND(handlers, E_EVENT_POINTER_WARP, _e_client_cb_pointer_warp, NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CONFIG_MODE_CHANGED, _e_client_cb_config_mode, NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_DESK_WINDOW_PROFILE_CHANGE, _e_client_cb_desk_window_profile_change, NULL);

   E_EVENT_CLIENT_ADD = ecore_event_type_new();
   E_EVENT_CLIENT_REMOVE = ecore_event_type_new();
   E_EVENT_CLIENT_DESK_SET = ecore_event_type_new();
   E_EVENT_CLIENT_ZONE_SET = ecore_event_type_new();
   E_EVENT_CLIENT_RESIZE = ecore_event_type_new();
   E_EVENT_CLIENT_MOVE = ecore_event_type_new();
   E_EVENT_CLIENT_SHOW = ecore_event_type_new();
   E_EVENT_CLIENT_HIDE = ecore_event_type_new();
   E_EVENT_CLIENT_ICONIFY = ecore_event_type_new();
   E_EVENT_CLIENT_UNICONIFY = ecore_event_type_new();
   E_EVENT_CLIENT_STACK = ecore_event_type_new();
   E_EVENT_CLIENT_FOCUS_IN = ecore_event_type_new();
   E_EVENT_CLIENT_FOCUS_OUT = ecore_event_type_new();
   E_EVENT_CLIENT_PROPERTY = ecore_event_type_new();
   E_EVENT_CLIENT_FULLSCREEN = ecore_event_type_new();
   E_EVENT_CLIENT_UNFULLSCREEN = ecore_event_type_new();
#ifdef _F_ZONE_WINDOW_ROTATION_
   E_EVENT_CLIENT_ROTATION_CHANGE_BEGIN = ecore_event_type_new();
   E_EVENT_CLIENT_ROTATION_CHANGE_CANCEL = ecore_event_type_new();
   E_EVENT_CLIENT_ROTATION_CHANGE_END = ecore_event_type_new();
#endif
   E_EVENT_CLIENT_VISIBILITY_CHANGE = ecore_event_type_new();
#ifdef HAVE_WAYLAND_ONLY
   E_EVENT_CLIENT_BUFFER_CHANGE = ecore_event_type_new();
#endif

   return (!!clients_hash[1]);
}

EINTERN void
e_client_shutdown(void)
{
   E_FREE_FUNC(clients_hash[0], eina_hash_free);
   E_FREE_FUNC(clients_hash[1], eina_hash_free);

   E_FREE_LIST(handlers, ecore_event_handler_del);

   E_FREE_FUNC(warp_timer, ecore_timer_del);
   warp_client = NULL;
}

E_API void
e_client_unignore(E_Client *ec)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->ignored) return;

   ec->ignored = 0;
   if (!e_client_util_ignored_get(ec))
     {
        if (starting)
          focus_stack = eina_list_prepend(focus_stack, ec);
        else
          focus_stack = eina_list_append(focus_stack, ec);
     }
   _e_client_event_simple(ec, E_EVENT_CLIENT_ADD);
}

E_API E_Client *
e_client_new(E_Pixmap *cp, int first_map, int internal)
{
   E_Client *ec;

   if (eina_hash_find(clients_hash[e_pixmap_type_get(cp)], &cp)) return NULL;

   ec = E_OBJECT_ALLOC(E_Client, E_CLIENT_TYPE, _e_client_free);
   if (!ec) return NULL;
   e_object_del_func_set(E_OBJECT(ec), E_OBJECT_CLEANUP_FUNC(_e_client_del));

#ifdef HAVE_WAYLAND
   uuid_generate(ec->uuid);
#endif

   ec->focus_policy_override = E_FOCUS_LAST;
   ec->w = 1;
   ec->h = 1;
   ec->internal = internal;

   ec->pixmap = cp;
   e_pixmap_client_set(cp, ec);
   ec->resize_mode = E_POINTER_RESIZE_NONE;
   ec->layer = E_LAYER_CLIENT_NORMAL;

   /* FIXME: if first_map is 1 then we should ignore the first hide event
    * or ensure the window is already hidden and events flushed before we
    * create a border for it */
   if (first_map)
     {
        ec->changes.pos = 1;
        ec->re_manage = 1;
        // needed to be 1 for internal windw and on restart.
        // ec->ignore_first_unmap = 2;
     }
   ec->offer_resistance = 1;
   ec->new_client = 1;
   e_comp->new_clients++;

   ec->exp_iconify.by_client = 0;
   ec->exp_iconify.not_raise = 0;
   ec->exp_iconify.skip_iconify = 0;

   if (!_e_client_hook_call(E_CLIENT_HOOK_NEW_CLIENT, ec)) 
     {
        /* delete the above allocated object */
        //e_object_del(E_OBJECT(ec));
        return NULL;
     }

#ifdef HAVE_WAYLAND_ONLY
     {
        E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;

        if (cdata && cdata->aux_hint.changed)
          {
             _e_client_hook_call(E_CLIENT_HOOK_AUX_HINT_CHANGE, ec);
             cdata->aux_hint.changed = 0;
          }
     }
#endif

   if (ec->override)
     _e_client_zone_update(ec);
   else
     e_client_desk_set(ec, e_desk_current_get(e_zone_current_get()));

   ec->icccm.title = NULL;
   ec->icccm.name = NULL;
   ec->icccm.class = NULL;
   ec->icccm.icon_name = NULL;
   ec->icccm.machine = NULL;
   ec->icccm.min_w = 1;
   ec->icccm.min_h = 1;
   ec->icccm.max_w = 32767;
   ec->icccm.max_h = 32767;
   ec->icccm.base_w = 0;
   ec->icccm.base_h = 0;
   ec->icccm.step_w = -1;
   ec->icccm.step_h = -1;
   ec->icccm.min_aspect = 0.0;
   ec->icccm.max_aspect = 0.0;

   ec->netwm.pid = 0;
   ec->netwm.name = NULL;
   ec->netwm.icon_name = NULL;
   ec->netwm.desktop = 0;
   ec->netwm.state.modal = 0;
   ec->netwm.state.sticky = 0;
   ec->netwm.state.shaded = 0;
   ec->netwm.state.hidden = 0;
   ec->netwm.state.maximized_v = 0;
   ec->netwm.state.maximized_h = 0;
   ec->netwm.state.skip_taskbar = 0;
   ec->netwm.state.skip_pager = 0;
   ec->netwm.state.fullscreen = 0;
   ec->netwm.state.stacking = E_STACKING_NONE;
   ec->netwm.action.move = 0;
   ec->netwm.action.resize = 0;
   ec->netwm.action.minimize = 0;
   ec->netwm.action.shade = 0;
   ec->netwm.action.stick = 0;
   ec->netwm.action.maximized_h = 0;
   ec->netwm.action.maximized_v = 0;
   ec->netwm.action.fullscreen = 0;
   ec->netwm.action.change_desktop = 0;
   ec->netwm.action.close = 0;
   ec->netwm.opacity = 255;

   ec->visibility.obscured = E_VISIBILITY_UNKNOWN;
   ec->visibility.opaque = -1;
   ec->visibility.changed = 0;

   ec->transform.zoom = 1.0;
   ec->transform.angle = 0.0;

   EC_CHANGED(ec);

   e_comp->clients = eina_list_append(e_comp->clients, ec);
   eina_hash_add(clients_hash[e_pixmap_type_get(cp)], &ec->pixmap, ec);

   ELOG("CLIENT ADD", ec->pixmap, ec);
   if (!ec->ignored)
     _e_client_event_simple(ec, E_EVENT_CLIENT_ADD);
   e_comp_object_client_add(ec);
   if (ec->frame)
     {
        evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW, _e_client_cb_evas_show, ec);
        evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE, _e_client_cb_evas_hide, ec);
        evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOVE, _e_client_cb_evas_move, ec);
        evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESIZE, _e_client_cb_evas_resize, ec);
        evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESTACK, _e_client_cb_evas_restack, ec);
        evas_object_smart_callback_add(ec->frame, "shade_done", _e_client_cb_evas_shade_done, ec);
        if (ec->override)
          evas_object_layer_set(ec->frame, E_LAYER_CLIENT_ABOVE);
        else
          evas_object_layer_set(ec->frame, E_LAYER_CLIENT_NORMAL);
     }
   if (!e_client_util_ignored_get(ec))
     {
        if (starting)
          focus_stack = eina_list_prepend(focus_stack, ec);
        else
          focus_stack = eina_list_append(focus_stack, ec);
     }

#ifdef _F_E_CLIENT_NEW_CLIENT_POST_HOOK_
   _e_client_hook_call(E_CLIENT_HOOK_NEW_CLIENT_POST, ec);
#endif

   return ec;
}

E_API Eina_Bool
e_client_desk_window_profile_available_check(E_Client *ec, const char *profile)
{
   int i;

   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(profile, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ec->e.state.profile.use, EINA_FALSE);
   if (ec->e.state.profile.num == 0) return EINA_TRUE;

   for (i = 0; i < ec->e.state.profile.num; i++)
     {
        if (!e_util_strcmp(ec->e.state.profile.available_list[i],
                           profile))
          return EINA_TRUE;
     }

   return EINA_FALSE;
}

E_API void
e_client_desk_window_profile_wait_desk_set(E_Client *ec, E_Desk *desk)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);

   if (ec->e.state.profile.wait_desk == desk) return;

   if (ec->e.state.profile.wait_desk_delfn)
     {
        if (ec->e.state.profile.wait_desk)
          e_object_delfn_del(E_OBJECT(ec->e.state.profile.wait_desk),
                             ec->e.state.profile.wait_desk_delfn);
        ec->e.state.profile.wait_desk_delfn = NULL;
     }

   if (ec->e.state.profile.wait_desk)
     e_object_unref(E_OBJECT(ec->e.state.profile.wait_desk));
   ec->e.state.profile.wait_desk = NULL;

   if (desk)
     {
        ec->e.state.profile.wait_desk_delfn =
           e_object_delfn_add(E_OBJECT(desk),
                              _e_client_desk_window_profile_wait_desk_delfn,
                              ec);
     }
   ec->e.state.profile.wait_desk = desk;
   if (ec->e.state.profile.wait_desk)
     e_object_ref(E_OBJECT(ec->e.state.profile.wait_desk));
}

E_API void
e_client_desk_set(E_Client *ec, E_Desk *desk)
{
   E_Event_Client_Desk_Set *ev;
   E_Desk *old_desk;

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);
   if (ec->desk == desk) return;
   if ((e_config->use_desktop_window_profile) &&
       (ec->e.state.profile.use))
     {
        if (e_util_strcmp(ec->e.state.profile.name, desk->window_profile))
          {
             if (e_client_desk_window_profile_available_check(ec, desk->window_profile))
               {
                  eina_stringshare_replace(&ec->e.state.profile.set, desk->window_profile);
                  eina_stringshare_replace(&ec->e.state.profile.wait, NULL);
                  ec->e.state.profile.wait_for_done = 0;
                  e_client_desk_window_profile_wait_desk_set(ec, desk);
                  EC_CHANGED(ec);
               }
          }
     }

   if (ec->fullscreen)
     {
        ec->desk->fullscreen_clients = eina_list_remove(ec->desk->fullscreen_clients, ec);
        desk->fullscreen_clients = eina_list_append(desk->fullscreen_clients, ec);
     }
   old_desk = ec->desk;
   ec->desk = desk;
   e_comp_object_effect_unclip(ec->frame);
   e_comp_object_effect_set(ec->frame, NULL);
   if (desk->visible || ec->sticky)
     {
        if ((!ec->hidden) && (!ec->iconic))
          evas_object_show(ec->frame);
     }
   else
     {
        ec->hidden = 1;
        evas_object_hide(ec->frame);
     }
   e_client_comp_hidden_set(ec, (!desk->visible) && (!ec->sticky));
   e_client_zone_set(ec, desk->zone);

   e_hints_window_desktop_set(ec);

   if (old_desk)
     {
        ev = E_NEW(E_Event_Client_Desk_Set, 1);
        if (ev)
          {
             ev->ec = ec;
             UNREFD(ec, 4);
             e_object_ref(E_OBJECT(ec));
             ev->desk = old_desk;
             e_object_ref(E_OBJECT(old_desk));
             ecore_event_add(E_EVENT_CLIENT_DESK_SET, ev, (Ecore_End_Cb)_e_client_event_desk_set_free, NULL);
          }

        if (old_desk->zone == ec->zone)
          {
             e_client_res_change_geometry_save(ec);
             e_client_res_change_geometry_restore(ec);
             ec->pre_res_change.valid = 0;
          }
     }

   if (e_config->transient.desktop)
     {
        E_Client *child;
        const Eina_List *l;

        EINA_LIST_FOREACH(ec->transients, l, child)
          e_client_desk_set(child, ec->desk);
     }

   e_remember_update(ec);
   _e_client_hook_call(E_CLIENT_HOOK_DESK_SET, ec);
   evas_object_smart_callback_call(ec->frame, "desk_change", ec);
}

E_API Eina_Bool
e_client_comp_grabbed_get(void)
{
   return comp_grabbed;
}

E_API E_Client *
e_client_action_get(void)
{
   return action_client;
}

E_API E_Client *
e_client_warping_get(void)
{
   return warp_client;
}


E_API Eina_List *
e_clients_immortal_list(void)
{
   const Eina_List *l;
   Eina_List *list = NULL;
   E_Client *ec;

   EINA_LIST_FOREACH(e_comp->clients, l, ec)
     {
        if (ec->lock_life)
          list = eina_list_append(list, ec);
     }
   return list;
}

//////////////////////////////////////////////////////////

E_API void
e_client_mouse_in(E_Client *ec, int x, int y)
{
   if (comp_grabbed) return;
   if (warp_client && (ec != warp_client)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (ec->desk && ec->desk->animate_count) return;
   ec->mouse.current.mx = x;
   ec->mouse.current.my = y;
   ec->mouse.in = 1;
   if ((!ec->iconic) && (!e_client_util_ignored_get(ec)))
     e_focus_event_mouse_in(ec);
}

E_API void
e_client_mouse_out(E_Client *ec, int x, int y)
{
   if (comp_grabbed) return;
   if (ec->fullscreen) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (ec->desk && ec->desk->animate_count) return;
   if (e_pixmap_is_x(ec->pixmap) && E_INSIDE(x, y, ec->x, ec->y, ec->w, ec->h)) return;

   ec->mouse.current.mx = x;
   ec->mouse.current.my = y;
   ec->mouse.in = 0;
   if ((!ec->iconic) && (!e_client_util_ignored_get(ec)))
     e_focus_event_mouse_out(ec);
}

E_API void
e_client_mouse_wheel(E_Client *ec, Evas_Point *output, E_Binding_Event_Wheel *ev)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (action_client) return;
   ec->mouse.current.mx = output->x;
   ec->mouse.current.my = output->y;
}

E_API void
e_client_mouse_down(E_Client *ec, int button, Evas_Point *output, E_Binding_Event_Mouse_Button *ev)
{
   Eina_Bool did_act = EINA_FALSE;
   E_Client *pfocus;
   int player;

   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (action_client || ec->iconic || e_client_util_ignored_get(ec)) return;
   if ((button >= 1) && (button <= 3))
     {
        ec->mouse.last_down[button - 1].mx = output->x;
        ec->mouse.last_down[button - 1].my = output->y;
        ec->mouse.last_down[button - 1].x = ec->x;
        ec->mouse.last_down[button - 1].y = ec->y;
        ec->mouse.last_down[button - 1].w = ec->w;
        ec->mouse.last_down[button - 1].h = ec->h;
     }
   else
     {
        ec->moveinfo.down.x = ec->x;
        ec->moveinfo.down.y = ec->y;
        ec->moveinfo.down.w = ec->w;
        ec->moveinfo.down.h = ec->h;
     }
   ec->mouse.current.mx = output->x;
   ec->mouse.current.my = output->y;
   pfocus = e_client_focused_get();
   player = ec->layer;
   if ((!did_act) || (((pfocus == e_client_focused_get()) || (ec == e_client_focused_get())) && (ec->layer >= player)))
     e_focus_event_mouse_down(ec);
   if ((button >= 1) && (button <= 3))
     {
        ec->mouse.last_down[button - 1].mx = output->x;
        ec->mouse.last_down[button - 1].my = output->y;
        ec->mouse.last_down[button - 1].x = ec->x;
        ec->mouse.last_down[button - 1].y = ec->y;
        ec->mouse.last_down[button - 1].w = ec->w;
        ec->mouse.last_down[button - 1].h = ec->h;
     }
   else
     {
        ec->moveinfo.down.x = ec->x;
        ec->moveinfo.down.y = ec->y;
        ec->moveinfo.down.w = ec->w;
        ec->moveinfo.down.h = ec->h;
     }
   ec->mouse.current.mx = output->x;
   ec->mouse.current.my = output->y;
}

E_API void
e_client_mouse_up(E_Client *ec, int button, Evas_Point *output, E_Binding_Event_Mouse_Button* ev)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (ec->iconic || e_client_util_ignored_get(ec)) return;
   if ((button >= 1) && (button <= 3))
     {
        ec->mouse.last_up[button - 1].mx = output->x;
        ec->mouse.last_up[button - 1].my = output->y;
        ec->mouse.last_up[button - 1].x = ec->x;
        ec->mouse.last_up[button - 1].y = ec->y;
     }
   ec->mouse.current.mx = output->x;
   ec->mouse.current.my = output->y;
   /* also we don't pass the same params that went in - then again that */
   /* should be ok as we are just ending the action if it has an end */
   if (ec->cur_mouse_action)
     {
        if (ec->cur_mouse_action->func.end_mouse)
          ec->cur_mouse_action->func.end_mouse(E_OBJECT(ec), "", ev);
        else if (ec->cur_mouse_action->func.end)
          ec->cur_mouse_action->func.end(E_OBJECT(ec), "");
        e_object_unref(E_OBJECT(ec->cur_mouse_action));
        ec->cur_mouse_action = NULL;
     }
   else
     {
        e_focus_event_mouse_up(ec);
     }
   if ((button >= 1) && (button <= 3))
     {
        ec->mouse.last_up[button - 1].mx = output->x;
        ec->mouse.last_up[button - 1].my = output->y;
        ec->mouse.last_up[button - 1].x = ec->x;
        ec->mouse.last_up[button - 1].y = ec->y;
     }

   ec->drag.start = 0;
}

E_API void
e_client_mouse_move(E_Client *ec, Evas_Point *output)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (ec->iconic || e_client_util_ignored_get(ec)) return;
   ec->mouse.current.mx = output->x;
   ec->mouse.current.my = output->y;
   if (ec->moving)
     {
        int x, y, new_x, new_y;
        int new_w, new_h;
        Eina_List *skiplist = NULL;

        if (action_handler_key) return;
        if ((ec->moveinfo.down.button >= 1) && (ec->moveinfo.down.button <= 3))
          {
             x = ec->mouse.last_down[ec->moveinfo.down.button - 1].x +
               (ec->mouse.current.mx - ec->moveinfo.down.mx);
             y = ec->mouse.last_down[ec->moveinfo.down.button - 1].y +
               (ec->mouse.current.my - ec->moveinfo.down.my);
          }
        else
          {
             x = ec->moveinfo.down.x +
               (ec->mouse.current.mx - ec->moveinfo.down.mx);
             y = ec->moveinfo.down.y +
               (ec->mouse.current.my - ec->moveinfo.down.my);
          }
        e_comp_object_frame_xy_adjust(ec->frame, x, y, &new_x, &new_y);

        skiplist = eina_list_append(skiplist, ec);
        e_resist_client_position(skiplist,
                                 ec->x, ec->y, ec->w, ec->h,
                                 x, y, ec->w, ec->h,
                                 &new_x, &new_y, &new_w, &new_h);
        eina_list_free(skiplist);

        if (e_config->screen_limits == E_CLIENT_OFFSCREEN_LIMIT_ALLOW_NONE)
          _e_client_stay_within_canvas(ec, x, y, &new_x, &new_y);

        ec->shelf_fix.x = 0;
        ec->shelf_fix.y = 0;
        ec->shelf_fix.modified = 0;
        evas_object_move(ec->frame, new_x, new_y);
        if (ec->zone) e_zone_flip_coords_handle(ec->zone, output->x, output->y);
     }
   else if (e_client_util_resizing_get(ec))
     {
        if (action_handler_key) return;
        _e_client_resize_handle(ec);
     }
   else if (ec->drag.start)
     {
        if ((ec->drag.x == -1) && (ec->drag.y == -1))
          {
             ec->drag.x = output->x;
             ec->drag.y = output->y;
          }
        else if (ec->zone)
          {
             int dx, dy;

             dx = ec->drag.x - output->x;
             dy = ec->drag.y - output->y;
             if (((dx * dx) + (dy * dy)) > (16 * 16))
               {
                  /* start drag! */
                  if (ec->netwm.icons || ec->internal_icon)
                    {
                       Evas_Object *o = NULL;
                       int x = 0, y = 0, w = 0, h = 0;
                       const char *drag_types[] = { "enlightenment/border" };

                       REFD(ec, 1);
                       e_object_ref(E_OBJECT(ec));

                       client_drag = e_drag_new(output->x, output->y,
                                                drag_types, 1, ec, -1,
                                                NULL,
                                                _e_client_cb_drag_finished);
                       client_drag->button_mask = evas_pointer_button_down_mask_get(e_comp->evas);
                       e_drag_resize(client_drag, w, h);

                       /* FIXME: fallback icon for drag */
                       o = evas_object_rectangle_add(client_drag->evas);
                       evas_object_color_set(o, 255, 255, 255, 255);

                       e_drag_object_set(client_drag, o);
                       e_drag_start(client_drag,
                                    output->x + (ec->drag.x - x),
                                    output->y + (ec->drag.y - y));
                    }
                  ec->drag.start = 0;
               }
          }
     }
}
///////////////////////////////////////////////////////

E_API void
e_client_res_change_geometry_save(E_Client *ec)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   if (ec->pre_res_change.valid) return;
   ec->pre_res_change.valid = 1;
   ec->pre_res_change.x = ec->x;
   ec->pre_res_change.y = ec->y;
   ec->pre_res_change.w = ec->w;
   ec->pre_res_change.h = ec->h;
   ec->pre_res_change.saved.x = ec->saved.x;
   ec->pre_res_change.saved.y = ec->saved.y;
   ec->pre_res_change.saved.w = ec->saved.w;
   ec->pre_res_change.saved.h = ec->saved.h;
}

E_API void
e_client_res_change_geometry_restore(E_Client *ec)
{
   struct
   {
      unsigned char valid : 1;
      int           x, y, w, h;
      struct
      {
         int x, y, w, h;
      } saved;
   } pre_res_change;

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->pre_res_change.valid) return;
   if (ec->new_client) return;
   if (!ec->zone) return;

   memcpy(&pre_res_change, &ec->pre_res_change, sizeof(pre_res_change));

   if (ec->fullscreen)
     {
        e_client_unfullscreen(ec);
        e_client_fullscreen(ec, e_config->fullscreen_policy);
     }
   else if (ec->maximized != E_MAXIMIZE_NONE)
     {
        E_Maximize max;

        max = ec->maximized;
        e_client_unmaximize(ec, E_MAXIMIZE_BOTH);
        e_client_maximize(ec, max);
     }
   else
     {
        int x, y, w, h, zx, zy, zw, zh;

        ec->saved.x = ec->pre_res_change.saved.x;
        ec->saved.y = ec->pre_res_change.saved.y;
        ec->saved.w = ec->pre_res_change.saved.w;
        ec->saved.h = ec->pre_res_change.saved.h;

        e_zone_useful_geometry_get(ec->zone, &zx, &zy, &zw, &zh);

        if (ec->saved.w > zw)
          ec->saved.w = zw;
        if ((ec->saved.x + ec->saved.w) > (zx + zw))
          ec->saved.x = zx + zw - ec->saved.w;

        if (ec->saved.h > zh)
          ec->saved.h = zh;
        if ((ec->saved.y + ec->saved.h) > (zy + zh))
          ec->saved.y = zy + zh - ec->saved.h;

        x = ec->pre_res_change.x;
        y = ec->pre_res_change.y;
        w = ec->pre_res_change.w;
        h = ec->pre_res_change.h;
        if (w > zw)
          w = zw;
        if (h > zh)
          h = zh;
        if ((x + w) > (zx + zw))
          x = zx + zw - w;
        if ((y + h) > (zy + zh))
          y = zy + zh - h;
        evas_object_geometry_set(ec->frame, x, y, w, h);
     }
   memcpy(&ec->pre_res_change, &pre_res_change, sizeof(pre_res_change));
}

E_API void
e_client_zone_set(E_Client *ec, E_Zone *zone)
{
   E_Event_Client_Zone_Set *ev;

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   E_OBJECT_CHECK(zone);
   E_OBJECT_TYPE_CHECK(zone, E_ZONE_TYPE);
   if (ec->zone == zone) return;

   ev = E_NEW(E_Event_Client_Zone_Set, 1);
   if (!ev) return;

   /* if the window does not lie in the new zone, move it so that it does */
   if (!E_INTERSECTS(ec->x, ec->y, ec->w, ec->h, zone->x, zone->y, zone->w, zone->h))
     {
        int x, y;

        if (ec->zone)
          {
             /* first guess -- get offset from old zone, and apply to new zone */
             x = zone->x + (ec->x - ec->zone->x);
             y = zone->y + (ec->y - ec->zone->y);
          }
        else
          x = ec->x, y = ec->y;

        /* keep window from hanging off bottom and left */
        if (x + ec->w > zone->x + zone->w) x += (zone->x + zone->w) - (x + ec->w);
        if (y + ec->h > zone->y + zone->h) y += (zone->y + zone->h) - (y + ec->h);

        /* make sure to and left are on screen (if the window is larger than the zone, it will hang off the bottom / right) */
        if (x < zone->x) x = zone->x;
        if (y < zone->y) y = zone->y;

        if (!E_INTERSECTS(x, y, ec->w, ec->h, zone->x, zone->y, zone->w, zone->h))
          {
             /* still not in zone at all, so just move it to closest edge */
             if (x < zone->x) x = zone->x;
             if (x >= zone->x + zone->w) x = zone->x + zone->w - ec->w;
             if (y < zone->y) y = zone->y;
             if (y >= zone->y + zone->h) y = zone->y + zone->h - ec->h;
          }
        evas_object_move(ec->frame, x, y);
     }

   ec->zone = zone;

   if ((!ec->desk) || (ec->desk->zone != ec->zone))
     e_client_desk_set(ec, e_desk_current_get(ec->zone));

   ev->ec = ec;
   REFD(ec, 5);
   e_object_ref(E_OBJECT(ec));
   ev->zone = zone;
   e_object_ref(E_OBJECT(zone));

   ecore_event_add(E_EVENT_CLIENT_ZONE_SET, ev, (Ecore_End_Cb)_e_client_event_zone_set_free, NULL);

   e_remember_update(ec);
   e_client_res_change_geometry_save(ec);
   e_client_res_change_geometry_restore(ec);
   ec->pre_res_change.valid = 0;
}

E_API void
e_client_geometry_get(E_Client *ec, int *x, int *y, int *w, int *h)
{
   int gx = 0;
   int gy = 0;
   int gw = 0;
   int gh = 0;

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   if (e_client_transform_core_enable_get(ec))
     {
        gx = ec->transform_core.result.boundary.x;
        gy = ec->transform_core.result.boundary.y;
        gw = ec->transform_core.result.boundary.w;
        gh = ec->transform_core.result.boundary.h;
     }
   else
     {
        if (ec->frame)
          {
             evas_object_geometry_get(ec->frame, &gx, &gy, &gw, &gh);
             if (gw == 0 && gh == 0)
               {
                  gw = ec->w;
                  gh = ec->h;
               }
          }
        else
          {
             gx = ec->x;
             gy = ec->y;
             gw = ec->w;
             gh = ec->h;
          }
     }

   if (x) *x = gx;
   if (y) *y = gy;
   if (w) *w = gw;
   if (h) *h = gh;
}

E_API E_Client *
e_client_above_get(const E_Client *ec)
{
   unsigned int x;
   E_Client *ec2;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);
   if (EINA_INLIST_GET(ec)->next) //check current layer
     {
        EINA_INLIST_FOREACH(EINA_INLIST_GET(ec)->next, ec2)
          if (!e_object_is_del(E_OBJECT(ec2)))
            return ec2;
     }
   if (ec->layer == E_LAYER_CLIENT_ALERT) return NULL;
   if (e_comp_canvas_client_layer_map(ec->layer) == 9999) return NULL;

   /* go up the layers until we find one */
   for (x = e_comp_canvas_layer_map(ec->layer) + 1; x <= e_comp_canvas_layer_map(E_LAYER_CLIENT_ALERT); x++)
     {
        if (!e_comp->layers[x].clients) continue;
        EINA_INLIST_FOREACH(e_comp->layers[x].clients, ec2)
          if (!e_object_is_del(E_OBJECT(ec2)))
            return ec2;
     }
   return NULL;
}

E_API E_Client *
e_client_below_get(const E_Client *ec)
{
   unsigned int x;
   E_Client *ec2;
   Eina_Inlist *l;

   E_OBJECT_CHECK_RETURN(ec, NULL);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, NULL);

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);
   if (EINA_INLIST_GET(ec)->prev) //check current layer
     {
        for (l = EINA_INLIST_GET(ec)->prev; l; l = l->prev)
          {
             ec2 = EINA_INLIST_CONTAINER_GET(l, E_Client);;
             if (!e_object_is_del(E_OBJECT(ec2)))
               return ec2;
          }
     }

   /* go down the layers until we find one */
   if (e_comp_canvas_layer_map(ec->layer) > e_comp_canvas_layer_map(E_LAYER_MAX)) return NULL;
   x = e_comp_canvas_layer_map(ec->layer);
   if (x > 0) x--;

   for (; x >= e_comp_canvas_layer_map(E_LAYER_CLIENT_DESKTOP); x--)
     {
        if (!e_comp->layers[x].clients) continue;
        EINA_INLIST_REVERSE_FOREACH(e_comp->layers[x].clients, ec2)
          if (!e_object_is_del(E_OBJECT(ec2)))
            return ec2;
     }
   return NULL;
}

E_API E_Client *
e_client_bottom_get(void)
{
   unsigned int x;

   for (x = e_comp_canvas_layer_map(E_LAYER_CLIENT_DESKTOP); x <= e_comp_canvas_layer_map(E_LAYER_CLIENT_ALERT); x++)
     {
        E_Client *ec2;

        if (!e_comp->layers[x].clients) continue;
        EINA_INLIST_FOREACH(e_comp->layers[x].clients, ec2)
          if (!e_object_is_del(E_OBJECT(ec2)))
            return ec2;
     }
   return NULL;
}

E_API E_Client *
e_client_top_get(void)
{
   unsigned int x;

   for (x = e_comp_canvas_layer_map(E_LAYER_CLIENT_ALERT); x >= e_comp_canvas_layer_map(E_LAYER_CLIENT_DESKTOP); x--)
     {
        E_Client *ec2;

        if (!e_comp->layers[x].clients) continue;
        EINA_INLIST_REVERSE_FOREACH(e_comp->layers[x].clients, ec2)
          if (!e_object_is_del(E_OBJECT(ec2)))
            return ec2;
     }
   return NULL;
}

E_API unsigned int
e_clients_count(void)
{
   return eina_list_count(e_comp->clients);
}


/**
 * Set a callback which will be called just prior to updating the
 * move coordinates for a border
 */
E_API void
e_client_move_intercept_cb_set(E_Client *ec, E_Client_Move_Intercept_Cb cb)
{
   ec->move_intercept_cb = cb;
}

///////////////////////////////////////

E_API E_Client_Hook *
e_client_hook_add(E_Client_Hook_Point hookpoint, E_Client_Hook_Cb func, const void *data)
{
   E_Client_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_CLIENT_HOOK_LAST, NULL);
   ch = E_NEW(E_Client_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_client_hooks[hookpoint] = eina_inlist_append(_e_client_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_client_hook_del(E_Client_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_client_hooks_walking == 0)
     {
        _e_client_hooks[ch->hookpoint] = eina_inlist_remove(_e_client_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_client_hooks_delete++;
}

///////////////////////////////////////

E_API void
e_client_focus_latest_set(E_Client *ec)
{
   if (!ec) CRI("ACK");
   if (focus_track_frozen > 0) return;
   focus_stack = eina_list_remove(focus_stack, ec);
   focus_stack = eina_list_prepend(focus_stack, ec);
}

E_API void
e_client_raise_latest_set(E_Client *ec)
{
   if (!ec) CRI("ACK");
   raise_stack = eina_list_remove(raise_stack, ec);
   raise_stack = eina_list_prepend(raise_stack, ec);
}

E_API Eina_Bool
e_client_focus_track_enabled(void)
{
   return !focus_track_frozen;
}

E_API void
e_client_focus_track_freeze(void)
{
   focus_track_frozen++;
}

E_API void
e_client_focus_track_thaw(void)
{
   if (focus_track_frozen)
     focus_track_frozen--;
}

E_API void
e_client_refocus(void)
{
   E_Client *ec;
   const Eina_List *l;

   EINA_LIST_FOREACH(e_client_focus_stack_get(), l, ec)
     if (ec->desk && ec->desk->visible && (!ec->iconic))
       {
          if (e_comp->input_key_grabs || e_comp->input_mouse_grabs) break;
          evas_object_focus_set(ec->frame, 1);
          break;
       }
}


/*
 * Sets the focus to the given client if necessary
 * There are 3 cases of different focus_policy-configurations:
 *
 * - E_FOCUS_CLICK: just set the focus, the most simple one
 *
 * - E_FOCUS_MOUSE: focus is where the mouse is, so try to
 *   warp the pointer to the window. If this fails (because
 *   the pointer is already in the window), just set the focus.
 *
 * - E_FOCUS_SLOPPY: focus is where the mouse is or on the
 *   last window which was focused, if the mouse is on the
 *   desktop. So, we need to look if there is another window
 *   under the pointer and warp to pointer to the right
 *   one if so (also, we set the focus afterwards). In case
 *   there is no window under pointer, the pointer is on the
 *   desktop and so we just set the focus.
 *
 *
 * This function is to be called when setting the focus was not
 * explicitly triggered by the user (by moving the mouse or
 * clicking for example), but implicitly (by closing a window,
 * the last focused window should get focus).
 *
 */
E_API void
e_client_focus_set_with_pointer(E_Client *ec)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   /* note: this is here as it seems there are enough apps that do not even
    * expect us to emulate a look of focus but not actually set x input
    * focus as we do - so simply abort any focuse set on such windows */
   /* be strict about accepting focus hint */
   if ((!ec->icccm.accepts_focus) &&
       (!ec->icccm.take_focus)) return;
   if (ec->lock_focus_out) return;
   if (ec == focused) return;

   TRACE_DS_BEGIN(CLIENT:FOCUS SET WITH POINTER);
   evas_object_focus_set(ec->frame, 1);

   if (e_config->focus_policy == E_FOCUS_CLICK)
     {
        TRACE_DS_END();
        return;
     }
   if (!ec->visible)
     {
        TRACE_DS_END();
        return;
     }

   TRACE_DS_END();
}

EINTERN void
e_client_focused_set(E_Client *ec)
{
   E_Client *ec2, *ec_unfocus = focused;
   Eina_List *l, *ll;

   if (ec == focused) return;

   TRACE_DS_BEGIN(CLIENT:FOCUSED SET);

   ELOG("CLIENT FOCUS_SET", NULL, ec);
   focused = ec;
   if ((ec) && (ec->zone))
     {
        ec->focused = 1;
        e_client_urgent_set(ec, 0);
        int x, total = ec->zone->desk_x_count * ec->zone->desk_y_count;

        for (x = 0; x < total; x++)
          {
             E_Desk *desk = ec->zone->desks[x];
             /* if there's any fullscreen non-parents on this desk, unfullscreen them */
             EINA_LIST_FOREACH_SAFE(desk->fullscreen_clients, l, ll, ec2)
               {
                  if (ec2 == ec) continue;
                  if (e_object_is_del(E_OBJECT(ec2))) continue;
                  /* but only if it's the same desk or one of the clients is sticky */
                  if ((desk == ec->desk) || (ec->sticky || ec2->sticky))
                    {
                       if (!eina_list_data_find(ec->transients, ec2))
                         e_client_unfullscreen(ec2);
                    }
               }
          }
     }

   while ((ec_unfocus) && (ec_unfocus->zone))
     {
        ec_unfocus->want_focus = ec_unfocus->focused = 0;
        if (!e_object_is_del(E_OBJECT(ec_unfocus)))
          e_focus_event_focus_out(ec_unfocus);
        if (ec_unfocus->mouse.in)
          e_client_mouse_out(ec_unfocus, ec_unfocus->x - 1, ec_unfocus->y - 1);

        /* if there unfocus client is fullscreen and visible */
        if ((ec_unfocus->fullscreen) && (!ec_unfocus->iconic) && (!ec_unfocus->hidden) &&
            (ec_unfocus->zone == e_zone_current_get()) &&
            ((ec_unfocus->desk == e_desk_current_get(ec_unfocus->zone)) || (ec_unfocus->sticky)))
          {
             Eina_Bool have_vis_child = EINA_FALSE;

             /* if any of its children are visible */
             EINA_LIST_FOREACH(ec_unfocus->transients, l, ec2)
               {
                  if ((ec2->zone == ec_unfocus->zone) &&
                      ((ec2->desk == ec_unfocus->desk) ||
                       (ec2->sticky) || (ec_unfocus->sticky)))
                    {
                       have_vis_child = EINA_TRUE;
                       break;
                    }
               }
             /* if no children are visible, unfullscreen */
             if ((!e_object_is_del(E_OBJECT(ec_unfocus))) && (!have_vis_child))
               e_client_unfullscreen(ec_unfocus);
          }

        _e_client_hook_call(E_CLIENT_HOOK_FOCUS_UNSET, ec_unfocus);
        /* only send event here if we're not being deleted */
        if ((!e_object_is_del(E_OBJECT(ec_unfocus))) && 
           (e_object_ref_get(E_OBJECT(ec_unfocus)) > 0))
          {
             _e_client_event_simple(ec_unfocus, E_EVENT_CLIENT_FOCUS_OUT);
             e_client_urgent_set(ec_unfocus, ec_unfocus->icccm.urgent);
          }
        break;
     }
   if (!ec)
     {
        TRACE_DS_END();
        return;
     }

   _e_client_hook_call(E_CLIENT_HOOK_FOCUS_SET, ec);
   e_focus_event_focus_in(ec);

   if (!focus_track_frozen)
     e_client_focus_latest_set(ec);

   e_hints_active_window_set(ec);
   _e_client_event_simple(ec, E_EVENT_CLIENT_FOCUS_IN);
   if (ec->sticky && ec->desk && (!ec->desk->visible))
     e_client_desk_set(ec, e_desk_current_get(ec->zone));

   TRACE_DS_END();
}

E_API void
e_client_activate(E_Client *ec, Eina_Bool just_do_it)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   TRACE_DS_BEGIN(CLIENT:ACTIVATE);

   if ((e_config->focus_setting == E_FOCUS_NEW_WINDOW) ||
       ((ec->parent) &&
        ((e_config->focus_setting == E_FOCUS_NEW_DIALOG) ||
         ((ec->parent->focused) &&
          (e_config->focus_setting == E_FOCUS_NEW_DIALOG_IF_OWNER_FOCUSED)))) ||
       (just_do_it))
     {
        ELOG("Un-Set ICONIFY BY CLIENT", ec->pixmap, ec);
        ec->exp_iconify.by_client = 0;
        ec->exp_iconify.not_raise = 0;

        if (ec->iconic)
          {
             if (!ec->lock_user_iconify)
               e_client_uniconify(ec);
          }
        if ((!ec->iconic) && (!ec->sticky))
          {
             e_desk_show(ec->desk);
          }
        if (!ec->lock_user_stacking)
          evas_object_raise(ec->frame);
        if (ec->shaded || ec->shading)
          e_client_unshade(ec, ec->shade_dir);
        if (!ec->lock_focus_out)
          evas_object_focus_set(ec->frame, 1);
     }

   TRACE_DS_END();
}

E_API E_Client *
e_client_focused_get(void)
{
   return focused;
}

E_API Eina_List *
e_client_focus_stack_get(void)
{
   return focus_stack;
}

YOLO E_API void
e_client_focus_stack_set(Eina_List *l)
{
   focus_stack = l;
}

E_API Eina_List *
e_client_raise_stack_get(void)
{
   return raise_stack;
}

E_API Eina_List *
e_client_lost_windows_get(E_Zone *zone)
{
   Eina_List *list = NULL;
   const Eina_List *l;
   E_Client *ec;
   int loss_overlap = 5;

   E_OBJECT_CHECK_RETURN(zone, NULL);
   E_OBJECT_TYPE_CHECK_RETURN(zone, E_ZONE_TYPE, NULL);
   EINA_LIST_FOREACH(e_comp->clients, l, ec)
     {
        if (ec->zone != zone) continue;
        if (e_client_util_ignored_get(ec)) continue;

        if (!E_INTERSECTS(ec->zone->x + loss_overlap,
                          ec->zone->y + loss_overlap,
                          ec->zone->w - (2 * loss_overlap),
                          ec->zone->h - (2 * loss_overlap),
                          ec->x, ec->y, ec->w, ec->h))
          {
             list = eina_list_append(list, ec);
          }
     }
   return list;
}

///////////////////////////////////////

E_API void
e_client_shade(E_Client *ec, E_Direction dir)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if ((ec->shaded) || (ec->shading) || (ec->fullscreen) ||
       ((ec->maximized) && (!e_config->allow_manip))) return;
   if (!e_util_strcmp("borderless", ec->bordername)) return;
   if (!e_comp_object_frame_allowed(ec->frame)) return;

   e_hints_window_shaded_set(ec, 1);
   e_hints_window_shade_direction_set(ec, dir);
   ec->take_focus = 0;
   ec->shading = 1;
   ec->shade_dir = dir;

   evas_object_smart_callback_call(ec->frame, "shaded", (uintptr_t*)dir);

   e_remember_update(ec);
}

E_API void
e_client_unshade(E_Client *ec, E_Direction dir)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if ((!ec->shaded) || (ec->shading))
     return;

   e_hints_window_shaded_set(ec, 0);
   e_hints_window_shade_direction_set(ec, dir);
   ec->shading = 1;
   ec->shade_dir = 0;

   evas_object_smart_callback_call(ec->frame, "unshaded", (uintptr_t*)dir);

   e_remember_update(ec);
}

///////////////////////////////////////

E_API void
e_client_maximize(E_Client *ec, E_Maximize max)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   if (!ec->zone) return;
   if (!(max & E_MAXIMIZE_DIRECTION)) max |= E_MAXIMIZE_BOTH;

   if ((ec->shaded) || (ec->shading)) return;
   evas_object_smart_callback_call(ec->frame, "maximize_pre", NULL);
   /* Only allow changes in vertical/ horizontal maximization */
   if (((ec->maximized & E_MAXIMIZE_DIRECTION) == (max & E_MAXIMIZE_DIRECTION)) ||
       ((ec->maximized & E_MAXIMIZE_DIRECTION) == E_MAXIMIZE_BOTH)) return;
   if (ec->new_client)
     {
        ec->changes.need_maximize = 1;
        ec->maximized &= ~E_MAXIMIZE_TYPE;
        ec->maximized |= max;
        EC_CHANGED(ec);
        return;
     }

   if (ec->fullscreen)
     e_client_unfullscreen(ec);
   ec->pre_res_change.valid = 0;
   if (!(ec->maximized & E_MAXIMIZE_HORIZONTAL))
     {
        /* Horizontal hasn't been set */
        ec->saved.x = ec->client.x - ec->zone->x;
        ec->saved.w = ec->client.w;
     }
   if (!(ec->maximized & E_MAXIMIZE_VERTICAL))
     {
        /* Vertical hasn't been set */
        ec->saved.y = ec->client.y - ec->zone->y;
        ec->saved.h = ec->client.h;
     }

   ec->saved.zone = ec->zone->num;
   e_hints_window_size_set(ec);

   _e_client_maximize(ec, max);

   /* Remove previous type */
   ec->maximized &= ~E_MAXIMIZE_TYPE;
   /* Add new maximization. It must be added, so that VERTICAL + HORIZONTAL == BOTH */
   ec->maximized |= max;
   ec->changes.need_unmaximize = 0;

   if ((ec->maximized & E_MAXIMIZE_DIRECTION) > E_MAXIMIZE_BOTH)
     /* left/right maximize */
     e_hints_window_maximized_set(ec, 0,
                                  ((ec->maximized & E_MAXIMIZE_DIRECTION) == E_MAXIMIZE_LEFT) ||
                                  ((ec->maximized & E_MAXIMIZE_DIRECTION) == E_MAXIMIZE_RIGHT));
   else
     e_hints_window_maximized_set(ec, ec->maximized & E_MAXIMIZE_HORIZONTAL,
                                  ec->maximized & E_MAXIMIZE_VERTICAL);
   e_remember_update(ec);
   evas_object_smart_callback_call(ec->frame, "maximize_done", NULL);
}

E_API void
e_client_unmaximize(E_Client *ec, E_Maximize max)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
   if (!(max & E_MAXIMIZE_DIRECTION))
     {
        CRI("BUG: Unmaximize call without direction!");
        return;
     }
   if (ec->new_client)
     {
        ec->changes.need_unmaximize = 1;
        EC_CHANGED(ec);
        return;
     }

   if ((ec->shaded) || (ec->shading)) return;
   evas_object_smart_callback_call(ec->frame, "unmaximize_pre", NULL);
   /* Remove directions not used */
   max &= (ec->maximized & E_MAXIMIZE_DIRECTION);
   /* Can only remove existing maximization directions */
   if (!max) return;
   if (ec->maximized & E_MAXIMIZE_TYPE)
     {
        ec->pre_res_change.valid = 0;
        ec->changes.need_maximize = 0;

        if ((ec->maximized & E_MAXIMIZE_TYPE) == E_MAXIMIZE_FULLSCREEN)
          {
             E_Maximize tmp_max = ec->maximized;

             //un-set maximized state for updating frame.
             ec->maximized = E_MAXIMIZE_NONE;
             _e_client_frame_update(ec);
             // re-set maximized state for unmaximize smart callback.
             ec->maximized = tmp_max;
             evas_object_smart_callback_call(ec->frame, "unfullscreen", NULL);
             // un-set maximized state.
             ec->maximized = E_MAXIMIZE_NONE;
             e_client_util_move_resize_without_frame(ec,
                                                     ec->saved.x + ec->zone->x,
                                                     ec->saved.y + ec->zone->y,
                                                     ec->saved.w, ec->saved.h);
             ec->saved.x = ec->saved.y = ec->saved.w = ec->saved.h = 0;
             e_hints_window_size_unset(ec);
          }
        else
          {
             int w, h, x, y;
             Eina_Bool horiz = EINA_FALSE, vert = EINA_FALSE;

             w = ec->client.w;
             h = ec->client.h;
             x = ec->client.x;
             y = ec->client.y;

             if (max & E_MAXIMIZE_VERTICAL)
               {
                  /* Remove vertical */
                  h = ec->saved.h;
                  vert = EINA_TRUE;
                  y = ec->saved.y + ec->zone->y;
                  if ((max & E_MAXIMIZE_VERTICAL) == E_MAXIMIZE_VERTICAL)
                    {
                       ec->maximized &= ~E_MAXIMIZE_VERTICAL;
                       ec->maximized &= ~E_MAXIMIZE_LEFT;
                       ec->maximized &= ~E_MAXIMIZE_RIGHT;
                    }
                  if ((max & E_MAXIMIZE_LEFT) == E_MAXIMIZE_LEFT)
                    ec->maximized &= ~E_MAXIMIZE_LEFT;
                  if ((max & E_MAXIMIZE_RIGHT) == E_MAXIMIZE_RIGHT)
                    ec->maximized &= ~E_MAXIMIZE_RIGHT;
               }
             if (max & E_MAXIMIZE_HORIZONTAL)
               {
                  /* Remove horizontal */
                  w = ec->saved.w;
                  x = ec->saved.x + ec->zone->x;
                  horiz = EINA_TRUE;
                  ec->maximized &= ~E_MAXIMIZE_HORIZONTAL;
               }

             if (!(ec->maximized & E_MAXIMIZE_DIRECTION))
               {
                  ec->maximized = E_MAXIMIZE_NONE;
                  _e_client_frame_update(ec);
                  evas_object_smart_callback_call(ec->frame, "unmaximize", NULL);
                  e_client_resize_limit(ec, &w, &h);
                  e_client_util_move_resize_without_frame(ec, x, y, w, h);
                  e_hints_window_size_unset(ec);
               }
             else
               {
                  evas_object_smart_callback_call(ec->frame, "unmaximize", NULL);
                  e_client_resize_limit(ec, &w, &h);
                  e_client_util_move_resize_without_frame(ec, x, y, w, h);
                  e_hints_window_size_set(ec);
               }
             if (vert)
               ec->saved.h = ec->saved.y = 0;
             if (horiz)
               ec->saved.w = ec->saved.x = 0;
          }
        e_hints_window_maximized_set(ec, ec->maximized & E_MAXIMIZE_HORIZONTAL,
                                     ec->maximized & E_MAXIMIZE_VERTICAL);
     }
   e_remember_update(ec);
   evas_object_smart_callback_call(ec->frame, "unmaximize_done", NULL);
   ec->changes.need_unmaximize = 0;
}

E_API void
e_client_fullscreen(E_Client *ec, E_Fullscreen policy)
{
   int x, y, w, h;

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;

   if ((ec->shaded) || (ec->shading) || (ec->fullscreen)) return;

   _e_client_hook_call(E_CLIENT_HOOK_FULLSCREEN_PRE, ec);

   if (ec->skip_fullscreen) return;
   if (!ec->desk->visible) return;
   if (ec->new_client)
     {
        ec->need_fullscreen = 1;
        return;
     }
   if (e_comp->nocomp_ec && (ec->desk == e_comp->nocomp_ec->desk))
     e_comp->nocomp_ec = ec;
   ec->desk->fullscreen_clients = eina_list_append(ec->desk->fullscreen_clients, ec);
   ec->pre_res_change.valid = 0;

   if (ec->maximized)
     {
        x = ec->saved.x;
        y = ec->saved.y;
        w = ec->saved.w;
        h = ec->saved.h;
     }
   else
     {
        ec->saved.x = ec->client.x - ec->zone->x;
        ec->saved.y = ec->client.y - ec->zone->y;
        ec->saved.w = ec->client.w;
        ec->saved.h = ec->client.h;
     }
   ec->saved.maximized = ec->maximized;
   ec->saved.zone = ec->zone->num;

   if (ec->maximized)
     {
        e_client_unmaximize(ec, E_MAXIMIZE_BOTH);
        ec->saved.x = x;
        ec->saved.y = y;
        ec->saved.w = w;
        ec->saved.h = h;
     }
   e_hints_window_size_set(ec);

   ec->saved.layer = ec->layer;
   evas_object_layer_set(ec->frame, E_LAYER_CLIENT_FULLSCREEN);

   ec->fullscreen = 1;
   if ((eina_list_count(e_comp->zones) > 1) || 
       (policy == E_FULLSCREEN_RESIZE))
     {
        evas_object_geometry_set(ec->frame, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h);
     }
   else if (policy == E_FULLSCREEN_ZOOM)
     {
        /* compositor backends! */
        evas_object_smart_callback_call(ec->frame, "fullscreen_zoom", NULL);
     }

   e_hints_window_fullscreen_set(ec, 1);
   e_hints_window_size_unset(ec);
   if (!e_client_util_ignored_get(ec))
     _e_client_frame_update(ec);
   ec->fullscreen_policy = policy;
   evas_object_smart_callback_call(ec->frame, "fullscreen", NULL);

   _e_client_event_simple(ec, E_EVENT_CLIENT_FULLSCREEN);

   e_remember_update(ec);
}

E_API void
e_client_unfullscreen(E_Client *ec)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
   if ((ec->shaded) || (ec->shading)) return;
   if (!ec->fullscreen) return;
   ec->pre_res_change.valid = 0;
   ec->fullscreen = 0;
   ec->need_fullscreen = 0;
   ec->desk->fullscreen_clients = eina_list_remove(ec->desk->fullscreen_clients, ec);

   if (ec->fullscreen_policy == E_FULLSCREEN_ZOOM)
     evas_object_smart_callback_call(ec->frame, "unfullscreen_zoom", NULL);

   if (!e_client_util_ignored_get(ec))
     _e_client_frame_update(ec);
   ec->fullscreen_policy = 0;
   evas_object_smart_callback_call(ec->frame, "unfullscreen", NULL);
   e_client_util_move_resize_without_frame(ec, ec->zone->x + ec->saved.x,
                                           ec->zone->y + ec->saved.y,
                                           ec->saved.w, ec->saved.h);

   if (ec->saved.maximized)
     e_client_maximize(ec, (e_config->maximize_policy & E_MAXIMIZE_TYPE) |
                       ec->saved.maximized);

   evas_object_layer_set(ec->frame, ec->saved.layer);

   e_hints_window_fullscreen_set(ec, 0);
   _e_client_event_simple(ec, E_EVENT_CLIENT_UNFULLSCREEN);

   e_remember_update(ec);
   if (!ec->desk->fullscreen_clients)
     e_comp_render_queue();
}

///////////////////////////////////////


E_API void
e_client_iconify(E_Client *ec)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   ELOGF("TZVIS", "ICONIFY  |not_raise:%d       |by_client:%d",
         ec->pixmap, ec, (unsigned int)ec->exp_iconify.not_raise,
         ec->exp_iconify.by_client);

   if (!ec->zone) return;
   if (ec->shading || ec->iconic) return;

   TRACE_DS_BEGIN(CLIENT:ICONIFY);

   ec->iconic = 1;
   ec->want_focus = ec->take_focus = 0;
   ec->changes.visible = 0;
   if (ec->fullscreen)
     ec->desk->fullscreen_clients = eina_list_remove(ec->desk->fullscreen_clients, ec);
   e_client_comp_hidden_set(ec, 1);
   if (!ec->new_client)
     {
        _e_client_revert_focus(ec);
     }
   evas_object_hide(ec->frame);
   e_client_urgent_set(ec, ec->icccm.urgent);

   _e_client_event_simple(ec, E_EVENT_CLIENT_ICONIFY);

   if (e_config->transient.iconify)
     {
        E_Client *child;
        Eina_List *list = eina_list_clone(ec->transients);

        EINA_LIST_FREE(list, child)
          e_client_iconify(child);
     }

   _e_client_hook_call(E_CLIENT_HOOK_ICONIFY, ec);

   e_remember_update(ec);

   TRACE_DS_END();
}

E_API void
e_client_uniconify(E_Client *ec)
{
   E_Desk *desk;
   Eina_Bool not_raise;

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   ELOGF("TZVIS", "UNICONIFY|not_raise:%d       |by_client:%d",
         ec->pixmap, ec, (unsigned int)ec->exp_iconify.not_raise,
         ec->exp_iconify.by_client);

   if (!ec->zone) return;
   if (ec->shading || (!ec->iconic)) return;

   TRACE_DS_BEGIN(CLIENT:UNICONIFY);

   desk = e_desk_current_get(ec->desk->zone);
   e_client_desk_set(ec, desk);
   not_raise = ec->exp_iconify.not_raise;
   if (!not_raise)
     evas_object_raise(ec->frame);
   evas_object_show(ec->frame);
   e_client_comp_hidden_set(ec, 0);
   ec->deskshow = ec->iconic = 0;
   evas_object_focus_set(ec->frame, 1);

   _e_client_event_simple(ec, E_EVENT_CLIENT_UNICONIFY);

   if (e_config->transient.iconify)
     {
        E_Client *child;
        Eina_List *list = eina_list_clone(ec->transients);

        EINA_LIST_FREE(list, child)
          {
             child->exp_iconify.not_raise = not_raise;
             e_client_uniconify(child);
          }
     }

   _e_client_hook_call(E_CLIENT_HOOK_UNICONIFY, ec);

   ec->exp_iconify.not_raise = 0;
   ec->exp_iconify.by_client = 0;
   e_remember_update(ec);

   TRACE_DS_END();
}

///////////////////////////////////////

E_API void
e_client_urgent_set(E_Client *ec, Eina_Bool urgent)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   if (!ec->zone) return;

   urgent = !!urgent;
   if (urgent == ec->urgent) return;
   _e_client_event_property(ec, E_CLIENT_PROPERTY_URGENCY);
   if (urgent && (!ec->focused) && (!ec->want_focus))
     {
        e_comp_object_signal_emit(ec->frame, "e,state,urgent", "e");
        ec->urgent = urgent;
     }
   else
     {
        e_comp_object_signal_emit(ec->frame, "e,state,not_urgent", "e");
        ec->urgent = 0;
     }
}

///////////////////////////////////////

E_API void
e_client_stick(E_Client *ec)
{
   E_Desk *desk;

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
   if (ec->sticky) return;
   desk = ec->desk;
   ec->desk = NULL;
   ec->sticky = 1;
   ec->hidden = 0;
   e_hints_window_sticky_set(ec, 1);
   e_client_desk_set(ec, desk);
   evas_object_smart_callback_call(ec->frame, "stick", NULL);

   if (e_config->transient.desktop)
     {
        E_Client *child;
        Eina_List *list = eina_list_clone(ec->transients);

        EINA_LIST_FREE(list, child)
          {
             child->sticky = 1;
             e_hints_window_sticky_set(child, 1);
             evas_object_show(ec->frame);
          }
     }

   _e_client_event_property(ec, E_CLIENT_PROPERTY_STICKY);
   e_remember_update(ec);
}

E_API void
e_client_unstick(E_Client *ec)
{
   E_Desk *desk;

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
   /* Set the desk before we unstick the client */
   if (!ec->sticky) return;
   desk = e_desk_current_get(ec->zone);
   ec->desk = NULL;
   ec->hidden = ec->sticky = 0;
   e_hints_window_sticky_set(ec, 0);
   e_client_desk_set(ec, desk);
   evas_object_smart_callback_call(ec->frame, "unstick", NULL);

   if (e_config->transient.desktop)
     {
        E_Client *child;
        Eina_List *list = eina_list_clone(ec->transients);

        EINA_LIST_FREE(list, child)
          {
             child->sticky = 0;
             e_hints_window_sticky_set(child, 0);
          }
     }

   _e_client_event_property(ec, E_CLIENT_PROPERTY_STICKY);

   e_client_desk_set(ec, e_desk_current_get(ec->zone));
   e_remember_update(ec);
}

E_API void
e_client_pinned_set(E_Client *ec, Eina_Bool set)
{
   E_Layer layer;

   EINA_SAFETY_ON_NULL_RETURN(ec);
   ec->borderless = !!set;
   ec->user_skip_winlist = !!set;
   if (set)
     layer = E_LAYER_CLIENT_BELOW;
   else
     layer = E_LAYER_CLIENT_NORMAL;

   evas_object_layer_set(ec->frame, layer);

   ec->border.changed = 1;
   EC_CHANGED(ec);
}

///////////////////////////////////////

E_API Eina_Bool
e_client_border_set(E_Client *ec, const char *name)
{
   Eina_Stringshare *pborder;

   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);
   if (!e_comp_object_frame_allowed(ec->frame)) return EINA_FALSE;
   if (ec->border.changed)
     CRI("CALLING WHEN border.changed SET!");

   if (!e_util_strcmp(ec->border.name, name)) return EINA_TRUE;
   if (ec->mwm.borderless && name && strcmp(name, "borderless"))
     {
        e_util_dialog_show(_("Client Error!"), _("Something has attempted to set a border when it shouldn't! Report this!"));
        CRI("border change attempted for MWM borderless client!");
     }
   pborder = ec->border.name;
   ec->border.name = eina_stringshare_add(name);
   if (e_comp_object_frame_theme_set(ec->frame, name))
     {
        eina_stringshare_del(pborder);
        return EINA_TRUE;
     }
   eina_stringshare_del(ec->border.name);
   ec->border.name = pborder;
   return EINA_FALSE;
}

///////////////////////////////////////

E_API void
e_client_comp_hidden_set(E_Client *ec, Eina_Bool hidden)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;

   hidden = !!hidden;
   if (ec->comp_hidden == hidden) return;
   ec->comp_hidden = hidden;
   evas_object_smart_callback_call(ec->frame, "comp_hidden", NULL);
}

///////////////////////////////////////

E_API void
e_client_act_move_keyboard(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (!ec->zone) return;

   if (!_e_client_move_begin(ec))
     return;

   _e_client_action_init(ec);
   _e_client_action_move_timeout_add();
   if (!_e_client_hook_call(E_CLIENT_HOOK_MOVE_UPDATE, ec)) return;
   evas_object_freeze_events_set(ec->frame, 1);

   if (!action_handler_key)
     action_handler_key = ecore_event_handler_add(ECORE_EVENT_KEY_DOWN, _e_client_move_key_down, NULL);

   if (!action_handler_mouse)
     action_handler_mouse = ecore_event_handler_add(ECORE_EVENT_MOUSE_BUTTON_DOWN, _e_client_move_mouse_down, NULL);
}

E_API void
e_client_act_resize_keyboard(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (!ec->zone) return;

   ec->resize_mode = E_POINTER_RESIZE_TL;
   ec->keyboard_resizing = 1;
   if (!e_client_resize_begin(ec))
     {
        ec->keyboard_resizing = 0;
        return;
     }

   _e_client_action_init(ec);
   _e_client_action_resize_timeout_add();
   evas_object_freeze_events_set(ec->frame, 1);

   if (!action_handler_key)
     action_handler_key = ecore_event_handler_add(ECORE_EVENT_KEY_DOWN, _e_client_resize_key_down, NULL);

   if (!action_handler_mouse)
     action_handler_mouse = ecore_event_handler_add(ECORE_EVENT_MOUSE_BUTTON_DOWN, _e_client_resize_mouse_down, NULL);
}

E_API void
e_client_act_move_begin(E_Client *ec, E_Binding_Event_Mouse_Button *ev)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
   if (e_client_util_resizing_get(ec) || (ec->moving)) return;
   if (ev)
     {
        char source[256];

        snprintf(source, sizeof(source) - 1, "mouse,down,%i", ev->button);
        _e_client_moveinfo_gather(ec, source);
     }
   if (!_e_client_move_begin(ec))
     return;

   _e_client_action_init(ec);
   e_zone_edge_disable();
   e_pointer_mode_push(ec, E_POINTER_MOVE);
}

E_API void
e_client_act_move_end(E_Client *ec, E_Binding_Event_Mouse_Button *ev EINA_UNUSED)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
   if (!ec->moving) return;
   e_zone_edge_enable();
   _e_client_move_end(ec);
   e_zone_flip_coords_handle(ec->zone, -1, -1);
   _e_client_action_finish();
}

E_API void
e_client_act_resize_begin(E_Client *ec, E_Binding_Event_Mouse_Button *ev)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
   if (ec->lock_user_size || ec->shaded || ec->shading) return;
   if (e_client_util_resizing_get(ec) || (ec->moving)) return;
   if (ev)
     {
        char source[256];

        snprintf(source, sizeof(source) - 1, "mouse,down,%i", ev->button);
        _e_client_moveinfo_gather(ec, source);

        /* Use canvas.x, canvas.y of event.
         * Transformed coordinates has to be considered for accurate resize_mode
         * rather than absolute coordinates. */
        if ((ev->canvas.x > (ec->x + ec->w / 5)) &&
            (ev->canvas.x < (ec->x + ec->w * 4 / 5)))
          {
             if (ev->canvas.y < (ec->y + ec->h / 2))
               {
                  ec->resize_mode = E_POINTER_RESIZE_T;
               }
             else
               {
                  ec->resize_mode = E_POINTER_RESIZE_B;
               }
          }
        else if (ev->canvas.x < (ec->x + ec->w / 2))
          {
             if ((ev->canvas.y > (ec->y + ec->h / 5)) &&
                 (ev->canvas.y < (ec->y + ec->h * 4 / 5)))
               {
                  ec->resize_mode = E_POINTER_RESIZE_L;
               }
             else if (ev->canvas.y < (ec->y + ec->h / 2))
               {
                  ec->resize_mode = E_POINTER_RESIZE_TL;
               }
             else
               {
                  ec->resize_mode = E_POINTER_RESIZE_BL;
               }
          }
        else
          {
             if ((ev->canvas.y > (ec->y + ec->h / 5)) &&
                 (ev->canvas.y < (ec->y + ec->h * 4 / 5)))
               {
                  ec->resize_mode = E_POINTER_RESIZE_R;
               }
             else if (ev->canvas.y < (ec->y + ec->h / 2))
               {
                  ec->resize_mode = E_POINTER_RESIZE_TR;
               }
             else
               {
                  ec->resize_mode = E_POINTER_RESIZE_BR;
               }
          }
     }
   if (!e_client_resize_begin(ec))
     return;
   _e_client_action_init(ec);
   e_pointer_mode_push(ec, ec->resize_mode);
}

E_API void
e_client_act_resize_end(E_Client *ec, E_Binding_Event_Mouse_Button *ev EINA_UNUSED)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
   if (e_client_util_resizing_get(ec))
     {
        _e_client_resize_end(ec);
        ec->changes.reset_gravity = 1;
        if (!e_object_is_del(E_OBJECT(ec)))
          EC_CHANGED(ec);
     }
   _e_client_action_finish();
}

E_API void
e_client_act_menu_begin(E_Client *ec, E_Binding_Event_Mouse_Button *ev, int key)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
}

E_API void
e_client_act_close_begin(E_Client *ec)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
   if (ec->lock_close) return;
   if (ec->icccm.delete_request)
     {
        ec->delete_requested = 1;
        evas_object_smart_callback_call(ec->frame, "delete_request", NULL);
     }
   else if (e_config->kill_if_close_not_possible)
     {
        e_client_act_kill_begin(ec);
     }
}

E_API void
e_client_act_kill_begin(E_Client *ec)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
   if (ec->internal) return;
   if (ec->lock_close) return;
   if ((ec->netwm.pid > 1) && (e_config->kill_process))
     {
        kill(ec->netwm.pid, SIGINT);
        ec->kill_timer = ecore_timer_add(e_config->kill_timer_wait,
                                         _e_client_cb_kill_timer, ec);
     }
   else
     evas_object_smart_callback_call(ec->frame, "kill_request", NULL);
}

////////////////////////////////////////////

E_API void
e_client_ping(E_Client *ec)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!e_config->ping_clients) return;
   ec->ping_ok = 0;
   evas_object_smart_callback_call(ec->frame, "ping", NULL);
   ec->ping = ecore_loop_time_get();
   if (ec->ping_poller) ecore_poller_del(ec->ping_poller);
   ec->ping_poller = ecore_poller_add(ECORE_POLLER_CORE,
                                      e_config->ping_clients_interval,
                                      _e_client_cb_ping_poller, ec);
}

static void
_e_client_map_transform(int width, int height, uint32_t transform,
                         int sx, int sy, int *dx, int *dy)
{
   switch (transform)
     {
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
        *dx = sx, *dy = sy;
        break;
      case WL_OUTPUT_TRANSFORM_90:
        *dx = height - sy, *dy = sx;
        break;
      case WL_OUTPUT_TRANSFORM_180:
        *dx = width - sx, *dy = height - sy;
        break;
      case WL_OUTPUT_TRANSFORM_270:
        *dx = sy, *dy = width - sx;
        break;
     }
}

////////////////////////////////////////////
E_API void
e_client_cursor_map_apply(E_Client *ec, int rotation, int x, int y)
{
   Evas_Map *map;
   int x1, y1, x2, y2, dx, dy;
   int32_t width, height;
   int cursor_w, cursor_h;
   uint32_t transform;
   int rot_x = x, rot_y = y;
   int zone_w = ec->zone->w;
   int zone_h = ec->zone->h;
   double awh = ((double)zone_w / (double)zone_h);
   double ahw = ((double)zone_h / (double)zone_w);

   evas_object_geometry_get(ec->frame, NULL, NULL, &cursor_w, &cursor_h);
   width = cursor_w;
   height = cursor_h;

   switch(rotation)
     {
      case 90:
         rot_x = y * awh;
         rot_y = ahw * (zone_w - x);
         transform = WL_OUTPUT_TRANSFORM_90;
         width = cursor_h;
         height = cursor_w;
         break;
      case 180:
         rot_x = zone_w - x;
         rot_y = zone_h - y;
         transform = WL_OUTPUT_TRANSFORM_180;
         break;
      case 270:
         rot_x = awh * (zone_h - y);
         rot_y = ahw * x;
         transform = WL_OUTPUT_TRANSFORM_270;
         width = cursor_h;
         height = cursor_w;
         break;
      default:
         transform = WL_OUTPUT_TRANSFORM_NORMAL;
         break;
     }
   ec->client.x = rot_x, ec->client.y = rot_y;
   ec->x = rot_x, ec->y = rot_y;

   map = evas_map_new(4);
   evas_map_util_points_populate_from_geometry(map,
                                               ec->x, ec->y,
                                               width, height, 0);

   x1 = 0.0;
   y1 = 0.0;
   x2 = width;
   y2 = height;

   _e_client_map_transform(width, height, transform,
                            x1, y1, &dx, &dy);
   evas_map_point_image_uv_set(map, 0, dx, dy);

   _e_client_map_transform(width, height, transform,
                            x2, y1, &dx, &dy);
   evas_map_point_image_uv_set(map, 1, dx, dy);

   _e_client_map_transform(width, height, transform,
                            x2, y2, &dx, &dy);
   evas_map_point_image_uv_set(map, 2, dx, dy);

   _e_client_map_transform(width, height, transform,
                            x1, y2, &dx, &dy);
   evas_map_point_image_uv_set(map, 3, dx, dy);

   evas_object_map_set(ec->frame, map);
   evas_object_map_enable_set(ec->frame, map ? EINA_TRUE : EINA_FALSE);

   evas_map_free(map);
}

E_API void
e_client_move_cancel(void)
{
   if (!ecmove) return;
   if (ecmove->cur_mouse_action)
     {
        E_Client *ec;

        ec = ecmove;
        e_object_ref(E_OBJECT(ec));
        if (ec->cur_mouse_action->func.end_mouse)
          ec->cur_mouse_action->func.end_mouse(E_OBJECT(ec), "", NULL);
        else if (ec->cur_mouse_action->func.end)
          ec->cur_mouse_action->func.end(E_OBJECT(ec), "");
        e_object_unref(E_OBJECT(ec->cur_mouse_action));
        ec->cur_mouse_action = NULL;
        e_object_unref(E_OBJECT(ec));
     }
   else
     _e_client_move_end(ecmove);
}

E_API void
e_client_resize_cancel(void)
{
   if (!ecresize) return;
   if (ecresize->cur_mouse_action)
     {
        E_Client *ec;

        ec = ecresize;
        e_object_ref(E_OBJECT(ec));
        if (ec->cur_mouse_action->func.end_mouse)
          ec->cur_mouse_action->func.end_mouse(E_OBJECT(ec), "", NULL);
        else if (ec->cur_mouse_action->func.end)
          ec->cur_mouse_action->func.end(E_OBJECT(ec), "");
        e_object_unref(E_OBJECT(ec->cur_mouse_action));
        ec->cur_mouse_action = NULL;
        e_object_unref(E_OBJECT(ec));
     }
   else
     _e_client_resize_end(ecresize);
}

E_API Eina_Bool
e_client_resize_begin(E_Client *ec)
{
   if ((ec->shaded) || (ec->shading) ||
       (ec->fullscreen) || (ec->lock_user_size))
     goto error;
   if (!_e_client_action_input_win_new()) goto error;
   ecresize = ec;
   _e_client_hook_call(E_CLIENT_HOOK_RESIZE_BEGIN, ec);
   if (ec->transformed)
     _e_client_transform_resize_begin(ec);
   if (!e_client_util_resizing_get(ec))
     {
        if (ecresize == ec) ecresize = NULL;
        _e_client_action_input_win_del();
        return EINA_FALSE;
     }
   if (!ec->lock_user_stacking)
     {
        if (e_config->border_raise_on_mouse_action)
          evas_object_raise(ec->frame);
     }
   return EINA_TRUE;
error:
   ec->resize_mode = E_POINTER_RESIZE_NONE;
   return EINA_FALSE;
}


////////////////////////////////////////////

E_API void
e_client_frame_recalc(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (!ec->frame) return;
   evas_object_smart_callback_call(ec->frame, "frame_recalc", NULL);
}

////////////////////////////////////////////

E_API void
e_client_signal_move_begin(E_Client *ec, const char *sig, const char *src EINA_UNUSED)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;

   if (e_client_util_resizing_get(ec) || (ec->moving)) return;
   _e_client_moveinfo_gather(ec, sig);
   if (!_e_client_move_begin(ec)) return;
   e_pointer_mode_push(ec, E_POINTER_MOVE);
   e_zone_edge_disable();
}

E_API void
e_client_signal_move_end(E_Client *ec, const char *sig EINA_UNUSED, const char *src EINA_UNUSED)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!ec->zone) return;
   if (!ec->moving) return;
   _e_client_move_end(ec);
   e_zone_edge_enable();
   e_zone_flip_coords_handle(ec->zone, -1, -1);
}

E_API void
e_client_signal_resize_begin(E_Client *ec, const char *dir, const char *sig, const char *src EINA_UNUSED)
{
   int resize_mode = E_POINTER_RESIZE_BR;

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   if (e_client_util_resizing_get(ec) || (ec->moving)) return;
   if (!strcmp(dir, "tl"))
     {
        resize_mode = E_POINTER_RESIZE_TL;
     }
   else if (!strcmp(dir, "t"))
     {
        resize_mode = E_POINTER_RESIZE_T;
     }
   else if (!strcmp(dir, "tr"))
     {
        resize_mode = E_POINTER_RESIZE_TR;
     }
   else if (!strcmp(dir, "r"))
     {
        resize_mode = E_POINTER_RESIZE_R;
     }
   else if (!strcmp(dir, "br"))
     {
        resize_mode = E_POINTER_RESIZE_BR;
     }
   else if (!strcmp(dir, "b"))
     {
        resize_mode = E_POINTER_RESIZE_B;
     }
   else if (!strcmp(dir, "bl"))
     {
        resize_mode = E_POINTER_RESIZE_BL;
     }
   else if (!strcmp(dir, "l"))
     {
        resize_mode = E_POINTER_RESIZE_L;
     }
   ec->resize_mode = resize_mode;
   _e_client_moveinfo_gather(ec, sig);
   if (!e_client_resize_begin(ec))
     return;
   e_pointer_mode_push(ec, ec->resize_mode);
}

E_API void
e_client_signal_resize_end(E_Client *ec, const char *dir EINA_UNUSED, const char *sig EINA_UNUSED, const char *src EINA_UNUSED)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);
   if (!e_client_util_resizing_get(ec)) return;
   _e_client_resize_handle(ec);
   _e_client_resize_end(ec);
   ec->changes.reset_gravity = 1;
   EC_CHANGED(ec);
}

////////////////////////////////////////////

E_API void
e_client_resize_limit(E_Client *ec, int *w, int *h)
{
   double a;
   Eina_Bool inc_h;

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   inc_h = (*h - ec->h > 0);
   if (ec->frame)
     e_comp_object_frame_wh_unadjust(ec->frame, *w, *h, w, h);
   if (*h < 1) *h = 1;
   if (*w < 1) *w = 1;
   if ((ec->icccm.base_w >= 0) &&
       (ec->icccm.base_h >= 0))
     {
        int tw, th;

        tw = *w - ec->icccm.base_w;
        th = *h - ec->icccm.base_h;
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
        a = (double)(tw) / (double)(th);
        if ((ec->icccm.min_aspect != 0.0) &&
            (a < ec->icccm.min_aspect))
          {
             if (inc_h)
               tw = th * ec->icccm.min_aspect;
             else
               th = tw / ec->icccm.max_aspect;
             *w = tw + ec->icccm.base_w;
             *h = th + ec->icccm.base_h;
          }
        else if ((ec->icccm.max_aspect != 0.0) &&
                 (a > ec->icccm.max_aspect))
          {
             tw = th * ec->icccm.max_aspect;
             *w = tw + ec->icccm.base_w;
          }
     }
   else
     {
        a = (double)*w / (double)*h;
        if ((ec->icccm.min_aspect != 0.0) &&
            (a < ec->icccm.min_aspect))
          {
             if (inc_h)
               *w = *h * ec->icccm.min_aspect;
             else
               *h = *w / ec->icccm.min_aspect;
          }
        else if ((ec->icccm.max_aspect != 0.0) &&
                 (a > ec->icccm.max_aspect))
          *w = *h * ec->icccm.max_aspect;
     }
   if (ec->icccm.step_w > 0)
     {
        if (ec->icccm.base_w >= 0)
          *w = ec->icccm.base_w +
            (((*w - ec->icccm.base_w) / ec->icccm.step_w) *
             ec->icccm.step_w);
        else
          *w = ec->icccm.min_w +
            (((*w - ec->icccm.min_w) / ec->icccm.step_w) *
             ec->icccm.step_w);
     }
   if (ec->icccm.step_h > 0)
     {
        if (ec->icccm.base_h >= 0)
          *h = ec->icccm.base_h +
            (((*h - ec->icccm.base_h) / ec->icccm.step_h) *
             ec->icccm.step_h);
        else
          *h = ec->icccm.min_h +
            (((*h - ec->icccm.min_h) / ec->icccm.step_h) *
             ec->icccm.step_h);
     }

   if (*h < 1) *h = 1;
   if (*w < 1) *w = 1;

   if ((ec->icccm.max_w > 0) && (*w > ec->icccm.max_w)) *w = ec->icccm.max_w;
   else if (*w < ec->icccm.min_w)
     *w = ec->icccm.min_w;
   if ((ec->icccm.max_h > 0) && (*h > ec->icccm.max_h)) *h = ec->icccm.max_h;
   else if (*h < ec->icccm.min_h)
     *h = ec->icccm.min_h;

   if (ec->frame)
     e_comp_object_frame_wh_adjust(ec->frame, *w, *h, w, h);
}

////////////////////////////////////////////



E_API E_Client *
e_client_under_pointer_get(E_Desk *desk, E_Client *exclude)
{
   int x, y;

   /* We need to ensure that we can get the comp window for the
    * zone of either the given desk or the desk of the excluded
    * window, so return if neither is given */
   if (desk)
     ecore_evas_pointer_xy_get(e_comp->ee, &x, &y);
   else if (exclude)
     ecore_evas_pointer_xy_get(e_comp->ee, &x, &y);
   else
     return NULL;

   if (!desk)
     {
        desk = exclude->desk;
        if (!desk)
          {
             if (exclude->zone)
               desk = e_desk_current_get(exclude->zone);
             else
               desk = e_desk_current_get(e_zone_current_get());
          }
     }

   return desk ? _e_client_under_pointer_helper(desk, exclude, x, y) : NULL;
}

////////////////////////////////////////////

E_API int
e_client_pointer_warp_to_center_now(E_Client *ec)
{
   if (warp_client == ec)
     {
        ecore_evas_pointer_warp(e_comp->ee, warp_to_x, warp_to_y);
        warp_to = 0;
        _e_client_pointer_warp_to_center_timer(NULL);
     }
   else
     {
        if (e_client_pointer_warp_to_center(ec))
          e_client_pointer_warp_to_center_now(ec);
     }
   return 1;
}

E_API int
e_client_pointer_warp_to_center(E_Client *ec)
{
   int x, y;
   E_Client *cec = NULL;

   if (!ec->zone) return 0;
   /* Only warp the pointer if it is not already in the area of
    * the given border */
   ecore_evas_pointer_xy_get(e_comp->ee, &x, &y);
   if ((x >= ec->x) && (x <= (ec->x + ec->w)) &&
       (y >= ec->y) && (y <= (ec->y + ec->h)))
     {
        cec = _e_client_under_pointer_helper(ec->desk, ec, x, y);
        if (cec == ec) return 0;
     }

   warp_to_x = ec->x + (ec->w / 2);
   if (warp_to_x < (ec->zone->x + 1))
     warp_to_x = ec->zone->x + ((ec->x + ec->w - ec->zone->x) / 2);
   else if (warp_to_x > (ec->zone->x + ec->zone->w))
     warp_to_x = (ec->zone->x + ec->zone->w + ec->x) / 2;

   warp_to_y = ec->y + (ec->h / 2);
   if (warp_to_y < (ec->zone->y + 1))
     warp_to_y = ec->zone->y + ((ec->y + ec->h - ec->zone->y) / 2);
   else if (warp_to_y > (ec->zone->y + ec->zone->h))
     warp_to_y = (ec->zone->y + ec->zone->h + ec->y) / 2;

   /* TODO: handle case where another border is over the exact center,
    * find a place where the requested border is not overlapped?
    *
   if (!cec) cec = _e_client_under_pointer_helper(ec->desk, ec, x, y);
   if (cec != ec)
     {
     }
   */

   warp_to = 1;
   warp_client = ec;
   ecore_evas_pointer_xy_get(e_comp->ee, &warp_x[0], &warp_y[0]);
   if (warp_timer) ecore_timer_del(warp_timer);
   warp_timer = ecore_timer_add(0.01, _e_client_pointer_warp_to_center_timer, ec);
   return 1;
}

////////////////////////////////////////////

E_API void
e_client_redirected_set(E_Client *ec, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (ec->input_only) return;
   set = !!set;
   if (ec->redirected == set) return;
   if (set)
     {
        e_client_frame_recalc(ec);
        if (!_e_client_hook_call(E_CLIENT_HOOK_REDIRECT, ec)) return;
     }
   else
     {
        if (!_e_client_hook_call(E_CLIENT_HOOK_UNREDIRECT, ec)) return;
     }
   e_comp_object_redirected_set(ec->frame, set);
   ec->redirected = !!set;
}

////////////////////////////////////////////

E_API Eina_Bool
e_client_is_stacking(const E_Client *ec)
{
   return e_comp->layers[e_comp_canvas_layer_map(ec->layer)].obj == ec->frame;
}

E_API Eina_Bool
e_client_has_xwindow(const E_Client *ec)
{
   (void)ec;
   return EINA_FALSE;
}

////////////////////////////////////////////

E_API void
e_client_layout_cb_set(E_Client_Layout_Cb cb)
{
   if (_e_client_layout_cb && cb)
     CRI("ATTEMPTING TO OVERWRITE EXISTING CLIENT LAYOUT HOOK!!!");
   _e_client_layout_cb = cb;
}

////////////////////////////////////////////

E_API void e_client_transform_update(E_Client *ec)
{
   if (e_client_util_resizing_get(ec))
     _e_client_transform_resize(ec);
}

////////////////////////////////////////////

E_API void e_client_transform_apply(E_Client *ec, double angle, double zoom, int cx, int cy)
{
   Evas_Map *map;
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   E_Client *subc;
   Eina_List *l;

   /* check if it's subsurface */
   if (cdata->sub.data) return;

   /* check if it's different with current state */
   if ((ec->transform.angle == angle) &&
       (ec->transform.zoom == zoom) &&
       (ec->transform.center.x == cx) &&
       (ec->transform.center.y == cy))
     return;

   /* use previous value if any required value is invalid */
   if (angle == -1.0)
     angle = ec->transform.angle;
   if (zoom == -1.0)
     zoom = ec->transform.zoom;
   if (!E_INSIDE(cx, cy,
                 ec->client.x, ec->client.y,
                 ec->client.w, ec->client.h))
     {
        cx = ec->transform.center.x;
        cy = ec->transform.center.y;
     }

   if ((angle == 0) && (zoom == 1.0))
     {
        e_client_transform_clear(ec);
        return;
     }

   map = evas_map_new(4);
   evas_map_util_points_populate_from_object_full(map, ec->frame, 0);

   evas_map_util_rotate(map, angle, cx, cy);
   _e_client_transform_geometry_save(ec, map);

   evas_map_util_zoom(map, zoom, zoom, cx, cy);

   evas_map_util_object_move_sync_set(map, EINA_TRUE);
   evas_object_map_set(ec->frame, map);
   evas_object_map_enable_set(ec->frame, EINA_TRUE);

   if (cdata->sub.below_obj)
     {
        evas_object_map_set(cdata->sub.below_obj, map);
        evas_object_map_enable_set(cdata->sub.below_obj, EINA_TRUE);
     }

   EINA_LIST_FOREACH(cdata->sub.list, l, subc)
     _e_client_transform_sub_apply(subc, ec, zoom);
   EINA_LIST_REVERSE_FOREACH(cdata->sub.below_list, l, subc)
     _e_client_transform_sub_apply(subc, ec, zoom);

   evas_map_free(map);

   ec->transform.zoom = zoom;
   ec->transform.angle = angle;
   ec->transform.center.x = cx;
   ec->transform.center.y = cy;
   ec->transformed = EINA_TRUE;
}

////////////////////////////////////////////

E_API void
e_client_transform_clear(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   E_Client *subc;
   Eina_List *l;

   evas_object_map_enable_set(ec->frame, EINA_FALSE);
   evas_object_map_set(ec->frame, NULL);

   if (cdata->sub.below_obj)
     {
        evas_object_map_enable_set(cdata->sub.below_obj, EINA_FALSE);
        evas_object_map_set(cdata->sub.below_obj, NULL);
     }

   EINA_LIST_FOREACH(cdata->sub.list, l, subc)
     _e_client_transform_sub_apply(subc, ec, 1.0);
   EINA_LIST_REVERSE_FOREACH(cdata->sub.below_list, l, subc)
     _e_client_transform_sub_apply(subc, ec, 1.0);

   ec->transform.zoom = 1.0;
   ec->transform.angle = 0.0;
   ec->transformed = EINA_FALSE;
}

E_API Eina_Bool e_client_transform_core_enable_get(E_Client *ec)
{
   if (!ec) return EINA_FALSE;
   return ec->transform_core.result.enable;
}

E_API void e_client_transform_core_add(E_Client *ec, E_Util_Transform *transform)
{
   if (!ec) return;
   if (!transform) return;

   // duplication check
   if (ec->transform_core.transform_list &&
       eina_list_data_find(ec->transform_core.transform_list, transform) == transform)
     {
        return;
     }

   ec->transform_core.transform_list = eina_list_append(ec->transform_core.transform_list, transform);
   ec->transform_core.changed = EINA_TRUE;
   e_util_transform_ref(transform);
   e_client_transform_core_update(ec);
}

E_API void e_client_transform_core_remove(E_Client *ec, E_Util_Transform *transform)
{
   if (!ec) return;
   if (!transform) return;

   if (ec->transform_core.transform_list &&
       eina_list_data_find(ec->transform_core.transform_list, transform) == transform)
     {
        ec->transform_core.transform_list = eina_list_remove(ec->transform_core.transform_list, transform);
        e_util_transform_unref(transform);
        ec->transform_core.changed = EINA_TRUE;
     }

   e_client_transform_core_update(ec);
}

E_API void e_client_transform_core_update(E_Client *ec)
{
   if (!ec) return;
   if (ec->new_client) return;
   if (!_e_client_transform_core_check_change(ec)) return;

   if (ec->transform_core.transform_list || ec->transform_core.parent.enable)
     {
        E_Util_Transform_Rect source_rect;
        E_Util_Transform_Matrix matrix, boundary_matrix;
        Eina_List *l;
        Eina_Bool keep_ratio = EINA_FALSE;
        E_Util_Transform *temp_trans;

        // 1. init state
        ec->transform_core.result.enable = EINA_TRUE;
        e_util_transform_rect_client_rect_get(&source_rect, ec);
        e_util_transform_init(&ec->transform_core.result.transform);

        // 2. merge transform
        EINA_LIST_FOREACH(ec->transform_core.transform_list, l, temp_trans)
          {
             ec->transform_core.result.transform = e_util_transform_merge(&ec->transform_core.result.transform, temp_trans);
          }

        // 3. covert to matrix and apply keep_ratio
        boundary_matrix = e_util_transform_convert_to_matrix(&ec->transform_core.result.transform, &source_rect);
        keep_ratio = e_util_transform_keep_ratio_get(&ec->transform_core.result.transform);

        if (keep_ratio)
          {
             ec->transform_core.result.transform = e_util_transform_keep_ratio_apply(&ec->transform_core.result.transform, ec->w, ec->h);
             matrix = e_util_transform_convert_to_matrix(&ec->transform_core.result.transform, &source_rect);
          }
        else
           matrix = boundary_matrix;

        if (keep_ratio != ec->transform_core.keep_ratio)
          {
             if (keep_ratio)
               {
                  e_comp_object_transform_bg_set(ec->frame, EINA_TRUE);
               }
             else
               {
                  e_comp_object_transform_bg_set(ec->frame, EINA_FALSE);
               }

             ec->transform_core.keep_ratio = keep_ratio;
          }

        // 3.5 parent matrix multiply
        if (ec->transform_core.parent.enable)
          {
             matrix = e_util_transform_matrix_multiply(&ec->transform_core.parent.matrix,
                                                       &matrix);
             boundary_matrix = e_util_transform_matrix_multiply(&ec->transform_core.parent.matrix,
                                                                &boundary_matrix);
          }

        // 4. apply matrix to vertices
        ec->transform_core.result.matrix = matrix;
        ec->transform_core.result.vertices = e_util_transform_rect_to_vertices(&source_rect);
        ec->transform_core.result.boundary.vertices = e_util_transform_rect_to_vertices(&source_rect);
        ec->transform_core.result.vertices = e_util_transform_matrix_multiply_rect_vertex(&matrix,
                                                                                          &ec->transform_core.result.vertices);
        ec->transform_core.result.boundary.vertices = e_util_transform_matrix_multiply_rect_vertex(&boundary_matrix,
                                                                                                   &ec->transform_core.result.boundary.vertices);

        // 5. apply vertices
        e_comp_object_transform_bg_vertices_set(ec->frame, &ec->transform_core.result.boundary.vertices);
        _e_client_transform_core_boundary_update(ec, &ec->transform_core.result.boundary.vertices);
        _e_client_transform_core_vertices_apply(ec, ec->frame, &ec->transform_core.result.vertices);

        // 6. subsurface update'
        _e_client_transform_core_sub_update(ec, &ec->transform_core.result.vertices);
     }
   else
     {
        ec->transform_core.result.enable = EINA_FALSE;
        _e_client_transform_core_vertices_apply(ec, ec->frame, NULL);
        _e_client_transform_core_sub_update(ec, NULL);
     }

   e_client_visibility_calculate();
}

E_API int
e_client_transform_core_transform_count_get(E_Client *ec)
{
   if (!ec) return 0;
   if (!ec->transform_core.transform_list) return 0;
   return eina_list_count(ec->transform_core.transform_list);
}

E_API E_Util_Transform*
e_client_transform_core_transform_get(E_Client *ec, int index)
{
   if (!ec) return NULL;
   if (!ec->transform_core.transform_list) return NULL;
   if (index < 0 || index >= e_client_transform_core_transform_count_get(ec))
      return NULL;

   return (E_Util_Transform*)eina_list_nth(ec->transform_core.transform_list, index);
}
