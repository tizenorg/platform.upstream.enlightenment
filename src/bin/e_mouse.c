#include "e.h"
#ifdef HAVE_WL_DRM
#include <Ecore_Drm.h>
#endif

E_API int
e_mouse_update(void)
{
#ifdef HAVE_WL_DRM
   if (strstr(ecore_evas_engine_name_get(e_comp->ee), "drm"))
     {
        const Eina_List *list, *l;
        Ecore_Drm_Device *dev;

        list = ecore_drm_devices_get();
        EINA_LIST_FOREACH(list, l, dev)
          {
             ecore_drm_device_pointer_left_handed_set(dev, (Eina_Bool)!e_config->mouse_hand);
          }
     }
#endif
   return 1;
}
