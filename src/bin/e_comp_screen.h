#ifdef E_TYPEDEFS


#else
#ifndef E_COMP_SCREEN_H
#define E_COMP_SCREEN_H


EINTERN void            e_comp_screen_apply(void);
EINTERN E_Comp_Screen * e_comp_screen_init_outputs(void);

E_API Eina_Bool         e_comp_screen_init(void);
E_API void              e_comp_screen_shutdown(void);

#endif /*E_COMP_SCREEN_H*/

#endif
