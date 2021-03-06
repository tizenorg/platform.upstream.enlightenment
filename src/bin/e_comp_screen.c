#include "e.h"
#include <Ecore_Drm.h>

static Eina_List *event_handlers = NULL;
static Eina_Bool session_state = EINA_FALSE;

static Eina_Bool dont_set_ecore_drm_keymap = EINA_FALSE;
static Eina_Bool dont_use_xkb_cache = EINA_FALSE;

static Eina_Bool
_e_comp_screen_cb_activate(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Activate *e;

   if (!(e = event)) goto end;

   if (e->active)
     {
        E_Client *ec;

        if (session_state) goto end;
        session_state = EINA_TRUE;

        ecore_evas_show(e_comp->ee);
        E_CLIENT_FOREACH(ec)
          {
             if (ec->visible && (!ec->input_only))
               e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
          }
        e_comp_render_queue();
        e_comp_shape_queue_block(0);
        ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
     }
   else
     {
        session_state = EINA_FALSE;
        ecore_evas_hide(e_comp->ee);
        edje_file_cache_flush();
        edje_collection_cache_flush();
        evas_image_cache_flush(e_comp->evas);
        evas_font_cache_flush(e_comp->evas);
        evas_render_dump(e_comp->evas);

        e_comp_render_queue();
        e_comp_shape_queue_block(1);
        ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_comp_screen_cb_output(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   const Eina_List *l;
   E_Output *eout;
   Ecore_Drm_Event_Output *e;

   if (!(e = event)) goto end;

   DBG("WL_DRM OUTPUT CHANGE");

   EINA_LIST_FOREACH(e_comp_screen->outputs, l, eout)
     {
        if ((!strcmp(eout->info.name, e->name)) &&
            (!strcmp(eout->info.screen, e->model)))
          {
             if (e->plug)
               {
                  if (!e_comp_wl_output_init(eout->id, e->make, e->model,
                                             e->x, e->y, e->w, e->h,
                                             e->phys_width, e->phys_height,
                                             e->refresh, e->subpixel_order,
                                             e->transform))
                    {
                       ERR("Could not setup new output: %s", eout->id);
                    }
               }
             else
               e_comp_wl_output_remove(eout->id);

             break;
          }
     }

   /* previous calculation of e_scale gave unsuitable value because
    * there were no sufficient information to calculate dpi.
    * so it's considerable to re-calculate e_scale with output geometry.
    */
   e_scale_manual_update(((e->w * 254 / e->phys_width) + 5) / 10);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_comp_screen_cb_input_device_add(void *data, int type, void *event)
{
   Ecore_Drm_Event_Input_Device_Add *e;
   E_Comp *comp = data;

   if (!(e = event)) goto end;

   if (e->caps & EVDEV_SEAT_POINTER)
     {
        if (comp->wl_comp_data->ptr.num_devices == 0)
          {
             e_pointer_object_set(comp->pointer, NULL, 0, 0);
             e_comp_wl_input_pointer_enabled_set(EINA_TRUE);
          }
        comp->wl_comp_data->ptr.num_devices++;
     }
   if (e->caps & EVDEV_SEAT_KEYBOARD)
     {
        comp->wl_comp_data->kbd.num_devices++;
        e_comp_wl_input_keyboard_enabled_set(EINA_TRUE);
     }
   if (e->caps & EVDEV_SEAT_TOUCH)
     {
        e_comp_wl_input_touch_enabled_set(EINA_TRUE);
        comp->wl_comp_data->touch.num_devices++;
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_comp_screen_cb_input_device_del(void *data, int type, void *event)
{
   Ecore_Drm_Event_Input_Device_Del *e;
   E_Comp *comp = data;

   if (!(e = event)) goto end;

   if (e->caps & EVDEV_SEAT_POINTER)
     {
        comp->wl_comp_data->ptr.num_devices--;
        if (comp->wl_comp_data->ptr.num_devices == 0)
          {
             e_comp_wl_input_pointer_enabled_set(EINA_FALSE);
             e_pointer_object_set(comp->pointer, NULL, 0, 0);
             e_pointer_hide(e_comp->pointer);
          }
     }

end:

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_comp_screen_cb_ee_resize(Ecore_Evas *ee EINA_UNUSED)
{
   e_comp_canvas_update();
}

static Ecore_Drm_Output_Mode *
_e_comp_screen_mode_screen_find(E_Output *eout, Ecore_Drm_Output *output)
{
   Ecore_Drm_Output_Mode *mode, *m = NULL;
   const Eina_List *l;
   int diff, distance = 0x7fffffff;

   EINA_LIST_FOREACH(ecore_drm_output_modes_get(output), l, mode)
     {
        diff = (100 * abs(eout->config.mode.w - mode->width)) +
           (100 * abs(eout->config.mode.h - mode->height)) +
           fabs((100 * eout->config.mode.refresh) - (100 * mode->refresh));
        if (diff < distance)
          {
             m = mode;
             distance = diff;
          }
     }

   return m;
}

static Eina_Bool
_e_comp_screen_output_exists(Ecore_Drm_Output *output, unsigned int crtc)
{
   /* find out if this output can go into the 'possibles' */
   return ecore_drm_output_possible_crtc_get(output, crtc);
}

static char *
_e_comp_screen_output_screen_get(Ecore_Drm_Output *output)
{
   const char *model;

   model = ecore_drm_output_model_get(output);
   if (!model) return NULL;

   return strdup(model);
}

EINTERN E_Comp_Screen *
e_comp_screen_init_outputs(void)
{
   Ecore_Drm_Device *dev;
   Ecore_Drm_Output *output;
   const Eina_List *l, *ll;
   E_Comp_Screen *r = NULL;

   r = E_NEW(E_Comp_Screen, 1);
   if (!r) return NULL;

   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     {
        EINA_LIST_FOREACH(dev->outputs, ll, output)
          {
             E_Output *eout;
             const Eina_List *m;
             Ecore_Drm_Output_Mode *omode;
             unsigned int j;
             int priority;
             Eina_Bool ok = EINA_FALSE;
             Eina_Bool possible = EINA_FALSE;
             int len = 0;

             eout = E_NEW(E_Output, 1);
             if (!eout) continue;

             eout->info.name = ecore_drm_output_name_get(output);
             printf("COMP TDM: .... out %s\n", eout->info.name);

             eout->info.connected = ecore_drm_output_connected_get(output);
             printf("COMP TDM: ...... connected %i\n", eout->info.connected);

             eout->info.screen = _e_comp_screen_output_screen_get(output);

             eout->info.edid = ecore_drm_output_edid_get(output);
             if (eout->info.edid)
               eout->id = malloc(strlen(eout->info.name) + 1 + strlen(eout->info.edid) + 1);
             else
               eout->id = malloc(strlen(eout->info.name) + 1 + 1);
             if (!eout->id)
               {
                  free(eout->info.screen);
                  free(eout->info.edid);
                  free(eout);
                  continue;
               }
             len = strlen(eout->info.name);
             strncpy(eout->id, eout->info.name, len + 1);
             strncat(eout->id, "/", 1);
             if (eout->info.edid) strncat(eout->id, eout->info.edid, strlen(eout->info.edid));

             printf("COMP TDM: ...... screen: %s\n", eout->id);

             ecore_drm_output_physical_size_get(output, &eout->info.size.w,
                                                &eout->info.size.h);

             EINA_LIST_FOREACH(ecore_drm_output_modes_get(output), m, omode)
               {
                  E_Output_Mode *rmode;

                  rmode = malloc(sizeof(E_Output_Mode));
                  if (!rmode) continue;

                  rmode->w = omode->width;
                  rmode->h = omode->height;
                  rmode->refresh = omode->refresh;
                  rmode->preferred = (omode->flags & DRM_MODE_TYPE_PREFERRED);

                  eout->info.modes = eina_list_append(eout->info.modes, rmode);
               }

             priority = 0;
             if (ecore_drm_output_primary_get(dev) == output)
               priority = 100;
             eout->config.priority = priority;

             for (j = 0; j < dev->crtc_count; j++)
               {
                  if (dev->crtcs[j] == ecore_drm_output_crtc_id_get(output))
                    {
                       ok = EINA_TRUE;
                       break;
                    }
               }

             if (!ok)
               {
                  /* get possible crtcs, compare to output_crtc_id_get */
                  for (j = 0; j < dev->crtc_count; j++)
                    {
                       if (_e_comp_screen_output_exists(output, dev->crtcs[j]))
                         {
                            ok = EINA_TRUE;
                            possible = EINA_TRUE;
                            break;
                         }
                    }
               }

             if (ok)
               {
                  if (!possible)
                    {
                       unsigned int refresh;

                       ecore_drm_output_position_get(output, &eout->config.geom.x,
                                                     &eout->config.geom.y);
                       ecore_drm_output_crtc_size_get(output, &eout->config.geom.w,
                                                      &eout->config.geom.h);

                       ecore_drm_output_current_resolution_get(output,
                                                               &eout->config.mode.w,
                                                               &eout->config.mode.h,
                                                               &refresh);
                       eout->config.mode.refresh = refresh;
                       eout->config.enabled =
                          ((eout->config.mode.w != 0) && (eout->config.mode.h != 0));

                       printf("COMP TDM: '%s' %i %i %ix%i\n", eout->info.name,
                              eout->config.geom.x, eout->config.geom.y,
                              eout->config.geom.w, eout->config.geom.h);

                    }

                  /* TODO: are rotations possible ?? */
               }
             eout->plane_count = 1; // TODO: get proper value using libtdm
             printf("COMP TDM: planes %i\n", eout->plane_count);
             for (j = 0; j < eout->plane_count; j++)
               {
                  printf("COMP TDM: added plane %i\n", j);
                  Eina_Bool pri = EINA_FALSE;
                  if (j == 0) pri = EINA_TRUE;
                  e_plane_new(eout, j, pri);
               }

             r->outputs = eina_list_append(r->outputs, eout);
          }
     }

   return r;
}

EINTERN Eina_Bool
e_comp_screen_available(void)
{
   return EINA_TRUE;
}

EINTERN void
e_comp_screen_apply(void)
{
   Ecore_Drm_Device *dev;
   Ecore_Drm_Output *out;
   E_Output *eout;
   const Eina_List *l, *ll;
   int nw, nh, pw, ph, ww, hh;
   int minw, minh, maxw, maxh;
   int top_priority = 0;

   /* TODO: what the actual fuck */
   nw = e_comp_screen->w;
   nh = e_comp_screen->h;
   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     {
        ecore_drm_screen_size_range_get(dev, &minw, &minh, &maxw, &maxh);
        printf("COMP TDM: size range: %ix%i -> %ix%i\n", minw, minh, maxw, maxh);

        ecore_drm_outputs_geometry_get(dev, NULL, NULL, &pw, &ph);
        if (nw > maxw) nw = maxw;
        if (nh > maxh) nh = maxh;
        if (nw < minw) nw = minw;
        if (nh < minh) nh = minh;
        ww = nw;
        hh = nh;
        if (nw < pw) ww = pw;
        if (nh < ph) hh = ph;

        printf("COMP TDM: set vsize: %ix%i\n", ww, hh);

        EINA_LIST_FOREACH(e_comp_screen->outputs, ll, eout)
          {
             Ecore_Drm_Output_Mode *mode = NULL;
             printf("COMP TDM: find output for '%s'\n", eout->info.name);

             out = ecore_drm_device_output_name_find(dev, eout->info.name);
             if (!out) continue;

             mode = _e_comp_screen_mode_screen_find(eout, out);

             if (eout->config.priority > top_priority)
               top_priority = eout->config.priority;

             printf("\tCOMP TDM: Priority: %d\n", eout->config.priority);

             printf("\tCOMP TDM: Geom: %d %d %d %d\n",
                    eout->config.geom.x, eout->config.geom.y,
                    eout->config.geom.w, eout->config.geom.h);

             if (mode)
               {
                  printf("\tCOMP TDM: Found Valid Drm Mode\n");
                  printf("\t\tCOMP TDM: %dx%d\n", mode->width, mode->height);
               }
             else
               printf("\tCOMP TDM: No Valid Drm Mode Found\n");

             ecore_drm_output_mode_set(out, mode,
                                       eout->config.geom.x, eout->config.geom.y);
             if (eout->config.priority == top_priority)
               ecore_drm_output_primary_set(out);

             ecore_drm_output_enable(out);

             printf("\tCOMP TDM: Mode\n");
             printf("\t\tCOMP TDM: Geom: %d %d\n",
                    eout->config.mode.w, eout->config.mode.h);
             printf("\t\tCOMP TDM: Refresh: %f\n", eout->config.mode.refresh);
             printf("\t\tCOMP TDM: Preferred: %d\n",
                    eout->config.mode.preferred);
          }
     }
}

static Eina_Bool
_drm_read_pixels(E_Comp_Wl_Output *output, void *pixels)
{
   Ecore_Drm_Device *dev;
   Ecore_Drm_Fb *fb = NULL;
   const Eina_List *drm_devs, *l;
   int i = 0, bstride;
   unsigned char *s, *d = pixels;

   drm_devs = ecore_drm_devices_get();
   EINA_LIST_FOREACH(drm_devs, l, dev)
     {
        fb = dev->next;
        if (!fb) fb = dev->current;
        if (fb) break;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(fb, EINA_FALSE);

   bstride = output->w * sizeof(int);

   for (i = output->y; i < output->y + output->h; i++)
     {
        s = fb->mmap;
        s += (fb->stride * i) + (output->x * sizeof(int));
        memcpy(d, s, (output->w * sizeof(int)));
        d += bstride;
     }

   return EINA_TRUE;
}

E_API void
_e_comp_screen_keymap_set(struct xkb_context **ctx, struct xkb_keymap **map)
{
   char *keymap_path = NULL;
   struct xkb_context *context;
   struct xkb_keymap *keymap;
   struct xkb_rule_names names = {0,};
   const char* default_rules, *default_model, *default_layout, *default_variant, *default_options;

   TRACE_INPUT_BEGIN(_e_comp_screen_keymap_set);

   context = xkb_context_new(0);
   EINA_SAFETY_ON_NULL_RETURN(context);

   /* assemble xkb_rule_names so we can fetch keymap */
   memset(&names, 0, sizeof(names));

   default_rules = e_comp_wl_input_keymap_default_rules_get();
   default_model = e_comp_wl_input_keymap_default_model_get();
   default_layout = e_comp_wl_input_keymap_default_layout_get();
   default_variant = e_comp_wl_input_keymap_default_variant_get();
   default_options = e_comp_wl_input_keymap_default_options_get();

   names.rules = strdup(default_rules);
   names.model = strdup(default_model);
   names.layout = strdup(default_layout);
   if (default_variant) names.variant = strdup(default_variant);
   if (default_options) names.options = strdup(default_options);

   keymap = e_comp_wl_input_keymap_compile(context, names, &keymap_path);
   eina_stringshare_del(keymap_path);
   EINA_SAFETY_ON_NULL_GOTO(keymap, cleanup);

   *ctx = context;
   *map = keymap;

   if (dont_set_ecore_drm_keymap == EINA_FALSE)
     {
        ecore_drm_device_keyboard_cached_context_set(*ctx);
        ecore_drm_device_keyboard_cached_keymap_set(*map);
     }

cleanup:
   free((char *)names.rules);
   free((char *)names.model);
   free((char *)names.layout);
   if (names.variant) free((char *)names.variant);
   if (names.options) free((char *)names.options);

   TRACE_INPUT_END();
}

E_API Eina_Bool
e_comp_screen_init()
{
   E_Comp *comp;
   int w = 0, h = 0, scr_w = 1, scr_h = 1;
   struct xkb_context *ctx = NULL;
   struct xkb_keymap *map = NULL;
   char buf[1024];

   dont_set_ecore_drm_keymap = getenv("NO_ECORE_DRM_KEYMAP_CACHE") ? EINA_TRUE : EINA_FALSE;
   dont_use_xkb_cache = getenv("NO_KEYMAP_CACHE") ? EINA_TRUE : EINA_FALSE;

   TRACE_DS_BEGIN(WL_DRM:INIT);

   if (!(comp = e_comp))
     {
        comp = e_comp_new();
        if (!comp)
          {
             TRACE_DS_END();
             EINA_SAFETY_ON_NULL_RETURN_VAL(comp, EINA_FALSE);
          }

        comp->comp_type = E_PIXMAP_TYPE_WL;
     }

   /* set gl available if we have ecore_evas support */
   if (ecore_evas_engine_type_supported_get(ECORE_EVAS_ENGINE_OPENGL_DRM))
     e_comp_gl_set(EINA_TRUE);

   INF("GL available:%d config engine:%d screen size:%dx%d",
       e_comp_gl_get(), e_comp_config_get()->engine, scr_w, scr_h);

   if (e_config->xkb.use_cache && !dont_use_xkb_cache)
     {
        e_main_ts("\tDRM Keymap Init");
        _e_comp_screen_keymap_set(&ctx, &map);
        e_main_ts("\tDRM Keymap Init Done");
     }

   if ((e_comp_gl_get()) &&
       (e_comp_config_get()->engine == E_COMP_ENGINE_GL))
     {
        e_main_ts("\tEE_GL_DRM New");
        comp->ee = ecore_evas_gl_drm_new(NULL, 0, 0, 0, scr_w, scr_h);
        snprintf(buf, sizeof(buf), "\tEE_GL_DRM New Done %p %dx%d", comp->ee, scr_w, scr_h);
        e_main_ts(buf);

        if (!comp->ee)
          e_comp_gl_set(EINA_FALSE);
        else
          {
             Evas_GL *evasgl = NULL;
             Evas_GL_API *glapi = NULL;

             e_main_ts("\tEvas_GL New");
             evasgl = evas_gl_new(ecore_evas_get(comp->ee));
             if (evasgl)
               {
                  glapi = evas_gl_api_get(evasgl);
                  if (!((glapi) && (glapi->evasglBindWaylandDisplay)))
                    {
                       e_comp_gl_set(EINA_FALSE);
                       ecore_evas_free(comp->ee);
                       comp->ee = NULL;
                       e_main_ts("\tEvas_GL New Failed 1");
                    }
                  else
                    {
                       e_main_ts("\tEvas_GL New Done");
                    }
               }
             else
               {
                  e_comp_gl_set(EINA_FALSE);
                  ecore_evas_free(comp->ee);
                  comp->ee = NULL;
                  e_main_ts("\tEvas_GL New Failed 2");
               }
             evas_gl_free(evasgl);
          }
     }

   /* fallback to framebuffer drm (non-accel) */
   if (!comp->ee)
     {
        e_main_ts("\tEE_DRM New");
        comp->ee = ecore_evas_drm_new(NULL, 0, 0, 0, scr_w, scr_h);
        snprintf(buf, sizeof(buf), "\tEE_DRM New Done %p %dx%d", comp->ee, scr_w, scr_h);
        e_main_ts(buf);
     }

   if (!comp->ee)
     {
        fprintf(stderr, "Could not create ecore_evas_drm canvas");
        TRACE_DS_END();
        return EINA_FALSE;
     }

   ecore_evas_data_set(e_comp->ee, "comp", e_comp);

   /* get the current screen geometry */
   ecore_evas_screen_geometry_get(e_comp->ee, NULL, NULL, &w, &h);

   /* resize the canvas */
   if (!((scr_w == w) && (scr_h == h)))
     {
        snprintf(buf, sizeof(buf), "\tEE Resize %dx%d -> %dx%d", scr_w, scr_h, w, h);
        e_main_ts(buf);

        ecore_evas_resize(comp->ee, w, h);

        e_main_ts("\tEE Resize Done");
     }

   ecore_evas_callback_resize_set(e_comp->ee, _e_comp_screen_cb_ee_resize);

   e_main_ts("\tE_Output Init");
   if (!e_output_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize output!\n"));
        TRACE_DS_END();
        return EINA_FALSE;
     }
   e_output_screens_setup(-1, -1);
   e_main_ts("\tE_Output Init Done");

   e_main_ts("\tE_Comp_Wl Init");
   if (!e_comp_wl_init())
     {
        TRACE_DS_END();
        return EINA_FALSE;
     }
   e_main_ts("\tE_Comp_Wl Init Done");

   e_main_ts("\tE_Comp_Canvas Init");
   if (!e_comp_canvas_init(w, h))
     {
        TRACE_DS_END();
        return EINA_FALSE;
     }
   e_main_ts("\tE_Comp_Canvas Init Done");

   e_comp_wl->screenshooter.read_pixels = _drm_read_pixels;

   ecore_evas_pointer_xy_get(e_comp->ee,
                             &e_comp_wl->ptr.x,
                             &e_comp_wl->ptr.y);

   evas_event_feed_mouse_in(e_comp->evas, 0, NULL);

   /* comp->pointer =  */
   /*   e_pointer_window_new(ecore_evas_window_get(comp->ee), 1); */
   e_main_ts("\tE_Pointer New");
   if ((comp->pointer = e_pointer_canvas_new(comp->ee, EINA_TRUE)))
     {
        comp->pointer->color = EINA_TRUE;
        e_pointer_hide(comp->pointer);
     }
   e_main_ts("\tE_Pointer New Done");

   /* FIXME: We need a way to trap for user changing the keymap inside of E
    *        without the event coming from X11 */

   /* FIXME: We should make a decision here ...
    *
    * Fetch the keymap from drm, OR set this to what the E config is....
    */

   /* FIXME: This is just for testing at the moment....
    * happens to jive with what drm does */
   e_main_ts("\tE_Comp_WL Keymap Init");
   e_comp_wl_input_keymap_set(e_comp_wl_input_keymap_default_rules_get(),
                              e_comp_wl_input_keymap_default_model_get(),
                              e_comp_wl_input_keymap_default_layout_get(),
                              e_comp_wl_input_keymap_default_variant_get(),
                              e_comp_wl_input_keymap_default_options_get(),
                              ctx, map);
   e_main_ts("\tE_Comp_WL Keymap Init Done");

   E_LIST_HANDLER_APPEND(event_handlers, ECORE_DRM_EVENT_ACTIVATE,         _e_comp_screen_cb_activate,         comp);
   E_LIST_HANDLER_APPEND(event_handlers, ECORE_DRM_EVENT_OUTPUT,           _e_comp_screen_cb_output,           comp);
   E_LIST_HANDLER_APPEND(event_handlers, ECORE_DRM_EVENT_INPUT_DEVICE_ADD, _e_comp_screen_cb_input_device_add, comp);
   E_LIST_HANDLER_APPEND(event_handlers, ECORE_DRM_EVENT_INPUT_DEVICE_DEL, _e_comp_screen_cb_input_device_del, comp);

   TRACE_DS_END();

   return EINA_TRUE;
}

E_API void
e_comp_screen_shutdown()
{
   /* shutdown ecore_drm */
   /* ecore_drm_shutdown(); */
   e_output_shutdown();

   dont_set_ecore_drm_keymap = EINA_FALSE;
   dont_use_xkb_cache = EINA_FALSE;
   E_FREE_LIST(event_handlers, ecore_event_handler_del);

}
