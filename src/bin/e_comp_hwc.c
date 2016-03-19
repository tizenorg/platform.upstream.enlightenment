#include "e.h"
#include "e_comp_wl.h"
#include "e_comp_hwc.h"

#include <Ecore_Drm.h>
#include <Evas_Engine_GL_Drm.h>

#include <gbm/gbm_tbm.h>
#include <tdm.h>
#include <tdm_helper.h>
#include <wayland-tbm-server.h>

#define HWC_DRM_MODE_DPMS_OFF 3
#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof (x))
#endif

#define HWC_SURFACE_TYPE_PRIMARY 7777

#define USE_FIXED_SCANOUT 1

typedef struct _E_Comp_Hwc E_Comp_Hwc;
typedef struct _E_Comp_Hwc_Output E_Comp_Hwc_Output;
typedef struct _E_Comp_Hwc_Layer E_Comp_Hwc_Layer;
typedef struct _E_Comp_Hwc_Commit_Data E_Comp_Hwc_Commit_Data;

typedef enum _E_Hwc_Mode
{
   E_HWC_MODE_COMPOSITE = 1,        /* display only canvas */
   E_HWC_MODE_NO_COMPOSITE = 2,      /* display only one surface */
   E_HWC_MODE_HWC_COMPOSITE = 3,    /* display one or more surfaces and a canvas */
   E_HWC_MODE_HWC_NO_COMPOSITE = 4, /* display multi surfaces */
} E_Hwc_Mode;

struct _E_Comp_Hwc_Commit_Data {
   E_Comp_Hwc_Layer *hwc_layer;
   Eina_Bool is_canvas;
};

struct _E_Comp_Hwc_Layer {
   int index;
   tdm_layer *tlayer;
   struct gbm_surface *gsurface;
   tbm_surface_queue_h tqueue;
   struct wayland_tbm_server_queue *tserver_queue;
   tbm_surface_h tsurface;
   tbm_surface_h tserver_surface;

   int zpos;
   E_Client *ec;
   E_Client *disp_ec;
   Eina_Bool primary;
   E_Client *activated_ec;

   E_Comp_Hwc *hwc;
};

struct _E_Comp_Hwc_Output {
   tdm_output *toutput;
   int x;
   int y;
   int w;
   int h;

   E_Hwc_Mode mode;

   int num_layers;
   Eina_List *hwc_layers;
   E_Comp_Hwc_Layer *primary_layer;
};

struct _E_Comp_Hwc {
   Evas_Engine_Info_GL_Drm *einfo;
   tdm_display *tdisplay;

   int num_outputs;
   Eina_List *hwc_outputs;
};

// Global variable.
static E_Comp_Hwc *g_hwc = NULL;

static E_Comp_Hwc_Commit_Data *
_e_comp_hwc_commit_data_create(void)
{
   E_Comp_Hwc_Commit_Data *data;

   data = E_NEW(E_Comp_Hwc_Commit_Data, 1);
   if (!data) return NULL;

   return data;
}

static void
_e_comp_hwc_commit_data_destroy(E_Comp_Hwc_Commit_Data *data)
{
   if (data) return;

   free(data);
}


#if USE_FIXED_SCANOUT
static struct wl_resource *
_e_comp_hwc_wl_surface_get(E_Client *ec)
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
_e_comp_hwc_wayland_tbm_client_queue_get(E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_surface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_comp_data, NULL);

   wl_surface = _e_comp_hwc_wl_surface_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_surface, NULL);

   cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, NULL);

   return cqueue;
}

static void
_e_comp_hwc_layer_client_buffer_destroy_cb(struct wl_client *client, struct wl_resource *wl_buffer)
{
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   tbm_surface_h tsurface = NULL;
   //struct wl_tbm_buffer *buffer = wl_resource_get_user_data(wl_buffer);

   EINA_SAFETY_ON_NULL_RETURN(wl_comp_data);

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, wl_buffer);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   INF("HWC:Destroy Wl_Buffer(%p)", wl_buffer);

   // TODO: do something

   // When NOCOMP A ==> COMP
   // 1. find the surface queue of the hwc layers which contains the tbm surface
   // 2. enqueue the tbm surface to the surface queue

   // when NOCOMP A ==> NOCOMP B
   // 1. export the tbm_surface to the B
}

