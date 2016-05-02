#ifdef E_TYPEDEFS

typedef struct _E_Drm_Output        E_Drm_Output;
typedef struct _E_Output_Screen E_Output_Screen;
typedef struct _E_Output_Mode   E_Output_Mode;

#else
#ifndef E_OUTPUT_H
#define E_OUTPUT_H

struct _E_Drm_Output
{
   Eina_List *screens; // available screens
   int        w, h; // virtual resolution needed for screens (calculated)
   unsigned char  ignore_hotplug_events;
   unsigned char  ignore_acpi_events; 
};

struct _E_Output_Mode
{
   int    w, h; // resolution width and height
   double refresh; // refresh in hz
   Eina_Bool preferred : 1; // is this the preferred mode for the device?
};

struct _E_Output_Screen
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
      Eina_Bool             configured : 1; // has screen been configured by e?
   } config;
   Eina_List           *planes;
   int                  plane_count;
   E_Zone              *zone;
};

extern E_API E_Drm_Output *e_drm_output;
extern E_API int E_EVENT_SCREEN_CHANGE;
extern E_API int E_EVENT_RANDR_CHANGE; // x randr

EINTERN Eina_Bool e_drm_output_init(void);
EINTERN int       e_drm_output_shutdown(void);
E_API    void      e_drm_output_config_apply(void);
E_API    void      e_drm_output_screeninfo_update(void);
E_API void e_drm_output_screen_refresh_queue(Eina_Bool lid_event);
E_API void e_drm_output_screens_setup(int rw, int rh);

#endif
#endif
