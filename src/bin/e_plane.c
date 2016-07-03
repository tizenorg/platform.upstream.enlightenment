#include "e.h"

# include <gbm/gbm_tbm.h>
# include <tdm.h>
# include <tdm_helper.h>
# include <tbm_surface.h>
# include <tbm_surface_internal.h>
# include <wayland-tbm-server.h>
# include <Evas_Engine_GL_Drm.h>

# ifndef CLEAR
# define CLEAR(x) memset(&(x), 0, sizeof (x))
# endif

# define E_PLANE_CLIENT_SURFACE_FLAGS_RESERVED 7777

typedef struct _E_Plane_Client E_Plane_Client;

struct _E_Plane_Client
{
   E_Client *ec;

   E_Plane *plane;
   Eina_Bool activated;

   E_Comp_Wl_Buffer *buffer;
   struct wl_listener buffer_destroy_listener;

   Eina_List *exported_surfaces;
};

/* E_Plane is a child object of E_Output. There is one Output per screen
 * E_plane represents hw overlay and a surface is assigned to disable composition
 * Each Output always has dedicated canvas and a zone
 */
///////////////////////////////////////////
static const char *_e_plane_ec_last_err = NULL;
static E_Client_Hook *client_hook_new = NULL;
static E_Client_Hook *client_hook_del = NULL;
static Eina_Hash *plane_clients = NULL;
static Eina_List *plane_hdlrs = NULL;
static Eina_Bool plane_trace_debug = 0;

#if HAVE_MEMCPY_SWC
extern void *memcpy_swc(void *dest, const void *src, size_t n);
#endif

static struct wl_resource *
_get_wl_buffer(E_Client *ec)
{
   E_Pixmap *pixmap = ec->pixmap;
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);

   if (!buffer) return NULL;

   return buffer->resource;
}

static struct wl_resource *
_get_wl_buffer_ref(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   if (!cdata) return NULL;

   E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata ->buffer_ref;

   if (!buffer_ref->buffer) return NULL;

   return buffer_ref->buffer->resource;
}

static struct wl_resource *
_e_plane_wl_surface_get(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cdata = NULL;
   struct wl_resource *wl_surface = NULL;

   cdata = (E_Comp_Wl_Client_Data *)e_pixmap_cdata_get(ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cdata, NULL);

   wl_surface = cdata->wl_surface;
   if (!wl_surface) return NULL;

   return wl_surface;
}

struct wayland_tbm_client_queue *
_e_plane_wayland_tbm_client_queue_get(E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_surface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_comp_data, NULL);

   wl_surface = _e_plane_wl_surface_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_surface, NULL);

   cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, NULL);

   return cqueue;
}

static E_Plane_Client *
_e_plane_client_new(E_Client *ec)
{
   E_Plane_Client *plane_client = NULL;

   plane_client = E_NEW(E_Plane_Client, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane_client, NULL);

   plane_client->ec = ec;
   plane_client->plane = NULL;
   plane_client->exported_surfaces = NULL;

   return plane_client;
}

static E_Plane_Client *
_e_plane_client_get(E_Client *ec)
{
   E_Plane_Client *plane_client = NULL;

   plane_client = eina_hash_find(plane_clients, &ec);

   return plane_client;
}

static void
_e_plane_client_cb_new(void *data EINA_UNUSED, E_Client *ec)
{
   E_Plane_Client *plane_client = NULL;

   plane_client = _e_plane_client_get(ec);
   if (!plane_client)
     {
        plane_client = _e_plane_client_new(ec);
        if (plane_client)
           eina_hash_add(plane_clients, &ec, plane_client);
     }
}

static void
_e_plane_client_cb_del(void *data EINA_UNUSED, E_Client *ec)
{
   E_Plane_Client *plane_client = NULL;

   plane_client = _e_plane_client_get(ec);
   if (plane_client)
     {
        /* destroy the plane_client */
        eina_hash_del_by_key(plane_clients, &ec);
     }
}

static tbm_surface_h
_e_plane_copied_surface_create(E_Client *ec, Eina_Bool refresh)
{
   tbm_surface_h tsurface = NULL;
   tbm_surface_h new_tsurface = NULL;
   E_Pixmap *pixmap = NULL;
   E_Comp_Wl_Buffer *buffer = NULL;
   tbm_surface_info_s src_info, dst_info;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;

   pixmap = ec->pixmap;

   if (refresh)
     e_pixmap_image_refresh(ec->pixmap);

   buffer = e_pixmap_resource_get(pixmap);
   if (!buffer) return NULL;

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, NULL);

   tbm_surface_map(tsurface, TBM_SURF_OPTION_READ, &src_info);
   tbm_surface_unmap(tsurface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(src_info.planes[0].ptr, NULL);

   new_tsurface = tbm_surface_create(src_info.width, src_info.height, src_info.format);

   tbm_surface_map(new_tsurface, TBM_SURF_OPTION_WRITE, &dst_info);
   tbm_surface_unmap(new_tsurface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dst_info.planes[0].ptr, NULL);

   /* copy from src to dst */
#if HAVE_MEMCPY_SWC
   memcpy_swc(dst_info.planes[0].ptr, src_info.planes[0].ptr, src_info.planes[0].size);
#else
   memcpy(dst_info.planes[0].ptr, src_info.planes[0].ptr, src_info.planes[0].size);
#endif

   return new_tsurface;
}

static void
_e_plane_copied_surface_destroy(tbm_surface_h tbm_surface)
{
   EINA_SAFETY_ON_NULL_RETURN(tbm_surface);

   tbm_surface_internal_unref(tbm_surface);
}

static void
_e_plane_client_backup_buffer_cb_destroy(struct wl_listener *listener, void *data)
{
   E_Plane_Client *plane_client = NULL;
   E_Client *ec = NULL;

   plane_client = container_of(listener, E_Plane_Client, buffer_destroy_listener);
   EINA_SAFETY_ON_NULL_RETURN(plane_client);

   if ((E_Comp_Wl_Buffer *)data != plane_client->buffer) return;

   ec = plane_client->ec;
   EINA_SAFETY_ON_NULL_RETURN(ec);

   if (e_pixmap_resource_get(ec->pixmap) == (E_Comp_Wl_Buffer *)data)
     {
         e_pixmap_resource_set(ec->pixmap, NULL);
         e_comp_object_native_surface_set(ec->frame, 0);
     }

   plane_client->buffer = NULL;
}

