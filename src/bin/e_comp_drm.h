#ifdef E_TYPEDEFS


#else
#ifndef E_COMP_DRM_H
#define E_COMP_DRM_H


EINTERN Eina_Bool       e_comp_screen_available(void);
EINTERN void            e_comp_screen_stub(void);
EINTERN void            e_comp_screen_apply(void);
EINTERN E_Comp_Screen * e_comp_screen_init_outputs(void);
EINTERN void            e_comp_screen_dpms(int set);

E_API Eina_Bool         e_comp_screen_init(void);
E_API void              e_comp_screen_shutdown(void);

#endif /*E_COMP_DRM_H*/

#endif
