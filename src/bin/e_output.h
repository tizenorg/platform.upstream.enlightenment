#ifdef E_TYPEDEFS

typedef struct _E_Output        E_Output;
typedef struct _E_Output_Mode   E_Output_Mode;
typedef enum   _E_Output_Dpms   E_OUTPUT_DPMS;
#else
#ifndef E_OUTPUT_H
#define E_OUTPUT_H

#define E_OUTPUT_TYPE (int)0xE0b11002

#include "e_comp_screen.h"
#include <Ecore_Drm.h>

enum _E_Output_Dpms
{
   E_OUTPUT_DPMS_ON,
   E_OUTPUT_DPMS_OFF,
   E_OUTPUT_DPMS_STANDBY,
   E_OUTPUT_DPMS_SUSPEND
};

struct _E_Output_Mode
{
   int    w, h; // resolution width and height
   double refresh; // refresh in hz
   Eina_Bool preferred : 1; // is this the preferred mode for the device?

   const tdm_output_mode *tmode;
};

struct _E_Output
{
   int index;
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

   int                  plane_count;
   Eina_List           *planes;
   E_Zone              *zone;

   tdm_output           *toutput;
   Ecore_Drm_Output     *output;  // for evas drm engine.

   E_Comp_Screen        *e_comp_screen;
   E_OUTPUT_DPMS        dpms;
};

EINTERN Eina_Bool         e_output_init(void);
EINTERN void              e_output_shutdown(void);
EINTERN E_Output        * e_output_new(E_Comp_Screen *e_comp_screen, int index);
EINTERN E_Output        * e_output_drm_new(Ecore_Drm_Output *output);
EINTERN void              e_output_del(E_Output *output);
EINTERN Eina_Bool         e_output_update(E_Output *output);
EINTERN Eina_Bool         e_output_drm_update(E_Output *output);
EINTERN Eina_Bool         e_output_mode_apply(E_Output *output, E_Output_Mode *mode);
EINTERN Eina_Bool         e_output_commit(E_Output *output);
EINTERN Eina_Bool         e_output_hwc_setup(E_Output *output);
EINTERN E_Output_Mode   * e_output_best_mode_find(E_Output *output);
EINTERN Eina_Bool         e_output_connected(E_Output *output);
EINTERN Eina_Bool         e_output_dpms_set(E_Output *output, E_OUTPUT_DPMS val);
E_API E_Output          * e_output_find(const char *id);
E_API const Eina_List   * e_output_planes_get(E_Output *output);
E_API void                e_output_util_planes_print(void);
E_API Eina_Bool           e_output_is_fb_composing(E_Output *output);

#endif
#endif