static Eina_Bool
_e_plane_client_backup_buffer_set(E_Plane_Client *plane_client)
{
   E_Comp_Wl_Buffer *backup_buffer = NULL;
   tbm_surface_h copied_tsurface = NULL;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane_client, EINA_FALSE);

   ec = plane_client->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   copied_tsurface = _e_plane_copied_surface_create(ec, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(copied_tsurface, EINA_FALSE);

   backup_buffer = e_comp_wl_tbm_buffer_get(copied_tsurface);
   EINA_SAFETY_ON_NULL_GOTO(backup_buffer, fail);

   if (plane_client->buffer)
      wl_list_remove(&plane_client->buffer_destroy_listener.link);

   plane_client->buffer = backup_buffer;
   wl_signal_add(&backup_buffer->destroy_signal, &plane_client->buffer_destroy_listener);
   plane_client->buffer_destroy_listener.notify = _e_plane_client_backup_buffer_cb_destroy;

   /* reference backup buffer to comp data */
   e_comp_wl_buffer_reference(&ec->comp_data->buffer_ref, backup_buffer);

   /* set the backup buffer resource to the pixmap */
   e_pixmap_resource_set(ec->pixmap, backup_buffer);
   e_pixmap_image_refresh(ec->pixmap);

   /* force update */
   e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   e_comp_object_dirty(ec->frame);
   e_comp_object_render(ec->frame);

   return EINA_TRUE;

fail :
   if (copied_tsurface)
      _e_plane_copied_surface_destroy(copied_tsurface);

   return EINA_FALSE;
}

