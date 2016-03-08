#ifdef E_TYPEDEFS
#else
#ifndef E_THEME_H
#define E_THEME_H

EINTERN int       e_theme_init(void);
EINTERN int       e_theme_shutdown(void);
E_API int         e_theme_edje_object_set(Evas_Object *o, const char *category, const char *group);
E_API const char *e_theme_edje_file_get(const char *category, const char *group);

#endif
#endif
