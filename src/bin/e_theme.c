#include "e.h"

/* externally accessible functions */

EINTERN int
e_theme_init(void)
{
   return 1;
}

EINTERN int
e_theme_shutdown(void)
{
   return 1;
}

E_API int
e_theme_edje_object_set(Evas_Object *o, const char *category EINA_UNUSED, const char *group)
{
   // TODO: yigl
   return edje_object_file_set(o, "/usr/share/elementary/themes/default.edj", group);
}

E_API const char *
e_theme_edje_file_get(const char *category EINA_UNUSED, const char *group)
{
   // TODO: yigl
   return "/usr/share/elementary/themes/default.edj";
}