static const struct wl_buffer_interface _e_comp_hwc_layer_client_buffer_impementation = {
	_e_comp_hwc_layer_client_buffer_destroy_cb
};

static tbm_surface_h
_e_comp_hwc_layer_client_acquire(E_Comp_Hwc_Layer *hwc_layer, E_Client *ec)
{
	tbm_surface_h tsurface = NULL;
	E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
	E_Pixmap *pixmap = ec->pixmap;
	uint32_t flags = 0;
	E_Comp_Wl_Buffer *buffer = NULL;

	if (hwc_layer->activated_ec != ec)
	  {
		 ERR("hwc_layer->activated_ec(%p) != ec(%p)", hwc_layer->activated_ec, ec);
		 return EINA_FALSE;
	  }

	buffer = e_pixmap_resource_get(pixmap);
	EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, NULL);

	tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
	EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, NULL);

	flags = wayland_tbm_server_get_buffer_flags(wl_comp_data->tbm.server, buffer->resource);
	INF("HWC:Acquire Client(%p) tbm_surface(%p) flags(%d)", ec, tsurface, flags);
	if (flags != HWC_SURFACE_TYPE_PRIMARY)
	  {
		 ERR("the flags of the enqueuing surface is %d. need flags(%d).", flags, HWC_SURFACE_TYPE_PRIMARY);
		 return EINA_FALSE;
	  }

   return tsurface;
}

static void
_e_comp_hwc_layer_client_release(E_Comp_Hwc_Layer *hwc_layer, E_Client *ec, tbm_surface_h tsurface)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_buffer = NULL;

   INF("HWC:Release Client(%p) tbm_surface(%p)", ec, tsurface);

   cqueue = _e_comp_hwc_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(cqueue);

   /* export the tbm_surface(wl_buffer) to the client_queue */
   wl_buffer = wayland_tbm_server_client_queue_export_buffer(cqueue, tsurface, HWC_SURFACE_TYPE_PRIMARY);
   if (!wl_buffer)
     {
        ERR("fail to get export buffer");
        return;
     }

   /* set the action when the buffer destroy */
   wayland_tbm_server_set_buffer_implementation(wl_buffer, &_e_comp_hwc_layer_client_buffer_impementation, NULL);
}

