#define E_COMP_WL
#include "e.h"
#include <wayland-server.h>
#include <Ecore_Wayland.h>
#include "e_screenshooter_server_protocol.h"
#include "e_screenshooter_server.h"

static void
_e_screenshooter_center_rect (int src_w, int src_h, int dst_w, int dst_h, Eina_Rectangle *fit)
{
   float rw, rh;

   if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 || !fit)
     return;

   rw = (float)src_w / dst_w;
   rh = (float)src_h / dst_h;

   if (rw > rh)
     {
        fit->w = dst_w;
        fit->h = src_h * rw;
        fit->x = 0;
        fit->y = (dst_h - fit->h) / 2;
     }
   else if (rw < rh)
     {
        fit->w = src_w * rw;
        fit->h = dst_h;
        fit->x = (dst_w - fit->w) / 2;
        fit->y = 0;
     }
   else
     {
        fit->w = dst_w;
        fit->h = dst_h;
        fit->x = 0;
        fit->y = 0;
     }
}

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

   _e_screenshooter_center_rect(output->w, output->h, bw, bh, &center);
printf ("@@@ %s(%d) %d, %d, %d, %x, s(%d,%d,%d,%d) d(%d,%d,%d,%d)\n", __FUNCTION__, __LINE__,
	bs, bw, bh, bf,
                    output->x, output->y, output->w, output->h,
                    center.x, center.y, center.w, center.h);
   /* do dump */
   wl_shm_buffer_begin_access(shm_buffer);
   evas_render_copy(e_comp->evas, bp, bs, bw, bh, bf,
                    output->x, output->y, output->w, output->h,
                    center.x, center.y, center.w, center.h);
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
