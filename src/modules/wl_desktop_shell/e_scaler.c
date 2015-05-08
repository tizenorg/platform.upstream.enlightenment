#define EXECUTIVE_MODE_ENABLED
#define E_COMP_WL
#include "e.h"
#include "e_scaler_protocol.h"

static void
_e_viewport_destroy(struct wl_resource *resource)
{
   E_Pixmap *ep = wl_resource_get_user_data(resource);
   E_Comp_Client_Data *cdata = e_pixmap_cdata_get(ep);

   EINA_SAFETY_ON_NULL_RETURN(cdata);
   EINA_SAFETY_ON_NULL_RETURN(cdata->scaler.viewport);

   cdata->scaler.viewport = NULL;
   cdata->pending.buffer_viewport.buffer.src_width = wl_fixed_from_int(-1);
   cdata->pending.buffer_viewport.surface.width = -1;
   cdata->pending.buffer_viewport.changed = 1;
}

static void
_e_viewport_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_viewport_cb_set(struct wl_client *client EINA_UNUSED,
                   struct wl_resource *resource,
                   wl_fixed_t src_x,
                   wl_fixed_t src_y,
                   wl_fixed_t src_width,
                   wl_fixed_t src_height,
                   int32_t dst_width,
                   int32_t dst_height)
{
   E_Pixmap *ep = wl_resource_get_user_data(resource);
   E_Comp_Client_Data *cdata = e_pixmap_cdata_get(ep);

   EINA_SAFETY_ON_NULL_RETURN(cdata);
   EINA_SAFETY_ON_NULL_RETURN(cdata->scaler.viewport);

   if (wl_fixed_to_double(src_width) < 0 || wl_fixed_to_double(src_height) < 0)
     {
        wl_resource_post_error(resource,
                               WL_VIEWPORT_ERROR_BAD_VALUE,
                               "source dimensions must be non-negative (%fx%f)",
                               wl_fixed_to_double(src_width),
                               wl_fixed_to_double(src_height));
        return;
     }

   if (dst_width <= 0 || dst_height <= 0)
     {
        wl_resource_post_error(resource,
                               WL_VIEWPORT_ERROR_BAD_VALUE,
                               "destination dimensions must be positive (%dx%d)",
                               dst_width, dst_height);
        return;
     }

   cdata->pending.buffer_viewport.buffer.src_x = src_x;
   cdata->pending.buffer_viewport.buffer.src_y = src_y;
   cdata->pending.buffer_viewport.buffer.src_width = src_width;
   cdata->pending.buffer_viewport.buffer.src_height = src_height;
   cdata->pending.buffer_viewport.surface.width = dst_width;
   cdata->pending.buffer_viewport.surface.height = dst_height;
   cdata->pending.buffer_viewport.changed = 1;
}

static void
_e_viewport_cb_set_source(struct wl_client *client EINA_UNUSED,
                          struct wl_resource *resource,
                          wl_fixed_t src_x,
                          wl_fixed_t src_y,
                          wl_fixed_t src_width,
                          wl_fixed_t src_height)
{
   E_Pixmap *ep = wl_resource_get_user_data(resource);
   E_Comp_Client_Data *cdata = e_pixmap_cdata_get(ep);

   EINA_SAFETY_ON_NULL_RETURN(cdata);
   EINA_SAFETY_ON_NULL_RETURN(cdata->scaler.viewport);

   if (src_width == wl_fixed_from_int(-1) && src_height == wl_fixed_from_int(-1))
     {
        /* unset source size */
        cdata->pending.buffer_viewport.buffer.src_width = wl_fixed_from_int(-1);
        cdata->pending.buffer_viewport.changed = 1;
        return;
     }

   if (src_width <= 0 || src_height <= 0)
     {
        wl_resource_post_error(resource,
                               WL_VIEWPORT_ERROR_BAD_VALUE,
                               "source size must be positive (%fx%f)",
                               wl_fixed_to_double(src_width),
                               wl_fixed_to_double(src_height));
        return;
     }

   cdata->pending.buffer_viewport.buffer.src_x = src_x;
   cdata->pending.buffer_viewport.buffer.src_y = src_y;
   cdata->pending.buffer_viewport.buffer.src_width = src_width;
   cdata->pending.buffer_viewport.buffer.src_height = src_height;
   cdata->pending.buffer_viewport.changed = 1;
}

