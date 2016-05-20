#ifdef E_TYPEDEFS

//typedef struct _E_Output        E_Output;
typedef struct _E_Output        E_Output;
typedef struct _E_Output_Screen E_Output_Screen;
typedef struct _E_Output_Mode   E_Output_Mode;
typedef struct _E_Screen        E_Screen;

#else
#ifndef E_OUTPUT_H
#define E_OUTPUT_H

#define E_OUTPUT_TYPE (int)0xE0b11002

struct _E_Output
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
   } config;

   int                  plane_count;
   Eina_List           *planes;
   E_Zone              *zone;
};

struct _E_Screen
{
   int screen, escreen;
   int x, y, w, h;
   char *id; // this is the same id we get from _E_Output_Screen so look it up there
};

extern E_API E_Output *e_output;
extern E_API int E_EVENT_SCREEN_CHANGE;

EINTERN Eina_Bool e_output_init(void);
EINTERN int       e_output_shutdown(void);
EINTERN E_Output_Screen * e_output_screen_new(E_Zone *zone, int nlayer);
EINTERN E_Output_Screen * e_output_screen_id_find(const char *id);

/// regarding hwc
EINTERN Eina_Bool         e_output_screen_need_change(void);
EINTERN Eina_Bool         e_output_screen_clear(E_Output_Screen * screen);
EINTERN void              e_output_screens_setup(int rw, int rh);
EINTERN Eina_Bool         e_output_screen_apply(E_Output_Screen * screen);


E_API   const Eina_List * e_output_screens_get(void);
E_API   Eina_Bool         e_output_planes_prepare(E_Output_Screen * screen, E_Hwc_Mode mode, Eina_List* clist); // going to remove out
E_API   Eina_Bool         e_output_util_planes_print(void);

#endif
#endif
