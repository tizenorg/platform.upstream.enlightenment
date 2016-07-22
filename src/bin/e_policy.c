#include "e.h"
#include "e_policy_keyboard.h"
#include "e_policy_transform_mode.h"
#include "e_policy_conformant.h"
#include "e_policy_wl.h"

E_Policy *e_policy = NULL;
Eina_Hash *hash_policy_desks = NULL;
Eina_Hash *hash_policy_clients = NULL;
E_Policy_System_Info e_policy_system_info =
{
   {NULL, EINA_FALSE},
   { -1, -1, EINA_FALSE}
};

static Eina_List *handlers = NULL;
static Eina_List *hooks_ec = NULL;
static Eina_List *hooks_cp = NULL;

static E_Policy_Client *_e_policy_client_add(E_Client *ec);
static void        _e_policy_client_del(E_Policy_Client *pc);
static Eina_Bool   _e_policy_client_normal_check(E_Client *ec);
static Eina_Bool   _e_policy_client_maximize_policy_apply(E_Policy_Client *pc);
static void        _e_policy_client_maximize_policy_cancel(E_Policy_Client *pc);
static void        _e_policy_client_floating_policy_apply(E_Policy_Client *pc);
static void        _e_policy_client_floating_policy_cancel(E_Policy_Client *pc);
static void        _e_policy_client_launcher_set(E_Policy_Client *pc);

