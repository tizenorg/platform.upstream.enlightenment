#ifdef E_TYPEDEFS

typedef struct _E_Output        E_Output;
typedef struct _E_Output_Mode   E_Output_Mode;

#else
#ifndef E_OUTPUT_H
#define E_OUTPUT_H

#include <Ecore_Drm.h>

#define E_OUTPUT_TYPE (int)0xE0b11002

struct _E_Output_Mode
{
   int    w, h; // resolution width and height
   double refresh; // refresh in hz
   Eina_Bool preferred : 1; // is this the preferred mode for the device?
};

struct _E_Output
{
   char *id; // string id which is "name/edid";
   struct {
        char                 *screen; // name of the screen device attached
        char                 *name; // name of the output itself
        char                 *edid; // full edid data
        Eina_Bool             connected : 1; // some screen is plugged in or not
        Eina_List            *modes; // available screen modes here
        struct {
             int                w, h; // physical width and height in mm
        } size;
   } info;
   struct {
        Eina_Rectangle        geom; // the geometry that is set (as a result)
        E_Output_Mode         mode; // screen res/refresh to use
        int                   rotation; // 0, 90, 180, 270
        int                   priority; // larger num == more important
        Eina_Bool             enabled : 1; // should this monitor be enabled?
   } config;

   Ecore_Drm_Output *output;

   int                  plane_count;
   Eina_List           *planes;
   E_Zone              *zone;
};

EINTERN E_Output        * e_output_new(Ecore_Drm_Output *output);
EINTERN void              e_output_del(E_Output *eout);
EINTERN Eina_Bool         e_output_update(E_Output *eout);
E_API E_Output          * e_output_find(const char *id);
E_API const Eina_List   * e_output_planes_get(E_Output *eout);
E_API void                e_output_util_planes_print(void);

#endif
#endif