static Eina_Bool
_e_comp_hwc_layer_surface_enqueue(E_Comp_Hwc_Layer *hwc_layer, tbm_surface_h tsurface)
{
   tbm_surface_queue_h tqueue = hwc_layer->tqueue;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   INF("HWC:Enqueue Layer(%p) tbm_surface_queue(%p) tbm_surface(%p)", hwc_layer, hwc_layer->tqueue, tsurface);

   tsq_err = tbm_surface_queue_enqueue(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("tbm_surface_queue_enqueue failed. tbm_surface_queue(%p) tbm_surface(%p)", tqueue, tsurface);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static tbm_surface_h
_e_comp_hwc_layer_surface_dequeue(E_Comp_Hwc_Layer *hwc_layer)
{
   tbm_surface_queue_h tqueue = hwc_layer->tqueue;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   tsq_err = tbm_surface_queue_dequeue(tqueue, &tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail to tbm_surface_queue_dequeue");
        return NULL;
     }

   INF("HWC:Dequeue Layer(%p) tbm_surface_queue(%p) tbm_surface(%p)", hwc_layer, hwc_layer->tqueue, tsurface);

   return tsurface;
}

static Eina_Bool
_e_comp_hwc_layer_client_can_activate(E_Comp_Hwc_Layer *hwc_layer, E_Client *ec)
{
	struct wayland_tbm_client_queue * cqueue = NULL;
	struct wl_resource *wl_surface = NULL;
	E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;

	INF("HWC:Check Activate Client(%p)", ec);

	EINA_SAFETY_ON_NULL_RETURN_VAL(wl_comp_data, EINA_FALSE);

	wl_surface = _e_comp_hwc_wl_surface_get(ec);
	EINA_SAFETY_ON_NULL_RETURN_VAL(wl_surface, EINA_FALSE);

	cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);
	EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* check if the layer queue is dequeueable */
	if(!tbm_surface_queue_can_dequeue(hwc_layer->tqueue, 0))
	  {
		 WRN("fail to tbm_surface_queue_can_dequeue");
         return EINA_FALSE;
	  }

   return EINA_TRUE;
}

static Eina_Bool
_e_comp_hwc_layer_client_activate(E_Comp_Hwc_Layer *hwc_layer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_h tqueue = hwc_layer->tqueue;

   INF("HWC:Activate Client(%p)", ec);

   /* deactivate the client of the layer before this call*/
   if (hwc_layer->activated_ec)
     {
        ERR("Previous activated client must be decativated.");
        return EINA_FALSE;
     }

   cqueue = _e_comp_hwc_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* activate the client queue */
   wayland_tbm_server_client_queue_activate(cqueue, 0);

   /* dequeue all tbm_surface and export then to the client */
   while (tbm_surface_queue_can_dequeue(tqueue, 0) == 1)
     {
        /* dequeue */
        tsurface = _e_comp_hwc_layer_surface_dequeue(hwc_layer);
        if (!tsurface)
          {
             // TODO: what to do here?
             ERR("fail to dequeue surface");
             return EINA_FALSE;
          }
		_e_comp_hwc_layer_client_release(hwc_layer, ec, tsurface);
     }

   hwc_layer->activated_ec = ec;

   return EINA_TRUE;
}

static Eina_Bool
_e_comp_hwc_layer_client_deactivate(E_Comp_Hwc_Layer *hwc_layer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_surface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;

   INF("HWC:Deactivate Client(%p)", ec);

   if (hwc_layer->activated_ec != ec)
     {
        ERR("ec(%p) is noe activated before. hwc_layer->activated_ec(%p)", ec, hwc_layer->activated_ec?hwc_layer->activated_ec:NULL);
        return EINA_FALSE;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_comp_data, EINA_FALSE);

   wl_surface = _e_comp_hwc_wl_surface_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_surface, EINA_FALSE);

   cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* deactive */
   wayland_tbm_server_client_queue_deactivate(cqueue);

   hwc_layer->activated_ec = NULL;

   return EINA_TRUE;
}
#endif

static tbm_surface_h
_e_comp_hwc_layer_surface_aquire(E_Comp_Hwc_Layer *hwc_layer)
{
   tbm_surface_queue_h tqueue = hwc_layer->tqueue;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   tsq_err = tbm_surface_queue_acquire(tqueue, &tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("Failed to acquire tbm_surface from tbm_surface_queue(%p): tsq_err = %d", tqueue, tsq_err);
        return NULL;
     }

   INF("HWC:Acquire Layer(%p) tbm_surface_queue(%p) tbm_surface(%p)", hwc_layer, tqueue, tsurface);

   return tsurface;
}

static void
_e_comp_hwc_layer_surface_release(E_Comp_Hwc_Layer *hwc_layer, tbm_surface_h tsurface)
{
   tbm_surface_queue_h tqueue = hwc_layer->tqueue;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   INF("HWC:Release Layer(%p) tbm_surface_queue(%p) tbm_surface(%p)", hwc_layer, tqueue, tsurface);

   tsq_err = tbm_surface_queue_release(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("Failed to release tbm_surface(%p) from tbm_surface_queue(%p): tsq_err = %d", tsurface, tqueue, tsq_err);
        return;
     }
}


E_Comp_Hwc_Output *_e_comp_hwc_output_find(Ecore_Drm_Output *drm_output)
{
   E_Comp_Hwc_Output * hwc_output = NULL;
   const Eina_List *l;
   tdm_output *toutput = NULL;

   EINA_LIST_FOREACH(g_hwc->hwc_outputs, l, hwc_output)
     {
        if (!hwc_output) continue;
        toutput = ecore_drm_output_hal_private_get(drm_output);
        if (toutput == hwc_output->toutput) return hwc_output;
     }

   return NULL;
}

