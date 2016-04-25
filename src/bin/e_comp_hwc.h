#ifdef E_TYPEDEFS

typedef struct _E_Output                     E_Output;

#else
# ifndef E_COMP_HWC_H
#  define E_COMP_HWC_H

#define E_OUTPUT_TYPE (int)0xE0b11002

typedef enum _E_Hwc_Mode
{
   E_HWC_MODE_COMPOSITE = 1,        /* display only canvas */
   E_HWC_MODE_NO_COMPOSITE = 2,      /* display only one surface */
   E_HWC_MODE_HWC_COMPOSITE = 3,    /* display one or more surfaces and a canvas */
   E_HWC_MODE_HWC_NO_COMPOSITE = 4, /* display multi surfaces */
   E_HWC_MODE_INVALID
} E_Hwc_Mode;

struct _E_Output
{
   E_Object             e_obj_inherit;

   Eina_Rectangle       geom;
   Eina_List           *planes;
   int                  plane_count;

   E_Zone              *zone;
};

EINTERN Eina_Bool e_comp_hwc_init(void);
EINTERN void      e_comp_hwc_shutdown(void);
EINTERN Eina_Bool e_comp_hwc_mode_nocomp(E_Client *ec);
EINTERN void      e_comp_hwc_display_client(E_Client *ec);
EINTERN void      e_comp_hwc_trace_debug(Eina_Bool onoff);
Eina_Bool e_comp_hwc_find_deactivated_surface(E_Client *ec);
void      e_comp_hwc_reset_deactivated_buffer(E_Client *ec);

EINTERN Eina_Bool e_comp_hwc_plane_init(E_Zone *zone);

E_API E_Output * e_output_new(E_Zone *zone);
E_API Eina_Bool e_output_planes_clear(E_Output * output);
E_API Eina_Bool e_output_planes_set(E_Output * output, E_Hwc_Mode mode, Eina_List* clist);
E_API Eina_Bool e_output_update(E_Output * output);


# endif
#endif