static Eina_Bool
_e_plane_renderer_disp_surface_find(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_LIST_FOREACH(renderer->disp_surfaces, l_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static void
_e_plane_surface_queue_release(E_Plane *plane, tbm_surface_h tsurface)
{

   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   E_Plane_Renderer *renderer = NULL;
   tbm_surface_queue_h tqueue = NULL;

   renderer = plane->renderer;
   EINA_SAFETY_ON_NULL_RETURN(renderer);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN(tqueue);

   /* debug */
   if (plane_trace_debug)
     {
        E_Client *ec = renderer->activated_ec;
        if (ec)
          ELOGF("E_PLANE", "Release Layer(%p)     wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
                ec->pixmap, ec, plane, _get_wl_buffer(ec), tsurface, renderer->tqueue, _get_wl_buffer_ref(ec));
        else
          ELOGF("E_PLANE", "Release Layer(%p)  tsurface(%p) tqueue(%p)",
                NULL, NULL, plane, tsurface, renderer->tqueue);
     }

   tsq_err = tbm_surface_queue_release(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("Failed to release tbm_surface(%p) from tbm_surface_queue(%p): tsq_err = %d", tsurface, tqueue, tsq_err);
        return;
     }
}

static Eina_Bool
_e_plane_client_exported_surface_find(E_Plane_Client *plane_client, tbm_surface_h tsurface)
{
   Eina_List *l_s;
   tbm_surface_h tmp_tsurface = NULL;

   /* destroy the plane_client */
   EINA_LIST_FOREACH(plane_client->exported_surfaces, l_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static void
_e_plane_renderer_exported_surface_release(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   E_Plane *plane = NULL;
   tbm_surface_h tmp_tsurface = NULL;
   Eina_List *l_s, *ll_s;

   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   EINA_LIST_FOREACH_SAFE(renderer->exported_surfaces, l_s, ll_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;

        if (tmp_tsurface == tsurface)
          {
             _e_plane_surface_queue_release(plane, tsurface);
             renderer->exported_surfaces = eina_list_remove_list(renderer->exported_surfaces, l_s);
          }
     }

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Release exported Renderer(%p)  tsurface(%p) tqueue(%p)",
           NULL, NULL, renderer, tsurface, renderer->tqueue);
}

static void
_e_plane_client_exported_surfaces_release(E_Plane_Client *plane_client)
{
   Eina_List *l_s, *ll_s;
   tbm_surface_h tsurface = NULL;
   E_Plane *plane = NULL;

   EINA_SAFETY_ON_NULL_RETURN(plane_client);

   plane = plane_client->plane;
   if (!plane) return;

   /* destroy the plane_client */
   EINA_LIST_FOREACH_SAFE(plane_client->exported_surfaces, l_s, ll_s, tsurface)
     {
        if (!tsurface) continue;

        _e_plane_renderer_exported_surface_release(plane->renderer, tsurface);
        plane_client->exported_surfaces = eina_list_remove_list(plane_client->exported_surfaces, l_s);
     }
}

static void
_e_plane_client_del(void *data)
{
   E_Plane_Client *plane_client = data;

   if (!plane_client) return;

   if (plane_client->buffer)
      wl_list_remove(&plane_client->buffer_destroy_listener.link);

   _e_plane_client_exported_surfaces_release(plane_client);

   free(plane_client);
}

static uint32_t
_e_plane_client_surface_flags_get(E_Plane_Client *plane_client)
{
   tbm_surface_h tsurface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Client *ec = plane_client->ec;
   E_Pixmap *pixmap = ec->pixmap;
   uint32_t flags = 0;
   E_Comp_Wl_Buffer *buffer = NULL;

   buffer = e_pixmap_resource_get(pixmap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, 0);

   switch (buffer->type)
     {
       case E_COMP_WL_BUFFER_TYPE_NATIVE:
       case E_COMP_WL_BUFFER_TYPE_VIDEO:
         tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
         EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, 0);

         flags = wayland_tbm_server_get_buffer_flags(wl_comp_data->tbm.server, buffer->resource);
         break;
       default:
         flags = 0;
         break;
     }

   return flags;
}

static tbm_surface_h
_e_plane_surface_queue_acquire(E_Plane *plane)
{
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   E_Plane_Renderer *renderer = NULL;

   renderer = plane->renderer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, NULL);

   if (tbm_surface_queue_can_acquire(tqueue, 1))
     {
        tsq_err = tbm_surface_queue_acquire(tqueue, &tsurface);
        if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
          {
             ERR("Failed to acquire tbm_surface from tbm_surface_queue(%p): tsq_err = %d", tqueue, tsq_err);
             return NULL;
          }
     }

   /* if not exist, add the surface to the renderer */
   if (!_e_plane_renderer_disp_surface_find(renderer, tsurface))
      renderer->disp_surfaces = eina_list_append(renderer->disp_surfaces, tsurface);

   /* debug */
   if (plane_trace_debug)
     {
        E_Client *ec = renderer->activated_ec;
        if (ec)
          ELOGF("E_PLANE", "Acquire Layer(%p)     wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
                ec->pixmap, ec, plane, _get_wl_buffer(ec), tsurface, tqueue, _get_wl_buffer_ref(ec));
        else
          ELOGF("E_PLANE", "Acquire Layer(%p)  tsurface(%p) tqueue(%p)",
                NULL, NULL, plane, tsurface, tqueue);
     }

   return tsurface;
}

static Eina_Bool
_e_plane_surface_queue_enqueue(E_Plane *plane, tbm_surface_h tsurface)
{
   tbm_surface_queue_h tqueue = NULL;
   E_Plane_Renderer *renderer = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   renderer = plane->renderer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   /* debug */
   if (plane_trace_debug)
    {
        E_Plane_Renderer *renderer = plane->renderer;
        E_Client *ec = renderer->activated_ec;
        ELOGF("E_PLANE", "Enqueue Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
              ec->pixmap, ec, renderer, _get_wl_buffer(ec), tsurface, renderer->tqueue, _get_wl_buffer_ref(ec));
    }

   tsq_err = tbm_surface_queue_enqueue(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("tbm_surface_queue_enqueue failed. tbm_surface_queue(%p) tbm_surface(%p)", tqueue, tsurface);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static int
_e_plane_surface_queue_can_dequeue(E_Plane *plane)
{
   tbm_surface_queue_h tqueue = NULL;
   E_Plane_Renderer *renderer = NULL;
   int num_free = 0;

   renderer = plane->renderer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   num_free = tbm_surface_queue_can_dequeue(tqueue, 0);

   return num_free;
}

static tbm_surface_h
_e_plane_surface_queue_dequeue(E_Plane *plane)
{
   E_Plane_Renderer *renderer = NULL;
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   renderer = plane->renderer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, NULL);

   tsq_err = tbm_surface_queue_dequeue(tqueue, &tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail to tbm_surface_queue_dequeue");
        return NULL;
     }

   /* debug */
   if (plane_trace_debug)
     {
         E_Plane_Renderer *renderer = plane->renderer;
         E_Client *ec = renderer->activated_ec;
         if (ec)
           ELOGF("E_PLANE", "Dequeue Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
                 ec->pixmap, ec, renderer, _get_wl_buffer(ec), tsurface, renderer->tqueue, _get_wl_buffer_ref(ec));
         else
           ELOGF("E_PLANE", "Dequeue Renderer(%p)  tsurface(%p) tqueue(%p)",
                 NULL, NULL, renderer, tsurface, renderer->tqueue);
     }
   return tsurface;
}

static Eina_Bool
_e_plane_renderer_sent_surface_find(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_LIST_FOREACH(renderer->sent_surfaces, l_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_plane_renderer_exported_surface_find(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_LIST_FOREACH(renderer->exported_surfaces, l_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static void
_e_plane_renderer_exported_surface_destroy_cb(tbm_surface_h tsurface, void *data)
{

   E_Plane_Renderer *renderer = NULL;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN(e_comp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);
   EINA_SAFETY_ON_NULL_RETURN(data);

   renderer = (E_Plane_Renderer *)data;
#if 0
   _e_plane_renderer_exported_surface_release(renderer, tsurface);
#endif

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Destroy Renderer(%p)  tsurface(%p) tqueue(%p)",
           NULL, NULL, renderer, tsurface, renderer->tqueue);
}

static void
_e_plane_ee_post_render_cb(void *data, Evas *e EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Plane *plane = data;

   if (!plane) return;

   /* mark when the post_render is called */
   plane->update_ee = EINA_TRUE;
}

static void
_e_plane_renderer_all_disp_surfaces_release(E_Plane_Renderer *renderer)
{
   Eina_List *l_s, *ll_s;
   tbm_surface_h tsurface = NULL;

   EINA_LIST_FOREACH(renderer->disp_surfaces, l_s, tsurface)
     {
        if (!tsurface) continue;

         _e_plane_surface_queue_release(renderer->plane, tsurface);
     }

   /* destroy the plane_client */
   EINA_LIST_FOREACH_SAFE(renderer->exported_surfaces, l_s, ll_s, tsurface)
     {
        if (!tsurface) continue;

        renderer->exported_surfaces = eina_list_remove_list(renderer->exported_surfaces, l_s);
     }
}

static void
_e_plane_renderer_surface_export(E_Plane_Renderer *renderer, tbm_surface_h tsurface, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_buffer = NULL;
   E_Plane_Client *plane_client = NULL;

   plane_client = _e_plane_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(plane_client);

   cqueue = _e_plane_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(cqueue);

   if (_e_plane_renderer_exported_surface_find(renderer, tsurface)) return;

   /* export the tbm_surface(wl_buffer) to the client_queue */
   wl_buffer = wayland_tbm_server_client_queue_export_buffer(cqueue, tsurface,
           E_PLANE_CLIENT_SURFACE_FLAGS_RESERVED, _e_plane_renderer_exported_surface_destroy_cb,
           (void *)renderer);

   renderer->exported_surfaces = eina_list_append(renderer->exported_surfaces, tsurface);

   /* add a sent surface to the sent list in renderer if it is not in the list */
   if (!_e_plane_renderer_sent_surface_find(renderer, tsurface))
       renderer->sent_surfaces = eina_list_append(renderer->sent_surfaces, tsurface);

   if (!_e_plane_client_exported_surface_find(plane_client, tsurface))
       plane_client->exported_surfaces = eina_list_append(plane_client->exported_surfaces, tsurface);

   if (wl_buffer && plane_trace_debug)
       ELOGF("E_PLANE", "Export  Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p)",
               ec->pixmap, ec, renderer, wl_buffer, tsurface, renderer->tqueue);
}


static void
_e_plane_renderer_all_disp_surfaces_export(E_Plane_Renderer *renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   Eina_List *l_s, *ll_s;
   tbm_surface_h tsurface = NULL;

   cqueue = _e_plane_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(cqueue);

   EINA_LIST_FOREACH_SAFE(renderer->disp_surfaces, l_s, ll_s, tsurface)
     {
        if (!tsurface) continue;

        _e_plane_renderer_surface_export(renderer, tsurface, ec);
     }
}

static void
_e_plane_renderer_dequeuable_surfaces_export(E_Plane_Renderer *renderer, E_Client *ec)
{
   E_Plane *plane = NULL;
   tbm_surface_h tsurface = NULL;

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   /* export dequeuable surface */
   while(_e_plane_surface_queue_can_dequeue(plane))
     {
        /* dequeue */
        tsurface = _e_plane_surface_queue_dequeue(plane);
        if (!tsurface) ERR("fail to dequeue surface");

        /* export the surface */
        if (!_e_plane_renderer_exported_surface_find(renderer, tsurface))
           _e_plane_renderer_surface_export(renderer, tsurface, ec);

        if (!_e_plane_renderer_sent_surface_find(renderer, tsurface))
           renderer->sent_surfaces = eina_list_append(renderer->sent_surfaces, tsurface);
    }
}

static tbm_surface_h
_e_plane_renderer_surface_revice(E_Plane_Renderer *renderer, E_Client *ec)
{
   tbm_surface_h tsurface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Pixmap *pixmap = ec->pixmap;
   uint32_t flags = 0;
   E_Comp_Wl_Buffer *buffer = NULL;

   if (renderer->activated_ec != ec)
     {
        ERR("Renderer(%p)  activated_ec(%p) != ec(%p)", renderer, renderer->activated_ec, ec);
        return NULL;
     }

   buffer = e_pixmap_resource_get(pixmap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, NULL);

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, NULL);

   flags = wayland_tbm_server_get_buffer_flags(wl_comp_data->tbm.server, buffer->resource);

   if (plane_trace_debug)
     {
        E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)renderer->activated_ec->comp_data;
        E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata ->buffer_ref;

        ELOGF("E_PLANE", "Receive Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p) flags(%d)",
        ec->pixmap, ec, renderer, buffer->resource, tsurface, renderer->tqueue, buffer_ref->buffer->resource, flags);
     }
   if (flags != E_PLANE_CLIENT_SURFACE_FLAGS_RESERVED)
     {
        ERR("the flags of the enqueuing surface is %d. need flags(%d).", flags, E_PLANE_CLIENT_SURFACE_FLAGS_RESERVED);
        return NULL;
     }

   /* remove a recieved surface from the sent list in renderer */
   renderer->sent_surfaces = eina_list_remove(renderer->sent_surfaces, (const void *)tsurface);

   return tsurface;
}

static void
_e_plane_renderer_surface_send(E_Plane_Renderer *renderer, E_Client *ec, tbm_surface_h tsurface)
{
   /* debug */
   if (plane_trace_debug)
     ELOGF("E_PLANE", "Send    Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
           ec->pixmap, ec, renderer, _get_wl_buffer(ec), tsurface, renderer->tqueue, _get_wl_buffer_ref(ec));

   /* wl_buffer release */
   e_pixmap_image_clear(ec->pixmap, 1);

   /* add a sent surface to the sent list in renderer if it is not in the list */
   if (!_e_plane_renderer_sent_surface_find(renderer, tsurface))
     renderer->sent_surfaces = eina_list_append(renderer->sent_surfaces, tsurface);
}

static Eina_Bool
_e_plane_renderer_deactivate(E_Plane_Renderer *renderer)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_surface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Client *ec = NULL;
   E_Plane_Client *plane_client = NULL;

   if (renderer->activated_ec)
     {
        ec = renderer->activated_ec;
     }
   else if (renderer->candidate_ec)
     {
        ec = renderer->candidate_ec;
     }
   else
     {
         ERR("NEVER HERE.");
         goto done;
     }

   EINA_SAFETY_ON_NULL_GOTO(wl_comp_data, done);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Deactivate", ec->pixmap, ec);

   plane_client = _e_plane_client_get(ec);
   EINA_SAFETY_ON_NULL_GOTO(plane_client, done);

   wl_surface = _e_plane_wl_surface_get(ec);
   EINA_SAFETY_ON_NULL_GOTO(wl_surface, done);

   cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);
   EINA_SAFETY_ON_NULL_GOTO(cqueue, done);

   /* deactive */
   wayland_tbm_server_client_queue_deactivate(cqueue);

   if (_e_plane_client_surface_flags_get(plane_client) == E_PLANE_CLIENT_SURFACE_FLAGS_RESERVED)
     {
        if (plane_trace_debug)
            ELOGF("E_PLANE", "Set Backup Buffer     wl_buffer(%p):Deactivate", ec->pixmap, ec, _get_wl_buffer(ec));

        if (!_e_plane_client_backup_buffer_set(plane_client))
           ERR("fail to _e_comp_hwc_set_backup_buffer");
     }

done:
   if (renderer->activated_ec)
     {
        _e_plane_client_exported_surfaces_release(plane_client);
        renderer->activated_ec = NULL;
     }
   if (renderer->candidate_ec)
     {
        _e_plane_client_exported_surfaces_release(plane_client);
        renderer->candidate_ec = NULL;
     }

   _e_plane_renderer_all_disp_surfaces_release(renderer);

   plane_client->activated = EINA_FALSE;

   return EINA_TRUE;
}

static void
_e_plane_renderer_queue_del(E_Plane_Renderer *renderer)
{
   tbm_surface_queue_h tqueue = NULL;

   if (!renderer) return;

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN(tqueue);

   tbm_surface_queue_destroy(tqueue);
   renderer->tqueue = NULL;

   renderer->disp_surfaces = eina_list_free(renderer->disp_surfaces);
}

static Eina_Bool
_e_plane_renderer_queue_create(E_Plane_Renderer *renderer, int width, int height)
{
   E_Plane *plane = NULL;
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_h tsurface = NULL;
   tdm_error tdm_err = TDM_ERROR_NONE;
   unsigned int buffer_flags = -1;
   int format = TBM_FORMAT_ARGB8888;
   int queue_size = 3; /* query tdm ????? */

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   if (renderer->tqueue)
     {
        ERR("already create queue in renderer");
        return EINA_FALSE;
     }

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   tdm_err = tdm_layer_get_buffer_flags(plane->tlayer, &buffer_flags);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tdm_err == TDM_ERROR_NONE, EINA_FALSE);

   tqueue = tbm_surface_queue_create(queue_size, width, height, format, buffer_flags);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tqueue, EINA_FALSE);

   renderer->tqueue = tqueue;

   /* dequeue the surfaces if the qeueue is available */
   /* add the surface to the disp_surfaces list, if it is not in the disp_surfaces */
   while (tbm_surface_queue_can_dequeue(renderer->tqueue, 0))
      {
         /* dequeue */
         tsurface = _e_plane_surface_queue_dequeue(plane);
         if (!tsurface)
            {
               ERR("fail to dequeue surface");
               continue;
            }

         /* if not exist, add the surface to the renderer */
         if (!_e_plane_renderer_disp_surface_find(renderer, tsurface))
            renderer->disp_surfaces = eina_list_append(renderer->disp_surfaces, tsurface);
      }

   _e_plane_renderer_all_disp_surfaces_release(renderer);

   return EINA_TRUE;
}

