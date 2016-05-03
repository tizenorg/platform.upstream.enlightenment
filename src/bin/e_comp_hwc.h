#ifdef E_TYPEDEFS

#else
# ifndef E_COMP_HWC_H
#  define E_COMP_HWC_H

typedef enum _E_Hwc_Mode
{
   E_HWC_MODE_COMPOSITE = 1,        /* display only canvas */
   E_HWC_MODE_NO_COMPOSITE = 2,      /* display only one surface */
   E_HWC_MODE_HWC_COMPOSITE = 3,    /* display one or more surfaces and a canvas */
   E_HWC_MODE_HWC_NO_COMPOSITE = 4, /* display multi surfaces */
   E_HWC_MODE_INVALID
} E_Hwc_Mode;

EINTERN Eina_Bool e_comp_hwc_init(void);
EINTERN void      e_comp_hwc_shutdown(void);
EINTERN Eina_Bool e_comp_hwc_mode_nocomp(E_Client *ec);
EINTERN void      e_comp_hwc_display_client(E_Client *ec);
EINTERN void      e_comp_hwc_trace_debug(Eina_Bool onoff);
Eina_Bool         e_comp_hwc_native_surface_set(E_Client *ec);
void              e_comp_hwc_client_commit(E_Client *ec);
# endif
#endif
