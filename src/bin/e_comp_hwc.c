#include "e.h"
#include "e_comp_wl.h"
#include "e_comp_hwc.h"

#include <Ecore_Drm.h>
#include <Evas_Engine_GL_Drm.h>

#include <gbm/gbm_tbm.h>
#include <tdm.h>
#include <tdm_helper.h>
#include <wayland-tbm-server.h>

//#define TDM_DISPLAY_HANDLE_EVENT
#define HWC_DRM_MODE_DPMS_OFF 3
#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof (x))
#endif

typedef struct _E_Comp_Hwc E_Comp_Hwc;
typedef struct _E_Comp_Hwc_Update_Data E_Comp_Hwc_Update_Data;
typedef struct _E_Comp_Hwc_Hw_Overlay E_Comp_Hwc_Hw_Overlay;

struct _E_Comp_Hwc_Hw_Overlay {
   E_Client *ec;
};

struct _E_Comp_Hwc_Update_Data {
   Eina_List *hw_overlays;
};

struct _E_Comp_Hwc {
   Evas_Engine_Info_GL_Drm *einfo;

   int drm_fd;
   Ecore_Fd_Handler *drm_hdlr;

   Ecore_Drm_Output *drm_output;

   struct gbm_surface *gsurface;
   struct gbm_bo *cur_gbo;

   tdm_display *tdisplay;
   int num_outputs;
   tdm_output *output;
   tdm_layer *ee_tlayer;
   int output_w;
   int output_h;
   Eina_Bool ee_wait_vblank;

   E_Client *nocomp_ec;

   int num_overlays;
   E_Comp_Hwc_Update_Data update_data;
};

// Global variable.
static E_Comp_Hwc *g_hwc = NULL;

#ifdef TDM_DISPLAY_HANDLE_EVENT
static void
_e_comp_hwc_tdm_output_wait_vblank_handler(tdm_output *output, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec, void *user_data)
{
   E_Comp_Hwc *hwc = NULL;

   hwc = user_data;

   if (!hwc) return;

   hwc->ee_wait_vblank = EINA_FALSE;
   hwc->einfo->info.wait_for_showup = EINA_FALSE;

   gbm_surface_release_buffer(hwc->gsurface, hwc->cur_gbo);

   INF("HWC: done wait vblank");
}
#endif


static Eina_Bool
_e_comp_hwc_set_layer(E_Comp_Hwc *hwc, tdm_layer *layer, tbm_surface_h surface)
{
   tdm_info_layer info;
   tbm_surface_info_s surf_info;
   tdm_error error;

   tbm_surface_get_info(surface, &surf_info);

   CLEAR(info);
   info.src_config.size.h = surf_info.planes[0].stride;
   info.src_config.size.v = surf_info.height;
   info.src_config.pos.x = 0;
   info.src_config.pos.y = 0;
   info.src_config.pos.w = surf_info.width;
   info.src_config.pos.h = surf_info.height;
   info.dst_pos.x = 0;
   info.dst_pos.y = 0;
   info.dst_pos.w = hwc->output_w;
   info.dst_pos.h = hwc->output_h;
   info.transform = TDM_TRANSFORM_NORMAL;

   error = tdm_layer_set_info(layer, &info);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_set_info");
        return EINA_FALSE;
     }
   error = tdm_layer_set_buffer(layer, surface);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_set_buffer");
        return EINA_FALSE;
     }

   error = tdm_output_commit(hwc->output, 0, NULL, NULL);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_output_commit");
        return EINA_FALSE;
     }

   INF("HWC: (%dx%d,[%d,%d,%d,%d]=>[%d,%d,%d,%d])\n",
       info.src_config.size.h, info.src_config.size.h,
       info.src_config.pos.x, info.src_config.pos.y, info.src_config.pos.w, info.src_config.pos.h,
       info.dst_pos.x, info.dst_pos.y, info.dst_pos.w, info.dst_pos.h);

   return EINA_TRUE;
}

static void
_e_comp_hwc_ec_wait_vblank_handler(void *data)
{
   E_Comp_Hwc *hwc = NULL;
   E_Client *ec = NULL;

   hwc = data;

   if (!hwc) return;

   ec = hwc->nocomp_ec;

   // release buffer
   e_pixmap_image_clear(ec->pixmap, 1);

   INF("HWC: done wait vblank E_Client");
}

static void
_e_comp_hwc_ee_wait_vblank_handler(void *data)
{
   E_Comp_Hwc *hwc = NULL;

   hwc = data;

   if (!hwc) return;

   hwc->ee_wait_vblank = EINA_FALSE;
   hwc->einfo->info.wait_for_showup = EINA_FALSE;

   gbm_surface_release_buffer(hwc->gsurface, hwc->cur_gbo);

   INF("HWC: done wait vblank Canvas");
}

