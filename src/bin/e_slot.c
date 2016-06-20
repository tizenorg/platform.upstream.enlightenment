#include "e.h"

/* global variables */
Eina_List *slot_list = NULL;
Eina_List *handlers = NULL;
static int id = 0;

/* static functions */
static E_Slot_Client* _e_slot_client_find_by_ec(Eina_List *slot_client_list, E_Client *ec);
static int _e_slot_generate_id(void);
static E_Slot* _e_slot_find_by_id(int slot_id);
static Eina_Bool _e_slot_cb_client_remove(void *data, int type, void *event);

static E_Slot_Client* _e_slot_client_new(E_Client* ec, E_Slot_Type type);
static void _e_slot_client_del(E_Slot_Client* slot_client);
static void _e_slot_client_type_set(E_Slot_Client* slot_client, E_Slot_Type type);
static void _e_slot_client_type_unset(E_Slot_Client* slot_client);
static void _e_slot_id_set(E_Client* ec, int slot_id);

EAPI void
e_slot_init (void)
{
   ELOGF("SLOT", "e_slot_init", 0, 0);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_REMOVE, _e_slot_cb_client_remove, NULL);
}

EAPI void
e_slot_shutdown(void)
{
   ELOGF("SLOT", "e_slot_shutdown", 0, 0);
   if (handlers)
     E_FREE_LIST(handlers, ecore_event_handler_del);
}


EAPI int
e_slot_new(void)
{
   E_Slot *slot = NULL;
   int slot_id = -1;

   slot = (E_Slot*)malloc(sizeof(E_Slot));
   if (!slot) return slot_id;

   memset(slot, 0, sizeof(E_Slot));
   slot_id = _e_slot_generate_id();
   slot->id = slot_id;
   slot_list = eina_list_append(slot_list, slot);

   ELOGF("SLOT", "e_slot_new |Create new slot - id:%d ", 0, 0, slot_id);

   return slot_id;
}

EAPI void
e_slot_del(int slot_id)
{
   E_Slot *slot = NULL;
   Eina_List *l;
   E_Slot_Client *slot_client = NULL;

   if (slot_id <= 0) return;

   slot = _e_slot_find_by_id(slot_id);
   if (!slot) return;

   if (slot->client_list)
     {
        EINA_LIST_FOREACH(slot->client_list, l, slot_client)
          {
             e_slot_client_remove(slot_client->ec->slot_id, slot_client->ec);
          }
     }

   slot_list = eina_list_remove(slot_list, slot);
   free(slot);
   slot = NULL;

   ELOGF("SLOT", "e_slot_del |Remove slot - id:%d ", 0, 0, slot_id);
}

EAPI void
e_slot_move(int slot_id, int x, int y)
{
   E_Slot *slot = NULL;

   if (slot_id <= 0) return;

   slot = _e_slot_find_by_id(slot_id);
   if (!slot) return;
   if (slot->rect.x == x && slot->rect.y == y) return;

   ELOGF("SLOT", "e_slot_move |Move slot - id:%d  (%d, %d) -> (%d, %d)", 0, 0,
         slot_id, slot->rect.x, slot->rect.y, x, y);

   slot->rect.x = x;
   slot->rect.y = y;
   slot->changed = 1;

   e_slot_update(slot_id);
}

EAPI void
e_slot_resize(int slot_id, int w, int h)
{
   E_Slot *slot = NULL;

   if (slot_id <= 0) return;

   slot = _e_slot_find_by_id(slot_id);
   if (!slot) return;
   if (slot->rect.w == w && slot->rect.h == h) return;

   ELOGF("SLOT", "e_slot_resize |Resize slot - id:%d  (%dx%d) -> (%dx%d)", 0, 0,
         slot_id, slot->rect.w, slot->rect.h, w, h);

   slot->rect.w = w;
   slot->rect.h = h;
   slot->changed = 1;

   e_slot_update(slot_id);
}



