#include "e.h"

/* E_Plane is a child object of E_Output. There is one Output per screen
 * E_plane represents hw overlay and a surface is assigned to disable composition
 * Each Output always has dedicated canvas and a zone
 */
///////////////////////////////////////////
static const char *_e_plane_ec_last_err = NULL;

/* local subsystem functions */
static void
_e_plane_reconfigure_clients(E_Plane *plane,
                             int dx,
                             int dy,
                             int dw,
                             int dh)
{
   EINA_SAFETY_ON_NULL_RETURN(plane->ec);

   /* TODO: config ec refer to resolution */
}

///////////////////////////////////////////
/*
   EINTERN int
   e_plane_init(void)
   {
      _e_plane_ec_last_err = eina_stringshare_add("UNKNOWN");
      return 1;
   }

   EINTERN int
   e_plane_shutdown(void)
   {
      eina_stringshare_del(_e_plane_ec_last_err);
      return 1;
   }
 */
EINTERN void
e_plane_free(E_Plane *plane)
{
   //printf("@@@@@@@@@@ e_plane_free: %i %i | %i %i %ix%i = %p\n", zone->num, zone->id, zone->x, zone->y, zone->w, zone->h, zone);

   if (!plane) return;
   if (plane->name) eina_stringshare_del(plane->name);

   free(plane);
}

EINTERN E_Plane *
e_plane_new(E_Output *eout,
            int zpos,
            Eina_Bool is_pri)
{
   E_Plane *plane;

   char name[40];

   if (!eout) return NULL;

   plane = E_NEW(E_Plane, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   snprintf(name, sizeof(name), "Plane %s", eout->id);
   plane->name = eina_stringshare_add(name);

   plane->type = E_PLANE_TYPE_INVALID;
   plane->eout = eout;

   plane->zpos = zpos;
   plane->is_primary = is_pri;

   /* config default resolution with output size*/
   plane->geometry.x = eout->config.geom.x;
   plane->geometry.y = eout->config.geom.y;
   plane->geometry.w = eout->config.geom.w;
   plane->geometry.h = eout->config.geom.h;

   eout->planes = eina_list_append(eout->planes, plane);

   printf("@@@@@@@@@@ e_plane_new:| %i %i %ix%i\n", plane->geometry.x , plane->geometry.y, plane->geometry.w, plane->geometry.h);

   return plane;
}

E_API Eina_Bool
e_plane_resolution_set(E_Plane *plane,
                       int w,
                       int h)
{
   int dx = 0, dy = 0, dw = 0, dh = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (plane->is_primary) return EINA_FALSE;

   if ((w == plane->geometry.w) && (h == plane->geometry.h))
     return EINA_FALSE;

   plane->geometry.w = w;
   plane->geometry.h = h;

   /* TODO: config clist refer to resolution */
   _e_plane_reconfigure_clients(plane, dx, dy, dw, dh);
   return EINA_TRUE;
}

E_API Eina_Bool
e_plane_type_set(E_Plane *plane,
                 E_Plane_Type_State type)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if ((type == E_PLANE_TYPE_VIDEO) ||
       (type == E_PLANE_TYPE_CURSOR))
     {
        if (plane->ec || plane->prepare_ec) return EINA_FALSE;
     }
   plane->type = type;
   return EINA_TRUE;
}

E_API E_Plane_Type_State
e_plane_type_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, E_PLANE_TYPE_INVALID);
   return plane->type;
}

E_API E_Client *
e_plane_ec_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);
   return plane->ec;
}

E_API E_Client *
e_plane_ec_prepare_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);
   return plane->prepare_ec;
}

E_API Eina_Bool
e_plane_ec_prepare_set(E_Plane *plane,
                       E_Client *ec)
{
   if(!plane)
     {
        eina_stringshare_replace(&_e_plane_ec_last_err, "Invalid e_plane were passed");
        goto err;
     }

   if (plane->type == E_PLANE_TYPE_OVERLAY)
     {
        eina_stringshare_replace(&_e_plane_ec_last_err, NULL);
        plane->prepare_ec = ec;
        return EINA_TRUE;
     }
   eina_stringshare_replace(&_e_plane_ec_last_err, "Type dismatch : ec not availabe on e_plane");
err:

   return EINA_FALSE;
}

E_API const char *
e_plane_ec_prepare_set_last_error_get(E_Plane *plane)
{
   return _e_plane_ec_last_err;
}

E_API Eina_Bool
e_plane_is_primary(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);
   if (plane->is_primary) return EINA_TRUE;
   return EINA_FALSE;
}

E_API Eina_Bool
e_plane_is_cursor(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);
   if (plane->type == E_PLANE_TYPE_CURSOR) return EINA_TRUE;
   return EINA_FALSE;
}

E_API E_Plane_Color
e_plane_color_val_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, E_PLANE_COLOR_INVALID);
   return plane->color;
}

E_API void
e_plane_geom_get(E_Plane *plane,
                 int *x,
                 int *y,
                 int *w,
                 int *h)
{
   if (!plane) return;
   if (x) *x = plane->geometry.x;
   if (y) *y = plane->geometry.y;
   if (w) *w = plane->geometry.w;
   if (h) *h = plane->geometry.h;
}