static Eina_Bool
_e_comp_hwc_ec_wait_vblank(E_Comp_Hwc *hwc)
{
   /* If not DPMS_ON, we call vblank handler directory to do post-process
    * for video frame buffer handling.
    */
   if (ecore_drm_output_dpms_get(hwc->output) == HWC_DRM_MODE_DPMS_OFF)
     {
        WRN("warning : DRM_MODE_DPMS_OFF\n");
        return EINA_FALSE;
     }

   if (!ecore_drm_output_wait_vblank(hwc->drm_output, 1, _e_comp_hwc_ec_wait_vblank_handler, hwc))
     {
        WRN("warning: fail ecore_drm_output_wait_vblank");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_comp_hwc_ee_wait_vblank(E_Comp_Hwc *hwc)
{
   /* If not DPMS_ON, we call vblank handler directory to do post-process
    * for video frame buffer handling.
    */
   if (ecore_drm_output_dpms_get(hwc->output) == HWC_DRM_MODE_DPMS_OFF)
     {
        WRN("warning : DRM_MODE_DPMS_OFF\n");
        return EINA_FALSE;
     }

#if 1
   if (!ecore_drm_output_wait_vblank(hwc->drm_output, 1, _e_comp_hwc_ee_wait_vblank_handler, hwc))
     {
        WRN("failed: ecore_drm_output_wait_vblank");
        return EINA_FALSE;
     }
#else
   tdm_error error = TDM_ERROR_NONE;

   error = tdm_output_wait_vblank(hwc->output, 1, 0, _e_comp_hwc_tdm_output_wait_vblank_handler, hwc);
   if (error != TDM_ERROR_NONE)
     {
        ERR("failed: tdm_output_wait_vblank\n");
        return EINA_FALSE;
     }
#endif

   return EINA_TRUE;
}


#ifdef TDM_DISPLAY_HANDLE_EVENT
static Eina_Bool
_e_comp_hwc_drm_device_cb_event(void *data, Ecore_Fd_Handler *hdlr EINA_UNUSED)
{
   E_Comp_Hwc *hwc;
   tdm_error err = TDM_ERROR_NONE;

   if (!(hwc = data)) return ECORE_CALLBACK_RENEW;

   err = tdm_display_handle_events(hwc->tdisplay);
   if (err != TDM_ERROR_NONE)
     {
        printf("\ttdm_display_handle_events fail(%d)\n", err);
     }

   return ECORE_CALLBACK_RENEW;
}
#endif

Evas_Engine_Info_GL_Drm *
_e_comp_hwc_get_evas_engine_info_gl_drm(E_Comp_Hwc *hwc)
{
   if (hwc->einfo) return hwc->einfo;

   hwc->einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc->einfo, NULL);

   return hwc->einfo;
}
tdm_layer *
_e_comp_hwc_get_tdm_layer(E_Comp_Hwc *hwc)
{
   int i, count = 0;
   tdm_output *output = NULL;
   tdm_layer *layer = NULL;

   if (hwc->ee_tlayer) return hwc->ee_tlayer;

   output = hwc->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   tdm_output_get_layer_count(output, &count);
   for (i = 0; i < count; i++)
     {
        tdm_layer *tmp_layer = tdm_output_get_layer(output, i, NULL);
        tdm_layer_capability capabilities = 0;
        EINA_SAFETY_ON_NULL_RETURN_VAL(tmp_layer, NULL);

        tdm_layer_get_capabilities(tmp_layer, &capabilities);
        if (capabilities & (TDM_LAYER_CAPABILITY_OVERLAY|TDM_LAYER_CAPABILITY_GRAPHIC))
          {
             unsigned int usable = 0;
             tdm_layer_is_usable(tmp_layer, &usable);
             if (!usable) continue;

             layer = tmp_layer;
             break;
          }
     }

   hwc->ee_tlayer = layer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc->einfo, NULL);

   return hwc->ee_tlayer;
}