static void
_e_comp_hwc_output_update_geom(E_Comp_Hwc_Output *hwc_output)
{
   Ecore_Drm_Device *dev;
   Ecore_Drm_Output *drm_output;
   E_Randr2_Screen *s;
   const Eina_List *l, *ll;
   int x, y, w, h;

   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     {
        EINA_LIST_FOREACH(e_randr2->screens, ll, s)
          {
             INF("HWC: find output for '%s'\n", s->info.name);
             if (!s->config.enabled) continue;
             drm_output = ecore_drm_device_output_name_find(dev, s->info.name);
             if (!drm_output) continue;

             ecore_drm_output_position_get(drm_output, &x, &y);
             ecore_drm_output_crtc_size_get(drm_output, &w, &h);
             if (w <= 0 || h <= 0) continue;

             hwc_output = _e_comp_hwc_output_find(drm_output);
             if (!hwc_output)
               {
                  ERR("could not find hwc_output");
                  continue;
               }
             hwc_output->x = x;
             hwc_output->y = y;
             hwc_output->w = w;
             hwc_output->h = h;

             INF("HWC: %s %d,%d,%d,%d", s->info.name, x, y, w, h);
          }
     }
}

static E_Comp_Hwc_Layer *
_e_comp_hwc_output_find_primary_layer(E_Comp_Hwc_Output *hwc_output)
{
   E_Comp_Hwc_Layer *hwc_layer = NULL;
   tdm_layer_capability capa;
   Eina_List *l_l, *ll_l;

   /* find the primary layer and get the geometry of the layer
    * The geometry from the primary layer is the geometry of the output.
    */
   EINA_LIST_FOREACH_SAFE(hwc_output->hwc_layers, l_l, ll_l, hwc_layer)
     {
        if (!hwc_layer) continue;
        tdm_layer_get_capabilities(hwc_layer->tlayer, &capa);

        if (capa & (TDM_LAYER_CAPABILITY_PRIMARY))
          {
             printf ("\tTDM_LAYER_CAPABILITY_PRIMARY layer found : %d\n", hwc_layer->index);
             return hwc_layer;
          }
     }

   return NULL;
}

static void
_e_comp_hwc_output_commit_handler(tdm_output *output, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec,
                                  void *user_data)
{
   E_Comp_Hwc_Commit_Data *data = user_data;
   E_Comp_Hwc_Layer *hwc_layer = NULL;
   E_Client *ec = NULL;

   if (!data) return;
   if (!data->hwc_layer) return;

   hwc_layer = data->hwc_layer;

   if (data->is_canvas && hwc_layer->primary)
     {
        INF("HWC:Commit Done Canvas Layer(%p) tbm_surface(%p). Layer # is %d", hwc_layer, hwc_layer->tsurface, hwc_layer->index);

        hwc_layer->hwc->einfo->info.wait_for_showup = EINA_FALSE;

        if (!hwc_layer->activated_ec)
          /* release */
          _e_comp_hwc_layer_surface_release(hwc_layer, hwc_layer->tsurface);
     }
   else
     {
        ec = hwc_layer->disp_ec;

        INF("HWC:Commit Done Client(%p) Layer(%p) tbm_surface(%p). Layer # is %d", ec, hwc_layer, hwc_layer->tsurface, hwc_layer->index);

        /* release */
        e_pixmap_image_clear(ec->pixmap, 1);
#if USE_FIXED_SCANOUT
        /* release */
        _e_comp_hwc_layer_surface_release(hwc_layer, hwc_layer->tsurface);

        /* dequeue */
        hwc_layer->tsurface = _e_comp_hwc_layer_surface_dequeue(hwc_layer);
        if (!hwc_layer->tsurface)
          // TODO: what to do here?
          ERR("fail to dequeue surface");
		_e_comp_hwc_layer_client_release(hwc_layer, ec, hwc_layer->tsurface);
#endif

     }

   _e_comp_hwc_commit_data_destroy(data);

}

