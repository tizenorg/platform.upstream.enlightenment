#ifdef E_TYPEDEFS

typedef enum _E_Plane_Type_State
{
   E_PLANE_TYPE_INVALID,
   E_PLANE_TYPE_VIDEO,
   E_PLANE_TYPE_OVERLAY,
   E_PLANE_TYPE_CURSOR
} E_Plane_Type_State;

typedef enum _E_Plane_Color
{
   E_PLANE_COLOR_INVALID,
   E_PLANE_COLOR_YUV,
   E_PLANE_COLOR_RGB
} E_Plane_Color;

typedef struct _E_Plane                      E_Plane;

#else
#ifndef E_PLANE_H
#define E_PLANE_H

#define E_PLANE_TYPE (int)0xE0b11001

struct _E_Plane
{
   int                 zpos;
   struct
     {
        int          x, y, w, h; // FIXME
     } geometry;

   const char         *name;
   E_Plane_Type_State  type;
   E_Plane_Color       color;

   E_Client           *ec;
   E_Client           *prepare_ec;
   E_Output           *eout;

   Eina_Bool           is_primary;
};

EINTERN int              e_plane_init(void);
EINTERN int              e_plane_shutdown(void);
EINTERN E_Plane         *e_plane_new(E_Output *eout, int zpos, Eina_Bool is_pri);
EINTERN void             e_plane_free(E_Plane *plane);
E_API Eina_Bool          e_plane_resolution_set(E_Plane *plane, int w, int h);
E_API Eina_Bool          e_plane_type_set(E_Plane *plane, E_Plane_Type_State type);
E_API E_Plane_Type_State e_plane_type_get(E_Plane *plane);
E_API E_Client          *e_plane_ec_get(E_Plane *plane);
E_API E_Client          *e_plane_ec_prepare_get(E_Plane *plane);
E_API Eina_Bool          e_plane_ec_prepare_set(E_Plane *plane, E_Client *ec);
E_API const char        *e_plane_ec_prepare_set_last_error_get(E_Plane *plane);
E_API Eina_Bool          e_plane_is_primary(E_Plane *plane);
E_API Eina_Bool          e_plane_is_cursor(E_Plane *plane);
E_API E_Plane_Color      e_plane_color_val_get(E_Plane *plane);
E_API void               e_plane_geom_get(E_Plane *plane, int *x, int *y, int *w, int *h);

#endif
#endif
