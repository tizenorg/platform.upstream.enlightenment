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
E_Client *e_comp_hwc_get_deactivated_ec();
void e_comp_hwc_reset_deactivated_ec();
void *e_comp_hwc_get_copied_surface();
E_Comp_Wl_Buffer *e_comp_hwc_get_deactivated_buffer();
Eina_Bool e_comp_hwc_check_buffer_scanout(E_Client *ec);
void e_comp_hwc_reset_all_deactivated_info(void);


# endif
#endif
