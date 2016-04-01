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

static const int key_renderer_state;
#define KEY_RENDERER_STATE ((unsigned long)&key_renderer_state)

#define USE_FIXED_SCANOUT 1

// trace debug
const Eina_Bool trace_debug = 1;

typedef struct _E_Comp_Hwc E_Comp_Hwc;
typedef struct _E_Comp_Hwc_Output E_Comp_Hwc_Output;
typedef struct _E_Comp_Hwc_Layer E_Comp_Hwc_Layer;
typedef struct _E_Comp_Hwc_Renderer E_Comp_Hwc_Renderer;
typedef struct _E_Comp_Hwc_Commit_Data E_Comp_Hwc_Commit_Data;

typedef enum _E_Hwc_Mode
{
   E_HWC_MODE_COMPOSITE = 1,        /* display only canvas */
   E_HWC_MODE_NO_COMPOSITE = 2,      /* display only one surface */
   E_HWC_MODE_HWC_COMPOSITE = 3,    /* display one or more surfaces and a canvas */
   E_HWC_MODE_HWC_NO_COMPOSITE = 4, /* display multi surfaces */
} E_Hwc_Mode;

typedef enum _E_HWC_RENDERER_STATE
{
   E_HWC_RENDERER_STATE_NONE,
   E_HWC_RENDERER_STATE_DEACTIVATE,
   E_HWC_RENDERER_STATE_ACTIVATE,
} E_HWC_RENDERER_STATE;

struct _E_Comp_Hwc_Commit_Data {
   E_Comp_Hwc_Layer *hwc_layer;
   tbm_surface_h tsurface;
   E_Client *ec;
   Eina_Bool is_canvas;
};

struct _E_Comp_Hwc_Renderer {
   tbm_surface_queue_h tqueue;

   E_Client *activated_ec;
   E_HWC_RENDERER_STATE state;

   struct gbm_surface *gsurface;
   Eina_List *disp_surfaces;
   Eina_List *sent_surfaces;

   E_Comp_Hwc_Layer *hwc_layer;
};

struct _E_Comp_Hwc_Layer {
   tbm_surface_queue_h tqueue;

   int index;
   tdm_layer *tlayer;
   tdm_info_layer info;
   int zpos;
   Eina_Bool primary;

   Eina_Bool pending;
   Eina_List *pending_surfaces;
   tbm_surface_h tsurface;

   E_Comp_Hwc_Renderer *hwc_renderer;
   E_Comp_Hwc_Output *hwc_output;
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

   Eina_Bool trace_debug;
};

// Global variable.
static E_Comp_Hwc *g_hwc = NULL;

static void
_e_comp_hwc_update_client_fps()
{
   static double time = 0.0;
   static double lapse = 0.0;
   static int cframes = 0;
   static int flapse = 0;

   if (e_comp->calc_fps)
     {
        double dt;
        double tim = ecore_time_get();

        dt = tim - e_comp->frametimes[0];
        e_comp->frametimes[0] = tim;

        time += dt;
        cframes++;

        if (lapse == 0.0)
          {
             lapse = tim;
             flapse = cframes;
          }
        else if ((tim - lapse) >= 1.0)
          {
             e_comp->fps = (cframes - flapse) / (tim - lapse);
             lapse = tim;
             flapse = cframes;
             time = 0.0;
          }
     }
}

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

static void
_e_comp_hwc_tbm_data_free(void *user_data)
{
   free(user_data);
}

static char *str_state[3] = {
    "UNKNOWN",
    "DEACTIVE",
    "ACTIVE",
};


static char *
_get_state_str(E_HWC_RENDERER_STATE state)
{
   return str_state[state];
}

E_HWC_RENDERER_STATE
_e_comp_hwc_get_renderer_state(tbm_surface_h tsurface)
{
   E_HWC_RENDERER_STATE *state = NULL;
   int ret = 0;

   ret = tbm_surface_internal_get_user_data(tsurface, KEY_RENDERER_STATE, (void **)&state);
   if (!ret)
     {
        tbm_surface_internal_add_user_data(tsurface, KEY_RENDERER_STATE, _e_comp_hwc_tbm_data_free);
        return E_HWC_RENDERER_STATE_NONE;
     }

   return *state;
}

