#ifdef E_TYPEDEFS
#else
#ifndef E_THEME_H
#define E_THEME_H

EINTERN int         e_theme_init(void);
EINTERN int         e_theme_shutdown(void);

E_API Eina_List *e_theme_collection_items_find(const char *category, const char *grp);
E_API int         e_theme_edje_object_set(Evas_Object *o, const char *category, const char *group);
E_API const char *e_theme_edje_file_get(const char *category, const char *group);
E_API const char *e_theme_edje_icon_fallback_file_get(const char *group);

#endif
#endif
