#include "e.h"

# include <gbm/gbm_tbm.h>
# include <tdm.h>
# include <tdm_helper.h>
#include <tbm_surface.h>
#include <tbm_surface_internal.h>
# include <wayland-tbm-server.h>
# include <Evas_Engine_GL_Drm.h>

# ifndef CLEAR
# define CLEAR(x) memset(&(x), 0, sizeof (x))
# endif

# define E_PLANE_SURFACE_RESEVED_SCANOUT 7777

typedef struct _E_Plane_Client E_Plane_Client;

struct _E_Plane_Client
{
   E_Client *ec;

   Eina_Bool activated;

   E_Comp_Wl_Buffer *buffer;
   E_Comp_Wl_Buffer *backup_buffer;

   Eina_Bool wait_for_commit;
};

/* E_Plane is a child object of E_Output. There is one Output per screen
 * E_plane represents hw overlay and a surface is assigned to disable composition
 * Each Output always has dedicated canvas and a zone
 */
///////////////////////////////////////////
static const char *_e_plane_ec_last_err = NULL;
static  E_Client_Hook *client_hook = NULL;
static Eina_List *plane_clients = NULL;

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
   if (!buffer_ref) return NULL;

   if (!buffer_ref->buffer) return NULL;

   return buffer_ref->buffer->resource;
}

static tbm_surface_h
_e_plane_create_copied_surface(E_Client *ec, Eina_Bool refresh)
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

   tbm_surface_map(tsurface, TBM_SURF_OPTION_READ, &src_info);
   tbm_surface_unmap(tsurface);

   new_tsurface = tbm_surface_create(src_info.width, src_info.height, src_info.format);

   tbm_surface_map(new_tsurface, TBM_SURF_OPTION_WRITE, &dst_info);
   tbm_surface_unmap(new_tsurface);

   /* copy from src to dst */
   memcpy(dst_info.planes[0].ptr, src_info.planes[0].ptr, src_info.planes[0].size);

   return new_tsurface;
}