static Eina_Bool
_e_comp_hwc_renderer_find_disp_surface(E_Comp_Hwc_Renderer *hwc_renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s, *ll_s;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_LIST_FOREACH_SAFE(hwc_renderer->disp_surfaces, l_s, ll_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_comp_hwc_renderer_find_sent_surface(E_Comp_Hwc_Renderer *hwc_renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s, *ll_s;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_LIST_FOREACH_SAFE(hwc_renderer->sent_surfaces, l_s, ll_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static tbm_surface_h
_e_comp_hwc_layer_queue_acquire(E_Comp_Hwc_Layer *hwc_layer, Eina_Bool is_canvas)
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

   /* set the current renderer_state to the tbm_surface user_data */
   E_HWC_RENDERER_STATE state = E_HWC_RENDERER_STATE_NONE;
   E_Comp_Hwc_Renderer *hwc_renderer = hwc_layer->hwc_renderer;

   state = _e_comp_hwc_get_renderer_state(tsurface);

   /* if different, set the current renderer state to the surface */
   if (state != hwc_renderer->state)
     {
        E_HWC_RENDERER_STATE *new_state = E_HWC_RENDERER_STATE_NONE;

        new_state = E_NEW(E_HWC_RENDERER_STATE, 1);
        *new_state = hwc_renderer->state;
        tbm_surface_internal_set_user_data(tsurface, KEY_RENDERER_STATE, (void *)new_state);
        state = *new_state;
     }

   /* if not exist, add the surface to the renderer */
   if (!_e_comp_hwc_renderer_find_disp_surface(hwc_renderer, tsurface))
     hwc_renderer->disp_surfaces = eina_list_append(hwc_renderer->disp_surfaces, tsurface);

   if (hwc_layer->hwc->trace_debug)
     {
        if (is_canvas)
          ELOGF("HWC", "Acquire Layer(%p) 	 tbm_surface(%p) tbm_surface_queue(%p) renderer_state(%s) surface_state(%s)",
                 NULL, NULL, hwc_layer, tsurface, tqueue,
                 _get_state_str(hwc_renderer->state), _get_state_str(state));

        else
          {
             E_Pixmap *pixmap = hwc_renderer->activated_ec->pixmap;
			 E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
             E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)hwc_renderer->activated_ec->comp_data;
			 E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata ->buffer_ref;

             ELOGF("HWC", "Acquire Layer(%p) 	 wl_buffer(%p) tbm_surface(%p) tbm_surface_queue(%p) renderer_state(%s) surface_state(%s) ref_wl_buffer(%p)",
                   NULL, NULL, hwc_layer, buffer->resource, tsurface, tqueue,
                   _get_state_str(hwc_renderer->state), _get_state_str(state), buffer_ref->buffer->resource);
          }
     }
   return tsurface;
}

static void
_e_comp_hwc_layer_queue_release(E_Comp_Hwc_Layer *hwc_layer, tbm_surface_h tsurface, E_Client *ec)
{
   tbm_surface_queue_h tqueue = hwc_layer->tqueue;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   E_Comp_Hwc_Renderer *hwc_renderer = hwc_layer->hwc_renderer;
   E_HWC_RENDERER_STATE state = E_HWC_RENDERER_STATE_NONE;

   state = _e_comp_hwc_get_renderer_state(tsurface);

   if (hwc_layer->hwc->trace_debug)
     {
        if (!ec)
          ELOGF("HWC", "Release Layer(%p)      tbm_surface(%p) tbm_surface_queue(%p) renderer_state(%s) surface_state(%s)",
                NULL, NULL, hwc_layer, tsurface, hwc_layer->tqueue, _get_state_str(hwc_renderer->state), _get_state_str(state));
       else
         {
			if (hwc_renderer->activated_ec)
              {
				  E_Pixmap *pixmap = ec->pixmap;
				  E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
				  E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)hwc_renderer->activated_ec->comp_data;
				  E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata ->buffer_ref;

                  ELOGF("HWC", "Release Layer(%p)      wl_buffer(%p) tbm_surface(%p) tbm_surface_queue(%p) renderer_state(%s) surface_state(%s) ref_wl_buffer(%p)",
                        NULL, NULL, hwc_layer, buffer->resource, tsurface, hwc_layer->tqueue,
                        _get_state_str(hwc_renderer->state), _get_state_str(state), buffer_ref->buffer->resource);
              }
            else
				ELOGF("HWC", "Release Layer(%p)       wl_buffer(NULL) tbm_surface(%p) tbm_surface_queue(%p) renderer_state(%s) surface_state(%s) ref_wl_buffer(NULL)",
					  NULL, NULL, hwc_layer, tsurface, hwc_layer->tqueue, _get_state_str(hwc_renderer->state), _get_state_str(state));

         }
     }

   tsq_err = tbm_surface_queue_release(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("Failed to release tbm_surface(%p) from tbm_surface_queue(%p): tsq_err = %d", tsurface, tqueue, tsq_err);
        return;
     }
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

static Eina_Bool
_e_comp_hwc_renderer_enqueue(E_Comp_Hwc_Renderer *hwc_renderer, tbm_surface_h tsurface)
{
   tbm_surface_queue_h tqueue = hwc_renderer->tqueue;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   E_HWC_RENDERER_STATE state = E_HWC_RENDERER_STATE_NONE;

   state = _e_comp_hwc_get_renderer_state(tsurface);

   if (hwc_renderer->hwc_layer->hwc->trace_debug)
    {
		E_Pixmap *pixmap = hwc_renderer->activated_ec->pixmap;
		E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
		E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)hwc_renderer->activated_ec->comp_data;
		E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata ->buffer_ref;

        ELOGF("HWC", "Enqueue Renderer(%p)   wl_buffer(%p) tbm_surface(%p) tbm_surface_queue(%p) renderer_state(%s) surface_state(%s) ref_wl_buffer(%p)",
              NULL, NULL, hwc_renderer, buffer->resource, tsurface, hwc_renderer->tqueue,
              _get_state_str(hwc_renderer->state), _get_state_str(state), buffer_ref->buffer->resource);
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
_e_comp_hwc_renderer_dequeue(E_Comp_Hwc_Renderer *hwc_renderer)
{
   tbm_surface_queue_h tqueue = hwc_renderer->tqueue;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   E_HWC_RENDERER_STATE state = E_HWC_RENDERER_STATE_NONE;

   tsq_err = tbm_surface_queue_dequeue(tqueue, &tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail to tbm_surface_queue_dequeue");
        return NULL;
     }


   state = _e_comp_hwc_get_renderer_state(tsurface);

   /* if different, set the current renderer state to the surface */
   if (state != hwc_renderer->state)
     {
        E_HWC_RENDERER_STATE *new_state = E_HWC_RENDERER_STATE_NONE;

        new_state = E_NEW(E_HWC_RENDERER_STATE, 1);
        *new_state = hwc_renderer->state;
        tbm_surface_internal_set_user_data(tsurface, KEY_RENDERER_STATE, (void *)new_state);
        state = *new_state;
     }

   if (hwc_renderer->hwc_layer->hwc->trace_debug)
     ELOGF("HWC", "Dequeue Renderer(%p)   tbm_surface(%p) tbm_surface_queue(%p) renderer_state(%s) surface_state(%s)",
           NULL, NULL, hwc_renderer, tsurface, hwc_renderer->tqueue, _get_state_str(hwc_renderer->state), _get_state_str(state));

   return tsurface;
}

static void
_e_comp_hwc_renderer_release_sent_surfaces(E_Comp_Hwc_Renderer *hwc_renderer)
{
   Eina_List *l_s, *ll_s;
   tbm_surface_h tsurface = NULL;

#if 1
   EINA_LIST_FOREACH_SAFE(hwc_renderer->disp_surfaces, l_s, ll_s, tsurface)
     {
        if (!tsurface) continue;

        /* release the surface to the layer_queue */
        _e_comp_hwc_layer_queue_release(hwc_renderer->hwc_layer, tsurface, NULL);
     }
#else
   EINA_LIST_FOREACH_SAFE(hwc_renderer->sent_surfaces, l_s, ll_s, tsurface)
     {
        if (!tsurface) continue;
        hwc_renderer->sent_surfaces = eina_list_remove(hwc_renderer->sent_surfaces, tsurface);

        /* release the surface to the layer_queue */
        _e_comp_hwc_layer_queue_release(hwc_renderer->hwc_layer, tsurface, NULL);
     }
#endif
}

static void
_e_comp_hwc_renderer_surface_destroy_cb(tbm_surface_h tsurface, void *data)
{
   E_Comp_Hwc_Renderer *hwc_renderer = NULL;

   EINA_SAFETY_ON_NULL_RETURN(tsurface);
   EINA_SAFETY_ON_NULL_RETURN(data);

   hwc_renderer = (E_Comp_Hwc_Renderer *)data;

#if 1
   /* actived_ec is null */
   hwc_renderer->activated_ec = NULL;
#endif

   if (hwc_renderer->hwc_layer->hwc->trace_debug)
     ELOGF("HWC", "Destroy Renderer(%p)	 tbm_surface(%p) tbm_surface_queue(%p)",
           NULL, NULL, hwc_renderer, tsurface, hwc_renderer->tqueue);
}

static void
_e_comp_hwc_renderer_export_all_disp_surfaces(E_Comp_Hwc_Renderer *hwc_renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   Eina_List *l_s, *ll_s;
   tbm_surface_h tsurface = NULL;
   int ret = 0;

   cqueue = _e_comp_hwc_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(cqueue);

   EINA_LIST_FOREACH_SAFE(hwc_renderer->disp_surfaces, l_s, ll_s, tsurface)
     {
        if (!tsurface) continue;

        /* export the tbm_surface(wl_buffer) to the client_queue */
        ret = wayland_tbm_server_client_queue_export_buffer(cqueue, tsurface,
                               HWC_SURFACE_TYPE_PRIMARY, _e_comp_hwc_renderer_surface_destroy_cb,
                               (void *)hwc_renderer);

        /* add a sent surface to the sent list in renderer if it is not in the list */
        if (!_e_comp_hwc_renderer_find_sent_surface(hwc_renderer, tsurface))
           hwc_renderer->sent_surfaces = eina_list_append(hwc_renderer->sent_surfaces, tsurface);

        if (ret && hwc_renderer->hwc_layer->hwc->trace_debug)
          ELOGF("HWC", "Export	Renderer(%p)   tbm_surface(%p) tbm_surface_queue(%p)",
                ec->pixmap, ec, hwc_renderer, tsurface, hwc_renderer->tqueue);
     }
}

static void
_e_comp_hwc_renderer_export_disp_surface(E_Comp_Hwc_Renderer *hwc_renderer, E_Client *ec, tbm_surface_h tsurface)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   int ret = 0;

   cqueue = _e_comp_hwc_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(cqueue);

   /* export the tbm_surface(wl_buffer) to the client_queue */
   ret = wayland_tbm_server_client_queue_export_buffer(cqueue, tsurface,
   					HWC_SURFACE_TYPE_PRIMARY, _e_comp_hwc_renderer_surface_destroy_cb,
   					(void *)hwc_renderer);

   if (ret && hwc_renderer->hwc_layer->hwc->trace_debug)
     ELOGF("HWC", "Export  Renderer(%p)   tbm_surface(%p) tbm_surface_queue(%p)",
           ec->pixmap, ec, hwc_renderer, tsurface, hwc_renderer->tqueue);
}

static tbm_surface_h
_e_comp_hwc_renderer_recieve_surface(E_Comp_Hwc_Renderer *hwc_renderer, E_Client *ec)
{
	tbm_surface_h tsurface = NULL;
	E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
	E_Pixmap *pixmap = ec->pixmap;
	uint32_t flags = 0;
	E_Comp_Wl_Buffer *buffer = NULL;

	if (hwc_renderer->activated_ec != ec)
	  {
		 ERR("Renderer(%p)   activated_ec(%p) != ec(%p)", hwc_renderer, hwc_renderer->activated_ec, ec);
		 return NULL;
	  }

	buffer = e_pixmap_resource_get(pixmap);
	EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, NULL);

	tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
	EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, NULL);

	flags = wayland_tbm_server_get_buffer_flags(wl_comp_data->tbm.server, buffer->resource);

	if (hwc_renderer->hwc_layer->hwc->trace_debug)
      {
		E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)hwc_renderer->activated_ec->comp_data;
		E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata ->buffer_ref;

        ELOGF("HWC", "Receive Renderer(%p)   wl_buffer(%p) tbm_surface(%p) tbm_surface_queue(%p) flags(%d) ref_wl_buffer(%p)",
              NULL, NULL, hwc_renderer, buffer->resource, tsurface, hwc_renderer->tqueue, flags, buffer_ref->buffer->resource);
      }
	if (flags != HWC_SURFACE_TYPE_PRIMARY)
	  {
		 ERR("the flags of the enqueuing surface is %d. need flags(%d).", flags, HWC_SURFACE_TYPE_PRIMARY);
		 return NULL;
	  }

   /* remove a recieved surface from the sent list in renderer */
   hwc_renderer->sent_surfaces = eina_list_remove(hwc_renderer->sent_surfaces, (const void *)tsurface);

   return tsurface;
}

