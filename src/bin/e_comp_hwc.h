#ifdef E_TYPEDEFS

#else
# ifndef E_COMP_HWC_H
#  define E_COMP_HWC_H

EINTERN Eina_Bool e_comp_hwc_init(void);
EINTERN void      e_comp_hwc_shutdown(void);
EINTERN void      e_comp_hwc_update_ec(E_Client *ec);
EINTERN E_Client *e_comp_hwc_fullscreen_check(void);
EINTERN void      e_comp_hwc_set_full_composite(char *location);

# endif
#endif