static void
_e_plane_destroy_copied_surface(tbm_surface_h tbm_surface)
{
   EINA_SAFETY_ON_NULL_RETURN(tbm_surface);

   tbm_surface_internal_unref(tbm_surface);
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

static E_Comp_Wl_Buffer *
_e_plane_client_create_backup_buffer(E_Plane_Client *plane_client)
{
   E_Comp_Wl_Buffer *buffer = NULL;
   tbm_surface_h tsurface = NULL;

   tsurface = _e_plane_create_copied_surface(plane_client->ec, EINA_TRUE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, NULL);

   if (!(buffer = E_NEW(E_Comp_Wl_Buffer, 1)))
     {
        _e_plane_destroy_copied_surface(tsurface);
        return NULL;
     }

   buffer->type = E_COMP_WL_BUFFER_TYPE_TBM;
   buffer->w = tbm_surface_get_width(tsurface);
   buffer->h = tbm_surface_get_height(tsurface);
   buffer->resource = NULL;
   buffer->tbm_surface = tsurface;
   wl_signal_init(&buffer->destroy_signal);

   return buffer;
}

static void
_e_plane_client_destroy_backup_buffer(E_Comp_Wl_Buffer *buffer)
{
   if (!buffer) return;
   if (buffer->tbm_surface)
     {
        _e_plane_destroy_copied_surface(buffer->tbm_surface);
        buffer->tbm_surface = NULL;
     }

   wl_signal_emit(&buffer->destroy_signal, buffer);
   E_FREE(buffer);
}

static void
_e_plane_client_cb_del(void *data EINA_UNUSED, E_Client *ec)
{
   E_Plane_Client *plane_client = NULL;
   Eina_List *l = NULL;

   /* destroy the plane_client */
   EINA_LIST_FOREACH(plane_clients, l, plane_client)
     {
        if (!plane_client) continue;
        if (plane_client->ec == ec)
          {
             plane_clients = eina_list_remove_list(plane_clients, l);
             if (plane_client->backup_buffer)
               _e_plane_client_destroy_backup_buffer(plane_client->backup_buffer);
             free(plane_client);
             break;
          }
     }
}

E_Plane_Client *
_e_plane_client_find(E_Client *ec)
{
   E_Plane_Client *plane_client = NULL;
   const Eina_List *l;

   EINA_LIST_FOREACH(plane_clients, l, plane_client)
     {
        if (!plane_client) continue;
        if (plane_client->ec == ec) return plane_client;
     }

   return NULL;
}

static Eina_Bool
_e_plane_renderer_find_disp_surface(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s, *ll_s;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_LIST_FOREACH_SAFE(renderer->disp_surfaces, l_s, ll_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_plane_renderer_find_sent_surface(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s, *ll_s;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_LIST_FOREACH_SAFE(renderer->sent_surfaces, l_s, ll_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static tbm_surface_h
_e_plane_surface_queue_acquire(E_Plane *plane)
{
   tbm_surface_queue_h tqueue = plane->tqueue;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   int num_duty = 0;
   E_Plane_Renderer *renderer = plane->renderer;

   if ((num_duty = tbm_surface_queue_can_acquire(tqueue, 1)))
     {
        tsq_err = tbm_surface_queue_acquire(tqueue, &tsurface);
        if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
          {
             ERR("Failed to acquire tbm_surface from tbm_surface_queue(%p): tsq_err = %d", tqueue, tsq_err);
             return NULL;
          }
     }

   /* if not exist, add the surface to the renderer */
   if (!_e_plane_renderer_find_disp_surface(renderer, tsurface))
     renderer->disp_surfaces = eina_list_append(renderer->disp_surfaces, tsurface);

   /* debug */
   if (plane->trace_debug)
     {
        E_Client *ec = renderer->activated_ec;
        if (ec)
          ELOGF("HWC", "Acquire Layer(%p)     wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
                ec->pixmap, ec, plane, _get_wl_buffer(ec), tsurface, tqueue, _get_wl_buffer_ref(ec));
        else
          ELOGF("HWC", "Acquire Layer(%p)  tsurface(%p) tqueue(%p)",
                 NULL, NULL, plane, tsurface, tqueue);
     }
   return tsurface;
}

static void
_e_plane_surface_queue_release(E_Plane *plane, tbm_surface_h tsurface)
{
   tbm_surface_queue_h tqueue = plane->tqueue;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   E_Plane_Renderer *renderer = plane->renderer;

   /* debug */
   if (plane->trace_debug)
     {
        E_Client *ec = renderer->activated_ec;
        if (ec)
          ELOGF("HWC", "Release Layer(%p)     wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
                ec->pixmap, ec, plane, _get_wl_buffer(ec), tsurface, plane->tqueue, _get_wl_buffer_ref(ec));
        else
          ELOGF("HWC", "Release Layer(%p)  tsurface(%p) tqueue(%p)",
                NULL, NULL, plane, tsurface, plane->tqueue);
     }

   tsq_err = tbm_surface_queue_release(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("Failed to release tbm_surface(%p) from tbm_surface_queue(%p): tsq_err = %d", tsurface, tqueue, tsq_err);
        return;
     }
}


static Eina_Bool
_e_plane_surface_queue_enqueue(E_Plane *plane, tbm_surface_h tsurface)
{
   tbm_surface_queue_h tqueue = plane->tqueue;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   /* debug */
   if (plane->trace_debug)
    {
        E_Plane_Renderer *renderer = plane->renderer;
        E_Client *ec = renderer->activated_ec;
        ELOGF("HWC", "Enqueue Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
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

static tbm_surface_h
_e_plane_surface_queue_dequeue(E_Plane *plane)
{
   tbm_surface_queue_h tqueue = plane->tqueue;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   tsq_err = tbm_surface_queue_dequeue(tqueue, &tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail to tbm_surface_queue_dequeue");
        return NULL;
     }

   /* debug */
   if (plane->trace_debug)
     {
         E_Plane_Renderer *renderer = plane->renderer;
         E_Client *ec = renderer->activated_ec;
         if (ec)
           ELOGF("HWC", "Dequeue Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
                 ec->pixmap, ec, renderer, _get_wl_buffer(ec), tsurface, renderer->tqueue, _get_wl_buffer_ref(ec));
         else
           ELOGF("HWC", "Dequeue Renderer(%p)  tsurface(%p) tqueue(%p)",
                 NULL, NULL, renderer, tsurface, renderer->tqueue);
     }
   return tsurface;
}

static void
_e_plane_renderer_release_all_disp_surfaces(E_Plane_Renderer *renderer)
{
   Eina_List *l_s, *ll_s;
   tbm_surface_h tsurface = NULL;

   EINA_LIST_FOREACH_SAFE(renderer->disp_surfaces, l_s, ll_s, tsurface)
     {
        if (!tsurface) continue;

        /* release the surface to the layer_queue */
        _e_plane_surface_queue_release(renderer->plane, tsurface);
     }
}

static void
_e_plane_renderer_surface_destroy_cb(tbm_surface_h tsurface, void *data)
{
   E_Plane_Renderer *renderer = NULL;

   EINA_SAFETY_ON_NULL_RETURN(tsurface);
   EINA_SAFETY_ON_NULL_RETURN(data);

   renderer = (E_Plane_Renderer *)data;

   if (renderer->plane->trace_debug)
     ELOGF("HWC", "Destroy Renderer(%p)  tsurface(%p) tqueue(%p)",
           NULL, NULL, renderer, tsurface, renderer->tqueue);
}

static void
_e_plane_renderer_export_all_disp_surfaces(E_Plane_Renderer *renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   Eina_List *l_s, *ll_s;
   tbm_surface_h tsurface = NULL;
   struct wl_resource *wl_buffer = NULL;

   cqueue = _e_plane_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(cqueue);

   EINA_LIST_FOREACH_SAFE(renderer->disp_surfaces, l_s, ll_s, tsurface)
     {
        if (!tsurface) continue;

        /* export the tbm_surface(wl_buffer) to the client_queue */
        wl_buffer = wayland_tbm_server_client_queue_export_buffer(cqueue, tsurface,
                               E_PLANE_SURFACE_RESEVED_SCANOUT, _e_plane_renderer_surface_destroy_cb,
                               (void *)renderer);

        /* add a sent surface to the sent list in renderer if it is not in the list */
        if (!_e_plane_renderer_find_sent_surface(renderer, tsurface))
           renderer->sent_surfaces = eina_list_append(renderer->sent_surfaces, tsurface);

        if (wl_buffer && renderer->plane->trace_debug)
          ELOGF("HWC", "Export  Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p)",
                ec->pixmap, ec, renderer, wl_buffer, tsurface, renderer->tqueue);

     }
}

static tbm_surface_h
_e_plane_renderer_recieve_surface(E_Plane_Renderer *renderer, E_Client *ec)
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

   if (renderer->plane->trace_debug)
     {
        E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)renderer->activated_ec->comp_data;
        E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata ->buffer_ref;

        ELOGF("HWC", "Receive Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p) flags(%d)",
        ec->pixmap, ec, renderer, buffer->resource, tsurface, renderer->tqueue, buffer_ref->buffer->resource, flags);
     }
   if (flags != E_PLANE_SURFACE_RESEVED_SCANOUT)
     {
        ERR("the flags of the enqueuing surface is %d. need flags(%d).", flags, E_PLANE_SURFACE_RESEVED_SCANOUT);
        return NULL;
     }

   /* remove a recieved surface from the sent list in renderer */
   renderer->sent_surfaces = eina_list_remove(renderer->sent_surfaces, (const void *)tsurface);

   return tsurface;
}

static void
_e_plane_renderer_send_surface(E_Plane_Renderer *renderer, E_Client *ec, tbm_surface_h tsurface)
{
   /* debug */
   if (renderer->plane->trace_debug)
     ELOGF("HWC", "Send    Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
           ec->pixmap, ec, renderer, _get_wl_buffer(ec), tsurface, renderer->tqueue, _get_wl_buffer_ref(ec));

   /* wl_buffer release */
   e_pixmap_image_clear(ec->pixmap, 1);

   /* add a sent surface to the sent list in renderer if it is not in the list */
   if (!_e_plane_renderer_find_sent_surface(renderer, tsurface))
     renderer->sent_surfaces = eina_list_append(renderer->sent_surfaces, tsurface);
}

static Eina_Bool
_e_plane_renderer_can_activate(E_Plane_Renderer *renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_surface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;

   if (renderer->plane->trace_debug)
     ELOGF("HWC", "Check Activate", ec->pixmap, ec);

   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_comp_data, EINA_FALSE);

   wl_surface = _e_plane_wl_surface_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_surface, EINA_FALSE);

   cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* check if the layer queue is dequeueable */
   if(!tbm_surface_queue_can_dequeue(renderer->tqueue, 0))
     {
        WRN("fail to tbm_surface_queue_can_dequeue");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}


static Eina_Bool
_e_plane_renderer_activate(E_Plane_Renderer *renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_h tqueue = renderer->tqueue;

   if (renderer->plane->trace_debug)
     ELOGF("HWC", "Activate", ec->pixmap, ec);

   /* deactivate the client of the layer before this call*/
   if (renderer->activated_ec)
     {
        ERR("Previous activated client must be decativated.");
        return EINA_FALSE;
     }

   cqueue = _e_plane_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* activate the client queue */
   wayland_tbm_server_client_queue_activate(cqueue, 0);

   /* dequeue the surfaces if the qeueue is available */
   /* add the surface to the disp_surfaces list, if it is not in the disp_surfaces */
   while (tbm_surface_queue_can_dequeue(tqueue, 0))
     {
        /* dequeue */
        tsurface = _e_plane_surface_queue_dequeue(renderer->plane);
        if (!tsurface)
          {
            ERR("fail to dequeue surface");
            return EINA_FALSE;
          }
        /* if not exist, add the surface to the renderer */
        if (!_e_plane_renderer_find_disp_surface(renderer, tsurface))
          renderer->disp_surfaces = eina_list_append(renderer->disp_surfaces, tsurface);
     }

   _e_plane_renderer_export_all_disp_surfaces(renderer, ec);

   /* wl_buffer release */
   e_pixmap_image_clear(ec->pixmap, 1);

   renderer->activated_ec = ec;

   /* register the plane client */
   E_Plane_Client *plane_client = _e_plane_client_find(ec);
   if (!plane_client)
     {
        plane_client = E_NEW(E_Plane_Client, 1);
        EINA_SAFETY_ON_NULL_RETURN_VAL(plane_client, EINA_FALSE);
        plane_client->ec = ec;
        plane_client->activated = EINA_TRUE;
        /* add plane_client to the list */
        plane_clients = eina_list_append(plane_clients, plane_client);
#ifdef USE_DLOG_HWC
        HWC_DLOG("plane_client,%p created.\n", plane_client);
#endif
     }
   else
     {
        plane_client->activated = EINA_TRUE;
     }

#ifdef USE_DLOG_HWC
   HWC_DLOG("plane_client,%p activated.\n", plane_client);
#endif

   return EINA_TRUE;
}

static Eina_Bool
_e_plane_renderer_deactivate(E_Plane_Renderer *renderer)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_surface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Client *ec = NULL;
   E_Plane_Client *plane_client = NULL;

   if (!renderer->activated_ec)
     {
        if (renderer->plane->trace_debug)
          ELOGF("HWC", "Deactivate Client is gone before.", NULL, NULL);
        goto done;
     }

   ec = renderer->activated_ec;

   if (renderer->plane->trace_debug)
     ELOGF("HWC", "Deactivate", ec->pixmap, ec);

   plane_client = _e_plane_client_find(ec);
   EINA_SAFETY_ON_NULL_GOTO(plane_client, done);

   if (!plane_client->activated) goto done;

   EINA_SAFETY_ON_NULL_GOTO(wl_comp_data, done);

   wl_surface = _e_plane_wl_surface_get(ec);
   EINA_SAFETY_ON_NULL_GOTO(wl_surface, done);

   cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);
   EINA_SAFETY_ON_NULL_GOTO(cqueue, done);

   /* deactive */
   wayland_tbm_server_client_queue_deactivate(cqueue);

   if (!plane_client->backup_buffer)
     {
#ifdef USE_DLOG_HWC
        HWC_DLOG("plane_client,%p create the backup_buffer.\n");
#endif
        /* get the tsurface for setting the native surface with it
          compositing is processed not calling e_comp_update_cb*/
        plane_client->buffer = e_pixmap_resource_get(ec->pixmap);
        plane_client->backup_buffer = _e_plane_client_create_backup_buffer(plane_client);
        plane_client->activated = EINA_FALSE;
        plane_client->wait_for_commit = EINA_TRUE;

        /* set the backup buffer resource to the pixmap */
        e_pixmap_resource_set(ec->pixmap, plane_client->backup_buffer);

        e_comp->nocomp = 0;
        e_comp->nocomp_ec = NULL;

        /* force update */
        e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
        e_comp_object_dirty(ec->frame);
        e_comp_object_render(ec->frame);

        /* set the native surface with tbm type for compositing */
        e_comp_object_native_surface_set(ec->frame, EINA_TRUE);
     }
   else
     {
#ifdef USE_DLOG_HWC
        HWC_DLOG("plane_client,%p dose not commit wl_buffer after the previous deactive.\n");
#endif
     }

#ifdef USE_DLOG_HWC
   HWC_DLOG("plane_client,%p deactivated\n", plane_client);
#endif

done:
   renderer->activated_ec = NULL;

   /* enqueue the tsurfaces to the layer queue */
   _e_plane_renderer_release_all_disp_surfaces(renderer);

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

   if (plane->trace_debug)
     {
        ELOGF("HWC", "Commit  Layer(%p)  tsurface(%p) (%dx%d,[%d,%d,%d,%d]=>[%d,%d,%d,%d])",
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

   plane->prepare_tsurface = tsurface;

   return EINA_TRUE;
}

static void
_e_plane_unset_info_client_reserved(E_Plane *plane)
{
   tbm_surface_h prepare_tsurface = plane->prepare_tsurface;
   tbm_surface_h tsurface = plane->tsurface;
   E_Plane_Renderer *renderer = plane->renderer;
   E_Client *ec = plane->ec;

   if (!ec)
     {
        ERR("no ec at plane.");
        return;
     }

   if (!prepare_tsurface)
     {
        ERR("no prepare_tsurface at plane.");
        return;
     }

   /* release the prepare_tsurface */
   _e_plane_renderer_send_surface(renderer, ec, prepare_tsurface);

   /* set the current tsurface again */
   if (!_e_plane_surface_set(plane, tsurface))
     {
        ERR("fail: _e_plane_set_info.");
        return;
     }
}

static void
_e_plane_set_info_client_reserved(E_Plane *plane)
{
   E_Plane_Renderer *renderer = plane->renderer;
   E_Client *ec = plane->prepare_ec;
   E_Pixmap *pixmap = ec->pixmap;
   tbm_surface_h tsurface = NULL;
   E_Plane_Client *plane_client = NULL;

   if (plane->trace_debug)
     ELOGF("HWC", "Display Client", ec->pixmap, ec);

   plane_client = _e_plane_client_find(ec);
   EINA_SAFETY_ON_NULL_RETURN(plane_client);

   if (plane_client->backup_buffer)
     {
#ifdef USE_DLOG_HWC
        HWC_DLOG("plane_client,%p display client. destroy the backup_buffer.\n", plane_client);
#endif
        _e_plane_client_destroy_backup_buffer(plane_client->backup_buffer);
        plane_client->backup_buffer = NULL;
     }

   /* acquire the surface from the client_queue */
   tsurface = _e_plane_renderer_recieve_surface(renderer, ec);
   if (!tsurface)
     {
        e_pixmap_image_clear(pixmap, 1);
        ERR("fail to _e_plane_renderer_recieve_surface");
        return;
     }

   /* enqueue the surface to the layer_queue */
   if (!_e_plane_surface_queue_enqueue(plane, tsurface))
     {
        _e_plane_renderer_send_surface(renderer, ec, tsurface);
        ERR("fail to _e_plane_surface_queue_enqueue");
        return;
     }

   /* aquire */
   tsurface = _e_plane_surface_queue_acquire(plane);
   if (!tsurface)
     {
        _e_plane_renderer_send_surface(renderer, ec, tsurface);
        ERR("fail _e_plane_surface_queue_acquire");
        return;
     }
}

static void
_e_plane_unset_info_client(E_Plane *plane)
{
   E_Client *ec = plane->ec;
   tbm_surface_h tsurface = plane->tsurface;

   if (!ec)
     {
        ERR("no ec at plane.");
        return;
     }

   /* release the prepare_tsurface */
   e_pixmap_image_clear(ec->pixmap, 1);

   /* set the current tsurface again */
   if (!_e_plane_surface_set(plane, tsurface))
     {
        ERR("fail: _e_plane_set_info.");
        return;
     }
}

static void
_e_plane_set_info_client(E_Plane *plane)
{
   E_Client *ec = plane->ec;
   E_Pixmap *pixmap = ec->pixmap;
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   tbm_surface_h tsurface = NULL;

   if (plane->trace_debug)
     ELOGF("HWC", "Display Client", pixmap, ec);

   EINA_SAFETY_ON_NULL_RETURN(buffer);

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
   if (!tsurface)
     {
        ERR("fail to _e_plane_renderer_recieve_surface");
        e_pixmap_image_clear(pixmap, 1);
        return;
     }

   /* set info and set buffer */
   if (!_e_plane_surface_set(plane, tsurface))
     {
        ERR("fail: _e_plane_set_info.");
        e_pixmap_image_clear(pixmap, 1);
        return;
     }
}

static void
_e_plane_unset_info_canvas(E_Plane *plane)
{
   tbm_surface_h prepare_tsurface = plane->prepare_tsurface;
   tbm_surface_h tsurface = plane->tsurface;

   if (!prepare_tsurface)
     {
        ERR("no prepare_tsurface at plane.");
        return;
     }

   /* release the prepare_tsurface */
   _e_plane_surface_queue_release(plane, prepare_tsurface);

   /* set the current tsurface again */
   if (!_e_plane_surface_set(plane, tsurface))
     {
        ERR("fail: _e_plane_set_info.");
        return;
     }
}

static void
_e_plane_set_info_canvas(E_Plane *plane)
{
   Evas_Engine_Info_GL_Drm *einfo = NULL;
   E_Plane_Renderer *renderer = NULL;
   tbm_surface_h tsurface = NULL;
   E_Output *output = plane->output;

   einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
   EINA_SAFETY_ON_NULL_RETURN(einfo);

   renderer = plane->renderer;

   if (renderer->gsurface != einfo->info.surface)
     {
        renderer->gsurface = einfo->info.surface;
        renderer->tqueue = gbm_tbm_get_surface_queue(renderer->gsurface);
        if (!renderer->tqueue)
          {
            ERR("no renderer->tqueue");
            return;
          }
        plane->tqueue = renderer->tqueue;

        /* dequeue the surfaces if the qeueue is available */
        /* add the surface to the disp_surfaces list, if it is not in the disp_surfaces */
        while (tbm_surface_queue_can_dequeue(plane->tqueue, 0))
          {
             /* dequeue */
             tsurface = _e_plane_surface_queue_dequeue(plane);
             if (!tsurface)
               {
                  ERR("fail to dequeue surface");
                  return;
               }
             /* if not exist, add the surface to the renderer */
             if (!_e_plane_renderer_find_disp_surface(renderer, tsurface))
               renderer->disp_surfaces = eina_list_append(renderer->disp_surfaces, tsurface);
          }
          _e_plane_renderer_release_all_disp_surfaces(renderer);

          /* dpms on at the first */
          if (tdm_output_set_dpms(output->toutput, TDM_OUTPUT_DPMS_ON) != TDM_ERROR_NONE)
            WRN("fail to set the dpms on.");
     }

     if (plane->trace_debug)
       ELOGF("HWC", "Display Canvas Layer(%p)", NULL, NULL, plane);

   /* aquire */
   tsurface = _e_plane_surface_queue_acquire(plane);
   if (!tsurface)
     {
        ERR("tsurface is NULL");
        return;
     }

   /* set info and set buffer */
   if (!_e_plane_surface_set(plane, tsurface))
     {
        ERR("fail: _e_plane_set_info.");
        _e_plane_surface_queue_release(plane, tsurface);
        return;
     }
}

EINTERN Eina_Bool
e_plane_init(void)
{
   if (client_hook) return EINA_TRUE;

   client_hook =  e_client_hook_add(E_CLIENT_HOOK_DEL, _e_plane_client_cb_del, NULL);

   return EINA_TRUE;
}

EINTERN void
e_plane_shutdown(void)
{
   if (client_hook)
     {
        e_client_hook_del(client_hook);
        client_hook = NULL;
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
   unsigned int zpos;

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
       plane->is_primary = EINA_TRUE;

   /* check that the layer uses the reserved memory */
   if (layer_capabilities&TDM_LAYER_CAPABILITY_RESEVED_MEMORY)
       plane->reserved_memory = EINA_TRUE;

   /* ????? */
   plane->type = E_PLANE_TYPE_INVALID;

   tdm_layer_get_zpos(tlayer, &zpos);
   plane->zpos = zpos;

   /* create a renderer */
   renderer = E_NEW(E_Plane_Renderer, 1);
   if (!renderer)
     {
        ERR("fail to create a renderer.");
        free(plane);
        return NULL;
     }
   renderer->plane = plane;
   plane->renderer = renderer;

   plane->output = output;

   INF("E_PLANE: (%d) name:%s capa:%s %s", index, plane->name, plane->is_primary?"primary":"", plane->reserved_memory?"reserved memory":"");

   return plane;
}

EINTERN void
e_plane_free(E_Plane *plane)
{
   if (!plane) return;

   if (plane->name) eina_stringshare_del(plane->name);
   if (plane->renderer) free(plane->renderer);

   free(plane);
}

EINTERN void
e_plane_set(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN(plane);

   if (plane->prepare_ec)
     {
        if (plane->reserved_memory)
          _e_plane_set_info_client_reserved(plane);
        else
          _e_plane_set_info_client(plane);
     }
   else
     {
        if (plane->is_primary)
          _e_plane_set_info_canvas(plane);
     }
}

EINTERN void
e_plane_unset(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN(plane);

   if (plane->prepare_ec)
     {
        if (plane->reserved_memory)
          _e_plane_unset_info_client_reserved(plane);
        else
          _e_plane_unset_info_client(plane);
     }
   else
     {
        if (plane->is_primary)
          _e_plane_unset_info_canvas(plane);
     }
}

EINTERN E_Plane_Commit_Data *
e_plane_commit_data_aquire(E_Plane *plane)
{
   E_Plane_Commit_Data *data = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   if (plane->is_primary && !plane->prepare_ec)
     {
        data = E_NEW(E_Plane_Commit_Data, 1);
        data->plane = plane;
        data->tsurface = plane->prepare_tsurface;
        tbm_surface_internal_ref(data->tsurface);
        data->prepare_ec = NULL;
        return data;
     }
   else
     {
        if (plane->prepare_ec)
          {
             data = E_NEW(E_Plane_Commit_Data, 1);
             data->plane = plane;
             data->tsurface = plane->prepare_tsurface;
             tbm_surface_internal_ref(data->tsurface);
             data->prepare_ec = plane->prepare_ec;
             if (plane->reserved_memory)
               e_comp_wl_buffer_reference(&data->buffer_ref, e_pixmap_resource_get(plane->prepare_ec->pixmap));
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

   if (plane->is_primary && !data->prepare_ec)
     {
        /* debug */
        if (plane->trace_debug)
          ELOGF("HWC", "Done    Layer(%p)  tsurface(%p) tqueue(%p) data(%p)::Canvas",
               NULL, NULL, plane, tsurface, plane->tqueue, data);

        if (plane->reserved_memory)
          {
             renderer = plane->renderer;

             if (!renderer->activated_ec)
               {
                  einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
                  if (!einfo)
                    einfo->info.wait_for_showup = EINA_FALSE;
                  else
                    WRN("no einfo.");
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
               {
                  /* dequeue */
                  send_tsurface = _e_plane_surface_queue_dequeue(plane);
                  if (!send_tsurface) ERR("fail to dequeue surface");
                  _e_plane_renderer_send_surface(renderer, renderer->activated_ec, send_tsurface);
               }
          }
        else
          {
             ec = data->prepare_ec;

             einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
             if (!einfo)
               einfo->info.wait_for_showup = EINA_FALSE;
             else
               WRN("no einfo.");

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
        /* debug */
        if (plane->trace_debug)
           ELOGF("HWC", "Done    Layer(%p)     wl_buffer(%p) tsurface(%p) tqueue(%p) data(%p) wl_buffer_ref(%p) ::Client",
             ec->pixmap, ec, plane, _get_wl_buffer(ec), tsurface, plane->tqueue, data, _get_wl_buffer_ref(ec));

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
                  /* dequeue */
                  send_tsurface = _e_plane_surface_queue_dequeue(plane);
                  if (!send_tsurface) ERR("fail to dequeue surface");
                  _e_plane_renderer_send_surface(renderer, renderer->activated_ec, send_tsurface);
               }
          }
        else
          {
             /* release wl_buffer */
             e_pixmap_image_clear(ec->pixmap, 1);
          }

        tbm_surface_internal_unref(tsurface);
        free(data);
     }
}

E_API Eina_Bool
e_plane_type_set(E_Plane *plane,
                 E_Plane_Type_State type)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if ((type == E_PLANE_TYPE_VIDEO) &&
       (type == E_PLANE_TYPE_CURSOR))
     {
        if (plane->ec || plane->prepare_ec) return EINA_FALSE;
     }
   plane->type = type;
   return EINA_TRUE;
}

E_API E_Plane_Type_State
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

E_API E_Client *
e_plane_ec_prepare_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);
   return plane->prepare_ec;
}

E_API Eina_Bool
e_plane_ec_prepare_set(E_Plane *plane,
                       E_Client *ec)
{
   if(!plane)
     {
        eina_stringshare_replace(&_e_plane_ec_last_err, "Invalid e_plane were passed");
        goto err;
     }

   if (plane->type == E_PLANE_TYPE_OVERLAY)
     {
        eina_stringshare_replace(&_e_plane_ec_last_err, NULL);
        plane->prepare_ec = ec;
        return EINA_TRUE;
     }
   eina_stringshare_replace(&_e_plane_ec_last_err, "Type dismatch : ec not availabe on e_plane");
err:

   return EINA_FALSE;
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
e_plane_is_fb(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);
   if (plane->is_FB) return EINA_TRUE;
   return EINA_FALSE;
}

E_API Eina_Bool
e_plane_fb_set(E_Plane *plane,
                       Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   plane->is_FB = set;
   return EINA_TRUE;
}