static Eina_Bool
_e_comp_hwc_render_ec(E_Comp_Hwc *hwc, E_Client *ec)
{
   Evas_Engine_Info_GL_Drm *einfo = NULL;
   E_Pixmap *pixmap = ec->pixmap;
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
   tbm_surface_h tsurface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   tdm_layer *layer  = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer != NULL, EINA_FALSE);

   /* get the evas_engine_gl_drm information */
   einfo = _e_comp_hwc_get_evas_engine_info_gl_drm(hwc);
   if (!einfo) return EINA_FALSE;

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface != 0, EINA_FALSE);

   layer = _e_comp_hwc_get_tdm_layer(hwc);
   EINA_SAFETY_ON_NULL_RETURN_VAL(layer != 0, EINA_FALSE);

   INF("HWC: display E_Client");

   if (!_e_comp_hwc_set_layer(hwc, layer, tsurface))
     {
        ERR("fail to _e_comp_hwc_set_layer");
        return EINA_FALSE;
     }

   hwc->nocomp_ec = ec;

   if (!_e_comp_hwc_ec_wait_vblank(hwc))
     {
        WRN("fail to _e_comp_hwc_ec_wait_vblank");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_comp_hwc_render_ee(E_Comp_Hwc *hwc)
{
   Evas_Engine_Info_GL_Drm *einfo;
   tbm_surface_h tsurface = NULL;
   tdm_layer *layer = NULL;

   if (hwc->ee_wait_vblank)
     {
        INF("ee_wait_vblank is TRUE... pending.");
        return;
     }

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
        INF("evas outbuf flush nothing!. nothing draws.\n");
        return;
     }
   /* uncheck the outbuf_flushed flag */
   einfo->info.outbuf_flushed = EINA_FALSE;

   if (hwc->gsurface != einfo->info.surface)
   hwc->gsurface = einfo->info.surface;

   hwc->cur_gbo = gbm_surface_lock_front_buffer(hwc->gsurface);
   if (!hwc->cur_gbo)
     {
        ERR("hwc->cur_gbo is NULL");
        return;
     }

   tsurface = gbm_tbm_get_surface(hwc->cur_gbo);
   if (!tsurface)
     {
        gbm_surface_release_buffer(hwc->gsurface, hwc->cur_gbo);
        ERR("fail to get tsurface");
        return;
     }

   layer = _e_comp_hwc_get_tdm_layer(hwc);
   if (!layer)
     {
        gbm_surface_release_buffer(hwc->gsurface, hwc->cur_gbo);
        ERR("fail to get tlayer");
        return;
     }

   INF("HWC: Display Canvas");

   if (!_e_comp_hwc_set_layer(hwc, layer, tsurface))
     {
        gbm_surface_release_buffer(hwc->gsurface, hwc->cur_gbo);
        ERR("fail to _e_comp_hwc_set_layer");
        return;
     }

   if (!_e_comp_hwc_ee_wait_vblank(hwc))
     {
        gbm_surface_release_buffer(hwc->gsurface, hwc->cur_gbo);
        ERR("fail to _e_comp_hwc_ee_wait_vblank");
        return;
     }

   hwc->ee_wait_vblank = EINA_TRUE;

   /* block the next update of ecore_evas until the current update is done */
   einfo->info.wait_for_showup = EINA_TRUE;

   return;
}

static Ecore_Drm_Output*
_e_comp_hwc_drm_output_find(int x, int y, int w, int h)
{
   Ecore_Drm_Device *dev;
   Ecore_Drm_Output *output;
   Eina_List *devs;
   Eina_List *l, *ll;

   devs = eina_list_clone(ecore_drm_devices_get());
   EINA_LIST_FOREACH(devs, l, dev)
   EINA_LIST_FOREACH(dev->outputs, ll, output)
     {
        int ox, oy, ow, oh;
        ecore_drm_output_position_get(output, &ox, &oy);
        ecore_drm_output_current_resolution_get(output, &ow, &oh, NULL);
        if (ox <= x && oy <= y && x < ox + ow && y < oy + oh)
          {
             eina_list_free(devs);
             return output;
          }
     }
   eina_list_free(devs);

   return NULL;
}

static void
_e_comp_hwc_canvas_render_post(void *data EINA_UNUSED, Evas *e EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Hwc *hwc = data;

   if (!hwc) return;

   _e_comp_hwc_render_ee(hwc);
}

static int
_e_comp_hwc_drm_fd_get(void)
{
   int fd;
   Eina_List *devs;
   Ecore_Drm_Device *dev;

   devs = eina_list_clone(ecore_drm_devices_get());
   EINA_SAFETY_ON_NULL_RETURN_VAL(devs, -1);

   if ((dev = eina_list_nth(devs, 0)))
     {
        fd = ecore_drm_device_fd_get(dev);
        if (fd >= 0)
          {
             eina_list_free(devs);
             return fd;
          }
     }

   eina_list_free(devs);
   return -1;
}


