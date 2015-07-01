#include "e.h"

EAPI double e_scale = 1.0;

EINTERN int
e_scale_init(void)
{
   e_scale_update();
   return 1;
}

EINTERN int
e_scale_shutdown(void)
{
   return 1;
}

EAPI void
e_scale_update(void)
{
   char buf[128];

   if (e_config->scale.use_dpi)
     {
#ifndef HAVE_WAYLAND_ONLY
        if (e_comp->comp_type == E_PIXMAP_TYPE_X)
          e_scale = (double)ecore_x_dpi_get() / (double)e_config->scale.base_dpi;
#else
        if (e_comp->comp_type == E_PIXMAP_TYPE_WL)
          e_scale = (double)ecore_wl_dpi_get() / (double)e_config->scale.base_dpi;
#endif
        if (e_scale > e_config->scale.max) e_scale = e_config->scale.max;
        else if (e_scale < e_config->scale.min)
          e_scale = e_config->scale.min;
     }
   else if (e_config->scale.use_custom)
     {
        e_scale = e_config->scale.factor;
        if (e_scale > e_config->scale.max) e_scale = e_config->scale.max;
        else if (e_scale < e_config->scale.min)
          e_scale = e_config->scale.min;
     }
   elm_config_scale_set(e_scale);
   elm_config_all_flush();
   edje_scale_set(e_scale);
   snprintf(buf, sizeof(buf), "%1.3f", e_scale);
   e_util_env_set("E_SCALE", buf);
   e_hints_scale_update();
}

EAPI void
e_scale_manual_update(int dpi)
{
   char buf[128];

   e_scale = (double)dpi / (double)e_config->scale.base_dpi;

   if (e_scale > e_config->scale.max) e_scale = e_config->scale.max;
   else if (e_scale < e_config->scale.min)
     e_scale = e_config->scale.min;

   elm_config_scale_set(e_scale);
   elm_config_all_flush();
   edje_scale_set(e_scale);
   snprintf(buf, sizeof(buf), "%1.3f", e_scale);
   e_util_env_set("E_SCALE", buf);
   e_hints_scale_update();
}