static void        _e_policy_cb_hook_client_eval_pre_new_client(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_eval_pre_fetch(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_eval_pre_post_fetch(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_eval_post_fetch(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_eval_post_new_client(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_desk_set(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_fullscreen_pre(void *data EINA_UNUSED, E_Client *ec);

static void        _e_policy_cb_hook_pixmap_del(void *data EINA_UNUSED, E_Pixmap *cp);
static void        _e_policy_cb_hook_pixmap_unusable(void *data EINA_UNUSED, E_Pixmap *cp);

static void        _e_policy_cb_desk_data_free(void *data);
static void        _e_policy_cb_client_data_free(void *data);
static Eina_Bool   _e_policy_cb_zone_add(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_zone_del(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_zone_move_resize(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_zone_desk_count_set(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_zone_display_state_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_desk_show(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_client_add(void *data EINA_UNUSED, int type, void *event);
static Eina_Bool   _e_policy_cb_client_move(void *data EINA_UNUSED, int type, void *event);
static Eina_Bool   _e_policy_cb_client_resize(void *data EINA_UNUSED, int type, void *event);
static Eina_Bool   _e_policy_cb_client_stack(void *data EINA_UNUSED, int type, void *event);
static Eina_Bool   _e_policy_cb_client_property(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_client_vis_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED);

static void
_e_policy_client_launcher_set(E_Policy_Client *pc)
{
   E_Policy_Client *pc2;

   pc2 = e_policy_client_launcher_get(pc->ec->zone);
   if (pc2) return;

   if (pc->ec->netwm.type != e_config->launcher.type)
     return;

   if (e_util_strcmp(pc->ec->icccm.class,
                     e_config->launcher.clas))
     return;


   if (e_util_strcmp(pc->ec->icccm.title,
                     e_config->launcher.title))
     {
        /* check netwm name instead, because comp_x had ignored
         * icccm name when fetching */
        if (e_util_strcmp(pc->ec->netwm.name,
                          e_config->launcher.title))
          {
             return;
          }
     }

   e_policy->launchers = eina_list_append(e_policy->launchers, pc);
}

static E_Policy_Client *
_e_policy_client_add(E_Client *ec)
{
   E_Policy_Client *pc;

   if (e_object_is_del(E_OBJECT(ec))) return NULL;

   pc = eina_hash_find(hash_policy_clients, &ec);
   if (pc) return NULL;

   pc = E_NEW(E_Policy_Client, 1);
   pc->ec = ec;

   eina_hash_add(hash_policy_clients, &ec, pc);

   return pc;
}

static void
_e_policy_client_del(E_Policy_Client *pc)
{
   eina_hash_del_by_key(hash_policy_clients, &pc->ec);
}

static Eina_Bool
_e_policy_client_normal_check(E_Client *ec)
{
   E_Policy_Client *pc;

   if ((e_client_util_ignored_get(ec)) ||
       (!ec->pixmap))
     {
        return EINA_FALSE;
     }

   if (e_policy_client_is_quickpanel(ec))
     {
        return EINA_FALSE;
     }

   if (e_policy_client_is_keyboard(ec) ||
       e_policy_client_is_keyboard_sub(ec))
     {
        e_policy_keyboard_layout_apply(ec);
        goto cancel_max;
     }
   else if (e_policy_client_is_volume_tv(ec))
     goto cancel_max;
   else if (!e_util_strcmp("e_demo", ec->icccm.window_role))
     goto cancel_max;
   else if (e_policy_client_is_floating(ec))
     {
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);
        _e_policy_client_floating_policy_apply(pc);
        return EINA_FALSE;
     }
   else if (e_policy_client_is_subsurface(ec))
     goto cancel_max;

   if ((ec->netwm.type == E_WINDOW_TYPE_NORMAL) ||
       (ec->netwm.type == E_WINDOW_TYPE_UNKNOWN) ||
       (ec->netwm.type == E_WINDOW_TYPE_NOTIFICATION))
     {
        return EINA_TRUE;
     }

   return EINA_FALSE;

cancel_max:
   pc = eina_hash_find(hash_policy_clients, &ec);
   _e_policy_client_maximize_policy_cancel(pc);

   return EINA_FALSE;
}

static void
_e_policy_client_maximize_pre(E_Policy_Client *pc)
{
   E_Client *ec;
   int zx, zy, zw, zh;

   ec = pc->ec;

   if (ec->desk->visible)
     e_zone_useful_geometry_get(ec->zone, &zx, &zy, &zw, &zh);
   else
     {
        zx = ec->zone->x;
        zy = ec->zone->y;
        zw = ec->zone->w;
        zh = ec->zone->h;
     }

   ec->x = ec->client.x = zx;
   ec->y = ec->client.y = zy;
   ec->w = ec->client.w = zw;
   ec->h = ec->client.h = zh;

   EC_CHANGED(ec);
}

static Eina_Bool
_e_policy_client_maximize_policy_apply(E_Policy_Client *pc)
{
   E_Client *ec;

   if (!pc) return EINA_FALSE;
   if (pc->max_policy_state) return EINA_FALSE;
   if (pc->allow_user_geom) return EINA_FALSE;

   ec = pc->ec;
   if (ec->netwm.type == E_WINDOW_TYPE_UTILITY) return EINA_FALSE;

   pc->max_policy_state = EINA_TRUE;

#undef _SET
# define _SET(a) pc->orig.a = pc->ec->a
   _SET(borderless);
   _SET(fullscreen);
   _SET(maximized);
   _SET(lock_user_location);
   _SET(lock_client_location);
   _SET(lock_user_size);
   _SET(lock_client_size);
   _SET(lock_client_stacking);
   _SET(lock_user_shade);
   _SET(lock_client_shade);
   _SET(lock_user_maximize);
   _SET(lock_client_maximize);
   _SET(lock_user_fullscreen);
   _SET(lock_client_fullscreen);
#undef _SET

   _e_policy_client_launcher_set(pc);

   if (ec->remember)
     {
        e_remember_del(ec->remember);
        ec->remember = NULL;
     }

   /* skip hooks of e_remeber for eval_pre_post_fetch and eval_post_new_client */
   ec->internal_no_remember = 1;

   if (!ec->borderless)
     {
        ec->borderless = 1;
        ec->border.changed = 1;
        EC_CHANGED(pc->ec);
     }

   if (!ec->maximized)
     {
        e_client_maximize(ec, E_MAXIMIZE_EXPAND | E_MAXIMIZE_BOTH);

        if (ec->changes.need_maximize)
          _e_policy_client_maximize_pre(pc);
     }

   /* do not allow client to change these properties */
   ec->lock_user_location = 1;
   ec->lock_client_location = 1;
   ec->lock_user_size = 1;
   ec->lock_client_size = 1;
   ec->lock_user_shade = 1;
   ec->lock_client_shade = 1;
   ec->lock_user_maximize = 1;
   ec->lock_client_maximize = 1;
   ec->lock_user_fullscreen = 1;
   ec->lock_client_fullscreen = 1;
   ec->skip_fullscreen = 1;

   if (!e_policy_client_is_home_screen(ec))
     ec->lock_client_stacking = 1;

   return EINA_TRUE;
}

static void
_e_policy_client_maximize_policy_cancel(E_Policy_Client *pc)
{
   E_Client *ec;
   Eina_Bool changed = EINA_FALSE;

   if (!pc) return;
   if (!pc->max_policy_state) return;

   pc->max_policy_state = EINA_FALSE;

   ec = pc->ec;

   if (pc->orig.borderless != ec->borderless)
     {
        ec->border.changed = 1;
        changed = EINA_TRUE;
     }

   if ((pc->orig.fullscreen != ec->fullscreen) &&
       (pc->orig.fullscreen))
     {
        ec->need_fullscreen = 1;
        changed = EINA_TRUE;
     }

   if (pc->orig.maximized != ec->maximized)
     {
        if (pc->orig.maximized)
          ec->changes.need_maximize = 1;
        else
          e_client_unmaximize(ec, ec->maximized);

        changed = EINA_TRUE;
     }

#undef _SET
# define _SET(a) ec->a = pc->orig.a
   _SET(borderless);
   _SET(fullscreen);
   _SET(maximized);
   _SET(lock_user_location);
   _SET(lock_client_location);
   _SET(lock_user_size);
   _SET(lock_client_size);
   _SET(lock_client_stacking);
   _SET(lock_user_shade);
   _SET(lock_client_shade);
   _SET(lock_user_maximize);
   _SET(lock_client_maximize);
   _SET(lock_user_fullscreen);
   _SET(lock_client_fullscreen);
#undef _SET

   ec->skip_fullscreen = 0;

   /* only set it if the border is changed or fullscreen/maximize has changed */
   if (changed)
     EC_CHANGED(pc->ec);

   e_policy->launchers = eina_list_remove(e_policy->launchers, pc);
}

static void
_e_policy_client_floating_policy_apply(E_Policy_Client *pc)
{
   E_Client *ec;

   if (!pc) return;
   if (pc->flt_policy_state) return;

   pc->flt_policy_state = EINA_TRUE;
   ec = pc->ec;

#undef _SET
# define _SET(a) pc->orig.a = pc->ec->a
   _SET(fullscreen);
   _SET(lock_client_stacking);
   _SET(lock_user_shade);
   _SET(lock_client_shade);
   _SET(lock_user_maximize);
   _SET(lock_client_maximize);
   _SET(lock_user_fullscreen);
   _SET(lock_client_fullscreen);
#undef _SET

   ec->skip_fullscreen = 1;
   ec->lock_client_stacking = 1;
   ec->lock_user_shade = 1;
   ec->lock_client_shade = 1;
   ec->lock_user_maximize = 1;
   ec->lock_client_maximize = 1;
   ec->lock_user_fullscreen = 1;
   ec->lock_client_fullscreen = 1;
}

static void
_e_policy_client_floating_policy_cancel(E_Policy_Client *pc)
{
   E_Client *ec;
   Eina_Bool changed = EINA_FALSE;

   if (!pc) return;
   if (!pc->flt_policy_state) return;

   pc->flt_policy_state = EINA_FALSE;
   ec = pc->ec;

   if ((pc->orig.fullscreen != ec->fullscreen) &&
       (pc->orig.fullscreen))
     {
        ec->need_fullscreen = 1;
        changed = EINA_TRUE;
     }

   if (pc->orig.maximized != ec->maximized)
     {
        if (pc->orig.maximized)
          ec->changes.need_maximize = 1;
        else
          e_client_unmaximize(ec, ec->maximized);

        changed = EINA_TRUE;
     }

   ec->skip_fullscreen = 0;

#undef _SET
# define _SET(a) ec->a = pc->orig.a
   _SET(fullscreen);
   _SET(lock_client_stacking);
   _SET(lock_user_shade);
   _SET(lock_client_shade);
   _SET(lock_user_maximize);
   _SET(lock_client_maximize);
   _SET(lock_user_fullscreen);
   _SET(lock_client_fullscreen);
#undef _SET

   if (changed)
     EC_CHANGED(pc->ec);
}

E_Config_Policy_Desk *
_e_policy_desk_get_by_num(unsigned int zone_num, int x, int y)
{
   Eina_List *l;
   E_Config_Policy_Desk *d2;

   EINA_LIST_FOREACH(e_config->policy_desks, l, d2)
     {
        if ((d2->zone_num == zone_num) &&
            (d2->x == x) && (d2->y == y))
          {
             return d2;
          }
     }

   return NULL;
}


static void
_e_policy_cb_hook_client_new(void *d EINA_UNUSED, E_Client *ec)
{
   if (EINA_UNLIKELY(!ec))
     return;

   _e_policy_client_add(ec);
}

static void
_e_policy_cb_hook_client_del(void *d EINA_UNUSED, E_Client *ec)
{
   E_Policy_Client *pc;

   if (EINA_UNLIKELY(!ec))
     return;

   e_policy_wl_win_brightness_apply(ec);
   e_policy_wl_client_del(ec);

   if (e_policy_client_is_lockscreen(ec))
     e_policy_stack_clients_restack_above_lockscreen(ec, EINA_FALSE);

   e_policy_stack_cb_client_remove(ec);
   e_client_visibility_calculate();

   pc = eina_hash_find(hash_policy_clients, &ec);
   _e_policy_client_del(pc);
}

static void
_e_policy_cb_hook_client_eval_pre_new_client(void *d EINA_UNUSED, E_Client *ec)
{
   short ly;

   if (e_object_is_del(E_OBJECT(ec))) return;

   if (e_policy_client_is_keyboard_sub(ec))
     {
        ec->placed = 1;
        ec->exp_iconify.skip_iconify = EINA_TRUE;

        EINA_SAFETY_ON_NULL_RETURN(ec->frame);
        if (ec->layer != E_LAYER_CLIENT_ABOVE)
          evas_object_layer_set(ec->frame, E_LAYER_CLIENT_ABOVE);
     }
   if (e_policy_client_is_noti(ec))
     {
        if (ec->frame)
          {
             ly = evas_object_layer_get(ec->frame);
             ELOGF("NOTI", "         |ec->layer:%d object->layer:%d", ec->pixmap, ec, ec->layer, ly);
             if (ly != ec->layer)
               evas_object_layer_set(ec->frame, ec->layer);
          }
     }
   if (e_policy_client_is_floating(ec))
     {
        if (ec->frame)
          {
             if (ec->layer != E_LAYER_CLIENT_ABOVE)
               evas_object_layer_set(ec->frame, E_LAYER_CLIENT_ABOVE);
          }
     }
}

static void
_e_policy_cb_hook_client_eval_pre_fetch(void *d EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;

   e_policy_stack_hook_pre_fetch(ec);
}

static void
_e_policy_cb_hook_client_eval_pre_post_fetch(void *d EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;

   e_policy_stack_hook_pre_post_fetch(ec);
   e_policy_wl_notification_level_fetch(ec);
   e_policy_wl_eval_pre_post_fetch(ec);
}

static void
_e_policy_cb_hook_client_eval_post_fetch(void *d EINA_UNUSED, E_Client *ec)
{
   E_Policy_Client *pc;
   E_Policy_Desk *pd;

   if (e_object_is_del(E_OBJECT(ec))) return;
   /* Following E_Clients will be added to module hash and will be managed.
    *
    *  - Not new client: Updating internal info of E_Client has been finished
    *    by e main evaluation, thus module can classify E_Client and manage it.
    *
    *  - New client that has valid buffer: This E_Client has been passed e main
    *    evaluation, and it has handled first wl_surface::commit request.
    */
   if ((ec->new_client) && (!e_pixmap_usable_get(ec->pixmap))) return;

   if (e_policy_client_is_keyboard(ec) ||
       e_policy_client_is_keyboard_sub(ec))
     {
        E_Policy_Client *pc;
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);

        e_policy_keyboard_layout_apply(ec);
     }

   if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role))
     {
        E_Policy_Client *pc;
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);
        return;
     }

   if (e_policy_client_is_floating(ec))
     {
        E_Policy_Client *pc;
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);
        _e_policy_client_floating_policy_apply(pc);
        return;
     }

   if (!_e_policy_client_normal_check(ec)) return;

   pd = eina_hash_find(hash_policy_desks, &ec->desk);
   if (!pd) return;

   pc = eina_hash_find(hash_policy_clients, &ec);
   if (!pc) return;

   if (pc->flt_policy_state)
     _e_policy_client_floating_policy_cancel(pc);

   _e_policy_client_maximize_policy_apply(pc);
}

static void
_e_policy_cb_hook_client_eval_post_new_client(void *d EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if ((ec->new_client) && (!e_pixmap_usable_get(ec->pixmap))) return;

   if (e_policy_client_is_lockscreen(ec))
     e_policy_stack_clients_restack_above_lockscreen(ec, EINA_TRUE);
}

static void
_e_policy_cb_hook_client_desk_set(void *d EINA_UNUSED, E_Client *ec)
{
   E_Policy_Client *pc;
   E_Policy_Desk *pd;

   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!_e_policy_client_normal_check(ec)) return;
   if (ec->internal) return;
   if (ec->new_client) return;

   pc = eina_hash_find(hash_policy_clients, &ec);
   if (EINA_UNLIKELY(!pc))
     return;

   pd = eina_hash_find(hash_policy_desks, &ec->desk);

   if (pd)
     _e_policy_client_maximize_policy_apply(pc);
   else
     _e_policy_client_maximize_policy_cancel(pc);
}

static void
_e_policy_cb_hook_client_fullscreen_pre(void* data EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!_e_policy_client_normal_check(ec)) return;
   if (ec->internal) return;

   ec->skip_fullscreen = 1;
}

static void
_e_policy_cb_hook_client_visibility(void *d EINA_UNUSED, E_Client *ec)
{
   if (ec->visibility.changed)
     {
        e_policy_client_visibility_send(ec);

        if (ec->visibility.obscured == E_VISIBILITY_UNOBSCURED)
          {
             e_policy_client_uniconify_by_visibility(ec);
          }
        else
          {
             e_policy_client_iconify_by_visibility(ec);
          }

        e_policy_wl_win_brightness_apply(ec);
     }
   else
     {
        if (ec->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
          {
             Eina_Bool obscured_by_alpha_opaque = EINA_FALSE;
             Eina_Bool find_above = EINA_FALSE;
             E_Client *above_ec;
             Evas_Object *o;

             if (ec->zone->display_state == E_ZONE_DISPLAY_STATE_ON)
               {
                  for (o = evas_object_above_get(ec->frame); o; o = evas_object_above_get(o))
                    {
                       above_ec = evas_object_data_get(o, "E_Client");
                       if (!above_ec) continue;
                       if (e_client_util_ignored_get(above_ec)) continue;

                       if (above_ec->exp_iconify.by_client) continue;
                       if (above_ec->exp_iconify.skip_iconify) continue;

                       if (!above_ec->iconic)
                         {
                            if (above_ec->argb && (above_ec->visibility.opaque > 0))
                              obscured_by_alpha_opaque = EINA_TRUE;
                         }
                       find_above = EINA_TRUE;
                       break;
                    }

                  if (!find_above) return;
                  if (obscured_by_alpha_opaque)
                    {
                       e_policy_client_uniconify_by_visibility(ec);
                    }
                  else
                    {
                       e_policy_client_iconify_by_visibility(ec);
                    }
               }
             else if (ec->zone->display_state == E_ZONE_DISPLAY_STATE_OFF)
               {
                  if (e_client_util_ignored_get(ec)) return;
                  if (ec->exp_iconify.by_client) return;
                  if (ec->exp_iconify.skip_iconify) return;
                  if (!ec->iconic)
                    {
                       e_policy_client_iconify_by_visibility(ec);
                    }
               }
          }
     }
}

static void
_e_policy_cb_hook_pixmap_del(void *data EINA_UNUSED, E_Pixmap *cp)
{
   e_policy_wl_pixmap_del(cp);
}

static void
_e_policy_cb_hook_pixmap_unusable(void *data EINA_UNUSED, E_Pixmap *cp)
{
   E_Client *ec = (E_Client *)e_pixmap_client_get(cp);

   if (!ec) return;
   if (!ec->iconic) return;
   if (ec->exp_iconify.by_client) return;
   if (ec->exp_iconify.skip_iconify) return;

   ec->exp_iconify.not_raise = 1;
   e_client_uniconify(ec);
   e_policy_wl_iconify_state_change_send(ec, 0);
}

static void
_e_policy_cb_desk_data_free(void *data)
{
   free(data);
}

static void
_e_policy_cb_client_data_free(void *data)
{
   free(data);
}

static Eina_Bool
_e_policy_cb_zone_add(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Zone_Add *ev;
   E_Zone *zone;
   E_Config_Policy_Desk *d;
   int i, n;

   ev = event;
   zone = ev->zone;
   n = zone->desk_y_count * zone->desk_x_count;
   for (i = 0; i < n; i++)
     {
        d = _e_policy_desk_get_by_num(zone->num,
                                      zone->desks[i]->x,
                                      zone->desks[i]->y);
        if (d)
          e_policy_desk_add(zone->desks[i]);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_zone_del(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Zone_Del *ev;
   E_Zone *zone;
   E_Policy_Desk *pd;
   int i, n;

   ev = event;
   zone = ev->zone;
   n = zone->desk_y_count * zone->desk_x_count;
   for (i = 0; i < n; i++)
     {
        pd = eina_hash_find(hash_policy_desks, &zone->desks[i]);
        if (pd) e_policy_desk_del(pd);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_zone_move_resize(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Zone_Move_Resize *ev;
   E_Policy_Softkey *softkey;

   ev = event;

   if (e_config->use_softkey)
     {
        softkey = e_policy_softkey_get(ev->zone);
        e_policy_softkey_update(softkey);
     }
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_zone_desk_count_set(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Zone_Desk_Count_Set *ev;
   E_Zone *zone;
   E_Desk *desk;
   Eina_Iterator *it;
   E_Policy_Desk *pd;
   E_Config_Policy_Desk *d;
   int i, n;
   Eina_Bool found;
   Eina_List *desks_del = NULL;

   ev = event;
   zone = ev->zone;

   /* remove deleted desk from hash */
   it = eina_hash_iterator_data_new(hash_policy_desks);
   while (eina_iterator_next(it, (void **)&pd))
     {
        if (pd->zone != zone) continue;

        found = EINA_FALSE;
        n = zone->desk_y_count * zone->desk_x_count;
        for (i = 0; i < n; i++)
          {
             if (pd->desk == zone->desks[i])
               {
                  found = EINA_TRUE;
                  break;
               }
          }
        if (!found)
          desks_del = eina_list_append(desks_del, pd->desk);
     }
   eina_iterator_free(it);

   EINA_LIST_FREE(desks_del, desk)
     {
        pd = eina_hash_find(hash_policy_desks, &desk);
        if (pd) e_policy_desk_del(pd);
     }

   /* add newly added desk to hash */
   n = zone->desk_y_count * zone->desk_x_count;
   for (i = 0; i < n; i++)
     {
        d = _e_policy_desk_get_by_num(zone->num,
                                      zone->desks[i]->x,
                                      zone->desks[i]->y);
        if (d)
          e_policy_desk_add(zone->desks[i]);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_zone_display_state_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Zone_Display_State_Change *ev;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;

   e_client_visibility_calculate();

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_desk_show(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Desk_Show *ev;
   E_Policy_Softkey *softkey;

   ev = event;

   if (e_config->use_softkey)
     {
        softkey = e_policy_softkey_get(ev->desk->zone);
        if (eina_hash_find(hash_policy_desks, &ev->desk))
          e_policy_softkey_show(softkey);
        else
          e_policy_softkey_hide(softkey);
     }
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_add(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;

   ev = event;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);

   e_policy_wl_client_add(ev->ec);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_move(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;

   ev = event;
   if (!ev) goto end;

   e_policy_wl_position_send(ev->ec);
   e_client_visibility_calculate();

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_resize(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;
   int zh = 0;

   ev = (E_Event_Client *)event;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, ECORE_CALLBACK_PASS_ON);

   /* re-calculate window's position with changed size */
   if (e_policy_client_is_volume_tv(ec))
     {
        e_zone_useful_geometry_get(ec->zone, NULL, NULL, NULL, &zh);
        evas_object_move(ec->frame, 0, (zh / 2) - (ec->h / 2));

        evas_object_pass_events_set(ec->frame, 1);
     }

   /* calculate e_client visibility */
   e_client_visibility_calculate();

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_stack(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;
   /* calculate e_client visibility */
   e_client_visibility_calculate();

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_property(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client_Property *ev;

   ev = event;
   if (!ev || (!ev->ec)) return ECORE_CALLBACK_PASS_ON;
   if (ev->property & E_CLIENT_PROPERTY_CLIENT_TYPE)
     {
        if (e_policy_client_is_home_screen(ev->ec))
          {
             ev->ec->lock_client_stacking = 0;
             return ECORE_CALLBACK_PASS_ON;
          }
        else if (e_policy_client_is_lockscreen(ev->ec))
          return ECORE_CALLBACK_PASS_ON;
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_vis_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   e_policy_wl_win_scrmode_apply();
   return ECORE_CALLBACK_PASS_ON;
}

void
e_policy_allow_user_geometry_set(E_Client *ec, Eina_Bool set)
{
   E_Policy_Client *pc;

   if (EINA_UNLIKELY(!ec))
     return;

   pc = eina_hash_find(hash_policy_clients, &ec);
   if (EINA_UNLIKELY(!pc))
     return;

   if (set) pc->user_geom_ref++;
   else     pc->user_geom_ref--;

   if (pc->user_geom_ref == 1 && !pc->allow_user_geom)
     {
        pc->allow_user_geom = EINA_TRUE;

        if (!e_policy_client_is_noti(ec))
          {
             ec->netwm.type = E_WINDOW_TYPE_UTILITY;
             ec->lock_client_location = EINA_FALSE;
          }

        ec->lock_client_size = EINA_FALSE;
        ec->placed = 1;
        EC_CHANGED(ec);
     }
   else if (pc->user_geom_ref == 0 && pc->allow_user_geom)
     {
        pc->allow_user_geom = EINA_FALSE;

        ec->lock_client_location = EINA_TRUE;
        ec->lock_client_size = EINA_TRUE;
        ec->placed = 0;
        ec->netwm.type = E_WINDOW_TYPE_NORMAL;
        EC_CHANGED(ec);
     }
}

void
e_policy_desk_add(E_Desk *desk)
{
   E_Policy_Desk *pd;
   E_Client *ec;
   E_Policy_Softkey *softkey;
   E_Policy_Client *pc;

   pd = eina_hash_find(hash_policy_desks, &desk);
   if (pd) return;

   pd = E_NEW(E_Policy_Desk, 1);
   pd->desk = desk;
   pd->zone = desk->zone;

   eina_hash_add(hash_policy_desks, &desk, pd);

   /* add clients */
   E_CLIENT_FOREACH(ec)
     {
       if (pd->desk == ec->desk)
         {
            pc = eina_hash_find(hash_policy_clients, &ec);
            _e_policy_client_maximize_policy_apply(pc);
         }
     }

   /* add and show softkey */
   if (e_config->use_softkey)
     {
        softkey = e_policy_softkey_get(desk->zone);
        if (!softkey)
          softkey = e_policy_softkey_add(desk->zone);
        if (e_desk_current_get(desk->zone) == desk)
          e_policy_softkey_show(softkey);
     }
}

void
e_policy_desk_del(E_Policy_Desk *pd)
{
   Eina_Iterator *it;
   E_Policy_Client *pc;
   E_Client *ec;
   Eina_List *clients_del = NULL;
   E_Policy_Softkey *softkey;
   Eina_Bool keep = EINA_FALSE;
   int i, n;

   /* hide and delete softkey */
   if (e_config->use_softkey)
     {
        softkey = e_policy_softkey_get(pd->zone);
        if (e_desk_current_get(pd->zone) == pd->desk)
          e_policy_softkey_hide(softkey);

        n = pd->zone->desk_y_count * pd->zone->desk_x_count;
        for (i = 0; i < n; i++)
          {
             if (eina_hash_find(hash_policy_desks, &pd->zone->desks[i]))
               {
                  keep = EINA_TRUE;
                  break;
               }
          }

        if (!keep)
          e_policy_softkey_del(softkey);
     }

   /* remove clients */
   it = eina_hash_iterator_data_new(hash_policy_clients);
   while (eina_iterator_next(it, (void **)&pc))
     {
        if (pc->ec->desk == pd->desk)
          clients_del = eina_list_append(clients_del, pc->ec);
     }
   eina_iterator_free(it);

   EINA_LIST_FREE(clients_del, ec)
     {
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);
     }

   eina_hash_del_by_key(hash_policy_desks, &pd->desk);
}

E_Policy_Client *
e_policy_client_launcher_get(E_Zone *zone)
{
   E_Policy_Client *pc;
   Eina_List *l;

   EINA_LIST_FOREACH(e_policy->launchers, l, pc)
     {
        if (pc->ec->zone == zone)
          return pc;
     }
   return NULL;
}

Eina_Bool
e_policy_client_maximize(E_Client *ec)
{
   E_Policy_Desk *pd;
   E_Policy_Client *pc;

   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (EINA_UNLIKELY(!ec))  return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   if ((e_policy_client_is_keyboard(ec)) ||
       (e_policy_client_is_keyboard_sub(ec)) ||
       (e_policy_client_is_floating(ec)) ||
       (e_policy_client_is_quickpanel(ec)) ||
       (e_policy_client_is_volume(ec)) ||
       (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role)) ||
       (!e_util_strcmp("e_demo", ec->icccm.window_role)))
     return EINA_FALSE;

   if (e_policy_client_is_subsurface(ec)) return EINA_FALSE;

   if ((ec->netwm.type != E_WINDOW_TYPE_NORMAL) &&
       (ec->netwm.type != E_WINDOW_TYPE_UNKNOWN) &&
       (ec->netwm.type != E_WINDOW_TYPE_NOTIFICATION))
     return EINA_FALSE;

   pd = eina_hash_find(hash_policy_desks, &ec->desk);
   if (!pd) return EINA_FALSE;

   pc = eina_hash_find(hash_policy_clients, &ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pc, EINA_FALSE);

   if (pc->flt_policy_state)
     _e_policy_client_floating_policy_cancel(pc);

   return _e_policy_client_maximize_policy_apply(pc);
}

Eina_Bool
e_policy_client_is_lockscreen(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (ec->client_type == 2)
     return EINA_TRUE;

   if (!e_util_strcmp(ec->icccm.title, "LOCKSCREEN"))
     return EINA_TRUE;

   if (!e_util_strcmp(ec->icccm.window_role, "lockscreen"))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_home_screen(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (ec->client_type == 1)
     return EINA_TRUE;


   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_quickpanel(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp(ec->icccm.window_role, "quickpanel"))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_conformant(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->comp_data, EINA_FALSE);

   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data *)ec->comp_data;
   if (cdata->conformant == 1)
     {
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_volume(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp(ec->netwm.name, "volume"))
     return EINA_TRUE;

   if (!e_util_strcmp(ec->icccm.title, "volume"))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_volume_tv(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp(ec->icccm.window_role, "tv-volume-popup"))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_noti(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp(ec->icccm.title, "noti_win"))
     return EINA_TRUE;

   if (ec->netwm.type == E_WINDOW_TYPE_NOTIFICATION)
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_subsurface(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cd;

   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   cd = (E_Comp_Wl_Client_Data *)ec->comp_data;
   if (cd && cd->sub.data)
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_floating(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (EINA_UNLIKELY(!ec))
     return EINA_FALSE;

   return ec->floating;
}

Eina_Bool
e_policy_client_is_cursor(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role))
     return EINA_TRUE;

   return EINA_FALSE;
}

E_API void
e_policy_deferred_job(void)
{
   if (!e_policy) return;

   e_policy_wl_defer_job();
}


#undef E_CLIENT_HOOK_APPEND
#define E_CLIENT_HOOK_APPEND(l, t, cb, d) \
  do                                      \
    {                                     \
       E_Client_Hook *_h;                 \
       _h = e_client_hook_add(t, cb, d);  \
       assert(_h);                        \
       l = eina_list_append(l, _h);       \
    }                                     \
  while (0)

#undef E_PIXMAP_HOOK_APPEND
#define E_PIXMAP_HOOK_APPEND(l, t, cb, d) \
  do                                      \
    {                                     \
       E_Pixmap_Hook *_h;                 \
       _h = e_pixmap_hook_add(t, cb, d);  \
       assert(_h);                        \
       l = eina_list_append(l, _h);       \
    }                                     \
  while (0)

E_API int
e_policy_init(void)
{
   E_Policy *pol;
   E_Zone *zone;
   E_Config_Policy_Desk *d;
   const Eina_List *l;
   int i, n;

   pol = E_NEW(E_Policy, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pol, EINA_FALSE);

   e_policy = pol;

   hash_policy_clients = eina_hash_pointer_new(_e_policy_cb_client_data_free);
   hash_policy_desks = eina_hash_pointer_new(_e_policy_cb_desk_data_free);

   e_policy_stack_init();
   e_policy_wl_init();
   e_policy_wl_aux_hint_init();

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        n = zone->desk_y_count * zone->desk_x_count;
        for (i = 0; i < n; i++)
          {
             d = _e_policy_desk_get_by_num(zone->num,
                                           zone->desks[i]->x,
                                           zone->desks[i]->y);
             if (d)
               e_policy_desk_add(zone->desks[i]);
          }
     }

   E_LIST_HANDLER_APPEND(handlers, E_EVENT_ZONE_ADD,                  _e_policy_cb_zone_add,                        NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_ZONE_DEL,                  _e_policy_cb_zone_del,                        NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_ZONE_MOVE_RESIZE,          _e_policy_cb_zone_move_resize,                NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_ZONE_DESK_COUNT_SET,       _e_policy_cb_zone_desk_count_set,             NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_ZONE_DISPLAY_STATE_CHANGE, _e_policy_cb_zone_display_state_change,       NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_DESK_SHOW,                 _e_policy_cb_desk_show,                       NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_ADD,                _e_policy_cb_client_add,                      NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_MOVE,               _e_policy_cb_client_move,                     NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_RESIZE,             _e_policy_cb_client_resize,                   NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_STACK,              _e_policy_cb_client_stack,                    NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_PROPERTY,           _e_policy_cb_client_property,                 NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_VISIBILITY_CHANGE,  _e_policy_cb_client_vis_change,               NULL);

   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_NEW_CLIENT,          _e_policy_cb_hook_client_new,                 NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_DEL,                 _e_policy_cb_hook_client_del,                 NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_PRE_NEW_CLIENT, _e_policy_cb_hook_client_eval_pre_new_client, NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_PRE_FETCH,      _e_policy_cb_hook_client_eval_pre_fetch,      NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_PRE_POST_FETCH, _e_policy_cb_hook_client_eval_pre_post_fetch, NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_POST_FETCH,     _e_policy_cb_hook_client_eval_post_fetch,     NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_POST_NEW_CLIENT,_e_policy_cb_hook_client_eval_post_new_client,NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_DESK_SET,            _e_policy_cb_hook_client_desk_set,            NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_FULLSCREEN_PRE,      _e_policy_cb_hook_client_fullscreen_pre,      NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_VISIBILITY,     _e_policy_cb_hook_client_visibility,          NULL);

   E_PIXMAP_HOOK_APPEND(hooks_cp,  E_PIXMAP_HOOK_DEL,                 _e_policy_cb_hook_pixmap_del,                 NULL);
   E_PIXMAP_HOOK_APPEND(hooks_cp,  E_PIXMAP_HOOK_UNUSABLE,            _e_policy_cb_hook_pixmap_unusable,            NULL);

   e_policy_transform_mode_init();
   e_policy_conformant_init();

   return EINA_TRUE;
}

E_API int
e_policy_shutdown(void)
{
   E_Policy *pol = e_policy;
   Eina_Inlist *l;
   E_Policy_Softkey *softkey;

   eina_list_free(pol->launchers);
   EINA_INLIST_FOREACH_SAFE(pol->softkeys, l, softkey)
     e_policy_softkey_del(softkey);
   E_FREE_LIST(hooks_cp, e_pixmap_hook_del);
   E_FREE_LIST(hooks_ec, e_client_hook_del);
   E_FREE_LIST(handlers, ecore_event_handler_del);

   E_FREE_FUNC(hash_policy_desks, eina_hash_free);
   E_FREE_FUNC(hash_policy_clients, eina_hash_free);

   e_policy_stack_shutdonw();
   e_policy_wl_shutdown();

   e_policy_transform_mode_shutdown();
   e_policy_conformant_shutdown();

   E_FREE(pol);

   e_policy = NULL;

   return 1;
}