static void
_e_comp_hwc_renderer_send_surface(E_Comp_Hwc_Renderer *hwc_renderer, E_Client *ec, tbm_surface_h tsurface, Eina_Bool release)
{
   if (hwc_renderer->hwc_layer->hwc->trace_debug)
     ELOGF("HWC", "Send    Renderer(%p)   tbm_surface(%p) tbm_surface_queue(%p)",
           NULL, NULL, hwc_renderer, tsurface, hwc_renderer->tqueue);

   /* wl_buffer release */
   if (release)
     e_pixmap_image_clear(ec->pixmap, 1);

   /* export the surface to the client */
   _e_comp_hwc_renderer_export_disp_surface(hwc_renderer, ec, tsurface);

   /* add a sent surface to the sent list in renderer if it is not in the list */
   if (!_e_comp_hwc_renderer_find_sent_surface(hwc_renderer, tsurface))
     hwc_renderer->sent_surfaces = eina_list_append(hwc_renderer->sent_surfaces, tsurface);
}

static Eina_Bool
_e_comp_hwc_renderer_can_activate(E_Comp_Hwc_Renderer *hwc_renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_surface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;

   if (hwc_renderer->hwc_layer->hwc->trace_debug)
     ELOGF("HWC", "Check Activate", ec->pixmap, ec);

   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_comp_data, EINA_FALSE);

   wl_surface = _e_comp_hwc_wl_surface_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_surface, EINA_FALSE);

   cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* check if the layer queue is dequeueable */
   if(!tbm_surface_queue_can_dequeue(hwc_renderer->tqueue, 0))
     {
        WRN("fail to tbm_surface_queue_can_dequeue");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_comp_hwc_renderer_activate(E_Comp_Hwc_Renderer *hwc_renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_h tqueue = hwc_renderer->tqueue;

   if (hwc_renderer->hwc_layer->hwc->trace_debug)
     ELOGF("HWC", "Activate", ec->pixmap, ec);

   if (hwc_renderer->state == E_HWC_RENDERER_STATE_ACTIVATE) return EINA_TRUE;

   /* deactivate the client of the layer before this call*/
   if (hwc_renderer->activated_ec)
     {
        ERR("Previous activated client must be decativated.");
        return EINA_FALSE;
     }

   cqueue = _e_comp_hwc_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* activate the client queue */
   wayland_tbm_server_client_queue_activate(cqueue, 0);

#if 1
   /* dequeue the surfaces if the qeueue is available */
   /* add the surface to the disp_surfaces list, if it is not in the disp_surfaces */
   while (tbm_surface_queue_can_dequeue(tqueue, 0))
     {
        /* dequeue */
        tsurface = _e_comp_hwc_renderer_dequeue(hwc_renderer);
        if (!tsurface)
          {
            ERR("fail to dequeue surface");
            return EINA_FALSE;
          }
        /* if not exist, add the surface to the renderer */
        if (!_e_comp_hwc_renderer_find_disp_surface(hwc_renderer, tsurface))
          hwc_renderer->disp_surfaces = eina_list_append(hwc_renderer->disp_surfaces, tsurface);
     }

   _e_comp_hwc_renderer_export_all_disp_surfaces(hwc_renderer, ec);
#else
   /* dequeue the surfaces if the qeueue is available */
   /* and export them to the client */
   while (tbm_surface_queue_can_dequeue(tqueue, 0))
     {
        /* dequeue */
        tsurface = _e_comp_hwc_renderer_dequeue(hwc_renderer);
        if (!tsurface)
          {
             ERR("fail to dequeue surface");
             return EINA_FALSE;
          }

		_e_comp_hwc_renderer_export_disp_surface(hwc_renderer, ec, tsurface);

        /* add a sent surface to the sent list in renderer if it is not in the list */
        if (!_e_comp_hwc_renderer_find_sent_surface(hwc_renderer, tsurface))
           hwc_renderer->sent_surfaces = eina_list_append(hwc_renderer->sent_surfaces, tsurface);
     }
#endif
   /* wl_buffer release */
   e_pixmap_image_clear(ec->pixmap, 1);

   hwc_renderer->state = E_HWC_RENDERER_STATE_ACTIVATE;
   hwc_renderer->activated_ec = ec;

   return EINA_TRUE;
}

static Eina_Bool
_e_comp_hwc_renderer_deactivate(E_Comp_Hwc_Renderer *hwc_renderer)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_surface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Client *ec = NULL;

   if (!hwc_renderer->activated_ec)
     {
        if (hwc_renderer->hwc_layer->hwc->trace_debug)
          ELOGF("HWC", "Deactivate Client is gone before.", NULL, NULL);
        goto done;
     }

   ec = hwc_renderer->activated_ec;

   if (hwc_renderer->hwc_layer->hwc->trace_debug)
     ELOGF("HWC", "Deactivate", ec->pixmap, ec);

   if (hwc_renderer->state == E_HWC_RENDERER_STATE_DEACTIVATE) return EINA_TRUE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_comp_data, EINA_FALSE);

   wl_surface = _e_comp_hwc_wl_surface_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_surface, EINA_FALSE);

   cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* deactive */
   wayland_tbm_server_client_queue_deactivate(cqueue);