static Eina_Bool
_e_comp_hwc_output_commit(E_Comp_Hwc_Output *hwc_output, E_Comp_Hwc_Layer *hwc_layer, tbm_surface_h surface, Eina_Bool is_canvas)
{
   tdm_info_layer info;
   tbm_surface_info_s surf_info;
   tdm_error error;
   tdm_output *toutput = hwc_output->toutput;
   tdm_layer *tlayer = hwc_layer->tlayer;
   E_Comp_Hwc_Commit_Data *data = NULL;

   tbm_surface_get_info(surface, &surf_info);

   CLEAR(info);
   info.src_config.size.h = surf_info.planes[0].stride;
   info.src_config.size.v = surf_info.height;
   info.src_config.pos.x = 0;
   info.src_config.pos.y = 0;
   info.src_config.pos.w = surf_info.width;
   info.src_config.pos.h = surf_info.height;
   info.dst_pos.x = hwc_output->x;
   info.dst_pos.y = hwc_output->y;
   info.dst_pos.w = hwc_output->w;
   info.dst_pos.h = hwc_output->h;
   info.transform = TDM_TRANSFORM_NORMAL;

   INF("HWC:Commit Request(%dx%d,[%d,%d,%d,%d]=>[%d,%d,%d,%d])",
       info.src_config.size.h, info.src_config.size.h,
       info.src_config.pos.x, info.src_config.pos.y, info.src_config.pos.w, info.src_config.pos.h,
       info.dst_pos.x, info.dst_pos.y, info.dst_pos.w, info.dst_pos.h);

   error = tdm_layer_set_info(tlayer, &info);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_set_info");
        return EINA_FALSE;
     }
   error = tdm_layer_set_buffer(tlayer, surface);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_set_buffer");
        return EINA_FALSE;
     }

   data = _e_comp_hwc_commit_data_create();
   if (!data) return EINA_FALSE;
   data->hwc_layer = hwc_layer;
   data->is_canvas = is_canvas;

   error = tdm_output_commit(toutput, 0, _e_comp_hwc_output_commit_handler, data);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_output_commit");
        _e_comp_hwc_commit_data_destroy(data);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_comp_hwc_output_display_client(E_Comp_Hwc_Output *hwc_output, E_Comp_Hwc_Layer *hwc_layer, E_Client *ec)
{
	INF("HWC:Display Client(%p)", ec);

#if USE_FIXED_SCANOUT
   E_Pixmap *pixmap = ec->pixmap;
   tbm_surface_h tsurface = NULL;

   /* acquire the surface from the client_queue */
   tsurface = _e_comp_hwc_layer_client_acquire(hwc_layer, ec);
   if (!tsurface)
     {
        e_pixmap_image_clear(pixmap, 1);
        ERR("fail to _e_comp_hwc_layer_client_acquire");
        return;
     }

   /* enqueue the surface to the layer_queue */
   if (!_e_comp_hwc_layer_surface_enqueue(hwc_layer, tsurface))
     {
       e_pixmap_image_clear(pixmap, 1);
       ERR("fail to _e_comp_hwc_layer_surface_enqueue");
       return;
     }

   /* aquire the surface from the layer_queue */
   hwc_layer->tsurface = _e_comp_hwc_layer_surface_aquire(hwc_layer);
   if (!hwc_layer->tsurface)
     {
        e_pixmap_image_clear(pixmap, 1);
        ERR("hwc_layer->tsurface is NULL");
        return;
     }
   /* commit */
   if (!_e_comp_hwc_output_commit(hwc_output, hwc_layer, hwc_layer->tsurface, EINA_FALSE))
     {
        e_pixmap_image_clear(pixmap, 1);
        _e_comp_hwc_layer_surface_release(hwc_layer, hwc_layer->tsurface);
        ERR("fail to _e_comp_hwc_output_commit");
        return;
     }
#else
   E_Pixmap *pixmap = ec->pixmap;
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN(buffer);

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   hwc_layer->tsurface = tsurface;
   /* commit */
   if (!_e_comp_hwc_output_commit(hwc_output, hwc_layer, hwc_layer->tsurface, EINA_TRUE))
     {
        e_pixmap_image_clear(pixmap, 1);
        ERR("fail to _e_comp_hwc_output_commit");
        return;
     }
#endif

   hwc_layer->disp_ec = ec;
}

Evas_Engine_Info_GL_Drm *
_e_comp_hwc_get_evas_engine_info_gl_drm(E_Comp_Hwc *hwc)
{
   if (hwc->einfo) return hwc->einfo;

   hwc->einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc->einfo, NULL);

   return hwc->einfo;
}

