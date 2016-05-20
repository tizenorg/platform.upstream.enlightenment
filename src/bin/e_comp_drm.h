#ifdef E_TYPEDEFS


#else
#ifndef E_COMP_DRM_H
#define E_COMP_DRM_H


EINTERN Eina_Bool       e_comp_drm_tdm_available(void);
EINTERN void            e_comp_drm_tdm_stub(void);
EINTERN void            e_comp_drm_tdm_apply(void);
EINTERN E_Comp_Tdm    * e_comp_drm_tdm_init(void);
EINTERN void            e_comp_drm_tdm_dpms(int set);

E_API Eina_Bool         e_comp_drm_init(void);
E_API void              e_comp_drm_shutdown(void);

#endif /*E_COMP_DRM_H*/

#endif