done:
   hwc_renderer->state = E_HWC_RENDERER_STATE_DEACTIVATE;
   hwc_renderer->activated_ec = NULL;

   /* enqueue the tsurfaces to the layer queue */
   _e_comp_hwc_renderer_release_sent_surfaces(hwc_renderer);

   return EINA_TRUE;
}
#endif

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
             ELOGF("HWC", "find output for '%s'", NULL, NULL, s->info.name);

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

             ELOGF("HWC", "%s %d,%d,%d,%d", NULL, NULL, s->info.name, x, y, w, h);
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
             ELOGF("HWC", "TDM_LAYER_CAPABILITY_PRIMARY layer found : %d", NULL, NULL, hwc_layer->index);
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
   E_Comp_Hwc_Renderer *hwc_renderer = NULL;
   E_Client *ec = NULL;
   tbm_surface_h tsurface = NULL;

   if (!data) return;
   if (!data->hwc_layer) return;

   hwc_layer = data->hwc_layer;
   hwc_renderer = hwc_layer->hwc_renderer;
   tsurface = data->tsurface;

#if USE_FIXED_SCANOUT
   tbm_surface_h send_tsurface = NULL;
 #endif

   if (data->is_canvas && hwc_layer->primary)
     {
		 if (hwc_layer->hwc->trace_debug)
           ELOGF("HWC", "Done    Layer(%p)      tbm_surface(%p) tbm_surface_queue(%p) data(%p)::Canvas",
                 NULL, NULL, hwc_layer, tsurface, hwc_layer->tqueue, data);

        if (!hwc_renderer->activated_ec)
          hwc_layer->hwc->einfo->info.wait_for_showup = EINA_FALSE;

        /* release */
        _e_comp_hwc_layer_queue_release(hwc_layer, tsurface, NULL);
     }
   else
     {
        if (hwc_layer->hwc->trace_debug)
          {
              if (hwc_renderer->activated_ec)
                {
					E_Pixmap *pixmap = data->ec->pixmap;
					E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
					E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)hwc_renderer->activated_ec->comp_data;
					E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata ->buffer_ref;

                    ELOGF("HWC", "Done    Layer(%p)      wl_buffer(%p) tbm_surface(%p) tbm_surface_queue(%p) data(%p) ref_wl_buffer(%p) ::Client",
                          data->ec->pixmap, data->ec, hwc_layer, buffer->resource, tsurface, hwc_layer->tqueue,
                          data, buffer_ref->buffer->resource);
                 }
              else
                ELOGF("HWC", "Done	Layer(%p)	   wl_buffer(NULL) tbm_surface(%p) tbm_surface_queue(%p) data(%p) ref_wl_buffer(NULL) ::Client",
                	data->ec->pixmap, data->ec, hwc_layer, tsurface, hwc_layer->tqueue, data);

           }
