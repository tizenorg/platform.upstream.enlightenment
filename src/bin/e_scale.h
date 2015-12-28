#ifdef E_TYPEDEFS
#else
#ifndef E_SCALE_H
#define E_SCALE_H

EINTERN int  e_scale_init(void);
EINTERN int  e_scale_shutdown(void);
<<<<<<< HEAD
EAPI void e_scale_update(void);
EAPI void e_scale_manual_update(int dpi);
=======
E_API void e_scale_update(void);
>>>>>>> upstream

extern E_API double e_scale;

#endif
#endif