EAPI Eina_Bool
e_slot_client_add(int slot_id, E_Client *ec, E_Slot_Type type)
{
   E_Slot *slot = NULL;
   E_Slot_Client* slot_client = NULL;

   if (slot_id <= 0) return EINA_FALSE;
   if (!ec) return EINA_FALSE;

   slot = _e_slot_find_by_id(slot_id);
   if (!slot) return EINA_FALSE;
   if (_e_slot_client_find_by_ec (slot->client_list, ec)) return EINA_FALSE;
   if (ec->slot_id > 0 && ec->slot_id != slot_id)
     {
        /* if already allocated in other slot, remove ec from prev. slot */
        ELOGF("SLOT", "e_slot_client_add |Client Remove from slot - id:%d", ec->pixmap, ec, ec->slot_id);
        e_slot_client_remove (ec->slot_id, ec);
     }

   slot_client = _e_slot_client_new(ec, type);
   if (!slot_client) return EINA_FALSE;
   _e_slot_client_type_set(slot_client, type);

   slot->client_list = eina_list_append(slot->client_list, slot_client);
   slot->changed = EINA_TRUE;
   _e_slot_id_set(ec, slot->id);

   ELOGF("SLOT", "e_slot_client_add |Client Add - id:%d type:%d", ec->pixmap, ec, slot_id, type);

   e_slot_update(slot_id);

   return EINA_TRUE;
}

EAPI Eina_Bool
e_slot_client_remove(int slot_id, E_Client *ec)
{
   E_Slot *slot = NULL;
   E_Slot_Client *slot_client = NULL;

   if (slot_id <= 0) return EINA_FALSE;

   slot = _e_slot_find_by_id(slot_id);
   if (!slot) return EINA_FALSE;
   if (!(slot_client = _e_slot_client_find_by_ec (slot->client_list, ec))) return EINA_FALSE;

   ELOGF("SLOT", "e_slot_client_remove |Client Remove - id:%d", ec->pixmap, ec, slot_id);

   _e_slot_client_type_unset(slot_client);

   slot->client_list = eina_list_remove(slot->client_list, slot_client);
   _e_slot_client_del(slot_client);

   slot->changed = EINA_TRUE;
   _e_slot_id_set(ec, 0);

   e_slot_update(slot_id);

   return EINA_TRUE;
}

EAPI Eina_List*
e_slot_client_list_get(int slot_id)
{
   E_Slot *slot = NULL;

   if (slot_id <= 0) return NULL;

   slot = _e_slot_find_by_id(slot_id);
   if (!slot) return NULL;

   return slot->client_list;
}

EAPI int
e_slot_client_count_get(int slot_id)
{
   E_Slot *slot = NULL;

   if (slot_id <= 0) return 0;

   slot = _e_slot_find_by_id(slot_id);
   if (!slot) return 0;

   return eina_list_count(slot->client_list);
}

EAPI int
e_slot_client_slot_id_get(E_Client *ec)
{
   if (!ec) return -1;

   return ec->slot_id;
}

EAPI E_Slot_Type
e_slot_client_type_get(E_Client *ec)
{
   E_Slot_Type type = -1;
   if (!ec) return type;
   if (ec->slot_id <= 0) return type;

   Eina_List *slot_client_list = NULL;
   slot_client_list = e_slot_client_list_get(ec->slot_id);
   if (!slot_client_list) return type;

   E_Slot_Client *slot_client = NULL;
   slot_client = _e_slot_client_find_by_ec(slot_client_list, ec);

   if (slot_client)
     type = slot_client->type;

   return type;
}

EAPI void
e_slot_raise(int slot_id)
{
   E_Slot *slot = NULL;
   Eina_List *l = NULL;
   E_Slot_Client *slot_client = NULL;
   Evas_Object *o = NULL;
   E_Client *ec = NULL;
   E_Client *ec2 = NULL;
   int cnt = 0;

   if (slot_id <= 0) return;

   slot = _e_slot_find_by_id(slot_id);
   if (!slot) return;

   for(o = evas_object_bottom_get(e_comp->evas); o; o = evas_object_above_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (e_client_util_ignored_get(ec)) continue;
        if (ec->slot_id != slot_id) continue;
        ELOGF("SLOT", "e_slot_raise |raise ec[list add] - id:%d [cnt:%d]", ec->pixmap, ec, slot_id, cnt++);
        l = eina_list_append(l, ec);
     }

   cnt = 0;
   EINA_LIST_FREE(l, ec2)
     {
        ELOGF("SLOT", "e_slot_raise |raise ec - id:%d [cnt:%d]", ec2->pixmap, ec2, slot_id, cnt++);
        evas_object_raise(ec2->frame);
     }
}