#if USE_FIXED_SCANOUT
        /* release */
        _e_comp_hwc_layer_queue_release(hwc_layer, tsurface, data->ec);
#else
        /* release */
        e_pixmap_image_clear(ec->pixmap, 1);
#endif
     }

#if USE_FIXED_SCANOUT
   /* send the done surface to the client  */
   /* , only when the renderer state is active(no composite) */
   if (hwc_renderer->activated_ec &&
       hwc_renderer->state == E_HWC_RENDERER_STATE_ACTIVATE)
     {
        /* dequeue */
        send_tsurface = _e_comp_hwc_renderer_dequeue(hwc_renderer);
        if (!send_tsurface)
          ERR("fail to dequeue surface");
        _e_comp_hwc_renderer_send_surface(hwc_renderer, hwc_renderer->activated_ec, send_tsurface, EINA_TRUE);
     }
#endif

   tbm_surface_internal_unref(tsurface);
   _e_comp_hwc_commit_data_destroy(data);

}

static Eina_Bool
_e_comp_hwc_output_commit(E_Comp_Hwc_Output *hwc_output, E_Comp_Hwc_Layer *hwc_layer, tbm_surface_h tsurface, Eina_Bool is_canvas)
{
   tbm_surface_info_s surf_info;
   tdm_error error;
   tdm_output *toutput = hwc_output->toutput;
   tdm_layer *tlayer = hwc_layer->tlayer;
   E_Comp_Hwc_Renderer *hwc_renderer = hwc_layer->hwc_renderer;
   E_Comp_Hwc_Commit_Data *data = NULL;
   tdm_output_dpms dpms_value;

   error = tdm_output_get_dpms(toutput, &dpms_value);
   if (error != TDM_ERROR_NONE)
     {
		 ERR("fail to get tdm_output_dpms value");
		 return EINA_FALSE;
     }

   /* check if the output is off */
   if (dpms_value != TDM_OUTPUT_DPMS_ON)
     {
		 WRN("DPMS IS NOT ON");
		 return EINA_FALSE;
     }

   data = _e_comp_hwc_commit_data_create();
   if (!data) return EINA_FALSE;
   data->hwc_layer = hwc_layer;
   tbm_surface_internal_ref(tsurface);
   data->tsurface = tsurface;
   /* hwc_renderer->activated_ec can be changed at the time of commit handler
      , so we stores the current activated_ec at data */
   if (hwc_renderer->activated_ec)
     data->ec = hwc_renderer->activated_ec;
   data->is_canvas = is_canvas;

   /* set layer when the layer infomation is different from the previous one */
   tbm_surface_get_info(tsurface, &surf_info);
   if (hwc_layer->info.src_config.size.h != surf_info.planes[0].stride ||
       hwc_layer->info.src_config.size.v != surf_info.height ||
       hwc_layer->info.src_config.pos.x != 0 ||
       hwc_layer->info.src_config.pos.y != 0 ||
       hwc_layer->info.src_config.pos.w != surf_info.width ||
       hwc_layer->info.src_config.pos.h != surf_info.height ||
       hwc_layer->info.dst_pos.x != hwc_output->x ||
       hwc_layer->info.dst_pos.y != hwc_output->y ||
       hwc_layer->info.dst_pos.w != hwc_output->w ||
       hwc_layer->info.dst_pos.h != hwc_output->h ||
       hwc_layer->info.transform != TDM_TRANSFORM_NORMAL)
     {
        hwc_layer->info.src_config.size.h = surf_info.planes[0].stride;
        hwc_layer->info.src_config.size.v = surf_info.height;
        hwc_layer->info.src_config.pos.x = 0;
        hwc_layer->info.src_config.pos.y = 0;
        hwc_layer->info.src_config.pos.w = surf_info.width;
        hwc_layer->info.src_config.pos.h = surf_info.height;
        hwc_layer->info.dst_pos.x = hwc_output->x;
        hwc_layer->info.dst_pos.y = hwc_output->y;
        hwc_layer->info.dst_pos.w = hwc_output->w;
        hwc_layer->info.dst_pos.h = hwc_output->h;
        hwc_layer->info.transform = TDM_TRANSFORM_NORMAL;

        error = tdm_layer_set_info(tlayer, &hwc_layer->info);
        if (error != TDM_ERROR_NONE)
          {
             _e_comp_hwc_commit_data_destroy(data);
             ERR("fail to tdm_layer_set_info");
             return EINA_FALSE;
          }
     }

   if (hwc_layer->hwc->trace_debug)
	 ELOGF("HWC", "Commit  Layer(%p)       tbm_surface(%p) (%dx%d,[%d,%d,%d,%d]=>[%d,%d,%d,%d]) is_canvas(%d) data(%p)",
		   NULL, NULL, hwc_layer, tsurface,
		   hwc_layer->info.src_config.size.h, hwc_layer->info.src_config.size.h,
		   hwc_layer->info.src_config.pos.x, hwc_layer->info.src_config.pos.y,
		   hwc_layer->info.src_config.pos.w, hwc_layer->info.src_config.pos.h,
		   hwc_layer->info.dst_pos.x, hwc_layer->info.dst_pos.y,
		   hwc_layer->info.dst_pos.w, hwc_layer->info.dst_pos.h, is_canvas, data);
   else
     {
		 E_Pixmap *pixmap = hwc_renderer->activated_ec->pixmap;
		 E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
		 E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)hwc_renderer->activated_ec->comp_data;
		 E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata ->buffer_ref;

		 ELOGF("HWC", "Commit  Layer(%p)       wl_buffer(%p) tbm_surface(%p) (%dx%d,[%d,%d,%d,%d]=>[%d,%d,%d,%d]) is_canvas(%d) data(%p) ref_wl_buffer(%p)",
			   NULL, NULL, hwc_layer, buffer->resource, tsurface,
			   hwc_layer->info.src_config.size.h, hwc_layer->info.src_config.size.h,
			   hwc_layer->info.src_config.pos.x, hwc_layer->info.src_config.pos.y,
			   hwc_layer->info.src_config.pos.w, hwc_layer->info.src_config.pos.h,
			   hwc_layer->info.dst_pos.x, hwc_layer->info.dst_pos.y,
			   hwc_layer->info.dst_pos.w, hwc_layer->info.dst_pos.h, is_canvas, data, buffer_ref->buffer->resource);
     }

   error = tdm_layer_set_buffer(tlayer, tsurface);
   if (error != TDM_ERROR_NONE)
     {
        _e_comp_hwc_commit_data_destroy(data);
        ERR("fail to tdm_layer_set_buffer");
        return EINA_FALSE;
     }

   error = tdm_output_commit(toutput, 0, _e_comp_hwc_output_commit_handler, data);
   if (error != TDM_ERROR_NONE)
     {
        _e_comp_hwc_commit_data_destroy(data);
        ERR("fail to tdm_output_commit");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_comp_hwc_output_display_client(E_Comp_Hwc_Output *hwc_output, E_Comp_Hwc_Layer *hwc_layer, E_Client *ec)
{
	if (hwc_layer->hwc->trace_debug)
      ELOGF("HWC", "Display Client", ec->pixmap, ec);

   E_Comp_Hwc_Renderer *hwc_renderer = hwc_layer->hwc_renderer;

#if USE_FIXED_SCANOUT
   E_Pixmap *pixmap = ec->pixmap;
   tbm_surface_h tsurface = NULL;

   /* acquire the surface from the client_queue */
   tsurface = _e_comp_hwc_renderer_recieve_surface(hwc_renderer, ec);
   if (!tsurface)
     {
        e_pixmap_image_clear(pixmap, 1);
        ERR("fail to _e_comp_hwc_renderer_recieve_surface");
        return;
     }

   /* enqueue the surface to the layer_queue */
   if (!_e_comp_hwc_renderer_enqueue(hwc_renderer, tsurface))
     {
        _e_comp_hwc_renderer_send_surface(hwc_renderer, ec, tsurface, EINA_TRUE);
        ERR("fail to _e_comp_hwc_renderer_enqueue");
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

   /* commit */
   if (!_e_comp_hwc_output_commit(hwc_output, hwc_layer, tsurface, EINA_TRUE))
     {
        e_pixmap_image_clear(pixmap, 1);
        ERR("fail to _e_comp_hwc_output_commit");
        return;
     }
#endif
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
_e_comp_hwc_layer_queue_acquirable_cb(tbm_surface_queue_h surface_queue, void *data)
{
   E_Comp_Hwc_Layer *hwc_layer = (E_Comp_Hwc_Layer *)data;
   Evas_Engine_Info_GL_Drm *einfo = NULL;
   tbm_surface_h tsurface = NULL;

   Eina_Bool is_canvas = EINA_TRUE;
   E_Comp_Hwc_Renderer *hwc_renderer = hwc_layer->hwc_renderer;

   if (hwc_renderer->state == E_HWC_RENDERER_STATE_DEACTIVATE) is_canvas = EINA_TRUE;
   else is_canvas = EINA_FALSE;

   /* get the evas_engine_gl_drm information */
   einfo = _e_comp_hwc_get_evas_engine_info_gl_drm(hwc_layer->hwc);
   if (!einfo) return;

   /* aquire */
   tsurface = _e_comp_hwc_layer_queue_acquire(hwc_layer, is_canvas);
   if (!tsurface)
     {
        ERR("fail _e_comp_hwc_layer_queue_acquire");
        return;
     }

   if (!is_canvas)
      _e_comp_hwc_update_client_fps();

   /* commit */
   if (!_e_comp_hwc_output_commit(hwc_layer->hwc_output, hwc_layer, tsurface, is_canvas))
	 {
        if (is_canvas)
    	  _e_comp_hwc_layer_queue_release(hwc_layer, tsurface, NULL);
        else
          {
             _e_comp_hwc_layer_queue_release(hwc_layer, tsurface, hwc_renderer->activated_ec);
             e_pixmap_image_clear(hwc_renderer->activated_ec->pixmap, EINA_TRUE);
          }
		ERR("fail to _e_comp_hwc_output_commit");
		return;
	 }

   /* block the next update of ecore_evas until the current update is done */
   if (is_canvas)
     einfo->info.wait_for_showup = EINA_TRUE;
}

static void
_e_comp_hwc_renderer_queue_dequeuable_cb(tbm_surface_queue_h surface_queue, void *data)
{
#if 0
   E_Comp_Hwc_Renderer *hwc_renderer = (E_Comp_Hwc_Renderer *)data;

   if (hwc_renderer->state == E_HWC_RENDERER_STATE_DEACTIVATE)
     {
        INF("##soolim: DEQUEUABLE DDK");
     }
   else
     {
	    INF("##soolim: DEQUEUABLE CLIENT");
     }
#endif
}

static void
_e_comp_hwc_display_canvas(void *data EINA_UNUSED, Evas *e EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Hwc *hwc = data;
   Evas_Engine_Info_GL_Drm *einfo = NULL;
   E_Comp_Hwc_Output *hwc_output = NULL;;
   E_Comp_Hwc_Layer *hwc_layer = NULL;;
   E_Comp_Hwc_Renderer *hwc_renderer = NULL;
   Eina_List *l_o, *ll_o;
   tdm_output_conn_status conn_status;
   tbm_surface_h tsurface = NULL;

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
        if (hwc->trace_debug)
          ELOGF("HWC", "Commit Canvas outbuf flush nothing!. Nothing Display.", NULL, NULL);
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
        hwc_renderer = hwc_layer->hwc_renderer;
        if (hwc_renderer->gsurface != einfo->info.surface)
          {
             hwc_renderer->gsurface = einfo->info.surface;
             hwc_renderer->tqueue = gbm_tbm_get_surface_queue(hwc_renderer->gsurface);
             if (!hwc_renderer->tqueue)
               {
                  ERR("no hwc_renderer->tqueue");
                  continue;
               }
             hwc_layer->tqueue = hwc_renderer->tqueue;

             tbm_surface_queue_add_acquirable_cb(hwc_layer->tqueue, _e_comp_hwc_layer_queue_acquirable_cb, hwc_layer);
             tbm_surface_queue_add_dequeuable_cb(hwc_renderer->tqueue, _e_comp_hwc_renderer_queue_dequeuable_cb, hwc_renderer);

			 if (hwc_layer->hwc->trace_debug)
			   ELOGF("HWC", "Display Canvas Layer(%p)", NULL, NULL, hwc_layer);

			 /* aquire */
			 tsurface = _e_comp_hwc_layer_queue_acquire(hwc_layer, EINA_TRUE);
			 if (!tsurface)
			   {
				  ERR("tsurface is NULL");
				  continue;
			   }

           /* commit */
           if (!_e_comp_hwc_output_commit(hwc_output, hwc_layer, tsurface, EINA_TRUE))
             {
                _e_comp_hwc_layer_queue_release(hwc_layer, tsurface, NULL);
                ERR("fail to _e_comp_hwc_output_commit");
                continue;
             }

           /* block the next update of ecore_evas until the current update is done */
           einfo->info.wait_for_showup = EINA_TRUE;
        }
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
             if (hwc_layer->hwc_renderer) free(hwc_layer->hwc_renderer);
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
   E_Comp_Hwc_Renderer *hwc_renderer = NULL;


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

   hwc->trace_debug = trace_debug;

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
             hwc_layer->hwc_output = hwc_output;
             hwc_layer->hwc = hwc;

             hwc_renderer = E_NEW(E_Comp_Hwc_Renderer, 1);
             if (!hwc_renderer) goto fail;
             hwc_renderer->hwc_layer = hwc_layer;
             hwc_renderer->state = E_HWC_RENDERER_STATE_DEACTIVATE;

             hwc_layer->hwc_renderer = hwc_renderer;
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
   E_Comp_Hwc_Renderer *hwc_renderer = NULL;


   EINA_LIST_FOREACH_SAFE(hwc->hwc_outputs, l_o, ll_o, hwc_output)
     {
        if (!hwc_output) continue;
        tdm_output_get_conn_status(hwc_output->toutput, &conn_status);
        // TODO: check TDM_OUTPUT_CONN_STATUS_MODE_SETTED
        if (conn_status != TDM_OUTPUT_CONN_STATUS_CONNECTED) continue;
        /* make the policy to configure the layers with the client candidates */

        hwc_renderer = hwc_output->primary_layer->hwc_renderer;

        if (ec)
          {
#if USE_FIXED_SCANOUT
             if (!_e_comp_hwc_renderer_can_activate(hwc_renderer, ec))
				 {
					ERR("fail to _e_comp_hwc_renderer_can_activate");
					goto fail;
				 }
#endif

#if USE_FIXED_SCANOUT
             /* activate the wl client queue */
             if (!_e_comp_hwc_renderer_activate(hwc_renderer, ec))
               {
                  ERR("fail to _e_comp_hwc_renderer_activate");
                  goto fail;
               }
#endif

             hwc_output->mode = E_HWC_MODE_NO_COMPOSITE;

             /* do not Canvas(ecore_evas) render */
             hwc->einfo->info.wait_for_showup = EINA_TRUE;

             if (hwc_renderer->hwc_layer->hwc->trace_debug)
               ELOGF("HWC", "NOCOMPOSITE Mode", ec->pixmap, ec);
          }
        else
          {
#if USE_FIXED_SCANOUT
             /* activate the wl client queue */
             if (!_e_comp_hwc_renderer_deactivate(hwc_renderer))
               {
                  ERR("fail to _e_comp_hwc_renderer_deactivate");
               }
#endif
             hwc_output->mode = E_HWC_MODE_COMPOSITE;

             /* do not Canvas(ecore_evas) render */
             hwc->einfo->info.wait_for_showup = EINA_FALSE;

             if (hwc_renderer->hwc_layer->hwc->trace_debug)
               ELOGF("HWC", "COMPOSITE Mode", NULL, NULL);
          }
     }
   return EINA_TRUE;
#if USE_FIXED_SCANOUT
fail:
   // TODO: sanity check

   return EINA_FALSE;
#endif
}
