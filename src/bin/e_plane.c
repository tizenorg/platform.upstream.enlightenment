#include "e.h"

/* E_Plane is a child object of E_Output_Screen. There is one Output per screen
 * E_plane represents hw overlay and a surface is assigned to disable composition
 * Each Output always has dedicated canvas and a zone
 */

E_API int E_EVENT_PLANE_ADD = 0;
E_API int E_EVENT_PLANE_DEL = 0;

///////////////////////////////////////////

/* local subsystem functions */
static void
_e_plane_reconfigure_clients(E_Plane *plane, int dx, int dy, int dw, int dh)
{
   EINA_SAFETY_ON_NULL_RETURN(plane->ec);

   /* TODO: config ec refer to resolution */
}

///////////////////////////////////////////
/*
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
*/
E_API void
e_plane_free(E_Plane *plane)
{
   //printf("@@@@@@@@@@ e_plane_free: %i %i | %i %i %ix%i = %p\n", zone->num, zone->id, zone->x, zone->y, zone->w, zone->h, zone);

   if (!plane) return;
   if (plane->name) eina_stringshare_del(plane->name);

   free(plane);
}

E_API E_Plane *
e_plane_new(E_Output_Screen *screen)
{
   E_Plane *plane;

   char name[40];

   if (!screen) return NULL;

   //plane = E_OBJECT_ALLOC(E_Plane, E_PLANE_TYPE, _e_plane_free);
   plane = E_NEW(E_Plane, 1);
   if (!plane) return NULL;
   printf("%s 2", __FUNCTION__);

   snprintf(name, sizeof(name), "Plane %s", screen->id);
   plane->name = eina_stringshare_add(name);

   plane->type = E_PLANE_TYPE_INVALID;
   plane->screen = screen;

   /* config default resolution with output size*/
   plane->resolution.x = screen->config.geom.x;
   plane->resolution.y = screen->config.geom.y;
   plane->resolution.w = screen->config.geom.w;
   plane->resolution.h = screen->config.geom.h;

   screen->planes = eina_list_append(screen->planes, plane);

   printf("@@@@@@@@@@ e_plane_new:| %i %i %ix%i\n", plane->resolution.x , plane->resolution.y, plane->resolution.w, plane->resolution.h);

   return plane;
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