static Eina_Bool
_e_plane_renderer_queue_set(E_Plane_Renderer *renderer, tbm_surface_queue_h tqueue)
{
   tbm_surface_h tsurface = NULL;
   E_Plane *plane = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   renderer->tqueue = tqueue;

   if (renderer->disp_surfaces)
      renderer->disp_surfaces = eina_list_free(renderer->disp_surfaces);

   /* dequeue the surfaces if the qeueue is available */
   /* add the surface to the disp_surfaces list, if it is not in the disp_surfaces */
   while (tbm_surface_queue_can_dequeue(renderer->tqueue, 0))
      {
         /* dequeue */
         tsurface = _e_plane_surface_queue_dequeue(plane);
         if (!tsurface)
            {
               ERR("fail to dequeue surface");
               continue;
            }

        /* if not exist, add the surface to the renderer */
        if (!_e_plane_renderer_disp_surface_find(renderer, tsurface))
           renderer->disp_surfaces = eina_list_append(renderer->disp_surfaces, tsurface);
      }

   _e_plane_renderer_all_disp_surfaces_release(renderer);

   return EINA_TRUE;
}

static E_Plane_Renderer *
_e_plane_renderer_new(E_Plane *plane)
{
   E_Plane_Renderer *renderer = NULL;
   /* create a renderer */
   renderer = E_NEW(E_Plane_Renderer, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);
   renderer->plane = plane;

   return renderer;
}