EAPI void
e_slot_lower (int slot_id)
{
   E_Slot *slot = NULL;
   Eina_List *l = NULL;
   E_Slot_Client *slot_client = NULL;
   Evas_Object *o = NULL;
   E_Client *ec = NULL;
   E_Client *ec2 = NULL;
   int cnt = 0;

   if (slot_id <= 0) return;

   slot = _e_slot_find_by_id(slot_id);
   if (!slot) return;

   for(o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (e_client_util_ignored_get(ec)) continue;
        if (ec->slot_id != slot_id) continue;
        ELOGF("SLOT", "e_slot_lower |lower ec[list add] - id:%d [cnt:%d]", ec->pixmap, ec, slot_id, cnt++);
        l = eina_list_append(l, ec);
     }

   cnt = 0;
   EINA_LIST_FREE(l, ec2)
     {
        ELOGF("SLOT", "e_slot_lower |lower ec - id:%d [cnt:%d]", ec2->pixmap, ec2, slot_id, cnt++);
        evas_object_lower(ec2->frame);
     }
}

EAPI void
e_slot_focus_set(int slot_id)
{
   E_Slot *slot = NULL;
   Eina_List *l = NULL;
   E_Slot_Client *slot_client = NULL;
   Evas_Object *o = NULL;
   E_Client *ec = NULL;
   int cnt = 0;

   if (slot_id <= 0) return;

   slot = _e_slot_find_by_id(slot_id);
   if (!slot) return;

   for(o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (e_client_util_ignored_get(ec)) continue;
        if (ec->slot_id != slot_id) continue;
        ELOGF("SLOT", "e_slot_focus_set |ec foucsed set - id:%d [cnt:%d]", ec->pixmap, ec, slot_id, cnt++);
        evas_object_raise(ec->frame);
        break;
     }
}

EAPI void
e_slot_update (int slot_id)
{
   E_Slot *slot = NULL;
   Eina_List *l = NULL;
   E_Slot_Client *slot_client = NULL;
   int x, y, w, h;
   int cnt = 0;

   if (slot_id <= 0) return;

   slot = _e_slot_find_by_id(slot_id);
   if (!slot) return;
   if (slot->client_list)
     {
        EINA_LIST_FOREACH(slot->client_list, l, slot_client)
          {
             if (e_object_is_del(E_OBJECT(slot_client->ec))) continue;
             if (e_client_util_ignored_get(slot_client->ec)) continue;
             if (!slot_client->ec->comp_data) continue;

             e_client_geometry_get(slot_client->ec, &x, &y, &w, &h);

             if (slot_client->type == E_SLOT_TYPE_TRANSFORM)
               {
                  if (slot_client->transform)
                    {
                       e_util_transform_move(slot_client->transform, (double)slot->rect.x, (double)slot->rect.y, 0);
                       e_util_transform_scale(slot_client->transform,  (double)slot->rect.w/(double)slot_client->ec->w, (double)slot->rect.h /(double)slot_client->ec->h, 1.0);
                       e_client_transform_core_update(slot_client->ec);

                       ELOGF("SLOT", "e_slot_update |transform update - id:%d (%d,%d) (%lf x %lf) [cnt:%d]", slot_client->ec->pixmap, slot_client->ec,
                             slot_id, slot->rect.x, slot->rect.y, (double)slot->rect.w/(double)slot_client->ec->w, (double)slot->rect.h /(double)slot_client->ec->h, cnt++);
                    }
               }
             else
               {
                  evas_object_move(slot_client->ec->frame, slot->rect.x, slot->rect.y);
                  evas_object_resize(slot_client->ec->frame, slot->rect.w, slot->rect.h);
                  ELOGF("SLOT", "e_slot_update |resize update - id:%d (%d,%d,%dx%d) [cnt:%d]", slot_client->ec->pixmap, slot_client->ec,
                        slot_id, slot->rect.x, slot->rect.y, slot->rect.w, slot->rect.h, cnt++);
               }
          }
     }

   slot->changed = 0;

   e_client_visibility_calculate();
}

EAPI Eina_List*
e_slot_list_get(void)
{
   return slot_list;
}

static E_Slot_Client*
_e_slot_client_find_by_ec(Eina_List *slot_client_list, E_Client *ec)
{
   E_Slot_Client *slot_client = NULL;
   Eina_List *l;
   EINA_LIST_FOREACH(slot_client_list, l, slot_client)
     {
        if (slot_client->ec == ec) break;
     }

   return slot_client;
}

