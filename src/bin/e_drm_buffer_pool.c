/*
 * Copyright © 2011 Kristian Høgsberg
 * Copyright © 2011 Benjamin Franzke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Kristian Høgsberg <krh@bitplanet.net>
 *    Benjamin Franzke <benjaminfranzke@googlemail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>

#include <Ecore_Drm.h>
#include "e.h"
#include "e_drm_buffer_pool.h"
#include <tizen-extension-server-protocol.h>

#undef MIN
#define MIN(x,y) (((x)<(y))?(x):(y))

struct _E_Drm_Buffer_Pool
{
   struct wl_display *display;
   E_Drm_Buffer_Callbacks *callbacks;
   void *user_data;

   Eina_List *format_list;
   struct wl_global *wl_global;
};

/* TODO: what's better way to get device_name? */
static const char*
get_device_name(void)
{
   Eina_List *devs = ecore_drm_devices_get();
   Ecore_Drm_Device *dev;

   if (!devs)
     return NULL;

   dev = eina_list_nth(devs, 0);

   return ecore_drm_device_name_get(dev);
}

static void
destroy_drm_buffer(struct wl_resource *resource)
{
   E_Drm_Buffer *buffer = wl_resource_get_user_data(resource);
   E_Drm_Buffer_Pool *pool = buffer->pool;

   pool->callbacks->release_buffer(pool->user_data, buffer);
   free(buffer);
}

static void
_e_drm_buffer_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct wl_buffer_interface _e_drm_buffer_interface = {
   _e_drm_buffer_cb_destroy
};

static void
create_drm_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                  int32_t width, int32_t height, uint32_t format,
                  uint32_t name0, int32_t offset0, int32_t stride0,
                  uint32_t name1, int32_t offset1, int32_t stride1,
                  uint32_t name2, int32_t offset2, int32_t stride2)
{
   E_Drm_Buffer_Pool *pool = wl_resource_get_user_data(resource);
   E_Drm_Buffer *buffer;
   void *fmt;
   Eina_List *f;
   Eina_Bool found = EINA_FALSE;

   EINA_LIST_FOREACH(pool->format_list, f, fmt)
     {
        if (format == (uint)(uintptr_t)fmt)
          {
            found = EINA_TRUE;
            break;
          }
     }

   if (!found)
     {
        wl_resource_post_error(resource, TIZEN_BUFFER_POOL_ERROR_INVALID_FORMAT, "invalid format");
        return;
     }

   buffer = calloc(1, sizeof *buffer);
   if (buffer == NULL)
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   buffer->resource = wl_resource_create(client, &wl_buffer_interface, 1, id);
   if (!buffer->resource)
     {
        wl_resource_post_no_memory(resource);
        free(buffer);
        return;
     }

   wl_resource_set_implementation(buffer->resource, &_e_drm_buffer_interface,
                                  buffer, destroy_drm_buffer);

   buffer->width = width;
   buffer->height = height;
   buffer->format = format;
   buffer->name[0] = name0;
   buffer->offset[0] = offset0;
   buffer->stride[0] = stride0;
   buffer->name[1] = name1;
   buffer->offset[1] = offset1;
   buffer->stride[1] = stride1;
   buffer->name[2] = name2;
   buffer->offset[2] = offset2;
   buffer->stride[2] = stride2;
   buffer->pool = pool;

   pool->callbacks->reference_buffer(pool->user_data, buffer);
   if (buffer->driver_buffer == NULL)
     {
        wl_resource_post_error(resource, TIZEN_BUFFER_POOL_ERROR_INVALID_NAME, "invalid name");
        free(buffer);
        return;
     }
}

static void
_e_drm_buffer_pool_authenticate(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   tizen_buffer_pool_send_authenticated(resource);
}

static void
_e_drm_buffer_pool_create_buffer(struct wl_client *client,
                                 struct wl_resource *resource, uint32_t id, uint32_t name,
                                 int32_t width, int32_t height, uint32_t stride, uint32_t format)
{
   create_drm_buffer(client, resource, id, width, height, format, name, 0, stride,
                     0, 0, 0, 0, 0, 0);
}

static void
_e_drm_buffer_pool_create_planar_buffer(struct wl_client *client,
                                        struct wl_resource *resource, uint32_t id,
                                        int32_t width, int32_t height, uint32_t format,
                                        uint32_t name0, int32_t offset0, int32_t stride0,
                                        uint32_t name1, int32_t offset1, int32_t stride1,
                                        uint32_t name2, int32_t offset2, int32_t stride2)
{
   create_drm_buffer(client, resource, id, width, height, format, name0, offset0, stride0,
                     name1, offset1, stride1, name2, offset2, stride2);
}

static const struct tizen_buffer_pool_interface _e_drm_buffer_pool_interface =
{
   _e_drm_buffer_pool_authenticate,
   _e_drm_buffer_pool_create_buffer,
   _e_drm_buffer_pool_create_planar_buffer
};

static void
_e_drm_buffer_pool_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Drm_Buffer_Pool *pool = data;
   struct wl_resource *res;
   const char *device_name = get_device_name();
   uint caps = 0;
   int i, fmt_cnt = 0;
   uint *fmts;

   if (!(res = wl_resource_create(client, &tizen_buffer_pool_interface, MIN(version, 1), id)))
     {
        ERR("Could not create tizen_screenshooter resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_drm_buffer_pool_interface, pool, NULL);

   if (pool->callbacks->get_capbilities)
     caps = pool->callbacks->get_capbilities(pool->user_data);

   fmts = pool->callbacks->get_formats(pool->user_data, &fmt_cnt);

   tizen_buffer_pool_send_device(res, device_name ? device_name : "unkown");
   tizen_buffer_pool_send_capabilities(res, caps);
   for (i = 0; i < fmt_cnt; i++)
     {
        pool->format_list = eina_list_append(pool->format_list, (void*)(uintptr_t)fmts[i]);
        tizen_buffer_pool_send_format(res, fmts[i]);
     }
   free(fmts);
}

EAPI int
e_drm_buffer_pool_init(struct wl_display *display, E_Drm_Buffer_Callbacks *callbacks, void *user_data)
{
   E_Drm_Buffer_Pool *pool;

   EINA_SAFETY_ON_NULL_RETURN_VAL(display, 0);
   EINA_SAFETY_ON_NULL_RETURN_VAL(callbacks, 0);
   EINA_SAFETY_ON_NULL_RETURN_VAL(callbacks->get_formats, 0);
   EINA_SAFETY_ON_NULL_RETURN_VAL(callbacks->reference_buffer, 0);
   EINA_SAFETY_ON_NULL_RETURN_VAL(callbacks->release_buffer, 0);

   pool = malloc(sizeof *pool);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pool, 0);

   pool->display = display;
   pool->callbacks = callbacks;
   pool->user_data = user_data;

   pool->format_list = NULL;
   pool->wl_global = wl_global_create(display, &tizen_buffer_pool_interface, 1,
                                      pool, _e_drm_buffer_pool_cb_bind);
   if (!pool->wl_global)
     {
        free (pool);
        return 0;
     }

   return 1;
}

EAPI E_Drm_Buffer*
e_drm_buffer_get(struct wl_resource *resource)
{
   if (resource == NULL)
     return NULL;

   if (wl_resource_instance_of(resource, &wl_buffer_interface, &_e_drm_buffer_interface))
     return wl_resource_get_user_data(resource);
   else
     return NULL;
}