static void
_e_plane_renderer_del(E_Plane_Renderer *renderer)
{
   E_Plane *plane = NULL;

   if (!renderer) return;

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   if (!plane->is_primary)
      _e_plane_renderer_queue_del(renderer);

   free(renderer);
}

static Eina_Bool
_e_plane_renderer_activate(E_Plane_Renderer *renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   tbm_surface_h tsurface = NULL;
   E_Plane_Client *plane_client = NULL;
   E_Plane *plane = NULL;

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   /* deactivate the client of the layer before this call*/
   if (renderer->activated_ec)
     {
        ERR("Previous activated client must be decativated.");
        return EINA_FALSE;
     }

   cqueue = _e_plane_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* register the plane client */
   plane_client = _e_plane_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane_client, EINA_FALSE);

   if (renderer->candidate_ec)
     {
        if (renderer->candidate_ec != ec)
          {
              /* deactive the candidate_ec */
              _e_plane_renderer_deactivate(renderer);

              /* activate the client queue */
              wayland_tbm_server_client_queue_activate(cqueue, 0);
              plane_client->activated = EINA_TRUE;

              if (_e_plane_client_surface_flags_get(plane_client) != E_PLANE_CLIENT_SURFACE_FLAGS_RESERVED)
                {
                   /* check dequeuable */
                   if (!_e_plane_surface_queue_can_dequeue(renderer->plane))
                     {
                        INF("There is any dequeuable surface.");
                        return EINA_FALSE;
                     }

                    /* dequeue */
                    tsurface = _e_plane_surface_queue_dequeue(renderer->plane);
                    if (!tsurface)
                      {
                        ERR("fail to dequeue surface");
                        return EINA_FALSE;
                      }/* export the surface */

                    /* export */
                    _e_plane_renderer_surface_export(renderer, tsurface, ec);

                    if (plane_trace_debug)
                       ELOGF("E_PLANE", "Candidate Plane(%p)", ec->pixmap, ec, renderer->plane);

                    renderer->candidate_ec = ec;

                    return EINA_FALSE;
                }
          }
        else
          {
             if (_e_plane_client_surface_flags_get(plane_client) != E_PLANE_CLIENT_SURFACE_FLAGS_RESERVED)
               {
                  INF("ec does not have the scanout surface yet.");
                  return EINA_FALSE;
               }
          }
     }
   else
     {
         wayland_tbm_server_client_queue_activate(cqueue, 0);
         plane_client->activated = EINA_TRUE;

         if (_e_plane_client_surface_flags_get(plane_client) != E_PLANE_CLIENT_SURFACE_FLAGS_RESERVED)
           {
               /* check dequeuable */
               if (!_e_plane_surface_queue_can_dequeue(renderer->plane))
                 {
                   INF("There is any dequeuable surface.");
                   return EINA_FALSE;
                 }

               /* dequeue */
               tsurface = _e_plane_surface_queue_dequeue(renderer->plane);
               if (!tsurface)
                 {
                    ERR("fail to dequeue surface");
                    return EINA_FALSE;
                 }

              /* export */
              _e_plane_renderer_surface_export(renderer, tsurface, ec);

              if (plane_trace_debug)
                  ELOGF("E_PLANE", "Candidate Plane(%p)", ec->pixmap, ec, renderer->plane);

              renderer->candidate_ec = ec;

              INF("ec does not have the scanout surface.");

              return EINA_FALSE;
          }
     }

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Activate Plane(%p)", ec->pixmap, ec, plane);

   renderer->activated_ec = ec;
   renderer->candidate_ec = NULL;

   _e_plane_renderer_dequeuable_surfaces_export(renderer, ec);

   return EINA_TRUE;
}


static Eina_Bool
_e_plane_surface_set(E_Plane *plane, tbm_surface_h tsurface)
{
   tbm_surface_info_s surf_info;
   tdm_error error;
   tdm_layer *tlayer = plane->tlayer;
   E_Output *output = plane->output;

   /* set layer when the layer infomation is different from the previous one */
   tbm_surface_get_info(tsurface, &surf_info);
   if (plane->info.src_config.size.h != surf_info.planes[0].stride ||
       plane->info.src_config.size.v != surf_info.height ||
       plane->info.src_config.pos.x != 0 ||
       plane->info.src_config.pos.y != 0 ||
       plane->info.src_config.pos.w != surf_info.width ||
       plane->info.src_config.pos.h != surf_info.height ||
       plane->info.dst_pos.x != output->config.geom.x ||
       plane->info.dst_pos.y != output->config.geom.y ||
       plane->info.dst_pos.w != output->config.geom.w ||
       plane->info.dst_pos.h != output->config.geom.h ||
       plane->info.transform != TDM_TRANSFORM_NORMAL)
     {
        plane->info.src_config.size.h = surf_info.planes[0].stride;
        plane->info.src_config.size.v = surf_info.height;
        plane->info.src_config.pos.x = 0;
        plane->info.src_config.pos.y = 0;
        plane->info.src_config.pos.w = surf_info.width;
        plane->info.src_config.pos.h = surf_info.height;
        plane->info.dst_pos.x = output->config.geom.x;
        plane->info.dst_pos.y = output->config.geom.y;
        plane->info.dst_pos.w = output->config.geom.w;
        plane->info.dst_pos.h = output->config.geom.h;
        plane->info.transform = TDM_TRANSFORM_NORMAL;

        error = tdm_layer_set_info(tlayer, &plane->info);
        if (error != TDM_ERROR_NONE)
          {
             ERR("fail to tdm_layer_set_info");
             return EINA_FALSE;
          }
     }

   if (plane_trace_debug)
     {
        ELOGF("E_PLANE", "Commit  Layer(%p)  tsurface(%p) (%dx%d,[%d,%d,%d,%d]=>[%d,%d,%d,%d])",
              NULL, NULL, plane, tsurface,
              plane->info.src_config.size.h, plane->info.src_config.size.h,
              plane->info.src_config.pos.x, plane->info.src_config.pos.y,
              plane->info.src_config.pos.w, plane->info.src_config.pos.h,
              plane->info.dst_pos.x, plane->info.dst_pos.y,
              plane->info.dst_pos.w, plane->info.dst_pos.h);
     }

   error = tdm_layer_set_buffer(tlayer, tsurface);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_set_buffer");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_plane_surface_on_client_reserved_release(E_Plane *plane, tbm_surface_h tsurface)
{
   E_Plane_Renderer *renderer = plane->renderer;
   E_Client *ec = plane->ec;

   if (!ec)
     {
        ERR("no ec at plane.");
        return;
     }

   /* release the tsurface */
   _e_plane_renderer_surface_send(renderer, ec, tsurface);
}

static tbm_surface_h
_e_plane_surface_from_client_acquire_reserved(E_Plane *plane)
{
   E_Client *ec = plane->ec;
   E_Pixmap *pixmap = ec->pixmap;
   tbm_surface_h tsurface = NULL;
   E_Plane_Client *plane_client = NULL;
   E_Plane_Renderer *renderer = plane->renderer;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Display Client", ec->pixmap, ec);

   plane_client = _e_plane_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane_client, NULL);

    /* acquire the surface from the client_queue */
   tsurface = _e_plane_renderer_surface_revice(renderer, ec);
   if (!tsurface)
     {
        e_pixmap_image_clear(pixmap, 1);
        ERR("fail to _e_plane_renderer_surface_revice");
        return NULL;
     }

   /* enqueue the surface to the layer_queue */
   if (!_e_plane_surface_queue_enqueue(plane, tsurface))
     {
        _e_plane_renderer_surface_send(renderer, ec, tsurface);
        ERR("fail to _e_plane_surface_queue_enqueue");
        return NULL;
     }

   /* aquire */
   tsurface = _e_plane_surface_queue_acquire(plane);
   if (!tsurface)
     {
        _e_plane_renderer_surface_send(renderer, ec, tsurface);
        ERR("fail _e_plane_surface_queue_acquire");
        return NULL;
     }

   return tsurface;
}

