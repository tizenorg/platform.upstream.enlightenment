#ifdef E_TYPEDEFS


#else
#ifndef E_COMP_DRM_H
#define E_COMP_DRM_H


EINTERN Eina_Bool e_comp_drm_available(void);
EINTERN void e_comp_drm_stub(void);
EINTERN void e_comp_drm_apply(void);
EINTERN E_Output * e_comp_drm_create(void);
EINTERN void e_comp_drm_dpms(int set);

E_API Eina_Bool e_comp_drm_init(void);
E_API void e_comp_drm_shutdown(void);

#endif /*E_COMP_DRM_H*/

#endif
