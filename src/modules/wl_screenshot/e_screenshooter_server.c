#define E_COMP_WL
#include "e.h"
#include <wayland-server.h>
#include <Ecore_Wayland.h>
#include <screenshooter-server-protocol.h>
#include "e_screenshooter_server.h"

static void
_e_screenshooter_shoot(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *output_resource,
                       struct wl_resource *buffer_resource)
{
   E_Comp_Wl_Output *output = wl_resource_get_user_data(output_resource);
   struct wl_shm_buffer *shm_buffer = wl_shm_buffer_get(buffer_resource);
   Eina_Rectangle center = {0,};
   int32_t bw, bh, bs;
   uint32_t bf;
   void *bp;

   if (!output || !shm_buffer)
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "screenshooter failed: wrong %s resource",
                               (!output)?"output":"buffer");
        return;
     }

   bw = wl_shm_buffer_get_width(shm_buffer);
   bh = wl_shm_buffer_get_height(shm_buffer);
   bf = wl_shm_buffer_get_format(shm_buffer);
   bp = wl_shm_buffer_get_data(shm_buffer);
   bs = wl_shm_buffer_get_stride(shm_buffer);

   if (bw != output->w || bh != output->h)
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "screenshooter failed: invalid buffer size");
        return;
     }

   /* do dump */
   wl_shm_buffer_begin_access(shm_buffer);
   evas_render_copy(e_comp->evas, bp, bs, bw, bh, bf,
                    output->x, output->y, output->w, output->h,
                    0, 0, bw, bh);
   wl_shm_buffer_end_access(shm_buffer);

   /* done */
   screenshooter_send_done(resource);
}

static const struct screenshooter_interface _e_screenshooter_interface =
{
   _e_screenshooter_shoot
};

static void
_e_screenshooter_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp_Data *cdata;
   struct wl_resource *res;

   if (!(cdata = data))
     {
        wl_client_post_no_memory(client);
        return;
     }

   if (!(res = wl_resource_create(client, &screenshooter_interface, MIN(version, 1), id)))
     {
        ERR("Could not create screenshooter resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_screenshooter_interface, cdata, NULL);
}

void*
e_screenshooter_server_init(E_Module *m)
{
   E_Comp_Data *cdata;

   if (!e_comp) return EINA_FALSE;
   if (!(cdata = e_comp->wl_comp_data)) return EINA_FALSE;
   if (cdata->available_hw_accel.scaler) return EINA_FALSE;
   if (!cdata->wl.disp) return EINA_FALSE;

   /* try to add screenshooter to wayland globals */
   if (!wl_global_create(cdata->wl.disp, &screenshooter_interface, 1,
                         cdata, _e_screenshooter_cb_bind))
     {
        ERR("Could not add screenshooter to wayland globals: %m");
        return EINA_FALSE;
     }

   return m;
}

int
e_screenshooter_server_shutdown(E_Module *m)
{
   return 1;
}