static void
_e_plane_surface_on_client_release(E_Plane *plane, tbm_surface_h tsurface)
{
   E_Client *ec = plane->ec;

   if (!ec)
     {
        ERR("no ec at plane.");
        return;
     }

   /* release the tsurface */
   e_pixmap_image_clear(ec->pixmap, 1);
}

static tbm_surface_h
_e_plane_surface_from_client_acquire(E_Plane *plane)
{
   E_Client *ec = plane->ec;
   E_Pixmap *pixmap = ec->pixmap;
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   tbm_surface_h tsurface = NULL;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Display Client", pixmap, ec);

   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, NULL);

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
   if (!tsurface)
     {
        ERR("fail to _e_plane_renderer_surface_revice");
        e_pixmap_image_clear(pixmap, 1);
        return NULL;
     }

   return tsurface;
}

static void
_e_plane_surface_on_ecore_evas_release(E_Plane *plane, tbm_surface_h tsurface)
{
   /* release the tsurface */
   _e_plane_surface_queue_release(plane, tsurface);
}

static tbm_surface_h
_e_plane_surface_from_ecore_evas_acquire(E_Plane *plane)
{
   Evas_Engine_Info_GL_Drm *einfo = NULL;
   E_Plane_Renderer *renderer = NULL;
   tbm_surface_h tsurface = NULL;
   E_Output *output = plane->output;

   einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
   EINA_SAFETY_ON_NULL_RETURN_VAL(einfo, NULL);

   renderer = plane->renderer;

   if (renderer->gsurface != einfo->info.surface)
     {
        tbm_surface_queue_h tqueue = NULL;

        renderer->gsurface = einfo->info.surface;
        tqueue = gbm_tbm_get_surface_queue(renderer->gsurface);
        if (!tqueue)
          {
             ERR("no renderer->tqueue");
             return NULL;
          }

        if (!_e_plane_renderer_queue_set(renderer, tqueue))
          {
             ERR("fail to _e_plane_renderer_queue_set");
             return NULL;
          }

        /* dpms on at the first */
        if (tdm_output_set_dpms(output->toutput, TDM_OUTPUT_DPMS_ON) != TDM_ERROR_NONE)
           WRN("fail to set the dpms on.");
     }

   if (plane_trace_debug)
      ELOGF("E_PLANE", "Display Canvas Layer(%p)", NULL, NULL, plane);

   /* aquire */
   tsurface = _e_plane_surface_queue_acquire(plane);
   if (!tsurface)
     {
        ERR("tsurface is NULL");
        return NULL;
     }

   return tsurface;
}

static Eina_Bool
_e_plane_cb_ec_buffer_change(void *data, int type, void *event)
{
   E_Client *ec = NULL;
   E_Event_Client *ev = event;
   E_Plane_Client *plane_client = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;

   if (e_object_is_del(E_OBJECT(ec))) return ECORE_CALLBACK_PASS_ON;

   plane_client = _e_plane_client_get(ec);
   if (!plane_client) return ECORE_CALLBACK_PASS_ON;

   if (plane_client->activated) return ECORE_CALLBACK_PASS_ON;

   if (_e_plane_client_surface_flags_get(plane_client) != E_PLANE_CLIENT_SURFACE_FLAGS_RESERVED)
      return ECORE_CALLBACK_PASS_ON;

   if (plane_trace_debug)
      ELOGF("E_PLANE", "Set Backup Buffer     wl_buffer(%p):buffer_change", ec->pixmap, ec, _get_wl_buffer(ec));

   if (!_e_plane_client_backup_buffer_set(plane_client))
      ERR("fail to _e_comp_hwc_set_backup_buffer");

   return ECORE_CALLBACK_PASS_ON;
}

EINTERN Eina_Bool
e_plane_init(void)
{
   if (client_hook_new) return EINA_TRUE;
   if (client_hook_del) return EINA_TRUE;

   client_hook_new =  e_client_hook_add(E_CLIENT_HOOK_NEW_CLIENT, _e_plane_client_cb_new, NULL);
   client_hook_del =  e_client_hook_add(E_CLIENT_HOOK_DEL, _e_plane_client_cb_del, NULL);

   plane_clients = eina_hash_pointer_new(_e_plane_client_del);

   E_LIST_HANDLER_APPEND(plane_hdlrs, E_EVENT_CLIENT_BUFFER_CHANGE,
                         _e_plane_cb_ec_buffer_change, NULL);

   // soolim debug
   plane_trace_debug = EINA_TRUE;

   return EINA_TRUE;
}

EINTERN void
e_plane_shutdown(void)
{
   if (client_hook_new)
     {
        e_client_hook_del(client_hook_new);
        client_hook_new = NULL;
     }

   if (client_hook_del)
     {
        e_client_hook_del(client_hook_del);
        client_hook_del = NULL;
     }
}