EINTERN Eina_Bool
e_comp_hwc_init(void)
{
   E_Comp_Hwc *hwc = NULL;
   tdm_output *output = NULL;
   tdm_output_conn_status status;
   tdm_error error = TDM_ERROR_NONE;
   int i;
   Evas_Engine_Info_GL_Drm *einfo;

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

   hwc->drm_fd = _e_comp_hwc_drm_fd_get();
   if (!hwc->drm_fd)
     {
        ERR("fail to get tdm_display\n");
        free(hwc);
        return EINA_FALSE;
     }

   /* this helper drm_fd help setting up the tdm display init with it */
   tdm_helper_drm_fd = hwc->drm_fd;

   hwc->tdisplay = tdm_display_init(&error);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to get tdm_display\n");
        free(hwc);
        return EINA_FALSE;
     }

   tdm_display_get_output_count(hwc->tdisplay, &hwc->num_outputs);
   if (hwc->num_outputs < 1)
     {
        ERR("fail to get tdm_display_get_output_count\n");
        tdm_display_deinit(hwc->tdisplay);
        free(hwc);
        return EINA_FALSE;
     }

   /* TODO: */
   for (i = 0; i < hwc->num_outputs; i++)
     {
        output = tdm_display_get_output(hwc->tdisplay, i, NULL);
        tdm_output_get_conn_status(output, &status);
        if (status == TDM_OUTPUT_CONN_STATUS_CONNECTED)
          {
             hwc->output = output;
             break;
          }
     }

   if (!hwc->output)
     {
        ERR("fail to get the connected output.\n");
        tdm_display_deinit(hwc->tdisplay);
        free(hwc);
        return EINA_FALSE;
     }

   /* temp code */
   hwc->output_w = e_comp->w;
   hwc->output_h = e_comp->h;

   /* temp : only get the primary output */
   hwc->drm_output = _e_comp_hwc_drm_output_find( 1, 1, 1, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc->drm_output, EINA_FALSE);

   /* get the evas_engine_gl_drm information */
   einfo = _e_comp_hwc_get_evas_engine_info_gl_drm(hwc);
   if (!einfo) return EINA_FALSE;

   /* enable hwc to evas engine gl_drm */
   einfo->info.hwc_enable = EINA_TRUE;

   evas_event_callback_add(e_comp->evas, EVAS_CALLBACK_RENDER_POST, _e_comp_hwc_canvas_render_post, hwc);

#ifdef TDM_DISPLAY_HANDLE_EVENT
   /* This handler works when tdm mode setting has to be set up */
   hwc->drm_hdlr = ecore_main_fd_handler_add(hwc->drm_fd, ECORE_FD_READ, _e_comp_hwc_drm_device_cb_event, hwc, NULL, NULL);
#endif

   /* set hwc to g_hwc */
   g_hwc = hwc;

   return EINA_TRUE;
}

EINTERN void
e_comp_hwc_shutdown(void)
{
   if (!e_comp)
     return;

   if (!g_hwc)
     {
        tdm_helper_drm_fd = -1;

#ifdef TDM_DISPLAY_HANDLE_EVENT
        if (g_hwc->drm_hdlr) ecore_main_fd_handler_del(g_hwc->drm_hdlr);
        g_hwc->drm_hdlr = NULL;
#endif
        if (g_hwc->tdisplay)
          tdm_display_deinit(g_hwc->tdisplay);

        if (g_hwc->drm_fd >= 0)
          close(g_hwc->drm_fd);

        free(g_hwc);
        g_hwc = NULL;
     }
}


EINTERN void
e_comp_hwc_update_ec(E_Client *ec)
{
   _e_comp_hwc_render_ec(g_hwc, ec);
}

EINTERN E_Client *
e_comp_hwc_fullscreen_check(void)
{
   E_Client *ec;
   Evas_Object *o;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (ec->ignored || ec->input_only || (!evas_object_visible_get(ec->frame)))
        continue;

        if (e_comp_object_mirror_visibility_check(ec->frame))
          {
             break;
          }

        if (!E_INTERSECTS(0, 0, e_comp->w, e_comp->h,ec->client.x, ec->client.y, ec->client.w, ec->client.h))
          {
             continue;
          }

        if ((ec->client.x == 0) && (ec->client.y == 0) &&
            ((ec->client.w) >= e_comp->w) &&
            ((ec->client.h) >= e_comp->h) &&
            (!ec->argb) &&
            (!ec->shaped))
          {
             return ec;
          }
        else
          {
             break;
          }
     }

   return NULL;
}


EINTERN void
e_comp_hwc_set_full_composite(char *location)
{
   if (!e_comp->nocomp) return;

   e_comp->nocomp = EINA_FALSE;
   e_comp->nocomp_ec = NULL;

   INF("NOCOMP END at %s", location);
}