static void
_e_comp_hwc_display_canvas(void *data EINA_UNUSED, Evas *e EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Hwc *hwc = data;
   Evas_Engine_Info_GL_Drm *einfo = NULL;
   E_Comp_Hwc_Output *hwc_output;
   E_Comp_Hwc_Layer *hwc_layer;
   Eina_List *l_o, *ll_o;
   tdm_output_conn_status conn_status;

   INF("HWC:Display Canvas");

   if (!hwc) return;

   /* get the evas_engine_gl_drm information */
   einfo = _e_comp_hwc_get_evas_engine_info_gl_drm(hwc);
   if (!einfo) return;

   if (!einfo->info.surface)
     {
        ERR("gbm_surface is NULL");
        return;
     }

   /* check outbuf flushed or ont */
   if (!einfo->info.outbuf_flushed)
     {
        INF("HWC:Display Canvas. Evas outbuf flush nothing!. Nothing Display.\n");
        return;
     }

   /* uncheck the outbuf_flushed flag */
   einfo->info.outbuf_flushed = EINA_FALSE;

   EINA_LIST_FOREACH_SAFE(hwc->hwc_outputs, l_o, ll_o, hwc_output)
     {
        if (!hwc_output) continue;
        tdm_output_get_conn_status(hwc_output->toutput, &conn_status);
        // TODO: check TDM_OUTPUT_CONN_STATUS_MODE_SETTED
        if (conn_status != TDM_OUTPUT_CONN_STATUS_CONNECTED) continue;
        if (!hwc_output->primary_layer)
          {
             ERR("no primary layer");
             continue;
          }
        hwc_layer = hwc_output->primary_layer;
        if (hwc_layer->gsurface != einfo->info.surface)
          {
             hwc_layer->gsurface = einfo->info.surface;
             hwc_layer->tqueue = gbm_tbm_get_surface_queue(hwc_layer->gsurface);
             if (!hwc_layer->tqueue)
               {
                  ERR("no hwc_layer->tqueue");
                  continue;
               }
          }

		/* aquire */
        hwc_layer->tsurface = _e_comp_hwc_layer_surface_aquire(hwc_layer);
        if (!hwc_layer->tsurface)
          {
             ERR("hwc_layer->tsurface is NULL");
             continue;
          }

        /* commit */
        if (!_e_comp_hwc_output_commit(hwc_output, hwc_layer, hwc_layer->tsurface, EINA_TRUE))
          {
             _e_comp_hwc_layer_surface_release(hwc_layer, hwc_layer->tsurface);
             ERR("fail to _e_comp_hwc_output_commit");
             continue;
          }

        /* block the next update of ecore_evas until the current update is done */
        einfo->info.wait_for_showup = EINA_TRUE;
     }
}

static void
_e_comp_hwc_remove(E_Comp_Hwc *hwc)
{
   E_Comp_Hwc_Output *hwc_output = NULL;
   E_Comp_Hwc_Layer *hwc_layer = NULL;
   Eina_List *l_o, *ll_o;
   Eina_List *l_l, *ll_l;
   tdm_output *tdisplay = NULL;

   if (!hwc) return;

   tdisplay = hwc->tdisplay;

   EINA_LIST_FOREACH_SAFE(hwc->hwc_outputs, l_o, ll_o, hwc_output)
     {
        if (!hwc_output) continue;
        EINA_LIST_FOREACH_SAFE(hwc_output->hwc_layers, l_l, ll_l, hwc_layer)
          {
             if (!hwc_layer) continue;
             hwc_output->hwc_layers = eina_list_remove_list(hwc_output->hwc_layers, l_l);
             free(hwc_layer);
          }
        hwc->hwc_outputs = eina_list_remove_list(hwc->hwc_outputs, l_o);
        free(hwc_output);
     }


   if (tdisplay) tdm_display_deinit(tdisplay);
   if (hwc) free(hwc);

   g_hwc = NULL;
}