EINTERN E_Plane *
e_plane_new(E_Output *output, int index)
{
   E_Plane *plane = NULL;
   tdm_layer *tlayer = NULL;
   tdm_output *toutput = NULL;
   tdm_layer_capability layer_capabilities;
   char name[40];
   E_Plane_Renderer *renderer = NULL;
   int zpos;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   toutput = output->toutput;
   EINA_SAFETY_ON_NULL_RETURN_VAL(toutput, NULL);

   plane = E_NEW(E_Plane, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);
   plane->index = index;

   tlayer = tdm_output_get_layer(toutput, index, NULL);
   if (!tlayer)
   {
      ERR("fail to get layer.");
      free(plane);
      return NULL;
   }
   plane->tlayer = tlayer;

   snprintf(name, sizeof(name), "%s-plane-%d", output->id, index);
   plane->name = eina_stringshare_add(name);

   CLEAR(layer_capabilities);
   tdm_layer_get_capabilities(plane->tlayer, &layer_capabilities);
   /* check the layer is the primary layer */
   if (layer_capabilities&TDM_LAYER_CAPABILITY_PRIMARY)
     {
        plane->is_primary = EINA_TRUE;
        plane->is_fb = EINA_TRUE; // TODO: query from libtdm if it is fb target plane
     }

   /* check that the layer uses the reserve nd memory */
   if (layer_capabilities&TDM_LAYER_CAPABILITY_RESEVED_MEMORY)
       plane->reserved_memory = EINA_TRUE;

   /* ????? */
   plane->type = E_PLANE_TYPE_INVALID;

   tdm_layer_get_zpos(tlayer, &zpos);
   plane->zpos = zpos;

   renderer = _e_plane_renderer_new(plane);
   if (!renderer)
     {
        ERR("fail to _e_plane_renderer_new");
        free(plane);
        return NULL;
     }

   plane->renderer = renderer;
   plane->output = output;

   INF("E_PLANE: (%d) name:%s zpos:%d capa:%s %s",
       index, plane->name, plane->zpos,plane->is_primary?"primary":"", plane->reserved_memory?"reserved_memory":"");

   return plane;
}

EINTERN void
e_plane_free(E_Plane *plane)
{
   if (!plane) return;

   if (plane->name) eina_stringshare_del(plane->name);
   if (plane->renderer) _e_plane_renderer_del(plane->renderer);

   free(plane);
}

EINTERN Eina_Bool
e_plane_hwc_setup(E_Plane *plane)
{
   Evas_Engine_Info_GL_Drm *einfo;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   /* we assume that the primary plane gets a ecore_evas */
   if (!plane->is_primary) return EINA_FALSE;

   /* get the evas_engine_gl_drm information */
   einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
   if (!einfo) return EINA_FALSE;
   /* enable hwc to evas engine gl_drm */
   einfo->info.hwc_enable = EINA_TRUE;

   plane->ee = e_comp->ee;
   plane->evas = ecore_evas_get(plane->ee);
   evas_event_callback_add(plane->evas, EVAS_CALLBACK_RENDER_POST, _e_plane_ee_post_render_cb, plane);
   ecore_evas_manual_render_set(plane->ee, 1);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_set(E_Plane *plane)
{
   tbm_surface_h tsurface = NULL;
   Evas_Engine_Info_GL_Drm *einfo;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (plane->is_primary && !plane->ec)
     {
        ecore_evas_manual_render(plane->ee);

        /* check the post_render is called */
        if (!plane->update_ee)
        {
          ELOGF("E_PLANE", "Post Render callback does not called. Nothing Display.", NULL, NULL);
          return EINA_FALSE;
        }
        plane->update_ee = EINA_FALSE;

        einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
        if (!einfo) return EINA_FALSE;
        /* check outbuf flushed or ont */
        if (!einfo->info.outbuf_flushed)
          {
             if (plane_trace_debug)
               ELOGF("E_PLANE", "Commit Canvas outbuf flush nothing!. Nothing Display.", NULL, NULL);
             if (plane->update_ee) plane->update_ee = EINA_FALSE;
             return EINA_FALSE;
          }

        /* uncheck the outbuf_flushed flag */
        einfo->info.outbuf_flushed = EINA_FALSE;

        tsurface = _e_plane_surface_from_ecore_evas_acquire(plane);
     }
   else
     {
        E_Comp_Wl_Buffer *buffer = NULL;

        if (!plane->ec) return EINA_FALSE;

        if (!e_comp_object_hwc_update_exists(plane->ec->frame)) return EINA_FALSE;

        e_comp_object_hwc_update_set(plane->ec->frame, EINA_FALSE);

        buffer = e_pixmap_resource_get(plane->ec->pixmap);
        if (!buffer)
          {
            ERR("buffer is null.");
            return EINA_FALSE;
          }

        if (plane->reserved_memory)
          tsurface = _e_plane_surface_from_client_acquire_reserved(plane);
        else
          tsurface = _e_plane_surface_from_client_acquire(plane);
     }

   plane->prepare_tsurface = tsurface;

   /* set plane info and set tsurface to the plane */
   if (!_e_plane_surface_set(plane, tsurface))
     {
        ERR("fail: _e_plane_set_info.");
        e_plane_unset(plane);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN void
e_plane_unset(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN(plane);
   EINA_SAFETY_ON_NULL_RETURN(plane->prepare_tsurface);

   if (plane->is_primary && !plane->ec)
     _e_plane_surface_on_ecore_evas_release(plane, plane->prepare_tsurface);
   else
     {
        if (!plane->ec) return;
        if (plane->reserved_memory) _e_plane_surface_on_client_reserved_release(plane, plane->prepare_tsurface);
        else _e_plane_surface_on_client_release(plane, plane->prepare_tsurface);
     }

   /* set plane info and set prevous tsurface to the plane */
   if (!_e_plane_surface_set(plane, plane->tsurface))
     {
        ERR("fail: _e_plane_set_info.");
        return;
     }
}

EINTERN E_Plane_Commit_Data *
e_plane_commit_data_aquire(E_Plane *plane)
{
   E_Plane_Commit_Data *data = NULL;
   Evas_Engine_Info_GL_Drm *einfo = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   if (plane->is_primary && !plane->ec)
     {
        data = E_NEW(E_Plane_Commit_Data, 1);
        data->plane = plane;
        data->tsurface = plane->prepare_tsurface;
        tbm_surface_internal_ref(data->tsurface);
        data->ec = NULL;

        einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
        einfo->info.wait_for_showup = EINA_TRUE;

        return data;
     }
   else
     {
        if (plane->ec)
          {
             data = E_NEW(E_Plane_Commit_Data, 1);
             data->plane = plane;
             data->tsurface = plane->prepare_tsurface;
             tbm_surface_internal_ref(data->tsurface);
             data->ec = plane->ec;
             e_comp_wl_buffer_reference(&data->buffer_ref, e_pixmap_resource_get(plane->ec->pixmap));

             /* send frame event enlightenment dosen't send frame evnet in nocomp */
             e_pixmap_image_clear(plane->ec->pixmap, 1);
             return data;
          }
     }

   return NULL;
}

EINTERN void
e_plane_commit_data_release(E_Plane_Commit_Data *data)
{
   E_Plane *plane = NULL;
   E_Plane_Renderer *renderer = NULL;
   Evas_Engine_Info_GL_Drm *einfo = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_h send_tsurface = NULL;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN(data);

   plane = data->plane;
   tsurface = data->tsurface;
   ec = data->ec;
   renderer = plane->renderer;

   if (plane->is_primary && !ec)
     {
        /* composite */
        /* debug */
        if (plane_trace_debug)
          ELOGF("E_PLANE", "Done    Layer(%p)  tsurface(%p) tqueue(%p) data(%p)::Canvas",
               NULL, NULL, plane, tsurface, renderer->tqueue, data);

        if (plane->reserved_memory)
          {
             if (!renderer->activated_ec)
               {
                  einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
                  einfo->info.wait_for_showup = EINA_FALSE;
               }

             /* initial setting of tsurface to the layer */
             if (plane->tsurface == NULL)
               plane->tsurface = tsurface;
             else
               {
                  _e_plane_surface_queue_release(plane, plane->tsurface);
                  e_comp_wl_buffer_reference(&plane->displaying_buffer_ref, NULL);
                  plane->tsurface = tsurface;
               }

             /* send the done surface to the client,
                only when the renderer state is active(no composite) */
             if (renderer->activated_ec)
                _e_plane_renderer_dequeuable_surfaces_export(renderer, renderer->activated_ec);
          }
        else
          {
             einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
             einfo->info.wait_for_showup = EINA_FALSE;

             /* initial setting of tsurface to the layer */
             if (plane->tsurface == NULL)
               plane->tsurface = tsurface;
             else
               {
                  _e_plane_surface_queue_release(plane, plane->tsurface);
                  plane->tsurface = tsurface;
               }
          }

        tbm_surface_internal_unref(tsurface);
        free(data);
     }
   else
     {
        /* no composite */
        /* debug */
        if (plane_trace_debug)
           ELOGF("E_PLANE", "Done    Layer(%p)     wl_buffer(%p) tsurface(%p) tqueue(%p) data(%p) wl_buffer_ref(%p) ::Client",
             ec->pixmap, ec, plane, _get_wl_buffer(ec), tsurface, renderer->tqueue, data, _get_wl_buffer_ref(ec));

        if (plane->reserved_memory)
          {
             /* release */
             if (plane->tsurface)
               {
                  _e_plane_surface_queue_release(plane, plane->tsurface);
                  e_comp_wl_buffer_reference(&plane->displaying_buffer_ref, data->buffer_ref.buffer);
                  plane->tsurface = tsurface;
               }

             /* send the done surface to the client,
                only when the renderer state is active(no composite) */
             if (renderer->activated_ec)
               {
                  _e_plane_renderer_dequeuable_surfaces_export(renderer, renderer->activated_ec);
                  _e_plane_surface_on_client_reserved_release(plane, send_tsurface);
               }
          }
        else
          {
             /* release wl_buffer */
             e_pixmap_image_clear(ec->pixmap, 1);
          }

        tbm_surface_internal_unref(tsurface);
        e_comp_wl_buffer_reference(&data->buffer_ref, NULL);
        free(data);
     }
}

E_API Eina_Bool
e_plane_type_set(E_Plane *plane,
                 E_Plane_Type type)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if ((type == E_PLANE_TYPE_VIDEO) ||
       (type == E_PLANE_TYPE_CURSOR))
     {
        if (plane->ec || plane->prepare_ec) return EINA_FALSE;
     }
   plane->type = type;
   return EINA_TRUE;
}

E_API E_Plane_Type
e_plane_type_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, E_PLANE_TYPE_INVALID);
   return plane->type;
}