static void
_e_viewport_cb_set_destination(struct wl_client *client EINA_UNUSED,
                               struct wl_resource *resource,
                               int32_t dst_width,
                               int32_t dst_height)
{
   E_Pixmap *ep = wl_resource_get_user_data(resource);
   E_Comp_Client_Data *cdata = e_pixmap_cdata_get(ep);

   EINA_SAFETY_ON_NULL_RETURN(cdata);
   EINA_SAFETY_ON_NULL_RETURN(cdata->scaler.viewport);

   if (dst_width == -1 && dst_height == -1)
     {
        /* unset destination size */
        cdata->pending.buffer_viewport.surface.width = -1;
        cdata->pending.buffer_viewport.changed = 1;
        return;
     }

   if (dst_width <= 0 || dst_height <= 0)
     {
        wl_resource_post_error(resource,
                               WL_VIEWPORT_ERROR_BAD_VALUE,
                               "destination size must be positive (%dx%d)",
                               dst_width, dst_height);
        return;
     }

   cdata->pending.buffer_viewport.surface.width = dst_width;
   cdata->pending.buffer_viewport.surface.height = dst_height;
   cdata->pending.buffer_viewport.changed = 1;
}

static const struct wl_viewport_interface _e_viewport_interface = {
   _e_viewport_cb_destroy,
   _e_viewport_cb_set,
   _e_viewport_cb_set_source,
   _e_viewport_cb_set_destination
};

static void
_e_scaler_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_scaler_cb_get_viewport(struct wl_client *client EINA_UNUSED, struct wl_resource *scaler, uint32_t id, struct wl_resource *surface_resource)
{
   int version = wl_resource_get_version(scaler);
   E_Pixmap *ep;
   struct wl_resource *res;
   E_Comp_Client_Data *cdata;

   if (!(ep = wl_resource_get_user_data(surface_resource))) return;
   if (!(cdata = e_pixmap_cdata_get(ep))) return;

   if (cdata->scaler.viewport)
     {
        wl_resource_post_error(scaler,
                               WL_SCALER_ERROR_VIEWPORT_EXISTS,
                               "a viewport for that surface already exists");
        return;
     }

   res = wl_resource_create(client, &wl_viewport_interface, version, id);
   if (res == NULL)
     {
        wl_client_post_no_memory(client);
        return;
     }

   cdata->scaler.viewport = res;
   wl_resource_set_implementation(res, &_e_viewport_interface, ep, _e_viewport_destroy);
}

static const struct wl_scaler_interface _e_scaler_interface =
{
   _e_scaler_cb_destroy,
   _e_scaler_cb_get_viewport
};

static void
_e_scaler_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;

   if (!(res = wl_resource_create(client, &wl_scaler_interface, MIN(version, 2), id)))
     {
        ERR("Could not create scaler resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_scaler_interface, NULL, NULL);
}

Eina_Bool
e_scaler_init(void)
{
   E_Comp_Data *cdata;

   if (!e_comp) return EINA_FALSE;
   if (!(cdata = e_comp->wl_comp_data)) return EINA_FALSE;
   if (!cdata->wl.disp) return EINA_FALSE;

   /* try to add scaler to wayland globals */
   if (!wl_global_create(cdata->wl.disp, &wl_scaler_interface, 2,
                         cdata, _e_scaler_cb_bind))
     {
        ERR("Could not add scaler to wayland globals: %m");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}