static int
_e_slot_generate_id(void)
{
   /* FIXME: */
   return ++id;
}

static E_Slot*
_e_slot_find_by_id(int slot_id)
{
   E_Slot *slot;
   Eina_List *l;
   EINA_LIST_FOREACH(slot_list, l, slot)
     {
        if (slot->id == slot_id) break;
     }

   return slot;
}

static Eina_Bool
_e_slot_cb_client_remove(void *data, int type, void *event)
{
   E_Client *ec = NULL;
   E_Event_Client *ev = (E_Event_Client*)event;

   if (ev) ec = ev->ec;
   if (ec)
     {
        if (ec->slot_id > 0)
          {
             ELOGF("SLOT", "_e_slot_cb_client_remove |remove slot.. because client remove! - id:%d", ec->pixmap, ec, ec->slot_id);
             e_slot_client_remove(ec->slot_id, ec);
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static E_Slot_Client*
_e_slot_client_new(E_Client* ec, E_Slot_Type type)
{
   E_Slot_Client *slot_client = NULL;

   slot_client = (E_Slot_Client*)malloc(sizeof(E_Slot_Client));
   if (!slot_client) return NULL;

   memset(slot_client, 0, sizeof(E_Slot_Client));
   slot_client->ec = ec;
   slot_client->backup_client.x = ec->x;
   slot_client->backup_client.y = ec->y;
   slot_client->backup_client.w = ec->w;
   slot_client->backup_client.h = ec->h;

   if (slot_client->backup_client.x == 0 &&
       slot_client->backup_client.y == 0 &&
       slot_client->backup_client.w == 1 &&
       slot_client->backup_client.h == 1)
     {
        slot_client->backup_client.x = ec->zone->x;
        slot_client->backup_client.y = ec->zone->y;
        slot_client->backup_client.w = ec->zone->w;
        slot_client->backup_client.h = ec->zone->h;
     }

   slot_client->transform = NULL;
   ELOGF("SLOT", "_e_slot_client_new |Create slot client - type:%d backup_client: %d,%d,%dx%d", ec->pixmap, ec, type, ec->x, ec->y, ec->w, ec->h);

   return slot_client;
}

static void
_e_slot_client_del(E_Slot_Client* slot_client)
{
   if (!slot_client) return;
   free(slot_client);
   slot_client = NULL;
}

static void
_e_slot_client_type_set(E_Slot_Client* slot_client, E_Slot_Type type)
{
   if (!slot_client) return;

   slot_client->type = type;

   if (slot_client->type == E_SLOT_TYPE_TRANSFORM)
     {
        if (!slot_client->transform)
          {
             slot_client->transform = e_util_transform_new();
             e_client_transform_core_add(slot_client->ec, slot_client->transform);
          }

        ELOGF("SLOT", "_e_slot_client_type_set |type transform - set transform ", slot_client->ec->pixmap, slot_client->ec);
     }
}

static void
_e_slot_client_type_unset(E_Slot_Client* slot_client)
{
   if (!slot_client) return;

   if (slot_client->type == E_SLOT_TYPE_TRANSFORM)
     {
        e_client_transform_core_remove(slot_client->ec, slot_client->transform);
        e_util_transform_del(slot_client->transform);
        slot_client->transform = NULL;

        ELOGF("SLOT", "_e_slot_client_type_unset |unset transform ", slot_client->ec->pixmap, slot_client->ec);
     }
   else
     {
        /* restore to its origin size when remove from slot */
        evas_object_move(slot_client->ec->frame, slot_client->backup_client.x, slot_client->backup_client.y);
        evas_object_resize(slot_client->ec->frame, slot_client->backup_client.w, slot_client->backup_client.h);

        ELOGF("SLOT", "_e_slot_client_type_unset |restore its origin size: %d,%d,%dx%d", slot_client->ec->pixmap, slot_client->ec,
              slot_client->backup_client.x, slot_client->backup_client.y, slot_client->backup_client.w, slot_client->backup_client.h);
     }
}

static void
_e_slot_id_set(E_Client* ec, int slot_id)
{
   if (!ec) return;

   ELOGF("SLOT", "_e_slot_id_set |set slot_id to EC - id:(%d->%d)", ec->pixmap, ec, ec->slot_id, slot_id);
   ec->slot_id = slot_id;
}

