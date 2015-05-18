#define E_COMP_WL
#include "e.h"
#include <Ecore_Drm.h>

EAPI E_Module_Api e_modapi = { E_MODULE_API_VERSION, "Wl_Drm" };

static Ecore_Event_Handler *activate_handler;
static Ecore_Event_Handler *output_handler;
static Eina_Bool session_state = EINA_FALSE;

static Eina_Bool
_e_mod_drm_cb_activate(void *data, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Activate *e;
   E_Comp *c;

   if ((!event) || (!data)) goto end;
   e = event;
   c = data;

   if (e->active)
     {
        E_Client *ec;

        if (session_state) goto end;
        session_state = EINA_TRUE;

        ecore_evas_show(c->ee);
        E_CLIENT_FOREACH(c, ec)
          {
             if (ec->visible && (!ec->input_only))
               e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
          }
        e_comp_render_queue(c);
        e_comp_shape_queue_block(c, 0);
        ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
     }
   else
     {
        session_state = EINA_FALSE;
        ecore_evas_hide(c->ee);
        edje_file_cache_flush();
        edje_collection_cache_flush();
        evas_image_cache_flush(c->evas);
        evas_font_cache_flush(c->evas);
        evas_render_dump(c->evas);

        e_comp_render_queue(c);
        e_comp_shape_queue_block(c, 1);
        ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_mod_drm_cb_output(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Output *e;
   char buff[PATH_MAX];

   if (!(e = event)) goto end;

   if (!e->plug) goto end;

   snprintf(buff, sizeof(buff), "%d", e->id);
   e_comp_wl_output_init(buff, e->make, e->model, e->x, e->y, e->w, e->h, 
                         e->phys_width, e->phys_height, e->refresh, 
                         e->subpixel_order, e->transform);

end:
   return ECORE_CALLBACK_PASS_ON;
}

EAPI void *
e_modapi_init(E_Module *m)
{
   E_Comp *comp;
   int w = 0, h = 0, scr_w = 0, scr_h = 0;
   char *env_w, *env_h;

   printf("LOAD WL_DRM MODULE\n");

   if (!(comp = e_comp))
     {
        comp = e_comp_new();
        comp->comp_type = E_PIXMAP_TYPE_WL;
     }

   /* set gl available if we have ecore_evas support */
   if (ecore_evas_engine_type_supported_get(ECORE_EVAS_ENGINE_WAYLAND_EGL) ||
       ecore_evas_engine_type_supported_get(ECORE_EVAS_ENGINE_OPENGL_DRM))
     e_comp_gl_set(EINA_TRUE);

   if ((env_w = getenv("E_SCREEN_WIDTH"))) scr_w = atoi(env_w);
   if ((env_h = getenv("E_SCREEN_HEIGHT"))) scr_h = atoi(env_h);

   if (scr_w <= 0) scr_w = 1;
   if (scr_h <= 0) scr_h = 1;

   DBG("GL available:%d config engine:%d screen size:%dx%d",
       e_comp_gl_get(), e_comp_config_get()->engine, scr_w, scr_h);

   if ((e_comp_gl_get()) &&
       (e_comp_config_get()->engine == E_COMP_ENGINE_GL))
     {
        comp->ee = ecore_evas_gl_drm_new(NULL, 0, 0, 0, scr_w, scr_h);
        DBG("Create ecore_evas_gl_drm canvas:%p", comp->ee);

        if (!comp->ee)
          e_comp_gl_set(EINA_FALSE);
        else
          {
             Evas_GL *evasgl = NULL;
             Evas_GL_API *glapi = NULL;

             evasgl = evas_gl_new(ecore_evas_get(comp->ee));
             if (evasgl)
               {
                  glapi = evas_gl_api_get(evasgl);
                  if (!((glapi) && (glapi->evasglBindWaylandDisplay)))
                    {
                       e_comp_gl_set(EINA_FALSE);
                       ecore_evas_free(comp->ee);
                       comp->ee = NULL;
                    }
               }
             else
               {
                  e_comp_gl_set(EINA_FALSE);
                  ecore_evas_free(comp->ee);
                  comp->ee = NULL;
               }
             evas_gl_free(evasgl);
          }
     }

   /* fallback to framebuffer drm (non-accel) */
   if (!comp->ee)
     {
        comp->ee = ecore_evas_drm_new(NULL, 0, 0, 0, scr_w, scr_h);
        DBG("Create ecore_evas_drm canvas:%p", comp->ee);
     }

   if (!comp->ee)
     {
        fprintf(stderr, "Could not create ecore_evas_drm canvas");
        return NULL;
     }

   /* get the current screen geometry */
   ecore_evas_screen_geometry_get(comp->ee, NULL, NULL, &w, &h);

   /* resize the canvas */
   if (!((scr_w == w) && (scr_h == h)))
     {
        DBG("Change ecore_evas canvas size %dx%d -> %dx%d", scr_w, scr_h, w, h);
        ecore_evas_resize(comp->ee, w, h);
     }

   /* TODO: hook ecore_evas_callback_resize_set */

   if (!e_xinerama_fake_screens_exist())
     {
        E_Screen *screen;

        screen = E_NEW(E_Screen, 1);
        screen->escreen = screen->screen = 0;
        screen->x = 0;
        screen->y = 0;
        screen->w = w;
        screen->h = h;
        e_xinerama_screens_set(eina_list_append(NULL, screen));
     }

   comp->man = e_manager_new(ecore_evas_window_get(comp->ee), comp, w, h);
   if (!e_comp_canvas_init(comp)) return NULL;
   e_comp_canvas_fake_layers_init(comp);

   /* NB: This needs to be called AFTER the comp canvas has been setup */
   if (!e_comp_wl_init()) return NULL;

   e_comp_wl_input_pointer_enabled_set(EINA_TRUE);
   e_comp_wl_input_keyboard_enabled_set(EINA_TRUE);
   e_comp_wl_input_touch_enabled_set(EINA_TRUE);

   /* comp->pointer =  */
   /*   e_pointer_window_new(ecore_evas_window_get(comp->ee), 1); */
   comp->pointer = e_pointer_canvas_new(comp->ee, EINA_TRUE);
   comp->pointer->color = EINA_TRUE;

   /* FIXME: We need a way to trap for user changing the keymap inside of E
    *        without the event coming from X11 */

   /* FIXME: We should make a decision here ...
    *
    * Fetch the keymap from drm, OR set this to what the E config is....
    */

   /* FIXME: This is just for testing at the moment....
    * happens to jive with what drm does */
   e_comp_wl_input_keymap_set(comp->wl_comp_data, NULL, NULL, NULL);

   activate_handler =
      ecore_event_handler_add(ECORE_DRM_EVENT_ACTIVATE,
                              _e_mod_drm_cb_activate, comp);

   output_handler =
      ecore_event_handler_add(ECORE_DRM_EVENT_OUTPUT,
                              _e_mod_drm_cb_output, comp);

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   /* shutdown ecore_drm */
   /* ecore_drm_shutdown(); */

   if (activate_handler) ecore_event_handler_del(activate_handler);
   activate_handler = NULL;

   return 1;
}