E_API E_Client *
e_plane_ec_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);
   return plane->ec;
}

E_API Eina_Bool
e_plane_ec_set(E_Plane *plane, E_Client *ec)
{
   E_Plane_Renderer *renderer = NULL;
   Evas_Engine_Info_GL_Drm *einfo = NULL;
   E_Plane_Client *plane_client = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   renderer = plane->renderer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);

   if (!ec && renderer->candidate_ec)
     {
        if (!plane->is_primary)
           _e_plane_renderer_queue_del(renderer);

        if (!_e_plane_renderer_deactivate(renderer))
          {
             ERR("fail to _e_plane_renderer_deactivate.");
             return EINA_FALSE;
          }
     }

   if (!ec && !plane->ec) return EINA_FALSE;

   /* activate/deactivate the client if the plane is the reserved memory */
   if (plane->reserved_memory)
     {
        if (ec)
          {
             if (!plane->is_primary)
                _e_plane_renderer_queue_create(renderer, ec->client.w, ec->client.h);

             if (!_e_plane_renderer_activate(renderer, ec))
               {
                  INF("can't activate ec:%p.", ec);

                  if (!_e_plane_surface_queue_can_dequeue(renderer->plane))
                     einfo->info.wait_for_showup = EINA_TRUE;

                  return EINA_FALSE;
               }

             einfo->info.wait_for_showup = EINA_TRUE;
             e_comp_object_hwc_update_set(ec->frame, EINA_TRUE);
          }
        else
          {
              if (!plane->is_primary)
                 _e_plane_renderer_queue_del(renderer);

              if (!_e_plane_renderer_deactivate(renderer))
                {
                   ERR("fail to _e_plane_renderer_deactivate.");
                   return EINA_FALSE;
                }

             einfo->info.wait_for_showup = EINA_FALSE;
          }
     }

   if (ec)
     {
        plane_client = _e_plane_client_get(ec);
        if (plane_client) plane_client->plane = plane;
     }

   plane->ec = ec;

   if (plane_trace_debug)
      ELOGF("E_PLANE", "Plane(%p) ec Set", (ec ? ec->pixmap : NULL), ec, plane);

   return EINA_TRUE;
}

E_API E_Client *
e_plane_ec_prepare_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);
   return plane->prepare_ec;
}

E_API Eina_Bool
e_plane_ec_prepare_set(E_Plane *plane, E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   plane->prepare_ec = ec;

   return EINA_TRUE;
}

E_API const char *
e_plane_ec_prepare_set_last_error_get(E_Plane *plane)
{
   return _e_plane_ec_last_err;
}

E_API Eina_Bool
e_plane_is_primary(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);
   if (plane->is_primary) return EINA_TRUE;
   return EINA_FALSE;
}

E_API Eina_Bool
e_plane_is_cursor(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);
   if (plane->type == E_PLANE_TYPE_CURSOR) return EINA_TRUE;
   return EINA_FALSE;
}

E_API E_Plane_Color
e_plane_color_val_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, E_PLANE_COLOR_INVALID);
   return plane->color;
}

E_API Eina_Bool
e_plane_is_fb_target(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);
   if (plane->is_fb) return EINA_TRUE;
   return EINA_FALSE;
}
