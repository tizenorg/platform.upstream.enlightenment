#ifdef E_TYPEDEFS
#else
# ifndef E_COMP_HWC_H
#  define E_COMP_HWC_H

EINTERN Eina_Bool e_comp_hwc_init(void);
EINTERN void      e_comp_hwc_shutdown(void);
EINTERN Eina_Bool e_comp_hwc_mode_nocomp(E_Client *ec);
EINTERN void      e_comp_hwc_display_client(E_Client *ec);
EINTERN void      e_comp_hwc_trace_debug(Eina_Bool onoff);
EINTERN Eina_Bool e_comp_hwc_plane_init(E_Zone *zone);

Eina_Bool e_comp_hwc_find_deactivated_surface(E_Client *ec);
void      e_comp_hwc_reset_deactivated_buffer(E_Client *ec);


# endif
#endif
