#include "e.h"
#ifdef HAVE_WAYLAND
#include <Ecore_Drm.h>
#endif

EAPI int
e_mouse_update(void)
{
   unsigned char map[256] = { 0 };
   int n;

#ifndef HAVE_WAYLAND_ONLY
   if (e_comp->comp_type == E_PIXMAP_TYPE_X)
     {
        if (!ecore_x_pointer_control_set(e_config->mouse_accel_numerator,
                                         e_config->mouse_accel_denominator,
                                         e_config->mouse_accel_threshold))
          return 0;

        if (!ecore_x_pointer_mapping_get(map, 256)) return 0;
     }
#endif

   for (n = 0; n < 256; n++)
     {
        if (!map[n]) break;
     }
   if (n < 3)
     {
        map[0] = 1;
        map[1] = 2;
        map[2] = 3;
        n = 3;
     }
   if (e_config->mouse_hand == E_MOUSE_HAND_RIGHT)
     {
        map[0] = 1;
        map[2] = 3;
     }
   else if (e_config->mouse_hand == E_MOUSE_HAND_LEFT)
     {
        map[0] = 3;
        map[2] = 1;
     }

#ifndef HAVE_WAYLAND_ONLY
   if (e_comp->comp_type == E_PIXMAP_TYPE_X)
     if (!ecore_x_pointer_mapping_set(map, n)) return 0;
#endif

   return 1;
}

EAPI int
e_mouse_left_handed_set(E_Mouse_Hand mouse_hand)
{
#ifdef HAVE_WAYLAND
   Eina_List *list, *l;
   Ecore_Drm_Device *dev;

   e_config->mouse_hand = mouse_hand;

   list = ecore_drm_devices_get();
   EINA_LIST_FOREACH(list, l, dev)
     {
        if (!dev) continue;

        ecore_drm_device_left_handed_set(dev, !e_config->mouse_hand);
     }

   e_config_save_queue();
#endif
#ifndef HAVE_WAYLAND_ONLY
   e_mouse_update();
#endif
   return 1;
}
