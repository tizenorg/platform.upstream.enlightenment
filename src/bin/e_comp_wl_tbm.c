#define E_COMP_WL
#include "e.h"
#include <Ecore_Drm.h>
#include <wayland-tbm-server.h>

EINTERN Eina_Bool
e_comp_wl_tbm_init(void)
{
   struct wayland_tbm_server *tbm_server = NULL;
   const Eina_List *devs;
   Ecore_Drm_Device *dev;
   int drm_fd = -1;
   const char *dev_name;

   if (!e_comp)
     {
        e_error_message_show(_("Enlightenment cannot has no e_comp at Wayland TBM!\n"));
        return EINA_FALSE;
     }

   EINA_SAFETY_ON_FALSE_RETURN_VAL(e_comp->wl_comp_data->wl.disp, EINA_FALSE);

   if (e_comp->wl_comp_data->tbm.server)
      return EINA_TRUE;

   devs = ecore_drm_devices_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(devs, EINA_FALSE);

   dev = eina_list_nth(devs, 0);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, EINA_FALSE);

   drm_fd = ecore_drm_device_fd_get(dev);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(drm_fd >= 0, EINA_FALSE);

   dev_name = ecore_drm_device_name_get(dev);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(dev_name, EINA_FALSE);

   tbm_server = wayland_tbm_server_init(e_comp->wl_comp_data->wl.disp, dev_name, drm_fd, 0);
   if (!tbm_server)
     {
        e_error_message_show(_("Enlightenment cannot initialize a Wayland TBM!\n"));
        return EINA_FALSE;
     }

   e_comp->wl_comp_data->tbm.server = (void *)tbm_server;

   return EINA_TRUE;
}

EINTERN void
e_comp_wl_tbm_shutdown(void)
{
   if (!e_comp)
      return;

   if (!e_comp->wl_comp_data)
      return;

   if (!e_comp->wl_comp_data->tbm.server)
      return;

   wayland_tbm_server_deinit((struct wayland_tbm_server *)e_comp->wl_comp_data->tbm.server);

   e_comp->wl_comp_data->tbm.server = NULL;
}


