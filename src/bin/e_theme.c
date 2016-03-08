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

E_API Eina_List *
e_theme_collection_items_find(const char *base EINA_UNUSED, const char *collname)
{
   // TODO: should be removed - yigl
#if 0
   Eina_List *list = NULL, *list2 = NULL, *l;
   const char *s, *s2;
   int len = strlen(collname);

   // FIXME
   //list = elm_theme_group_base_list(NULL, collname);
   if (!list) return NULL;
   EINA_LIST_FREE(list, s)
     {
        char *trans, *p, *p2;
        size_t slen;

        slen = strlen(s);
        trans = memcpy(alloca(slen + 1), s, slen + 1);
        p = trans + len + 1;
        if (*p)
          {
             p2 = strchr(p, '/');
             if (p2) *p2 = 0;
             EINA_LIST_FOREACH(list2, l, s2)
               {
                  if (!strcmp(s2, p)) break;
               }
             if (!l) list2 = eina_list_append(list2, eina_stringshare_add(p));
          }
     }
   return list2;
#else
   return NULL;
#endif
}

E_API int
e_theme_edje_object_set(Evas_Object *o, const char *category EINA_UNUSED, const char *group)
{
   // TODO: yigl
#if 0
   const char *file = NULL;
   file = elm_theme_group_path_find(NULL, group);
   if (!file) return 0;
   edje_object_file_set(o, file, group);
   return 1;
#else
   edje_object_file_set(o, "/usr/share/elementary/themes/default.edj", group);
   //Eina_Bool res = edje_object_file_set(o, "/var/lib/enlightenment/.e/e/themes/default.edj", group);
   //printf("[yigl] %s group:%s res:%d\n", __FUNCTION__, group, res);
   return 1;
#endif
}

E_API const char *
e_theme_edje_file_get(const char *category EINA_UNUSED, const char *group)
{
   // TODO: yigl
#if 0
   const char *file = elm_theme_group_path_find(NULL, group);
   if (!file) return "";
   return file;
#else
   return "/usr/share/elementary/themes/default.edj";
   //return "/var/lib/enlightenment/.e/e/themes/default.edj";
#endif
}

E_API const char *
e_theme_edje_icon_fallback_file_get(const char *group)
{
   // TODO: yigl
#if 0
   const char *file = NULL;
   if ((e_config->icon_theme) && (!strncmp(group, "e/icons", 7))) return "";
   file = elm_theme_group_path_find(NULL, group);
   if (!file) return "";
   return file;
#else
   return "/usr/share/elementary/themes/default.edj";
#endif
}