EINTERN Eina_Bool
e_comp_hwc_init(void)
{
   E_Comp_Hwc *hwc = NULL;
   E_Comp_Hwc_Output *hwc_output = NULL;
   E_Comp_Hwc_Layer *hwc_layer = NULL;

   tdm_output *tdisplay = NULL;
   tdm_output *toutput = NULL;
   tdm_layer *tlayer = NULL;
   tdm_error error = TDM_ERROR_NONE;

   Evas_Engine_Info_GL_Drm *einfo;

   int i, j;
   int num_outputs, num_layers;
   unsigned int zpos;

   if (!e_comp)
     {
        e_error_message_show(_("Enlightenment cannot has no e_comp at HWC(HardWare Composite)!\n"));
        return EINA_FALSE;
     }

   if (g_hwc)
     {
        e_error_message_show(_("Enlightenment alreary get the global HWC Controller !\n"));
        return EINA_FALSE;
     }

   hwc = E_NEW(E_Comp_Hwc, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);
   g_hwc = hwc;

   /* tdm display init */
   tdisplay = tdm_display_init(&error);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to get tdm_display\n");
        goto fail;
     }
   hwc->tdisplay = tdisplay;

   /* get the num of outputs */
   tdm_display_get_output_count(tdisplay, &num_outputs);
   if (num_outputs < 1)
     {
        ERR("fail to get tdm_display_get_output_count\n");
        goto fail;
     }
   hwc->num_outputs = num_outputs;

   /* initialize outputs */
   for (i = 0; i < num_outputs; i++)
     {
        hwc_output = E_NEW(E_Comp_Hwc_Output, 1);
        if (!hwc_output) goto fail;
        hwc->hwc_outputs = eina_list_append(hwc->hwc_outputs, hwc_output);

        toutput = tdm_display_get_output(hwc->tdisplay, i, NULL);
        if (!toutput) goto fail;
        hwc_output->toutput = toutput;

        tdm_output_get_layer_count(toutput, &num_layers);
        if (num_layers < 1)
          {
             ERR("fail to get tdm_output_get_layer_count\n");
             goto fail;
          }
        hwc_output->num_layers = num_layers;

        for (j = 0; j < num_layers; j++)
          {
             hwc_layer = E_NEW(E_Comp_Hwc_Layer, 1);
             if (!hwc_layer) goto fail;
             hwc_output->hwc_layers = eina_list_append(hwc_output->hwc_layers, hwc_layer);

             tlayer = tdm_output_get_layer(toutput, j, NULL);
             if (!tlayer) goto fail;
             hwc_layer->tlayer = tlayer;
             hwc_layer->index = j;

             tdm_layer_get_zpos(tlayer, &zpos);
             hwc_layer->zpos = zpos;
             hwc_layer->hwc = hwc;
          }

        hwc_layer = _e_comp_hwc_output_find_primary_layer(hwc_output);
        hwc_layer->primary = EINA_TRUE;
        hwc_output->primary_layer = hwc_layer; /* register the primary layer */
     }

   _e_comp_hwc_output_update_geom(hwc_output);

   /* get the evas_engine_gl_drm information */
   einfo = _e_comp_hwc_get_evas_engine_info_gl_drm(hwc);
   if (!einfo) return EINA_FALSE;

   /* enable hwc to evas engine gl_drm */
   einfo->info.hwc_enable = EINA_TRUE;

   evas_event_callback_add(e_comp->evas, EVAS_CALLBACK_RENDER_POST, _e_comp_hwc_display_canvas, hwc);

   return EINA_TRUE;

fail:
   _e_comp_hwc_remove(hwc);

   return EINA_FALSE;
}

EINTERN void
e_comp_hwc_shutdown(void)
{
   if (!e_comp) return;

   _e_comp_hwc_remove(g_hwc);
}


