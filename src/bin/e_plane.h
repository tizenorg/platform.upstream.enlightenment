#ifdef E_TYPEDEFS

typedef struct _E_Plane                      E_Plane;

#else
#ifndef E_PLANE_H
#define E_PLANE_H

#define E_PLANE_TYPE (int)0xE0b11001

typedef enum _E_Plane_Type_State
{
   E_PLANE_TYPE_INVALID,
   E_PLANE_TYPE_VIDEO,
   E_PLANE_TYPE_PRIMARY,
   E_PLANE_TYPE_OVERLAY,
   E_PLANE_TYPE_CURSOR
} E_Plane_Type_State;

struct _E_Plane
{
   int                 zpos;
   struct
     {
        int          x, y, w, h; // FIXME
     } geometry;
   const char         *name;
   E_Plane_Type_State  type;
   E_Client           *ec;
   E_Client           *prepare_ec;
   E_Output           *eout;

   Eina_Bool           is_primary;
};

extern E_API int E_EVENT_PLANE_ADD;
extern E_API int E_EVENT_PLANE_DEL;

EINTERN int              e_plane_init(void);
EINTERN int              e_plane_shutdown(void);
EINTERN E_Plane         *e_plane_new(E_Output *eout, int zpos);
EINTERN void             e_plane_free(E_Plane *plane);
E_API Eina_Bool          e_plane_resolution_set(E_Plane *plane, int w, int h);
E_API void               e_plane_type_set(E_Plane *plane, E_Plane_Type_State type);
E_API E_Plane_Type_State e_plane_type_get(E_Plane *plane);

E_API E_Client          *e_plane_hwc_get(E_Plane *plane);
E_API E_Client          *e_plane_hwc_prepare_get(E_Plane *plane);
E_API Eina_Bool          e_plane_hwc_prepare_set(E_Plane *plane, E_Client *ec);

#endif
#endif
