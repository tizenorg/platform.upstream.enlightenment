#ifdef E_TYPEDEFS

typedef struct _E_Plane                      E_Plane;

#else
#ifndef E_PLANE_H
#define E_PLANE_H

#define E_PLANE_TYPE (int)0xE0b11001

typedef enum _E_Plane_Type_State
{
   E_PLANE_TYPE_INVALID,
   E_PLANE_TYPE_PRIMARY,
   E_PLANE_TYPE_SELECTIVE,
   E_PLANE_TYPE_CURSOR
} E_Plane_Type_State;

struct _E_Plane
{
   E_Object     e_obj_inherit;

   struct
   {
      int          x, y, w, h; // FIXME
   } resolution;

   const char  *name;
   E_Plane_Type_State type;
   E_Zone		*zone;
   Eina_List	*clist;
   Eina_List	*handlers;
};

extern E_API int E_EVENT_PLANE_ADD;
extern E_API int E_EVENT_PLANE_DEL;

EINTERN int    e_plane_init(void);
EINTERN int    e_plane_shutdown(void);
E_API E_Plane   * e_plane_new(E_Zone *zone);
E_API void       e_plane_name_set(E_Zone *zone, const char *name);
E_API Eina_Bool  e_plane_resolution_set(E_Plane *plane, int x, int y, int w, int h);
E_API void       e_plane_type_set(E_Plane *plane, E_Plane_Type_State type);
E_API E_Plane_Type_State e_plane_type_get(E_Plane *plane);


#endif
#endif