EINTERN void
e_comp_hwc_display_client(E_Client *ec)
{
   E_Comp_Hwc *hwc = g_hwc;
   E_Comp_Hwc_Output *hwc_output;
   E_Comp_Hwc_Layer *hwc_layer;
   Eina_List *l_o, *ll_o;
   tdm_output_conn_status conn_status;

   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(hwc);

   EINA_LIST_FOREACH_SAFE(hwc->hwc_outputs, l_o, ll_o, hwc_output)
     {
        if (!hwc_output) continue;
        tdm_output_get_conn_status(hwc_output->toutput, &conn_status);
        // TODO: check TDM_OUTPUT_CONN_STATUS_MODE_SETTED
        if (conn_status != TDM_OUTPUT_CONN_STATUS_CONNECTED) continue;
        if (hwc_output->mode == E_HWC_MODE_COMPOSITE) continue;

        switch (hwc_output->mode)
          {
           case E_HWC_MODE_COMPOSITE:
             // TODO:
             break;

           case E_HWC_MODE_NO_COMPOSITE:
             if (hwc_output->primary_layer)
               {
                  hwc_layer = hwc_output->primary_layer;
                  _e_comp_hwc_output_display_client(hwc_output, hwc_layer, ec);
               }
             else
               ERR("no primary layer");
             break;

           case E_HWC_MODE_HWC_COMPOSITE:
             // TODO:
             break;

           case E_HWC_MODE_HWC_NO_COMPOSITE:
             // TODO:
             break;

           default:
             break;
          }
     }
}

EINTERN Eina_Bool
e_comp_hwc_mode_nocomp(E_Client *ec)
{
   E_Comp_Hwc *hwc = g_hwc;
   E_Comp_Hwc_Output *hwc_output;
   Eina_List *l_o, *ll_o;
   tdm_output_conn_status conn_status;

   EINA_LIST_FOREACH_SAFE(hwc->hwc_outputs, l_o, ll_o, hwc_output)
     {
        if (!hwc_output) continue;
        tdm_output_get_conn_status(hwc_output->toutput, &conn_status);
        // TODO: check TDM_OUTPUT_CONN_STATUS_MODE_SETTED
        if (conn_status != TDM_OUTPUT_CONN_STATUS_CONNECTED) continue;
        /* make the policy to configure the layers with the client candidates */

        if (ec)
          {
#if USE_FIXED_SCANOUT
             if (!_e_comp_hwc_layer_client_can_activate(hwc_output->primary_layer, ec))
				 {
					ERR("fail to _e_comp_hwc_layer_client_activate");
					goto fail;
				 }
#endif

             if (e_comp->nocomp && e_comp->nocomp_ec != ec)
               {
                  INF("HWC:CHANGE NOCOMPOSITE Mode Client(%p) ==> ec(%p)", e_comp->nocomp_ec, ec);
#if USE_FIXED_SCANOUT
                  /* activate the wl client queue */
                  if (!_e_comp_hwc_layer_client_deactivate(hwc_output->primary_layer, e_comp->nocomp_ec))
                    {
                       ERR("fail to _e_comp_hwc_layer_client_activate");
                       goto fail;
                    }
#endif
               }

             INF("HWC:NOCOMPOSITE Mode Client(%p)", ec);

#if USE_FIXED_SCANOUT
             /* activate the wl client queue */
             if (!_e_comp_hwc_layer_client_activate(hwc_output->primary_layer, ec))
               {
                  ERR("fail to _e_comp_hwc_layer_client_activate");
                  goto fail;
               }
#endif

             hwc_output->mode = E_HWC_MODE_NO_COMPOSITE;
             hwc_output->primary_layer->ec = ec;

             /* do not Canvas(ecore_evas) render */
             hwc->einfo->info.wait_for_showup = EINA_TRUE;
          }
        else
          {
             INF("HWC: COMPOSITE Mode");
#if USE_FIXED_SCANOUT
             /* activate the wl client queue */
             if (!_e_comp_hwc_layer_client_deactivate(hwc_output->primary_layer, e_comp->nocomp_ec))
               {
                  ERR("fail to _e_comp_hwc_layer_client_activate");
                  goto fail;
               }
#endif
             hwc_output->mode = E_HWC_MODE_COMPOSITE;
             hwc_output->primary_layer->ec = NULL;

             /* do not Canvas(ecore_evas) render */
             hwc->einfo->info.wait_for_showup = EINA_FALSE;
          }
     }
   return EINA_TRUE;
#if USE_FIXED_SCANOUT
fail:
   // TODO: sanity check

   return EINA_FALSE;
#endif
}
