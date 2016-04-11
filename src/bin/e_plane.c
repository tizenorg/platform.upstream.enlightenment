#include "e.h"

/* E_Plane is a child object of E_Zone. There is one zone per screen
 * in a xinerama setup. Each zone has one or more planes.
 */

E_API int E_EVENT_PLANE_ADD = 0;
E_API int E_EVENT_PLANE_DEL = 0;

///////////////////////////////////////////

/* local subsystem functions */
static void
_e_plane_free(E_Plane *plane)
{
   //printf("@@@@@@@@@@ e_plane_free: %i %i | %i %i %ix%i = %p\n", zone->num, zone->id, zone->x, zone->y, zone->w, zone->h, zone);

   /* Delete the object event callbacks */

   /* remove clients list */
   eina_list_free(plane->clist);

   /* remove handlers */
   E_FREE_LIST(plane->handlers, ecore_event_handler_del);

   if (plane->name) eina_stringshare_del(plane->name);

   free(plane);
}

static void
_e_plane_reconfigure_clients(E_Plane *plane, int dx, int dy, int dw, int dh)
{
   E_Client *ec;

   Eina_List *l, *ll;
   EINA_LIST_FOREACH_SAFE(plane->clist, l, ll, ec)
     {
        if (!ec) break;
        /* TODO: config ec refer to resolution */
     }
}

///////////////////////////////////////////

EINTERN int
e_plane_init(void)
{
   E_EVENT_PLANE_ADD = ecore_event_type_new();
   E_EVENT_PLANE_DEL = ecore_event_type_new();

   return 1;
}

EINTERN int
e_plane_shutdown(void)
{
   return 1;
}

E_API E_Plane *
e_plane_new(E_Zone *zone)
{
   E_Plane *plane;

   char name[40];

   if (!zone) return NULL;

   plane = E_OBJECT_ALLOC(E_Plane, E_PLANE_TYPE, _e_plane_free);
   if (!plane) return NULL;

   snprintf(name, sizeof(name), "Plane %d", zone->num);
   plane->name = eina_stringshare_add(name);

   plane->type = E_PLANE_TYPE_INVALID;
   plane->zone = zone;

   /* config default resolution with zone size*/
   plane->resolution.x = zone->x;
   plane->resolution.y = zone->y;
   plane->resolution.w = zone->w;
   plane->resolution.h = zone->h;

   zone->planes = eina_list_append(zone->planes, plane);

   printf("@@@@@@@@@@ e_plane_new: %s | %i %i %ix%i = %p\n", zone->randr2_id, plane->resolution.x , plane->resolution.y, plane->resolution.w, plane->resolution.h, zone);

   return plane;
}

E_API void
e_plane_name_set(E_Zone *zone,
                const char *name)
{
   E_OBJECT_CHECK(zone);
   E_OBJECT_TYPE_CHECK(zone, E_PLANE_TYPE);

   if (zone->name) eina_stringshare_del(zone->name);
   zone->name = eina_stringshare_add(name);
}


E_API Eina_Bool
e_plane_resolution_set(E_Plane *plane,
                   int x,
                   int y,
                   int w,
                   int h)
{
   int dx = 0, dy = 0, dw = 0, dh = 0;

   E_OBJECT_CHECK_RETURN(plane, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(plane, E_PLANE_TYPE, EINA_FALSE);

   if ((x == plane->resolution.x) && (plane->resolution.y) && (w == plane->resolution.w) && (h == plane->resolution.h))
     return EINA_FALSE;

   plane->resolution.x = x;
   plane->resolution.y = y;
   plane->resolution.w = w;
   plane->resolution.h = h;

   /* TODO: config clist refer to resolution */
   _e_plane_reconfigure_clients(plane, dx, dy, dw, dh);
   return EINA_TRUE;
}

E_API void
e_plane_type_set(E_Plane *plane, E_Plane_Type_State type)
{
   E_OBJECT_CHECK(plane);
   E_OBJECT_TYPE_CHECK(plane, E_PLANE_TYPE);

   plane->type = type;
}

E_API E_Plane_Type_State
e_plane_type_get(E_Plane *plane)
{
   E_OBJECT_CHECK_RETURN(plane, E_ZONE_DISPLAY_STATE_OFF);
   E_OBJECT_TYPE_CHECK_RETURN(plane, E_PLANE_TYPE, E_ZONE_DISPLAY_STATE_OFF);

   return plane->type;
}
