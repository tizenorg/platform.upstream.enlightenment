#include "e.h"

#define UPDATE_MAX 512 // same as evas
#define SMART_NAME     "e_comp_object"

/* for non-util functions */
#define API_ENTRY      E_Comp_Object *cw; \
                       cw = evas_object_smart_data_get(obj); \
                       if ((!obj) || (!cw) || (e_util_strcmp(evas_object_type_get(obj), SMART_NAME))) return

/* for util functions (obj may or may not be E_Comp_Object */
#define SOFT_ENTRY(...)      E_Comp_Object *cw; \
                       if (!obj) \
                         { \
                            CRI("YOU PASSED NULL! ARGH!"); \
                            return __VA_ARGS__; \
                         } \
                       cw = evas_object_smart_data_get(obj); \
                       if ((!cw) || (e_util_strcmp(evas_object_type_get(obj), SMART_NAME))) \
                         cw = NULL
#define INTERNAL_ENTRY E_Comp_Object *cw; cw = evas_object_smart_data_get(obj);

/* enable for lots of client size info in console output */
#if 1
# define e_util_size_debug_set(x, y)
#endif

/* enable along with display-specific damage INF calls to enable render tracing
 * SLOW!
 */
#if 0
#define RENDER_DEBUG(...) INF(__VA_ARGS__)
#else
#define RENDER_DEBUG(...)
#endif

typedef struct _E_Comp_Object
{
   EINA_INLIST;

   int                  x, y, w, h;  // geometry
   Eina_Rectangle       input_rect;

   E_Client *ec;
   E_Comp *comp;

   E_Comp_Object_Frame client_inset;
   struct
   {
      double          start;
      double          val;
      int             x, y;
      E_Direction     dir;
      Ecore_Animator *anim;
   } shade;

   Eina_Stringshare   *frame_theme;
   Eina_Stringshare   *frame_name;

   Evas_Object         *smart_obj;  // smart object
   Evas_Object         *clip; // clipper over effect object
   Evas_Object         *input_obj; // input rect
   Evas_Object         *obj;  // composite object
   Evas_Object         *frame_object; // for client frames
   Evas_Object         *frame_icon; // for client frames
   Evas_Object         *zoomobj; // zoomap
   Evas_Object         *shobj;  // shadow object
   Evas_Object         *effect_obj; // effects object
   unsigned int         layer; //e_comp_canvas_layer_map(cw->ec->layer)
   Eina_List           *obj_mirror;  // extra mirror objects
   Eina_Tiler          *updates; //render update regions
   Eina_Tiler          *pending_updates; //render update regions which are about to render

   unsigned int         update_count;  // how many updates have happened to this obj

   unsigned int         opacity;  // opacity set with _NET_WM_WINDOW_OPACITY

   unsigned int         animating;  // it's busy animating
   Eina_Bool            delete_pending : 1;  // delete pendig
   Eina_Bool            defer_hide : 1;  // flag to get hide to work on deferred hide
   Eina_Bool            visible : 1;  // is visible

   Eina_Bool            shaped : 1;  // is shaped
   Eina_Bool            update : 1;  // has updates to fetch
   Eina_Bool            redirected : 1;  // has updates to fetch
   Eina_Bool            native : 1;  // native

   Eina_Bool            nocomp : 1;  // nocomp applied
   Eina_Bool            nocomp_need_update : 1;  // nocomp in effect, but this window updated while in nocomp mode
   Eina_Bool            real_hid : 1;  // last hide was a real window unmap

   Eina_Bool            effect_clip : 1; //effect_obj is clipped
   Eina_Bool            effect_clip_able : 1; //effect_obj will be clipped for effects

   Eina_Bool            zoomap_disabled : 1; //whether zoomap is usable
   Eina_Bool            updates_exist : 1;
   Eina_Bool            updates_full : 1; // entire object will be updated

   Eina_Bool            force_move : 1;
   Eina_Bool            frame_extends : 1; //frame may extend beyond object size
} E_Comp_Object;


struct E_Comp_Object_Mover
{
   EINA_INLIST;
   E_Comp_Object_Mover_Cb func;
   const char *sig;
   void *data;
   int pri;
};

static Eina_Inlist *_e_comp_object_movers = NULL;
static Evas_Smart *_e_comp_smart = NULL;

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
static int _e_comp_object_intercept_hooks_delete = 0;
static int _e_comp_object_intercept_hooks_walking = 0;

static Eina_Inlist *_e_comp_object_intercept_hooks[] =
{
   [E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER] = NULL,
   [E_COMP_OBJECT_INTERCEPT_HOOK_HIDE] = NULL,
};
#endif

/* sekrit functionzzz */
EINTERN void e_client_focused_set(E_Client *ec);

/* emitted every time a new noteworthy comp object is added */
EAPI int E_EVENT_COMP_OBJECT_ADD = -1;

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
static void
_e_comp_object_intercept_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Comp_Object_Intercept_Hook *ch;
   unsigned int x;

   for (x = 0; x < E_COMP_OBJECT_INTERCEPT_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_comp_object_intercept_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_comp_object_intercept_hooks[x] = eina_inlist_remove(_e_comp_object_intercept_hooks[x], EINA_INLIST_GET(ch));
          free(ch);
       }
}

static Eina_Bool
_e_comp_object_intercept_hook_call(E_Comp_Object_Intercept_Hook_Point hookpoint, E_Client *ec)
{
   E_Comp_Object_Intercept_Hook *ch;
   Eina_Bool ret = EINA_TRUE;

   if (e_object_is_del(E_OBJECT(ec))) return ret;
   e_object_ref(E_OBJECT(ec));
   _e_comp_object_intercept_hooks_walking++;
   EINA_INLIST_FOREACH(_e_comp_object_intercept_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        if (!(ch->func(ch->data, ec)))
          {
             ret = EINA_FALSE;
             break;
          }
     }
   _e_comp_object_intercept_hooks_walking--;
   if ((_e_comp_object_intercept_hooks_walking == 0) && (_e_comp_object_intercept_hooks_delete > 0))
     _e_comp_object_intercept_hooks_clean();

   e_object_unref(E_OBJECT(ec));

   return ret;
}
#endif

static void
_e_comp_object_event_free(void *d EINA_UNUSED, void *event)
{
   E_Event_Comp_Object *ev = event;
   E_Client *ec;

   ec = evas_object_data_get(ev->comp_object, "E_Client");
   if (ec) e_object_unref(E_OBJECT(ec));
   evas_object_unref(ev->comp_object);
   free(ev);
}

static void
_e_comp_object_event_add(Evas_Object *obj)
{
   E_Event_Comp_Object *ev;

   if (stopping) return;
   ev = E_NEW(E_Event_Comp_Object, 1);
   evas_object_ref(obj);
   ev->comp_object = obj;
   ecore_event_add(E_EVENT_COMP_OBJECT_ADD, ev, _e_comp_object_event_free, NULL);
}

/////////////////////////////////////

static inline Eina_Bool
_e_comp_shaped_check(int w, int h, const Eina_Rectangle *rects, int num)
{
   if ((!rects) || (num < 1)) return EINA_FALSE;
   if (num > 1) return EINA_TRUE;
   if ((rects[0].x == 0) && (rects[0].y == 0) &&
       ((int)rects[0].w == w) && ((int)rects[0].h == h))
     return EINA_FALSE;
   return EINA_TRUE;
}

/////////////////////////////////////

/* add a client to the layer-client list */
static void
_e_comp_object_layers_add(E_Comp_Object *cw, E_Comp_Object *above, E_Comp_Object *below, Eina_Bool prepend)
{
   E_Comp_Object *layer_cw = NULL;

   /* try to get the internal data for the layer;
    * will return NULL for fake layers (eg. wayland)
    */
   if (cw->comp->layers[cw->layer].obj)
     layer_cw = evas_object_smart_data_get(cw->comp->layers[cw->layer].obj);
   if (layer_cw == cw) layer_cw = NULL;
/*
   if (above)
     cw->comp->layers[cw->layer].objs = eina_inlist_append_relative(cw->comp->layers[cw->layer].objs, EINA_INLIST_GET(cw), EINA_INLIST_GET(cw2));
   else if (below)
     cw->comp->layers[cw->layer].objs = eina_inlist_prepend_relative(cw->comp->layers[cw->layer].objs, EINA_INLIST_GET(cw), EINA_INLIST_GET(cw2));
   else
     {
        if (prepend)
          cw->comp->layers[cw->layer].objs = eina_inlist_prepend(cw->comp->layers[cw->layer].objs, EINA_INLIST_GET(cw));
        else
          cw->comp->layers[cw->layer].objs = eina_inlist_append(cw->comp->layers[cw->layer].objs, EINA_INLIST_GET(cw));
     }
   cw->comp->layers[cw->layer].objs_count++;
   if (!cw->ec) return;
*/
   if (above)
    cw->comp->layers[above->layer].clients = eina_inlist_append_relative(cw->comp->layers[above->layer].clients, EINA_INLIST_GET(cw->ec), EINA_INLIST_GET(above->ec));
   else if (below)
     cw->comp->layers[below->layer].clients = eina_inlist_prepend_relative(cw->comp->layers[below->layer].clients, EINA_INLIST_GET(cw->ec), EINA_INLIST_GET(below->ec));
   if ((!above) && (!below))
     {
        if (prepend)
          cw->comp->layers[cw->layer].clients = eina_inlist_prepend(cw->comp->layers[cw->layer].clients, EINA_INLIST_GET(cw->ec));
        else if (layer_cw)
          cw->comp->layers[cw->layer].clients = eina_inlist_prepend_relative(cw->comp->layers[cw->layer].clients, EINA_INLIST_GET(cw->ec), EINA_INLIST_GET(layer_cw->ec));
        else //this is either the layer object or a tough actin tinactin^W^W^Wfast stacking client
          cw->comp->layers[cw->layer].clients = eina_inlist_append(cw->comp->layers[cw->layer].clients, EINA_INLIST_GET(cw->ec));
     }
   cw->comp->layers[cw->layer].clients_count++;
#ifndef E19_RELEASE_BUILD
   if (layer_cw)
     {
        E_Client *below_ec = e_client_below_get(cw->ec);
        if (below_ec)
          {
             if (cw->comp->layers[cw->layer].obj == below_ec->frame)
               CRI("ACK!");
          }
     }
#endif
}

static void
_e_comp_object_layers_remove(E_Comp_Object *cw)
{
   if (cw->ec && cw->comp->layers[cw->layer].clients)
     {
        cw->comp->layers[cw->layer].clients = eina_inlist_remove(cw->comp->layers[cw->layer].clients, EINA_INLIST_GET(cw->ec));
        cw->comp->layers[cw->layer].clients_count--;
     }
/*
   cw->comp->layers[cw->layer].objs = eina_inlist_remove(cw->comp->layers[cw->layer].objs, EINA_INLIST_GET(cw));
   cw->comp->layers[cw->layer].objs_count--;
*/
}

/////////////////////////////////////

static void
_e_comp_object_shadow(E_Comp_Object *cw)
{
   if (e_client_util_shadow_state_get(cw->ec))
     edje_object_signal_emit(cw->frame_object ?: cw->shobj, "e,state,shadow,on", "e");
   else
     edje_object_signal_emit(cw->frame_object ?: cw->shobj, "e,state,shadow,off", "e");
   if (cw->frame_object)
     edje_object_signal_emit(cw->shobj, "e,state,shadow,off", "e");
   evas_object_smart_callback_call(cw->smart_obj, "shadow_change", cw->ec);
}

/* trigger e_binding from an edje signal on a client frame */
static void
_e_comp_object_cb_signal_bind(void *data, Evas_Object *obj EINA_UNUSED, const char *emission, const char *source)
{
   E_Comp_Object *cw = data;

#ifndef HAVE_WAYLAND_ONLY
   if (e_dnd_active()) return;
#endif
   if (cw->ec->iconic) return;
   e_bindings_signal_handle(E_BINDING_CONTEXT_WINDOW, E_OBJECT(cw->ec),
                            emission, source);
}

/////////////////////////////////////

/* handle evas mouse-in events on client object */
static void
_e_comp_object_cb_mouse_in(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_In *ev = event_info;
   E_Comp_Object *cw = data;

   e_client_mouse_in(cw->ec, ev->output.x, ev->output.y);
}

/* handle evas mouse-out events on client object */
static void
_e_comp_object_cb_mouse_out(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_Out *ev = event_info;
   E_Comp_Object *cw = data;

   e_client_mouse_out(cw->ec, ev->output.x, ev->output.y);
}

/* handle evas mouse wheel events on client object */
static void
_e_comp_object_cb_mouse_wheel(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_Wheel *ev = event_info;
   E_Comp_Object *cw = data;
   E_Binding_Event_Wheel ev2;

   if (!cw->ec) return;
   if (e_client_action_get()) return;
   e_bindings_evas_event_mouse_wheel_convert(ev, &ev2);
   e_client_mouse_wheel(cw->ec, &ev->output, &ev2);
}

/* handle evas mouse down events on client object */
static void
_e_comp_object_cb_mouse_down(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_Down *ev = event_info;
   E_Comp_Object *cw = data;
   E_Binding_Event_Mouse_Button ev2;

   if (!cw->ec) return;
   if (e_client_action_get()) return;
   e_bindings_evas_event_mouse_button_convert(ev, &ev2);
   e_client_mouse_down(cw->ec, ev->button, &ev->output, &ev2);
}

/* handle evas mouse up events on client object */
static void
_e_comp_object_cb_mouse_up(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_Down *ev = event_info;
   E_Comp_Object *cw = data;
   E_Binding_Event_Mouse_Button ev2;

   if (!cw->ec) return;
   if (e_client_action_get() && (e_client_action_get() != cw->ec)) return;
   e_bindings_evas_event_mouse_button_convert(ev, &ev2);
   e_client_mouse_up(cw->ec, ev->button, &ev->output, &ev2);
}

/* handle evas mouse movement events on client object */
static void
_e_comp_object_cb_mouse_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_Move *ev = event_info;
   E_Comp_Object *cw = data;

   if (!cw->ec) return;
   if (e_client_action_get() && (e_client_action_get() != cw->ec)) return;
   e_client_mouse_move(cw->ec, &ev->cur.output);
}
/////////////////////////////////////

/* helper function for checking compositor themes based on user-defined matches */
static Eina_Bool
_e_comp_object_shadow_client_match(const E_Client *ec, E_Comp_Match *m)
{
   if (((m->title) && (!ec->netwm.name)) ||
       ((ec->netwm.name) && (m->title) && (!e_util_glob_match(ec->netwm.name, m->title))))
     return EINA_FALSE;
   if (((m->clas) && (!ec->icccm.class)) ||
       ((ec->icccm.class) && (m->clas) && (!e_util_glob_match(ec->icccm.class, m->clas))))
     return EINA_FALSE;
   if (((m->role) && (!ec->icccm.window_role)) ||
       ((ec->icccm.window_role) && (m->role) && (!e_util_glob_match(ec->icccm.window_role, m->role))))
     return EINA_FALSE;
   if ((ec->netwm.type != E_WINDOW_TYPE_UNKNOWN) &&
       (m->primary_type != E_WINDOW_TYPE_UNKNOWN) &&
       ((int)ec->netwm.type != m->primary_type))
     return EINA_FALSE;
  if (m->borderless != 0)
    {
       int borderless = 0;

       if (e_client_util_borderless(ec))
         borderless = 1;
       if (!(((m->borderless == -1) && (!borderless)) ||
             ((m->borderless == 1) && (borderless))))
         return EINA_FALSE;
    }
  if (m->dialog != 0)
    {
       int dialog = 0;

       if (((ec->icccm.transient_for != 0) ||
            (ec->dialog)))
         dialog = 1;
       if (!(((m->dialog == -1) && (!dialog)) ||
             ((m->dialog == 1) && (dialog))))
         return EINA_FALSE;
    }
  if (m->accepts_focus != 0)
    {
       int accepts_focus = 0;

       if (ec->icccm.accepts_focus)
         accepts_focus = 1;
       if (!(((m->accepts_focus == -1) && (!accepts_focus)) ||
             ((m->accepts_focus == 1) && (accepts_focus))))
         return EINA_FALSE;
    }
  if (m->vkbd != 0)
    {
       int vkbd = 0;

       if (ec->vkbd.vkbd)
         vkbd = 1;
       if (!(((m->vkbd == -1) && (!vkbd)) ||
             ((m->vkbd == 1) && (vkbd))))
         return EINA_FALSE;
    }
  if (m->argb != 0)
    {
       if (!(((m->argb == -1) && (!ec->argb)) ||
             ((m->argb == 1) && (ec->argb))))
         return EINA_FALSE;
    }
  if (m->fullscreen != 0)
    {
       int fullscreen = ec->fullscreen;

       if (!(((m->fullscreen == -1) && (!fullscreen)) ||
             ((m->fullscreen == 1) && (fullscreen))))
         return EINA_FALSE;
    }
  if (m->modal != 0)
    {
       int modal = 0;

       if (ec->netwm.state.modal)
         modal = 1;
       if (!(((m->modal == -1) && (!modal)) ||
             ((m->modal == 1) && (modal))))
         return EINA_FALSE;
    }
  return EINA_TRUE;
}

/* function for setting up a client's compositor frame theme (cw->shobj) */
static Eina_Bool
_e_comp_object_shadow_setup(E_Comp_Object *cw)
{
   int ok = 0;
   char buf[4096];
   Eina_List *list = NULL, *l;
   E_Comp_Match *m;
   Eina_Stringshare *reshadow_group = NULL;
   Eina_Bool focus = EINA_FALSE, urgent = EINA_FALSE, skip = EINA_FALSE, fast = EINA_FALSE, reshadow = EINA_FALSE, no_shadow = EINA_FALSE;
   Eina_Stringshare *name, *title;
   E_Comp_Config *conf = e_comp_config_get();

   edje_object_file_get(cw->shobj, NULL, &reshadow_group);
   /* match correct client type */
   list = cw->ec->override ? conf->match.overrides : conf->match.borders;
   name = cw->ec->icccm.name;
   title = cw->ec->icccm.title;
   skip = (cw->ec->override ? conf->match.disable_overrides : conf->match.disable_borders) || (title && (!strncmp(title, "noshadow", 8)));
   fast = cw->ec->override ? conf->fast_overrides : conf->fast_borders;

   /* skipping here is mostly a hack for systray because I hate it */
   if (!skip)
     {
        EINA_LIST_FOREACH(list, l, m)
          {
             if (((m->name) && (!name)) ||
                 ((name) && (m->name) && (!e_util_glob_match(name, m->name))))
               continue;
             if (!_e_comp_object_shadow_client_match(cw->ec, m)) continue;
             
             focus = m->focus;
             urgent = m->urgent;
             no_shadow = m->no_shadow;
             if (m->shadow_style)
               {
                  /* fast effects are just themes with "/fast" appended and shorter effect times */
                  if (fast)
                    {
                       snprintf(buf, sizeof(buf), "e/comp/frame/%s/fast", m->shadow_style);
                       reshadow = ok = !e_util_strcmp(reshadow_group, buf);
                       if (!ok)
                         ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", buf);
                    }
                  /* default to non-fast style if fast not available */
                  if (!ok)
                    {
                       snprintf(buf, sizeof(buf), "e/comp/frame/%s", m->shadow_style);
                       reshadow = ok = !e_util_strcmp(reshadow_group, buf);
                       if (!ok)
                         ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", buf);
                    }
                  if (ok) break;
               }
          }
     }
   while (!ok)
     {
        if (skip || (cw->ec->e.state.video))
          {
             reshadow = ok = !e_util_strcmp(reshadow_group, "e/comp/frame/none");
             if (!ok)
               ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", "e/comp/frame/none");
          }
        if (ok) break;
        if (conf->shadow_style)
          {
             if (fast)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s/fast", conf->shadow_style);
                  reshadow = ok = !e_util_strcmp(reshadow_group, buf);
                  if (!ok)
                    ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", buf);
               }
             if (!ok)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s", conf->shadow_style);
                  reshadow = ok = !e_util_strcmp(reshadow_group, buf);
                  if (!ok)
                    ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", buf);
               }
          }
        if (!ok)
          {
             if (fast)
               {
                  reshadow = ok = !e_util_strcmp(reshadow_group, "e/comp/frame/default/fast");
                  if (!ok)
                    ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", "e/comp/frame/default/fast");
               }
             if (!ok)
               {
                  reshadow = ok = !e_util_strcmp(reshadow_group, "e/comp/frame/default");
                  if (!ok)
                    ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", "e/comp/frame/default");
               }
          }
        break;
     }
   /* reshadow means this entire function call has been a no-op since we're re-setting the current style */
   if (reshadow)
     {
        if (cw->zoomap_disabled)
          {
             if (cw->frame_object && (e_zoomap_child_get(cw->zoomobj) == cw->frame_object)) return EINA_FALSE;
          }
        else
          {
             if (cw->frame_object && (edje_object_part_swallow_get(cw->shobj, "e.swallow.content") == cw->frame_object)) return EINA_FALSE;
          }
     }
   if (cw->ec->override)
     {
        if ((!cw->ec->shaped) && (!no_shadow) && (!cw->ec->argb))
          edje_object_signal_emit(cw->shobj, "e,state,shadow,on", "e");
        else
          edje_object_signal_emit(cw->shobj, "e,state,shadow,off", "e");
        evas_object_smart_callback_call(cw->smart_obj, "shadow_change", cw->ec);
     }
   else
     {
        if (no_shadow)
          {
             edje_object_signal_emit(cw->shobj, "e,state,shadow,off", "e");
             evas_object_smart_callback_call(cw->smart_obj, "shadow_change", cw->ec);
          }
        else
          _e_comp_object_shadow(cw);
     }

   if (focus || cw->ec->focused)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,focused", "e");
   else
     e_comp_object_signal_emit(cw->smart_obj, "e,state,unfocused", "e");
   if (urgent || cw->ec->urgent)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,urgent", "e");
   else
     e_comp_object_signal_emit(cw->smart_obj, "e,state,not_urgent", "e");
   if (cw->ec->shaded)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,shaded", "e");
   if (cw->ec->sticky)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,sticky", "e");
   if (cw->ec->hung)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,hung", "e");
   /* visibility must always be enabled for re_manage clients to prevent
    * pop-in animations every time the user sees a persistent client again;
    * applying visibility for iconic clients prevents the client from getting
    * stuck as hidden
    */
   if (cw->visible || cw->ec->iconic || cw->ec->re_manage)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,visible", "e");
   else
     e_comp_object_signal_emit(cw->smart_obj, "e,state,hidden", "e");

   if (cw->ec->iconic)
     e_comp_object_signal_emit(cw->smart_obj, "e,action,iconify", "e");
   if (!cw->zoomap_disabled)
     e_zoomap_child_set(cw->zoomobj, NULL);
   if (cw->frame_object)
     {
        edje_object_part_swallow(cw->frame_object, "e.swallow.client", cw->obj);
        edje_object_part_swallow(cw->frame_object, "e.swallow.icon", cw->frame_icon);
        if (cw->zoomap_disabled)
          edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->frame_object);
        else
          {
             e_zoomap_child_set(cw->zoomobj, cw->frame_object);
             edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->zoomobj);
          }
        no_shadow = 1;
     }
   else
     {
        no_shadow = 1;
        if (cw->zoomobj)
          {
             e_zoomap_child_set(cw->zoomobj, cw->obj);
             edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->zoomobj);
          }
        else
          edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->obj);
     }
#ifdef BORDER_ZOOMAPS
   e_zoomap_child_edje_solid_setup(cw->zoomobj);
#endif
   return EINA_TRUE;
}

/////////////////////////////////////////////

/* handle the end of a compositor animation */
static void
_e_comp_object_done_defer(void *data, Evas_Object *obj EINA_UNUSED, const char *emission, const char *source EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   //INF("DONE DEFER %p: %dx%d - %s", cw->ec, cw->w, cw->h, emission);
   /* visible clients which have never been sized are a bug */
   if ((!cw->ec->new_client) && (!cw->ec->changes.size) && ((cw->w < 0) || (cw->h < 0)) && (!strcmp(emission, "e,action,show,done")))
     CRI("ACK!");
   if (cw->animating)
     {
        cw->animating--;
        cw->comp->animating--;
        /* remove ref from animation start, account for possibility of deletion from unref */
        if (!e_object_unref(E_OBJECT(cw->ec))) return;
     }
   /* hide only after animation finishes to guarantee a full run of the animation */
   if (cw->defer_hide && (!strcmp(emission, "e,action,hide,done")))
     evas_object_hide(cw->smart_obj);
}

/////////////////////////////////////////////

/* create necessary objects for clients that e manages */
static void
_e_comp_object_setup(E_Comp_Object *cw)
{
   cw->clip = evas_object_rectangle_add(cw->comp->evas);
   evas_object_resize(cw->clip, cw->comp->man->w, cw->comp->man->h);
   evas_object_smart_member_add(cw->clip, cw->smart_obj);
   cw->effect_obj = edje_object_add(cw->comp->evas);
   evas_object_move(cw->effect_obj, cw->x, cw->y);
   evas_object_clip_set(cw->effect_obj, cw->clip);
   evas_object_smart_member_add(cw->effect_obj, cw->smart_obj);
   e_theme_edje_object_set(cw->effect_obj, "base/theme/comp", "e/comp/effects/none");
   cw->shobj = edje_object_add(cw->comp->evas);
   evas_object_data_set(cw->shobj, "comp_smart_obj", cw->smart_obj);
   edje_object_part_swallow(cw->effect_obj, "e.swallow.content", cw->shobj);
   edje_object_signal_callback_add(cw->shobj, "e,action,*,done", "e", _e_comp_object_done_defer, cw);

   /* name objects appropriately for nicer printing when using e_comp_util_wins_print() */
   if (cw->ec->override)
     {
        evas_object_name_set(cw->shobj, "cw->shobj::WINDOW");
        evas_object_name_set(cw->effect_obj, "cw->effect_obj::WINDOW");
        evas_object_name_set(cw->clip, "cw->clip::WINDOW");
     }
   else if (!cw->ec->input_only)
     {
        evas_object_name_set(cw->shobj, "cw->shobj::CLIENT");
        evas_object_name_set(cw->effect_obj, "cw->effect_obj::CLIENT");
        evas_object_name_set(cw->clip, "cw->clip::CLIENT");
     }
   cw->real_hid = !cw->ec->input_only;
   if (!cw->ec->input_only)
     {
        //e_util_size_debug_set(cw->clip, 1);
        e_util_size_debug_set(cw->effect_obj, 1);
        //e_util_size_debug_set(cw->shobj, 1);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_IN, _e_comp_object_cb_mouse_in, cw);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_OUT, _e_comp_object_cb_mouse_out, cw);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_DOWN, _e_comp_object_cb_mouse_down, cw);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_UP, _e_comp_object_cb_mouse_up, cw);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_MOVE, _e_comp_object_cb_mouse_move, cw);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_WHEEL, _e_comp_object_cb_mouse_wheel, cw);
     }
}

/////////////////////////////////////////////

/* for fast path evas rendering; only called during render */
static void
_e_comp_object_pixels_get(void *data, Evas_Object *obj EINA_UNUSED)
{
   E_Comp_Object *cw = data;
   E_Client *ec = cw->ec;
   int pw, ph;

   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!e_pixmap_size_get(ec->pixmap, &pw, &ph)) return;
   //INF("PIXEL GET %p: %dx%d || %dx%d", ec, ec->w, ec->h, pw, ph);
   if (cw->native)
     E_FREE_FUNC(cw->pending_updates, eina_tiler_free);
   else if (e_comp_object_render(ec->frame))
     {
        /* apply shape mask if necessary */
        if ((!cw->native) && (ec->shaped || ec->shape_changed))
          e_comp_object_shape_apply(ec->frame);
        ec->shape_changed = 0;
     }
   /* shaped clients get precise mouse events to handle transparent pixels */
   evas_object_precise_is_inside_set(cw->obj, ec->shaped || ec->shaped_input);

   /* queue another render if client is still dirty; cannot refresh here. */
   if (e_pixmap_dirty_get(ec->pixmap) && e_pixmap_size_get(ec->pixmap, &pw, &ph))
     e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   //INF("%p PX(%dx%d) EC(%dx%d) CW(%dx%d)", ec, pw, ph, ec->w, ec->h, cw->w, cw->h);
   //e_comp_object_frame_wh_adjust(cw->smart_obj, pw, ph, &pw, &ph);
   //if ((ec->w != pw) || (ec->h != ph))
     //{
        ///* DO NOT RESIZE HERE. */
        //INF("CW RSZ FIX: %dx%d -> %dx%d", ec->w, ec->h, pw, ph);
        //ec->w = pw, ec->h = ph;
        //ec->changes.size = 1;
        //EC_CHANGED(ec);
     //}
}

/////////////////////////////////////////////

static void
_e_comp_object_client_pending_resize_add(E_Client *ec,
                                         int w,
                                         int h,
                                         unsigned int serial)
{
   E_Client_Pending_Resize *pnd;

   pnd = E_NEW(E_Client_Pending_Resize, 1);
   if (!pnd) return;
   pnd->w = w;
   pnd->h = h;
   pnd->serial = serial;
   ec->pending_resize = eina_list_append(ec->pending_resize, pnd);
}

static void
_e_comp_intercept_move(void *data, Evas_Object *obj, int x, int y)
{
   E_Comp_Object *cw = data;
   int ix, iy;

   if ((cw->x == x) && (cw->y == y))
     {
        if ((cw->ec->x != x) || (cw->ec->y != y))
          {
             /* handle case where client tries to move to position and back very quickly */
             cw->ec->x = x, cw->ec->y = y;
             cw->ec->client.x = x + cw->client_inset.l;
             cw->ec->client.y = y + cw->client_inset.t;
          }
        return;
     }
   if (!cw->ec->maximize_override)
     {
        /* prevent moving in some directions while directionally maximized */
        if ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) == E_MAXIMIZE_VERTICAL)
          y = cw->y;
        if ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) == E_MAXIMIZE_HORIZONTAL)
          x = cw->x;
     }
   ix = x + cw->client_inset.l;
   iy = y + cw->client_inset.t;
   if (cw->ec->maximized && (!cw->ec->maximize_override) && ((cw->ec->x != x) || (cw->ec->y != y)) &&
       ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) != E_MAXIMIZE_VERTICAL) &&
       ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) != E_MAXIMIZE_HORIZONTAL))
     {
        /* prevent moving at all if move isn't allowed in current maximize state */
        if ((!e_config->allow_manip) && ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) == E_MAXIMIZE_BOTH)) return;
        /* queue unmaximize if we are allowing move and update unmaximize geometry */
        if ((!cw->ec->shading) && (!cw->ec->shaded))
          {
             cw->ec->changes.need_unmaximize = 1;
             cw->ec->saved.x = ix - cw->ec->zone->x;
             cw->ec->saved.y = iy - cw->ec->zone->y;
             cw->ec->saved.w = cw->ec->client.w;
             cw->ec->saved.h = cw->ec->client.h;
             EC_CHANGED(cw->ec);
             return;
          }
        return;
     }
   cw->ec->x = x, cw->ec->y = y;
   /* only update during resize if triggered by resize */
   if (e_client_util_resizing_get(cw->ec) && (!cw->force_move)) return;
   if (cw->ec->new_client)
     {
        /* don't actually do anything until first client idler loop */
        cw->ec->placed = ((!cw->ec->dialog) && (!cw->ec->parent));
        cw->ec->changes.pos = 1;
        EC_CHANGED(cw->ec);
     }
   else
     {
        /* only update xy position of client to avoid invalid
         * first damage region if it is not a new_client. */
        if (!cw->ec->shading)
          {
             cw->ec->client.x = ix;
             cw->ec->client.y = iy;
          }
        evas_object_move(obj, x, y);
     }
}

static void
_e_comp_intercept_resize(void *data, Evas_Object *obj, int w, int h)
{
   E_Comp_Object *cw = data;
   int pw, ph, fw, fh, iw, ih, prev_w, prev_h;

   if ((cw->w == w) && (cw->h == h))
     {
        if (cw->ec->shading || cw->ec->shaded) return;
        if (((cw->ec->w != w) || (cw->ec->h != h)) ||
            (cw->ec->client.w != w - cw->client_inset.l - cw->client_inset.r) ||
            (cw->ec->client.h != h - cw->client_inset.t - cw->client_inset.b))
          {
             /* handle case where client tries to resize itself and back very quickly */
             cw->ec->w = w, cw->ec->h = h;
             cw->ec->client.w = w - cw->client_inset.l - cw->client_inset.r;
             cw->ec->client.h = h - cw->client_inset.t - cw->client_inset.b;
             evas_object_smart_callback_call(obj, "client_resize", NULL);
          }
        return;
     }
   /* guarantee that fullscreen is fullscreen */
   if (cw->ec->fullscreen && ((w != cw->ec->zone->w) || (h != cw->ec->zone->h)))
     return;
   /* calculate client size */
   iw = w - cw->client_inset.l - cw->client_inset.r;
   ih = h - cw->client_inset.t - cw->client_inset.b;
   if (cw->ec->maximized && (!cw->ec->maximize_override) && ((cw->ec->w != w) || (cw->ec->h != h)))
     {
        /* prevent resizing while maximized depending on direction and config */
        if ((!e_config->allow_manip) && ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) == E_MAXIMIZE_BOTH)) return;
        if ((!cw->ec->shading) && (!cw->ec->shaded))
          {
             cw->ec->changes.need_unmaximize = 1;
             cw->ec->saved.w = iw;
             cw->ec->saved.h = ih;
             cw->ec->saved.x = cw->ec->client.x - cw->ec->zone->x;
             cw->ec->saved.y = cw->ec->client.y - cw->ec->zone->y;
             EC_CHANGED(cw->ec);
             return;
          }
     }
   if (cw->ec->new_client)
     {
        /* do nothing until client idler loops */
        if (!cw->ec->maximized)
          {
             cw->ec->w = w, cw->ec->h = h;
             cw->ec->changes.size = 1;
             EC_CHANGED(cw->ec);
          }
        return;
     }
   if ((!cw->ec->internal) && e_client_util_resizing_get(cw->ec) && cw->ec->netwm.sync.request &&
       ((cw->ec->w != w) && (cw->ec->h != h)))
     {
        /* this is ugly. */
        //INF("PENDING %dx%d", iw, ih);
        /* netwm sync resizes queue themselves and then trigger later on */
        _e_comp_object_client_pending_resize_add(cw->ec, iw, ih, cw->ec->netwm.sync.serial);
     }
   cw->ec->w = w, cw->ec->h = h;
   if ((!cw->ec->shading) && (!cw->ec->shaded))
     {
        /* client geom never changes when shading since the client is never altered */
        //INF("%p: CUR(%dx%d) || REQ(%dx%d)", cw->ec, cw->ec->client.w, cw->ec->client.h, iw, ih);
        cw->ec->client.w = iw;
        cw->ec->client.h = ih;
        if ((cw->ec->client.w < 0) || (cw->ec->client.h < 0)) CRI("WTF");
     }
   if ((!cw->ec->input_only) && (!e_pixmap_size_get(cw->ec->pixmap, &pw, &ph)))
     {
        /* client can't be resized if its pixmap isn't usable, try again */
        e_pixmap_dirty(cw->ec->pixmap);
        e_comp_object_render_update_add(obj);
        cw->ec->changes.size = 1;
        EC_CHANGED(cw->ec);
        return;
     }
   prev_w = cw->w, prev_h = cw->h;
   e_comp_object_frame_wh_adjust(obj, 0, 0, &fw, &fh);
   /* check shading and clamp to pixmap size for regular clients */
   if ((!cw->ec->shading) && (!cw->ec->shaded) && (!cw->ec->input_only) && (!cw->ec->override) &&
       (((w - fw != pw) || (h - fh != ph))))
     {
        //INF("CALLBACK: REQ(%dx%d) != CUR(%dx%d)", w - fw, h - fh, pw, ph);
        evas_object_smart_callback_call(obj, "client_resize", NULL);
        e_comp_object_frame_wh_adjust(obj, pw, ph, &w, &h);
        if ((cw->w == w) && (cw->h == h))
          {
             /* going to be a noop resize which won't trigger smart resize */
             RENDER_DEBUG("DAMAGE RESIZE(%p): %dx%d", cw->ec, cw->ec->client.w, cw->ec->client.h);
             if (cw->updates) eina_tiler_area_size_set(cw->updates, cw->ec->client.w, cw->ec->client.h);
          }
        evas_object_resize(obj, w, h);
     }
   else
     {
        /* "just do it" for overrides */
        //INF("INTERCEPT %dx%d", w, h);
        evas_object_resize(obj, w, h);
     }
   if (!cw->ec->override)
     {
        /* shape probably changed for non-overrides */
        cw->ec->need_shape_merge |= cw->ec->shaped || cw->ec->shaped_input;
        cw->ec->need_shape_export |= cw->ec->shaped;
        if (cw->ec->shaped || cw->ec->shaped_input)
          EC_CHANGED(cw->ec);
     }

   /* this fixes positioning jiggles when using a resize mode
    * which also changes the client's position
    */
   cw->force_move = 1;
   switch (cw->ec->resize_mode)
     {
      case E_POINTER_RESIZE_BL:
      case E_POINTER_RESIZE_L:
        evas_object_move(obj, cw->x + prev_w - cw->w, cw->y);
        break;
      case E_POINTER_RESIZE_TL:
        evas_object_move(obj, cw->x + prev_w - cw->w, cw->y + prev_h - cw->h);
        break;
      case E_POINTER_RESIZE_T:
      case E_POINTER_RESIZE_TR:
        evas_object_move(obj, cw->x, cw->y + prev_h - cw->h);
        break;
      default:
        break;
     }
   cw->force_move = 0;
}

static void
_e_comp_intercept_layer_set(void *data, Evas_Object *obj, int layer)
{
   E_Comp_Object *cw = data;
   unsigned int l = e_comp_canvas_layer_map(layer);
   int oldraise;

   if (cw->ec->layer_block)
     {
        /* doing a compositor effect, follow directions */
        evas_object_layer_set(obj, layer);
        if (layer == cw->ec->layer) //trying to put layer back
          {
             E_Client *ec;

             e_comp_shape_queue(cw->comp);
             ec = e_client_above_get(cw->ec);
             if (ec && (evas_object_layer_get(ec->frame) != evas_object_layer_get(obj)))
               {
                  ec = e_client_below_get(cw->ec);
                  if (ec && (evas_object_layer_get(ec->frame) == evas_object_layer_get(cw->smart_obj)))
                    {
                       evas_object_stack_above(obj, ec->frame);
                       return;
                    }
                  ec = NULL;
               }
             if (ec && (cw->ec->parent == ec))
               evas_object_stack_above(obj, ec->frame);
             else
               evas_object_stack_below(obj, ec ? ec->frame : cw->comp->layers[cw->layer].obj);
          }
        return;
     }
   if (cw->layer == l) return;
   if (e_comp_canvas_client_layer_map(layer) == 9999)
     return; //invalid layer for clients not doing comp effects
   if (cw->ec->fullscreen)
     {
        cw->ec->saved.layer = layer;
        return;
     }
   oldraise = e_config->transient.raise;
   _e_comp_object_layers_remove(cw);
   /* clamp to valid client layer */
   layer = e_comp_canvas_client_layer_map_nearest(layer);
   cw->ec->layer = layer;
   if (e_config->transient.layer)
     {
        E_Client *child;
        Eina_List *list = eina_list_clone(cw->ec->transients);

        /* We need to set raise to one, else the child wont
         * follow to the new layer. It should be like this,
         * even if the user usually doesn't want to raise
         * the transients.
         */
        e_config->transient.raise = 1;
        EINA_LIST_FREE(list, child)
          evas_object_layer_set(child->frame, layer);
     }
   if (!cw->ec->override)
     {
        /* set client stacking hints based on layer */
        if (layer == E_LAYER_CLIENT_BELOW)
          e_hints_window_stacking_set(cw->ec, E_STACKING_BELOW);
        else if (layer == E_LAYER_CLIENT_ABOVE)
          e_hints_window_stacking_set(cw->ec, E_STACKING_ABOVE);
        else
          e_hints_window_stacking_set(cw->ec, E_STACKING_NONE);
     }

   e_config->transient.raise = oldraise;
   cw->layer = e_comp_canvas_layer_map(layer);
   _e_comp_object_layers_add(cw, NULL, NULL, 0);
   //if (cw->ec->new_client)
     //INF("CLIENT STACKED %p: %u", cw->ec, layer);
   evas_object_layer_set(obj, layer);
   if (!cw->comp->layers[cw->layer].obj) return; //this is a layer marker
   evas_object_stack_below(obj, cw->comp->layers[cw->layer].obj);
   if (evas_object_below_get(obj) == cw->comp->layers[cw->layer].obj)
     {
        /* can't stack a client above its own layer marker */
        CRI("STACKING ERROR!!!");
     }
   e_comp_shape_queue(cw->comp);
}

typedef void (*E_Comp_Object_Stack_Func)(Evas_Object *obj, Evas_Object *stack);

static void
_e_comp_intercept_stack_helper(E_Comp_Object *cw, Evas_Object *stack, E_Comp_Object_Stack_Func stack_cb)
{
   E_Comp_Object *cw2 = NULL;
   E_Client *ecstack;
   short layer;
   Evas_Object *o = stack;
   Eina_Bool raising = stack_cb == evas_object_stack_above;

   if (cw->ec->layer_block)
     {
        /* obey compositor effects! */
        if (cw->ec->layer == evas_object_layer_get(cw->smart_obj))
          evas_object_data_set(cw->smart_obj, "client_restack", (void*)1);
        stack_cb(cw->smart_obj, stack);
        if (cw->ec->layer == evas_object_layer_get(cw->smart_obj))
          evas_object_data_del(cw->smart_obj, "client_restack");
        return;
     }
   /* assume someone knew what they were doing during client init */
   if (cw->ec->new_client)
     layer = cw->ec->layer;
   else
     layer = evas_object_layer_get(stack);
   ecstack = e_client_below_get(cw->ec);
   if (layer != e_comp_canvas_layer_map_to(cw->layer))
     {
        /* some FOOL is trying to restack a layer marker */
        if (cw->smart_obj == cw->comp->layers[cw->layer].obj) return;
        evas_object_layer_set(cw->smart_obj, layer);
        /* we got our layer wrangled, return now! */
        if (layer != e_comp_canvas_layer_map_to(cw->layer)) return;
     }

   /* check if we're stacking below another client */
   cw2 = evas_object_data_get(o, "comp_obj");
   while (!cw2)
     {
        /* check for non-client layer object */
        if (!e_util_strcmp(evas_object_name_get(o), "layer_obj"))
          break;
        /* find an existing client to use for layering
         * by walking up the object stack
         *
         * this is guaranteed to be pretty quick since we'll either:
         * - run out of client layers
         * - find a stacking client
         */
        o = evas_object_above_get(o);
        if ((!o) || (o == cw->smart_obj)) break;
        if (evas_object_layer_get(o) != layer)
          {
             /* reached the top client layer somehow
              * use top client object
              */
             o = cw->comp->layers[e_comp_canvas_layer_map(E_LAYER_CLIENT_PRIO)].obj;
          }
        if (!o)
          /* top client layer window hasn't been stacked yet. this probably shouldn't happen?
           * return here since the top client layer window 
           */
          {
             E_Client *ec;

             ec = e_client_top_get(cw->comp);
             if (ec)
               o = ec->frame;
             //else //wat
          }
        if (o) cw2 = evas_object_data_get(o, "comp_obj");
     }


   /* remove existing layers */
   _e_comp_object_layers_remove(cw);
   if (cw2)
     {
        if (o == stack) //if stacking above, cw2 is above; else cw2 is below
          _e_comp_object_layers_add(cw, raising ? cw2 : NULL, raising ? NULL : cw2, 0);
        else if (o == cw->smart_obj) //prepend (lower) if not stacking above
          _e_comp_object_layers_add(cw, NULL, NULL, !raising);
        else //if no stacking objects found, either raise or lower
          _e_comp_object_layers_add(cw, raising ? NULL : cw2, raising ? cw2 : NULL, 0);
     }
   else
     _e_comp_object_layers_add(cw, NULL, NULL, 0);
   /* set restack if stacking has changed */
   if (cw->ec->new_client || (!ecstack) || (ecstack->frame != o))
     evas_object_data_set(cw->smart_obj, "client_restack", (void*)1);
   stack_cb(cw->smart_obj, stack);
   if (cw->comp->layers[cw->layer].obj)
     if (evas_object_below_get(cw->smart_obj) == cw->comp->layers[cw->layer].obj)
       {
          CRI("STACKING ERROR!!!");
       }
   if (cw->ec->new_client || (!ecstack) || (ecstack->frame != o))
     evas_object_data_del(cw->smart_obj, "client_restack");
   e_comp_shape_queue(cw->comp);
}

static void
_e_comp_intercept_stack_above(void *data, Evas_Object *obj, Evas_Object *above)
{
   EINA_SAFETY_ON_TRUE_RETURN(obj == above);
   if (evas_object_below_get(obj) == above) return;
   _e_comp_intercept_stack_helper(data, above, evas_object_stack_above);
}

static void
_e_comp_intercept_stack_below(void *data, Evas_Object *obj, Evas_Object *below)
{
   EINA_SAFETY_ON_TRUE_RETURN(obj == below);
   if (evas_object_above_get(obj) == below) return;
   _e_comp_intercept_stack_helper(data, below, evas_object_stack_below);
}

static void
_e_comp_intercept_lower(void *data, Evas_Object *obj)
{
   E_Comp_Object *cw = data;
   Evas_Object *o;

   if (cw->ec->layer_block)
     {
        evas_object_lower(obj);
        return;
     }
   if (!EINA_INLIST_GET(cw->ec)->prev) return; //already lowest on layer
   o = evas_object_below_get(obj);
   _e_comp_object_layers_remove(cw);
   /* prepend to client list since this client should be the first item now */
   _e_comp_object_layers_add(cw, NULL, NULL, 1);
   if (evas_object_layer_get(o) != evas_object_layer_get(obj)) return; //already at bottom!
   if (obj == cw->comp->layers[cw->layer].obj) return; //never lower a layer marker!
   evas_object_data_set(obj, "client_restack", (void*)1);
   evas_object_lower(obj);
   evas_object_data_del(obj, "client_restack");
   e_comp_shape_queue(cw->comp);
}

static void
_e_comp_intercept_raise(void *data, Evas_Object *obj)
{
   E_Comp_Object *cw = data;
   Evas_Object *o;

   if (cw->ec->layer_block)
     {
        evas_object_raise(obj);
        return;
     }
   if (!EINA_INLIST_GET(cw->ec)->next) return;//already highest on layer
   o = evas_object_above_get(obj);
   {
      E_Client *ecabove = e_client_above_get(cw->ec);
      if (ecabove && (ecabove->frame == cw->comp->layers[cw->layer].obj) &&
          (ecabove->frame == o)) return; //highest below marker
   }
   if (evas_object_layer_get(o) != evas_object_layer_get(obj)) return; //already at top!
   if (obj == cw->comp->layers[cw->layer].obj) //never raise a non-layer marker!
     evas_object_raise(obj);
   else
     {
        /* still stack below layer marker */
        evas_object_stack_below(obj, cw->comp->layers[cw->layer].obj);
        if (e_client_focus_track_enabled())
          e_client_raise_latest_set(cw->ec); //modify raise list if necessary
     }
   e_comp_shape_queue(cw->comp);
}

static void
_e_comp_intercept_hide(void *data, Evas_Object *obj)
{
   E_Comp_Object *cw = data;

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
   if( !_e_comp_object_intercept_hook_call(E_COMP_OBJECT_INTERCEPT_HOOK_HIDE, cw->ec)) return;
#endif

   if (cw->ec->hidden)
     {
        /* hidden flag = just do it */
        evas_object_hide(obj);
        return;
     }

   if (cw->ec->input_only)
     {
        /* input_only = who cares */
        evas_object_hide(obj);
        return;
     }
   /* already hidden or currently animating */
   if ((!cw->visible) || (cw->animating && (!cw->ec->iconic))) return;

   /* don't try hiding during shutdown */
   cw->defer_hide |= stopping;
   if (!cw->defer_hide)
     {
        if ((!cw->ec->iconic) && (!cw->ec->override))
          {
             /* unset delete requested so the client doesn't break */
             cw->ec->delete_requested = 0;
             e_hints_window_hidden_set(cw->ec);
          }
        if ((!cw->animating) || (cw->ec->iconic))
          {
             if (cw->ec->iconic)
               e_comp_object_signal_emit(obj, "e,action,iconify", "e");
             else
               {
                  e_comp_object_signal_emit(obj, "e,state,hidden", "e");
                  cw->comp->animating++;
                  cw->animating++;
                  e_object_ref(E_OBJECT(cw->ec));
               }
             cw->defer_hide = !!cw->animating;
             if (!cw->animating)
               e_comp_object_effect_set(obj, NULL);
          }
     }
   if (cw->animating) return;
   /* if we have no animations running, go ahead and hide */
   cw->defer_hide = 0;
   evas_object_hide(obj);
}

static void
_e_comp_intercept_show_helper(E_Comp_Object *cw)
{
   int w = 0, h = 0;

   if (cw->ec->sticky)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,sticky", "e");
   if (cw->visible)
     {
        if (cw->ec->iconic && cw->animating)
          {
             /* triggered during iconify animation */
             e_comp_object_signal_emit(cw->smart_obj, "e,action,uniconify", "e");
             cw->defer_hide = 0;
          }
        return;
     }
   if (cw->ec->new_client)
     {
        /* ignore until client idler first run */
        cw->ec->changes.visible = !cw->ec->hidden;
        cw->ec->visible = 1;
        EC_CHANGED(cw->ec);

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
        if (!_e_comp_object_intercept_hook_call(E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER, cw->ec)) return;
#endif
        return;
     }
   if (cw->ec->input_only)
     {
        /* who cares */
        cw->real_hid = 0;
        evas_object_move(cw->smart_obj, cw->ec->x, cw->ec->y);
        evas_object_resize(cw->smart_obj, cw->ec->w, cw->ec->h);
        evas_object_show(cw->smart_obj);
        return;
     }
   /* re-set geometry */
   evas_object_move(cw->smart_obj, cw->ec->x, cw->ec->y);
   /* ensure that some kind of frame calc has occurred if there's a frame */
   if (cw->frame_object && (cw->ec->h == cw->ec->client.h) && (cw->ec->w == cw->ec->client.w))
     CRI("ACK!");
   /* force resize in case it hasn't happened yet, or just to update size */
   evas_object_resize(cw->smart_obj, cw->ec->w, cw->ec->h);
   if ((cw->w < 1) || (cw->h < 1))
     {
        /* if resize didn't go through, try again */
        cw->ec->visible = cw->ec->changes.visible = 1;
        EC_CHANGED(cw->ec);
        return;
     }
   /* if pixmap not available, clear pixmap since we're going to fetch it again */
   if (!e_pixmap_size_get(cw->ec->pixmap, &w, &h))
     e_pixmap_clear(cw->ec->pixmap);

   if (cw->real_hid && w && h)
     {
        DBG("  [%p] real hid - fix", cw->ec);
        cw->real_hid = 0;
        /* force comp theming in case it didn't happen already */
        e_comp_object_frame_theme_set(cw->smart_obj, E_COMP_OBJECT_FRAME_RESHADOW);
     }

   /* only do the show if show is allowed */
   if (!cw->real_hid)
     {
        Ecore_X_Window_Attributes att;
        memset(&att, 0, sizeof(Ecore_X_Window_Attributes));
        ecore_x_window_attributes_get(e_client_util_pwin_get(cw->ec), &att);

        if (cw->ec->internal) //internal clients render when they feel like it
          e_comp_object_damage(cw->smart_obj, 0, 0, cw->w, cw->h);

        if (!cw->update_count || !(att.visible)) return;

        evas_object_show(cw->smart_obj);
     }
}

static void
_e_comp_intercept_show(void *data, Evas_Object *obj EINA_UNUSED)
{
   E_Comp_Object *cw = data;
   E_Client *ec = cw->ec;

   if (ec->ignored) return;

   if (cw->effect_obj)
     {
        //INF("SHOW2 %p", ec);
        _e_comp_intercept_show_helper(cw);
        return;
     }
   //INF("SHOW %p", ec);
   if (ec->input_only)
     {
        cw->effect_obj = evas_object_rectangle_add(cw->comp->evas);
        evas_object_color_set(cw->effect_obj, 0, 0, 0, 0);
        evas_object_smart_member_add(cw->effect_obj, cw->smart_obj);
     }
   else
     {
        _e_comp_object_setup(cw);
        cw->obj = evas_object_image_filled_add(cw->comp->evas);
        e_util_size_debug_set(cw->obj, 1);
        evas_object_image_pixels_get_callback_set(cw->obj, _e_comp_object_pixels_get, cw);
        evas_object_image_smooth_scale_set(cw->obj, e_comp_config_get()->smooth_windows);
        evas_object_name_set(cw->obj, "cw->obj");
        evas_object_image_colorspace_set(cw->obj, EVAS_COLORSPACE_ARGB8888);
        evas_object_image_alpha_set(cw->obj, ec->argb);
#ifdef BORDER_ZOOMAPS
        e_comp_object_zoomap_set(o, 1);
#else
        cw->zoomap_disabled = 1;
#endif
        cw->redirected = 1;
        evas_object_color_set(cw->clip, ec->netwm.opacity, ec->netwm.opacity, ec->netwm.opacity, ec->netwm.opacity);

        if ((!cw->ec->input_only) && (!cw->ec->ignored))
          {
             cw->updates = eina_tiler_new(cw->ec->client.w, cw->ec->client.h);
             eina_tiler_tile_size_set(cw->updates, 1, 1);
          }
     }

   _e_comp_intercept_show_helper(cw);
}

static void
_e_comp_intercept_focus(void *data, Evas_Object *obj, Eina_Bool focus)
{
   E_Comp_Object *cw = data;
   E_Client *ec;

   ec = cw->ec;
   /* note: this is here as it seems there are enough apps that do not even
    * expect us to emulate a look of focus but not actually set x input
    * focus as we do - so simply abort any focuse set on such windows */
   /* be strict about accepting focus hint */
   if (e_pixmap_is_x(ec->pixmap))
     {
        /* be strict about accepting focus hint */
        if ((!ec->icccm.accepts_focus) &&
            (!ec->icccm.take_focus)) return;
     }
   if (focus && ec->lock_focus_out) return;
   if (e_object_is_del(E_OBJECT(ec)) && focus)
     CRI("CAN'T FOCUS DELETED CLIENT!");

   /* filter focus setting based on current state */
   if (focus)
     {
        if (ec->focused)
          {
             evas_object_focus_set(obj, focus);
             return;
          }
        if ((ec->iconic) && (!ec->deskshow))
          {
             /* dont focus an iconified window. that's silly! */
             e_client_uniconify(ec);
             if (e_client_focus_track_enabled())
               e_client_focus_latest_set(ec);
             return;
          }
        if (!ec->visible)
          {
             return;
          }
        /* FIXME: hack for deskflip animation:
         * dont update focus when sliding previous desk */
        if ((!ec->sticky) &&
                 (ec->desk != e_desk_current_get(ec->desk->zone)))
          {
             return;
          }
     }

   if (focus)
     {
        /* check for dialog children that steal focus */
        if ((ec->modal) && (ec->modal != ec) &&
            (ec->modal->visible) && (!e_object_is_del(E_OBJECT(ec->modal))))
          {
             evas_object_focus_set(ec->modal->frame, focus);
             return;
          }
        else if ((ec->leader) && (ec->leader->modal) &&
                 (ec->leader->modal != ec) && ec->leader->modal->visible &&
                 (!e_object_is_del(E_OBJECT(ec->leader->modal))))
          {
             evas_object_focus_set(ec->leader->modal->frame, focus);
             return;
          }
        if (!cw->visible)
          {
             /* not yet visible, wait till the next time... */
             ec->want_focus = !ec->hidden;
             if (ec->want_focus)
               EC_CHANGED(ec);
             return;
          }
        e_client_focused_set(ec);
     }
   else
     {
        if (e_client_focused_get() == ec)
          e_client_focused_set(NULL);
     }
   evas_object_focus_set(obj, focus);
}

////////////////////////////////////////////////////

static void
_e_comp_object_frame_recalc(E_Comp_Object *cw)
{
   int w, h, ox, oy, ow, oh;

   if (cw->frame_object)
     {
        if (cw->obj) edje_object_part_unswallow(cw->frame_object, cw->obj);
        evas_object_geometry_get(cw->frame_object, NULL, NULL, &w, &h);
        /* set a fixed size, force edje calc, check size difference */
        evas_object_resize(cw->frame_object, MAX(w, 50), MAX(h, 50));
        edje_object_message_signal_process(cw->frame_object);
        edje_object_calc_force(cw->frame_object);
        edje_object_part_geometry_get(cw->frame_object, "e.swallow.client", &ox, &oy, &ow, &oh);
        cw->client_inset.l = ox;
        cw->client_inset.r = MAX(w, 50) - (ox + ow);
        cw->client_inset.t = oy;
        cw->client_inset.b = MAX(h, 50) - (oy + oh);
        if (cw->obj) edje_object_part_swallow(cw->frame_object, "e.swallow.client", cw->obj);
        evas_object_resize(cw->frame_object, w, h);
        if (cw->input_rect.w && (!cw->input_obj))
          evas_object_pass_events_set(cw->obj, 0);
     }
   else
     {
        cw->client_inset.l = 0;
        cw->client_inset.r = 0;
        cw->client_inset.t = 0;
        cw->client_inset.b = 0;
     }
   cw->client_inset.calc = 1;
}

static void
_e_comp_smart_cb_frame_recalc(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;
   int w, h, pw, ph;

   /* - get current size
    * - calc new size
    * - readjust for new frame size
    */

   w = cw->ec->w, h = cw->ec->h;
   e_comp_object_frame_wh_unadjust(obj, w, h, &pw, &ph);

   _e_comp_object_frame_recalc(cw);

   if (!cw->ec->fullscreen)
     e_comp_object_frame_wh_adjust(obj, cw->ec->client.w, cw->ec->client.h, &w, &h);

   evas_object_smart_callback_call(cw->smart_obj, "frame_recalc_done", &cw->client_inset);
   if (cw->ec->shading || cw->ec->shaded) return;
   if (cw->ec->fullscreen)
     evas_object_resize(cw->ec->frame, cw->ec->zone->w, cw->ec->zone->h);
   else if (cw->ec->new_client)
     {
        e_comp_object_frame_wh_adjust(obj, pw, ph, &w, &h);
        evas_object_resize(cw->ec->frame, w, h);
     }
   else if ((w != cw->ec->w) || (h != cw->ec->h))
     evas_object_resize(cw->ec->frame, w, h);
}

static Eina_Bool
_e_comp_object_shade_animator(void *data)
{
   E_Comp_Object *cw = data;
   Eina_Bool move = EINA_FALSE;
   int x, y, w, h;
   double dt, val;
   double dur;

   dt = ecore_loop_time_get() - cw->shade.start;
   dur = cw->ec->client.h / e_config->border_shade_speed;
   val = dt / dur;

   if (val < 0.0)
     val = 0.0;
   else if (val > 1.0)
     val = 1.0;

   if (e_config->border_shade_transition == E_TRANSITION_SINUSOIDAL)
     {
        cw->shade.val =
          ecore_animator_pos_map(val, ECORE_POS_MAP_SINUSOIDAL, 0.0, 0.0);
        if (!cw->ec->shaded) cw->shade.val = 1.0 - cw->shade.val;
     }
   else if (e_config->border_shade_transition == E_TRANSITION_DECELERATE)
     {
        cw->shade.val =
          ecore_animator_pos_map(val, ECORE_POS_MAP_DECELERATE, 0.0, 0.0);
        if (!cw->ec->shaded) cw->shade.val = 1.0 - cw->shade.val;
     }
   else if (e_config->border_shade_transition == E_TRANSITION_ACCELERATE)
     {
        cw->shade.val =
          ecore_animator_pos_map(val, ECORE_POS_MAP_ACCELERATE, 0.0, 0.0);
        if (!cw->ec->shaded) cw->shade.val = 1.0 - cw->shade.val;
     }
   else if (e_config->border_shade_transition == E_TRANSITION_LINEAR)
     {
        cw->shade.val =
          ecore_animator_pos_map(val, ECORE_POS_MAP_LINEAR, 0.0, 0.0);
        if (!cw->ec->shaded) cw->shade.val = 1.0 - cw->shade.val;
     }
   else if (e_config->border_shade_transition == E_TRANSITION_ACCELERATE_LOTS)
     {
        cw->shade.val =
          ecore_animator_pos_map(val, ECORE_POS_MAP_ACCELERATE_FACTOR, 1.7, 0.0);
        if (!cw->ec->shaded) cw->shade.val = 1.0 - cw->shade.val;
     }
   else if (e_config->border_shade_transition == E_TRANSITION_DECELERATE_LOTS)
     {
        cw->shade.val =
          ecore_animator_pos_map(val, ECORE_POS_MAP_DECELERATE_FACTOR, 1.7, 0.0);
        if (!cw->ec->shaded) cw->shade.val = 1.0 - cw->shade.val;
     }
   else if (e_config->border_shade_transition == E_TRANSITION_SINUSOIDAL_LOTS)
     {
        cw->shade.val =
          ecore_animator_pos_map(val, ECORE_POS_MAP_SINUSOIDAL_FACTOR, 1.7, 0.0);
        if (!cw->ec->shaded) cw->shade.val = 1.0 - cw->shade.val;
     }
   else if (e_config->border_shade_transition == E_TRANSITION_BOUNCE)
     {
        cw->shade.val =
          ecore_animator_pos_map(val, ECORE_POS_MAP_BOUNCE, 1.2, 3.0);
        if (!cw->ec->shaded) cw->shade.val = 1.0 - cw->shade.val;
     }
   else if (e_config->border_shade_transition == E_TRANSITION_BOUNCE_LOTS)
     {
        cw->shade.val =
          ecore_animator_pos_map(val, ECORE_POS_MAP_BOUNCE, 1.2, 5.0);
        if (!cw->ec->shaded) cw->shade.val = 1.0 - cw->shade.val;
     }
   else
     {
        cw->shade.val =
          ecore_animator_pos_map(val, ECORE_POS_MAP_LINEAR, 0.0, 0.0);
        if (!cw->ec->shaded) cw->shade.val = 1.0 - cw->shade.val;
     }

   /* due to M_PI's innacuracy, cos(M_PI/2) != 0.0, so we need this */
   if (cw->shade.val < 0.001) cw->shade.val = 0.0;
   else if (cw->shade.val > .999)
     cw->shade.val = 1.0;

   x = cw->ec->x, y = cw->ec->y, w = cw->ec->w, h = cw->ec->h;
   if (cw->shade.dir == E_DIRECTION_UP)
     h = cw->client_inset.t + cw->ec->client.h * cw->shade.val;
   else if (cw->shade.dir == E_DIRECTION_DOWN)
     {
        h = cw->client_inset.t + cw->ec->client.h * cw->shade.val;
        y = cw->shade.y + cw->ec->client.h * (1 - cw->shade.val);
        move = EINA_TRUE;
     }
   else if (cw->shade.dir == E_DIRECTION_LEFT)
     w = cw->client_inset.t + cw->ec->client.w * cw->shade.val;
   else if (cw->shade.dir == E_DIRECTION_RIGHT)
     {
        w = cw->client_inset.t + cw->ec->client.w * cw->shade.val;
        x = cw->shade.x + cw->ec->client.w * (1 - cw->shade.val);
        move = EINA_TRUE;
     }

   if (move) evas_object_move(cw->smart_obj, x, y);
   evas_object_resize(cw->smart_obj, w, h);

   /* we're done */
   if (val == 1)
     {
        cw->shade.anim = NULL;

        evas_object_smart_callback_call(cw->smart_obj, "shade_done", NULL);
        if (cw->ec->shaded)
          e_comp_object_signal_emit(cw->smart_obj, "e,state,shaded", "e");
        else
          e_comp_object_signal_emit(cw->smart_obj, "e,state,unshaded", "e");
        edje_object_message_signal_process(cw->frame_object);
        _e_comp_smart_cb_frame_recalc(cw, cw->smart_obj, NULL);
     }
   return cw->ec->shading;
}

static void
_e_comp_smart_cb_shading(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   E_FREE_FUNC(cw->shade.anim, ecore_timer_del);

   cw->shade.x = cw->x;
   cw->shade.y = cw->y;
   e_comp_object_signal_emit(cw->smart_obj, "e,state,shading", "e");
   cw->shade.start = ecore_loop_time_get();
   cw->shade.dir = (E_Direction)event_info;
   cw->shade.anim = ecore_animator_add(_e_comp_object_shade_animator, cw);
}

static void
_e_comp_smart_cb_shaded(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   E_FREE_FUNC(cw->shade.anim, ecore_timer_del);

   cw->shade.x = cw->x;
   cw->shade.y = cw->y;
   e_comp_object_signal_emit(cw->smart_obj, "e,state,shaded", "e");
   cw->shade.start = -100;
   cw->shade.dir = (E_Direction)event_info;
   _e_comp_object_shade_animator(cw);
}

static void
_e_comp_smart_cb_unshading(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   E_FREE_FUNC(cw->shade.anim, ecore_timer_del);

   cw->shade.dir = (E_Direction)event_info;
   if (cw->shade.dir == E_DIRECTION_UP ||
       cw->shade.dir == E_DIRECTION_LEFT)
     {
        cw->shade.x = cw->x;
        cw->shade.y = cw->y;
     }
   else
     {
        cw->shade.x = cw->x - cw->w;
        cw->shade.y = cw->y - cw->h;
     }
   e_comp_object_signal_emit(cw->smart_obj, "e,state,unshading", "e");
   cw->shade.start = ecore_loop_time_get();
   cw->shade.anim = ecore_animator_add(_e_comp_object_shade_animator, cw);
}

static void
_e_comp_smart_cb_unshaded(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   E_FREE_FUNC(cw->shade.anim, ecore_timer_del);

   cw->shade.dir = (E_Direction)event_info;
   if (cw->shade.dir == E_DIRECTION_UP ||
       cw->shade.dir == E_DIRECTION_LEFT)
     {
        cw->shade.x = cw->x;
        cw->shade.y = cw->y;
     }
   else
     {
        cw->shade.x = cw->x - cw->w;
        cw->shade.y = cw->y - cw->h;
     }
   e_comp_object_signal_emit(cw->smart_obj, "e,state,unshaded", "e");
   cw->shade.start = -100;
   _e_comp_object_shade_animator(cw);
}

static void
_e_comp_smart_cb_maximize(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   _e_comp_object_shadow_setup(cw);
   if (cw->frame_object)
     {
        _e_comp_object_shadow(cw);
        e_comp_object_signal_emit(cw->smart_obj, "e,action,maximize", "e");
        _e_comp_object_frame_recalc(cw);
        evas_object_smart_callback_call(cw->smart_obj, "frame_recalc_done", &cw->client_inset);
     }
}

static void
_e_comp_smart_cb_fullscreen(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (_e_comp_object_shadow_setup(cw))
     e_comp_object_damage(cw->smart_obj, 0, 0, cw->ec->w, cw->ec->h);
   if (cw->frame_object)
     {
        _e_comp_object_shadow(cw);
        e_comp_object_signal_emit(cw->smart_obj, "e,action,maximize,fullscreen", "e");
        _e_comp_object_frame_recalc(cw);
        evas_object_smart_callback_call(cw->smart_obj, "frame_recalc_done", &cw->client_inset);
     }
}

static void
_e_comp_smart_cb_unmaximize(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (cw->frame_object)
     {
        _e_comp_object_shadow(cw);
        e_comp_object_signal_emit(obj, "e,action,unmaximize", "e");
        _e_comp_object_frame_recalc(cw);
        evas_object_smart_callback_call(cw->smart_obj, "frame_recalc_done", &cw->client_inset);
     }
}

static void
_e_comp_smart_cb_unfullscreen(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (_e_comp_object_shadow_setup(cw))
     {
        EC_CHANGED(cw->ec);
        cw->ec->changes.size = 1;
     }
   if (cw->frame_object)
     {
        _e_comp_object_shadow(cw);
        e_comp_object_signal_emit(obj, "e,action,unmaximize,unfullscreen", "e");
        _e_comp_object_frame_recalc(cw);
        evas_object_smart_callback_call(cw->smart_obj, "frame_recalc_done", &cw->client_inset);
     }
}

static void
_e_comp_smart_cb_sticky(void *data EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   e_comp_object_signal_emit(obj, "e,state,sticky", "e");
}

static void
_e_comp_smart_cb_unsticky(void *data EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   e_comp_object_signal_emit(obj, "e,state,unsticky", "e");
}

static void
_e_comp_smart_cb_unhung(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   e_comp_object_signal_emit(cw->smart_obj, "e,state,unhung", "e");
}

static void
_e_comp_smart_cb_hung(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   e_comp_object_signal_emit(cw->smart_obj, "e,state,hung", "e");
}

static void
_e_comp_smart_focus_in(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   e_comp_object_signal_emit(obj, "e,state,focused", "e");
}

static void
_e_comp_smart_focus_out(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (!e_object_is_del(E_OBJECT(cw->ec)))
     e_comp_object_signal_emit(obj, "e,state,unfocused", "e");
}

static void
_e_comp_smart_add(Evas_Object *obj)
{
   E_Comp_Object *cw;

   cw = E_NEW(E_Comp_Object, 1);
   cw->smart_obj = obj;
   cw->x = cw->y = cw->w = cw->h = -1;
   evas_object_smart_data_set(obj, cw);
   cw->opacity = 255.0;
   evas_object_data_set(obj, "comp_obj", cw);
   evas_object_move(obj, -1, -1);
   /* intercept ALL the callbacks! */
   evas_object_intercept_stack_above_callback_add(obj, _e_comp_intercept_stack_above, cw);
   evas_object_intercept_stack_below_callback_add(obj, _e_comp_intercept_stack_below, cw);
   evas_object_intercept_raise_callback_add(obj, _e_comp_intercept_raise, cw);
   evas_object_intercept_lower_callback_add(obj, _e_comp_intercept_lower, cw);
   evas_object_intercept_layer_set_callback_add(obj, _e_comp_intercept_layer_set, cw);
   evas_object_intercept_move_callback_add(obj, _e_comp_intercept_move, cw);
   evas_object_intercept_resize_callback_add(obj, _e_comp_intercept_resize, cw);
   evas_object_intercept_show_callback_add(obj, _e_comp_intercept_show, cw);
   evas_object_intercept_hide_callback_add(obj, _e_comp_intercept_hide, cw);
   evas_object_intercept_focus_set_callback_add(obj, _e_comp_intercept_focus, cw);

   evas_object_smart_callback_add(obj, "shading", _e_comp_smart_cb_shading, cw);
   evas_object_smart_callback_add(obj, "shaded", _e_comp_smart_cb_shaded, cw);
   evas_object_smart_callback_add(obj, "unshading", _e_comp_smart_cb_unshading, cw);
   evas_object_smart_callback_add(obj, "unshaded", _e_comp_smart_cb_unshaded, cw);

   evas_object_smart_callback_add(obj, "maximize", _e_comp_smart_cb_maximize, cw);
   evas_object_smart_callback_add(obj, "fullscreen", _e_comp_smart_cb_fullscreen, cw);
   evas_object_smart_callback_add(obj, "unmaximize", _e_comp_smart_cb_unmaximize, cw);
   evas_object_smart_callback_add(obj, "unfullscreen", _e_comp_smart_cb_unfullscreen, cw);

   evas_object_smart_callback_add(obj, "stick", _e_comp_smart_cb_sticky, cw);
   evas_object_smart_callback_add(obj, "unstick", _e_comp_smart_cb_unsticky, cw);

   evas_object_smart_callback_add(obj, "hung", _e_comp_smart_cb_hung, cw);
   evas_object_smart_callback_add(obj, "unhung", _e_comp_smart_cb_unhung, cw);

   evas_object_smart_callback_add(obj, "frame_recalc", _e_comp_smart_cb_frame_recalc, cw);

   evas_object_event_callback_add(obj, EVAS_CALLBACK_FOCUS_IN, _e_comp_smart_focus_in, cw);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_FOCUS_OUT, _e_comp_smart_focus_out, cw);
}

static void
_e_comp_smart_color_set(Evas_Object *obj, int r, int g, int b, int a)
{
   INTERNAL_ENTRY;
   evas_object_color_set(cw->clip, r, g, b, a);
   evas_object_smart_callback_call(obj, "color_set", NULL);
}


static void
_e_comp_smart_clip_set(Evas_Object *obj, Evas_Object *clip)
{
   INTERNAL_ENTRY;
   evas_object_clip_set(cw->clip, clip);
}

static void
_e_comp_smart_clip_unset(Evas_Object *obj)
{
   INTERNAL_ENTRY;
   evas_object_clip_unset(cw->clip);
}

static void
_e_comp_smart_hide(Evas_Object *obj)
{
   INTERNAL_ENTRY;
   cw->visible = 0;
   evas_object_hide(cw->clip);
   if (cw->input_obj) evas_object_hide(cw->input_obj);
   evas_object_hide(cw->effect_obj);
   if (stopping) return;
   /* ensure focus-out */
   if (cw->ec->focused)
     evas_object_focus_set(cw->ec->frame, 0);
   e_comp_render_queue(cw->comp); //force nocomp recheck
   e_comp_shape_queue(cw->comp);
}

static void
_e_comp_smart_show(Evas_Object *obj)
{
   E_Client *tmp;
   Eina_List *l;

   INTERNAL_ENTRY;
   cw->defer_hide = 0;
   cw->visible = 1;
   if ((cw->w < 0) || (cw->h < 0))
     CRI("ACK!");

   //INF("SMART SHOW: %p EC(%dx%d) CW(%dx%d)", cw->ec, cw->ec->w, cw->ec->h, cw->w, cw->h);

   EINA_LIST_FOREACH(cw->ec->e.state.video_child, l, tmp)
     evas_object_show(tmp->frame);

   evas_object_show(cw->clip);
   if (cw->input_obj) evas_object_show(cw->input_obj);
   evas_object_show(cw->effect_obj);
   e_comp_shape_queue(cw->comp);
   if (cw->ec->input_only) return;
   if (cw->ec->iconic && (!cw->ec->new_client))
     e_comp_object_signal_emit(cw->smart_obj, "e,action,uniconify", "e");
   else
     {
        e_comp_object_signal_emit(cw->smart_obj, "e,state,visible", "e");
        cw->comp->animating++;
        cw->animating++;
        e_object_ref(E_OBJECT(cw->ec));
     }
   /* ensure some random effect doesn't lock the client offscreen */
   if (!cw->animating)
     e_comp_object_effect_set(obj, NULL);
}

static void
_e_comp_object_cb_mirror_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   cw->obj_mirror = eina_list_remove(cw->obj_mirror, obj);
}

static void
_e_comp_smart_del(Evas_Object *obj)
{
   Eina_List *l;

   INTERNAL_ENTRY;

   if (cw->animating)
     {
        cw->comp->animating--;
     }
   cw->animating = 0;
   E_FREE_FUNC(cw->updates, eina_tiler_free);
   E_FREE_FUNC(cw->pending_updates, eina_tiler_free);
   DBG("  [%p] del", cw->ec);
   e_comp_object_render_update_del(cw->smart_obj);

   if (cw->obj_mirror)
     {
        Evas_Object *o;

        EINA_LIST_FREE(cw->obj_mirror, o)
          {
             evas_object_image_data_set(o, NULL);
             evas_object_freeze_events_set(o, 1);
             evas_object_event_callback_del_full(o, EVAS_CALLBACK_DEL, _e_comp_object_cb_mirror_del, cw);
             evas_object_del(o);
          }
     }
   _e_comp_object_layers_remove(cw);
   l = evas_object_data_get(obj, "comp_object-to_del");
   E_FREE_LIST(l, evas_object_del);
   evas_object_del(cw->clip);
   evas_object_del(cw->effect_obj);
   evas_object_del(cw->shobj);
   evas_object_del(cw->frame_icon);
   evas_object_del(cw->frame_object);
   evas_object_del(cw->zoomobj);
   evas_object_del(cw->input_obj);
   evas_object_del(cw->obj);
   e_comp_shape_queue(cw->comp);
   eina_stringshare_del(cw->frame_theme);
   eina_stringshare_del(cw->frame_name);
   free(cw);
}

static void
_e_comp_smart_move(Evas_Object *obj, int x, int y)
{
   INTERNAL_ENTRY;

   cw->x = x, cw->y = y;
   evas_object_move(cw->clip, 0, 0);
   evas_object_move(cw->effect_obj, x, y);
   if (cw->input_obj)
     evas_object_geometry_set(cw->input_obj, cw->x + cw->input_rect.x, cw->y + cw->input_rect.y, cw->input_rect.w, cw->input_rect.h);
   /* this gets called once during setup to init coords offscreen and guarantee first move */
   if (cw->comp)
     e_comp_shape_queue(cw->comp);
}

static void
_e_comp_smart_resize(Evas_Object *obj, int w, int h)
{
   Eina_Bool first = EINA_FALSE;

   INTERNAL_ENTRY;

   //INF("RSZ(%p): %dx%d -> %dx%d", cw->ec, cw->w, cw->h, w, h);
   if (!cw->effect_obj) CRI("ACK!");
   first = ((cw->w < 1) || (cw->h < 1));
   cw->w = w, cw->h = h;
   evas_object_resize(cw->clip, cw->comp->man->w, cw->comp->man->h);
   if ((!cw->ec->shading) && (!cw->ec->shaded))
     {
        int ww, hh, pw, ph;
        e_comp_object_frame_wh_unadjust(obj, w, h, &ww, &hh);
        /* verify pixmap:object size */
        if (e_pixmap_size_get(cw->ec->pixmap, &pw, &ph) && (!cw->ec->override))
          {
             //INF("CW RSZ: %dx%d PIX(%dx%d)", w, h, pw, ph);
             //if (cw->obj)
               //{
                  //evas_object_size_hint_max_set(cw->obj, pw, ph);
                  //evas_object_size_hint_min_set(cw->obj, pw, ph);
               //}
             if ((ww != pw) || (hh != ph))
               CRI("CW RSZ: %dx%d || PX: %dx%d", ww, hh, pw, ph);
          }
        evas_object_resize(cw->effect_obj, w, h);
        if (cw->zoomobj) e_zoomap_child_resize(cw->zoomobj, pw, ph);
        if (cw->input_obj)
          evas_object_geometry_set(cw->input_obj, cw->x + cw->input_rect.x, cw->y + cw->input_rect.y, cw->input_rect.w, cw->input_rect.h);
        /* resize render update tiler */
        if (!first)
          {
             RENDER_DEBUG("DAMAGE UNFULL: %p", cw->ec);
             cw->updates_full = 0;
             if (cw->updates) eina_tiler_clear(cw->updates);
          }
        else
          {
             RENDER_DEBUG("DAMAGE RESIZE(%p): %dx%d", cw->ec, cw->ec->client.w, cw->ec->client.h);
             if (cw->updates) eina_tiler_area_size_set(cw->updates, cw->ec->client.w, cw->ec->client.h);
          }
     }
   else
     {
        evas_object_resize(cw->effect_obj, w, h);
     }
   e_comp_shape_queue(cw->comp);
}

static void
_e_comp_smart_init(void)
{
   if (_e_comp_smart) return;
   {
      static const Evas_Smart_Class sc =
      {
         SMART_NAME,
         EVAS_SMART_CLASS_VERSION,
         _e_comp_smart_add,
         _e_comp_smart_del,
         _e_comp_smart_move,
         _e_comp_smart_resize,
         _e_comp_smart_show,
         _e_comp_smart_hide,
         _e_comp_smart_color_set,
         _e_comp_smart_clip_set,
         _e_comp_smart_clip_unset,
         NULL,
         NULL,
         NULL,

         NULL,
         NULL,
         NULL,
         NULL
      };
      _e_comp_smart = evas_smart_class_new(&sc);
   }
}

EAPI void
e_comp_object_zoomap_set(Evas_Object *obj, Eina_Bool enabled)
{
   API_ENTRY;

   enabled = !enabled;
   if (cw->zoomap_disabled == enabled) return;
   if (enabled)
     {
        cw->zoomobj = e_zoomap_add(cw->comp->evas);
        e_zoomap_smooth_set(cw->zoomobj, e_comp_config_get()->smooth_windows);
        e_zoomap_child_set(cw->zoomobj, cw->ec ? cw->frame_object : cw->obj);
        edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->zoomobj);
        e_zoomap_child_edje_solid_setup(cw->zoomobj);
        if (cw->ec->override)
          evas_object_name_set(cw->zoomobj, "cw->zoomobj::WINDOW");
        else if (!cw->ec->input_only)
          evas_object_name_set(cw->zoomobj, "cw->zoomobj::CLIENT");
     }
   else
     {
        edje_object_part_unswallow(cw->shobj, cw->zoomobj);
        E_FREE_FUNC(cw->zoomobj, evas_object_del);
        edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->ec ? cw->frame_object : cw->obj);
     }
   cw->zoomap_disabled = enabled;
}

/////////////////////////////////////////////////////////

static void
_e_comp_object_util_del(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Eina_List *l;
   Eina_Bool comp_object;

   comp_object = !!evas_object_data_get(obj, "comp_object");
   if (comp_object)
     {
        Evas_Object *o;

        o = edje_object_part_swallow_get(obj, "e.swallow.content");
        evas_object_del(o);
        e_comp_shape_queue(e_comp_util_evas_object_comp_get(obj));
     }
   l = evas_object_data_get(obj, "comp_object-to_del");
   E_FREE_LIST(l, evas_object_del);
}

static void
_e_comp_object_util_restack(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   if (e_comp_util_object_is_above_nocomp(obj) &&
       (!evas_object_data_get(obj, "comp_override")))
     {
        evas_object_data_set(obj, "comp_override", (void*)1);
        e_comp_override_add(e_comp_util_evas_object_comp_get(obj));
     }
}

static void
_e_comp_object_util_show(void *data EINA_UNUSED, Evas_Object *obj)
{
   Eina_Bool ref = EINA_TRUE;
   if (evas_object_visible_get(obj))
     {
        void *d;

        d = evas_object_data_del(obj, "comp_hiding");
        if (d)
          /* currently trying to hide */
          ref = EINA_FALSE;
        else
          /* already visible */
          return;
     }
   else
     e_comp_shape_queue(e_comp_util_evas_object_comp_get(obj));
   
   evas_object_show(obj);
   if (ref)
     {
        evas_object_ref(obj);
        evas_object_data_set(obj, "comp_ref", (void*)1);
     }
   edje_object_signal_emit(obj, "e,state,visible", "e");
   evas_object_data_set(obj, "comp_showing", (void*)1);
   if (e_comp_util_object_is_above_nocomp(obj))
     {
        evas_object_data_set(obj, "comp_override", (void*)1);
        e_comp_override_add(e_comp_util_evas_object_comp_get(obj));
     }
}

static void
_e_comp_object_util_hide(void *data EINA_UNUSED, Evas_Object *obj)
{
   if (!evas_object_visible_get(obj)) return;
   /* already hiding */
   if (evas_object_data_get(obj, "comp_hiding")) return;
   if (!evas_object_data_del(obj, "comp_showing"))
     {
        evas_object_ref(obj);
        evas_object_data_set(obj, "comp_ref", (void*)1);
     }
   edje_object_signal_emit(obj, "e,state,hidden", "e");
   evas_object_data_set(obj, "comp_hiding", (void*)1);

   if (evas_object_data_del(obj, "comp_override"))
     e_comp_override_timed_pop(e_comp_util_evas_object_comp_get(obj));
}

static void
_e_comp_object_util_done_defer(void *data, Evas_Object *obj, const char *emission, const char *source EINA_UNUSED)
{
   if (!e_util_strcmp(emission, "e,action,hide,done"))
     {
        if (!evas_object_data_del(obj, "comp_hiding")) return;
        evas_object_intercept_hide_callback_del(obj, _e_comp_object_util_hide);
        evas_object_hide(obj);
        e_comp_shape_queue(e_comp_util_evas_object_comp_get(obj));
        evas_object_intercept_hide_callback_add(obj, _e_comp_object_util_hide, data);
     }
   else
     evas_object_data_del(obj, "comp_showing");
   if (evas_object_data_del(obj, "comp_ref"))
     evas_object_unref(obj);
}

static void
_e_comp_object_util_moveresize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   if (data)
     {
        int w, h;

        evas_object_geometry_get(obj, NULL, NULL, &w, &h);
        e_zoomap_child_resize(data, w, h);
     }
     
   if (evas_object_visible_get(obj))
     e_comp_shape_queue(e_comp_util_evas_object_comp_get(obj));
}

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
EAPI E_Comp_Object_Intercept_Hook *
e_comp_object_intercept_hook_add(E_Comp_Object_Intercept_Hook_Point hookpoint, E_Comp_Object_Intercept_Hook_Cb func, const void *data)
{
   E_Comp_Object_Intercept_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_COMP_OBJECT_INTERCEPT_HOOK_LAST, NULL);
   ch = E_NEW(E_Comp_Object_Intercept_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_comp_object_intercept_hooks[hookpoint] = eina_inlist_append(_e_comp_object_intercept_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

EAPI void
e_comp_object_intercept_hook_del(E_Comp_Object_Intercept_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_comp_object_intercept_hooks_walking == 0)
     {
        _e_comp_object_intercept_hooks[ch->hookpoint] = eina_inlist_remove(_e_comp_object_intercept_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_comp_object_intercept_hooks_delete++;
}
#endif

EAPI Evas_Object *
e_comp_object_util_add(Evas_Object *obj, E_Comp_Object_Type type)
{
   Evas_Object *o, *z = NULL;
   E_Comp *c;
   const char *name;
   Eina_List *l, *list = NULL;
   E_Comp_Config *conf = e_comp_config_get();
   Eina_Bool skip = EINA_FALSE, fast = EINA_FALSE, shadow = EINA_FALSE;
   E_Comp_Match *m;
   char buf[1024];
   int ok = 0;
   int x, y, w, h;
   Eina_Bool vis;

   EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);

   switch (type)
     {
      case E_COMP_OBJECT_TYPE_MENU:
        list = conf->match.menus;
        skip = conf->match.disable_menus;
        fast = conf->fast_menus;
        shadow = EINA_TRUE;
        break;
      case E_COMP_OBJECT_TYPE_POPUP:
        list = conf->match.popups;
        skip = conf->match.disable_popups;
        fast = conf->fast_popups;
        break;
      default:
        list = conf->match.objects;
        skip = conf->match.disable_objects;
        fast = conf->fast_objects;
     }
   name = evas_object_name_get(obj);
   vis = evas_object_visible_get(obj);
   c = e_comp_util_evas_object_comp_get(obj);
   o = edje_object_add(c->evas);
   evas_object_data_set(o, "comp_object", (void*)1);
   if (name && (!skip))
     skip = (!strncmp(name, "noshadow", 8));
   if (skip)
     evas_object_data_set(o, "comp_object_skip", (void*)1);
   else if (list)
     {
        EINA_LIST_FOREACH(list, l, m)
          {
             if (((m->name) && (!name)) ||
                 ((name) && (m->name) && (!e_util_glob_match(name, m->name))))
               continue;
             if (!m->shadow_style) continue;
             if (fast)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s/fast", m->shadow_style);
                  ok = e_theme_edje_object_set(o, "base/theme/comp", buf);
               }
             if (!ok)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s", m->shadow_style);
                  ok = e_theme_edje_object_set(o, "base/theme/comp", buf);
               }
             if (ok)
               {
                  shadow = !m->no_shadow;
                  break;
               }
          }
     }
   else
     skip = EINA_TRUE;
   while (!ok)
     {
        if (skip)
          ok = e_theme_edje_object_set(o, "base/theme/comp", "e/comp/frame/none");
        if (ok) break;
        if (conf->shadow_style)
          {
             if (fast)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s/fast", conf->shadow_style);
                  ok = e_theme_edje_object_set(o, "base/theme/comp", buf);
               }
             if (!ok)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s", conf->shadow_style);
                  ok = e_theme_edje_object_set(o, "base/theme/comp", buf);
               }
             if (ok) break;
          }
        if (fast)
          ok = e_theme_edje_object_set(o, "base/theme/comp", "e/comp/frame/default/fast");
        if (!ok)
          ok = e_theme_edje_object_set(o, "base/theme/comp", "e/comp/frame/default");
        break;
     }
   if (shadow)
     edje_object_signal_emit(o, "e,state,shadow,on", "e");
   else
     edje_object_signal_emit(o, "e,state,shadow,off", "e");

   evas_object_geometry_get(obj, &x, &y, &w, &h);
   evas_object_geometry_set(o, x, y, w, h);
   evas_object_pass_events_set(o, evas_object_pass_events_get(obj));

   if (list)
     {
        z = e_zoomap_add(c->evas);
        e_zoomap_child_edje_solid_setup(z);
        e_zoomap_smooth_set(z, conf->smooth_windows);
        e_zoomap_child_set(z, obj);
        e_zoomap_child_resize(z, w, h);
        evas_object_show(z);
     }
   edje_object_signal_callback_add(o, "e,action,*,done", "e", _e_comp_object_util_done_defer, z);

   evas_object_intercept_show_callback_add(o, _e_comp_object_util_show, z);
   evas_object_intercept_hide_callback_add(o, _e_comp_object_util_hide, z);
   evas_object_event_callback_add(o, EVAS_CALLBACK_MOVE, _e_comp_object_util_moveresize, z);
   evas_object_event_callback_add(o, EVAS_CALLBACK_DEL, _e_comp_object_util_del, z);
   evas_object_event_callback_add(o, EVAS_CALLBACK_RESIZE, _e_comp_object_util_moveresize, z);
   evas_object_event_callback_add(o, EVAS_CALLBACK_RESTACK, _e_comp_object_util_restack, z);

   e_comp_object_signal_emit(o, "e,state,hidden", "e");

   edje_object_part_swallow(o, "e.swallow.content", z ?: obj);

   _e_comp_object_event_add(o);

   if (vis)
     evas_object_show(o);

   return o;
}

/* utility functions for deleting objects when their "owner" is deleted */
EAPI void
e_comp_object_util_del_list_append(Evas_Object *obj, Evas_Object *to_del)
{
   Eina_List *l;

   SOFT_ENTRY();
   EINA_SAFETY_ON_NULL_RETURN(to_del);
   l = evas_object_data_get(obj, "comp_object-to_del");
   evas_object_data_set(obj, "comp_object-to_del", eina_list_append(l, to_del));
   evas_object_event_callback_del(obj, EVAS_CALLBACK_DEL, _e_comp_object_util_del);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _e_comp_object_util_del, NULL);
}

EAPI void
e_comp_object_util_del_list_remove(Evas_Object *obj, Evas_Object *to_del)
{
   Eina_List *l;

   SOFT_ENTRY();
   EINA_SAFETY_ON_NULL_RETURN(to_del);
   l = evas_object_data_get(obj, "comp_object-to_del");
   if (l)
     evas_object_data_set(obj, "comp_object-to_del", eina_list_remove(l, to_del));
}

/////////////////////////////////////////////////////////

EAPI Evas_Object *
e_comp_object_client_add(E_Client *ec)
{
   Evas_Object *o;
   E_Comp_Object *cw;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);
   if (ec->frame) return NULL;
   _e_comp_smart_init();
   o = evas_object_smart_add(ec->comp->evas, _e_comp_smart);
   cw = evas_object_smart_data_get(o);
   cw->comp = ec->comp;
   evas_object_data_set(o, "E_Client", ec);
   cw->ec = ec;
   ec->frame = o;
   evas_object_data_set(o, "comp_object", (void*)1);

   e_object_ref(E_OBJECT(ec));
   _e_comp_object_event_add(o);

   return o;
}

/* utility functions for getting client inset */
EAPI void
e_comp_object_frame_xy_adjust(Evas_Object *obj, int x, int y, int *ax, int *ay)
{
   API_ENTRY;
   if (!cw->client_inset.calc)
     {
        if (ax) *ax = x;
        if (ay) *ay = y;
        return;
     }
   if (ax) *ax = x - cw->client_inset.l;
   if (ay) *ay = y - cw->client_inset.t;
}

EAPI void
e_comp_object_frame_xy_unadjust(Evas_Object *obj, int x, int y, int *ax, int *ay)
{
   API_ENTRY;
   if (!cw->client_inset.calc)
     {
        if (ax) *ax = x;
        if (ay) *ay = y;
        return;
     }
   if (ax) *ax = x + cw->client_inset.l;
   if (ay) *ay = y + cw->client_inset.t;
}

EAPI void
e_comp_object_frame_wh_adjust(Evas_Object *obj, int w, int h, int *aw, int *ah)
{
   API_ENTRY;
   if (!cw->client_inset.calc)
     {
        if (aw) *aw = w;
        if (ah) *ah = h;
        return;
     }
   if (aw) *aw = w + cw->client_inset.l + cw->client_inset.r;
   if (ah) *ah = h + cw->client_inset.t + cw->client_inset.b;
}

EAPI void
e_comp_object_frame_wh_unadjust(Evas_Object *obj, int w, int h, int *aw, int *ah)
{
   API_ENTRY;
   if (!cw->client_inset.calc)
     {
        if (aw) *aw = w;
        if (ah) *ah = h;
        return;
     }
   if (aw) *aw = w - cw->client_inset.l - cw->client_inset.r;
   if (ah) *ah = h - cw->client_inset.t - cw->client_inset.b;
}

EAPI E_Client *
e_comp_object_client_get(Evas_Object *obj)
{
   Evas_Object *o;

   SOFT_ENTRY(NULL);
   /* FIXME: remove this when eo is used */
   o = evas_object_data_get(obj, "comp_smart_obj");
   if (o)
     return e_comp_object_client_get(o);
   return cw ? cw->ec : NULL;
}

EAPI void
e_comp_object_frame_extends_get(Evas_Object *obj, int *x, int *y, int *w, int *h)
{
   API_ENTRY;
   if (cw->frame_extends)
     edje_object_parts_extends_calc(cw->frame_object, x, y, w, h);
   else
     {
        if (x) *x = 0;
        if (y) *y = 0;
        if (w) *w  = cw->ec->w;
        if (h) *h  = cw->ec->h;
     }
}

EAPI E_Zone *
e_comp_object_util_zone_get(Evas_Object *obj)
{
   E_Zone *zone;

   SOFT_ENTRY(NULL);
   if (cw)
     zone = cw->ec->zone;
   else
     {
        int x, y;
        E_Comp *c;

        c = e_comp_util_evas_object_comp_get(obj);
        if (c)
          {
             evas_object_geometry_get(obj, &x, &y, NULL, NULL);
             zone = e_comp_zone_xy_get(c, x, y);
          }
        else
          {
             Ecore_Evas *ee;

             ee = ecore_evas_ecore_evas_get(evas_object_evas_get(obj));
             if (ee)
               {
                  E_Win *win;

                  win = ecore_evas_data_get(ee, "E_Win");
                  if (win)
                    {
                       if (win->client)
                         zone = win->client->zone;
                       else
                         zone = e_comp_zone_xy_get(NULL, win->x, win->y);
                    }
                  else
                    {
                       ecore_evas_geometry_get(ee, &x, &y, NULL, NULL);
                       zone = e_comp_zone_xy_get(NULL, x, y);
                    }
               }
             else
               {
                  evas_object_geometry_get(obj, &x, &y, NULL, NULL);
                  zone = e_comp_zone_xy_get(NULL, x, y);
               }
          }
     }
   return zone;
}

EAPI void
e_comp_object_util_center(Evas_Object *obj)
{
   int x, y, w, h, ow, oh;
   E_Zone *zone;

   SOFT_ENTRY();

   zone = e_comp_object_util_zone_get(obj);
   EINA_SAFETY_ON_NULL_RETURN(zone);
   e_zone_useful_geometry_get(zone, &x, &y, &w, &h);
   if (cw && cw->ec->changes.size)
     ow = cw->ec->w, oh = cw->ec->h;
   else
     evas_object_geometry_get(obj, NULL, NULL, &ow, &oh);
   x = x + (w - ow) / 2;
   y = y + (h - oh) / 2;
   evas_object_move(obj, x, y);
}

EAPI void
e_comp_object_util_center_on(Evas_Object *obj, Evas_Object *on)
{
   int x, y, w, h, ow, oh;

   SOFT_ENTRY();
   EINA_SAFETY_ON_NULL_RETURN(on);
   evas_object_geometry_get(on, &x, &y, &w, &h);
   if (cw && cw->ec->changes.size)
     ow = cw->ec->w, oh = cw->ec->h;
   else
     evas_object_geometry_get(obj, NULL, NULL, &ow, &oh);
   evas_object_move(obj, x + (w / 2) - (ow / 2), y + (h / 2) - (oh / 2));
}

EAPI void
e_comp_object_util_fullscreen(Evas_Object *obj)
{
   SOFT_ENTRY();

   if (cw)
     e_client_fullscreen(cw->ec, E_FULLSCREEN_RESIZE);
   else
     {
        E_Comp *c = e_comp_util_evas_object_comp_get(obj);

        EINA_SAFETY_ON_NULL_RETURN(c);
        evas_object_move(obj, 0, 0);
        evas_object_resize(obj, c->man->w, c->man->h);
     }
}

EAPI void
e_comp_object_util_center_pos_get(Evas_Object *obj, int *x, int *y)
{
   E_Zone *zone;
   int zx, zy, zw, zh;
   SOFT_ENTRY();

   zone = e_comp_object_util_zone_get(obj);
   e_zone_useful_geometry_get(zone, &zx, &zy, &zw, &zh);
   if (x) *x = zx + (zw - cw->w) / 2;
   if (y) *y = zy + (zh - cw->h) / 2;
}

EAPI void
e_comp_object_input_area_set(Evas_Object *obj, int x, int y, int w, int h)
{
   API_ENTRY;

   //INF("%d,%d %dx%d", x, y, w, h);
   E_RECTS_CLIP_TO_RECT(x, y, w, h, 0, 0, cw->ec->w, cw->ec->h);
   if ((cw->input_rect.x == x) && (cw->input_rect.y == y) &&
       (cw->input_rect.w == w) && (cw->input_rect.h == h)) return;
   EINA_RECTANGLE_SET(&cw->input_rect, x, y, w, h);
   if (x || y || (w != cw->w) || (h != cw->h))
     {
        if (!cw->input_obj)
          {
             cw->input_obj = evas_object_rectangle_add(cw->comp->evas);
             //e_util_size_debug_set(cw->input_obj, 1);
             evas_object_name_set(cw->input_obj, "cw->input_obj");
             evas_object_color_set(cw->input_obj, 0, 0, 0, 0);
             evas_object_pass_events_set(cw->obj, 1);
             evas_object_clip_set(cw->input_obj, cw->clip);
             evas_object_smart_member_add(cw->input_obj, obj);
          }
        evas_object_geometry_set(cw->input_obj, MAX(cw->x, 0) + x, MAX(cw->y, 0) + y, w, h);
        evas_object_layer_set(cw->input_obj, 9999);
        if (cw->visible) evas_object_show(cw->input_obj);
     }
   else
     {
        evas_object_smart_member_del(cw->input_obj);
        E_FREE_FUNC(cw->input_obj, evas_object_del);
        evas_object_pass_events_set(cw->obj, 0);
     }
}

EAPI void
e_comp_object_frame_geometry_get(Evas_Object *obj, int *l, int *r, int *t, int *b)
{
   API_ENTRY;
   if (l) *l = cw->client_inset.l;
   if (r) *r = cw->client_inset.r;
   if (t) *t = cw->client_inset.t;
   if (b) *b = cw->client_inset.b;
}

EAPI void
e_comp_object_frame_icon_geometry_get(Evas_Object *obj, int *x, int *y, int *w, int *h)
{
   API_ENTRY;

   if (x) *x = 0;
   if (y) *y = 0;
   if (w) *w = 0;
   if (h) *h = 0;
   if (!cw->frame_icon) return;
   evas_object_geometry_get(cw->frame_icon, x, y, w, h);
}

EAPI Eina_Bool
e_comp_object_frame_title_set(Evas_Object *obj, const char *name)
{
   API_ENTRY EINA_FALSE;
   if (!e_util_strcmp(cw->frame_name, name)) return EINA_FALSE;
   eina_stringshare_replace(&cw->frame_name, name);
   if (cw->frame_object)
     edje_object_part_text_set(cw->frame_object, "e.text.title", cw->frame_name);
   return EINA_TRUE;
}

EAPI Eina_Bool
e_comp_object_frame_exists(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;
   return !!cw->frame_object;
}

EAPI void
e_comp_object_frame_icon_update(Evas_Object *obj)
{
   API_ENTRY;

   E_FREE_FUNC(cw->frame_icon, evas_object_del);
   if (!edje_object_part_exists(cw->frame_object, "e.swallow.icon"))
     return;
   cw->frame_icon = e_client_icon_add(cw->ec, cw->comp->evas);
   if (!cw->frame_icon) return;
   if (!edje_object_part_swallow(cw->frame_object, "e.swallow.icon", cw->frame_icon))
     E_FREE_FUNC(cw->frame_icon, evas_object_del);
}

EAPI Eina_Bool
e_comp_object_frame_theme_set(Evas_Object *obj, const char *name)
{
   Evas_Object *o, *pbg;
   char buf[4096];
   int ok;
   Eina_Stringshare *theme;

   API_ENTRY EINA_FALSE;

   if (!e_util_strcmp(cw->frame_theme, name))
    return edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->frame_object ?: cw->obj);
   if (!e_util_strcmp(name, "COMP_RESHADOW"))
     return _e_comp_object_shadow_setup(cw);
   pbg = cw->frame_object;
   theme = eina_stringshare_add(name);

   if (cw->frame_object)
     {
        int w, h;

        w = cw->ec->w, h = cw->ec->h;
        e_comp_object_frame_wh_unadjust(obj, w, h, &cw->ec->w, &cw->ec->h);
        if ((cw->ec->w != w) || (cw->ec->h != h))
          {
             cw->ec->changes.size = 1;
             EC_CHANGED(cw->ec);
          }
        E_FREE_FUNC(cw->frame_object, evas_object_del);
        if (!name) goto reshadow;
     }
   o = edje_object_add(cw->comp->evas);
   snprintf(buf, sizeof(buf), "e/widgets/border/%s/border", name);
   ok = e_theme_edje_object_set(o, "base/theme/border", buf);
   if ((!ok) && (!e_util_strcmp(name, "borderless")))
     {
        cw->frame_object = NULL;
        E_FREE_FUNC(cw->frame_icon, evas_object_del);
        evas_object_del(o);
        eina_stringshare_del(cw->frame_theme);
        cw->frame_theme = theme;
        goto reshadow;
     }
   if (!ok)
     {
        if (theme != e_config->theme_default_border_style)
          {
             snprintf(buf, sizeof(buf), "e/widgets/border/%s/border", e_config->theme_default_border_style);
             ok = e_theme_edje_object_set(o, "base/theme/border", buf);
          }
        if (!ok)
          {
             ok = e_theme_edje_object_set(o, "base/theme/border",
                                          "e/widgets/border/default/border");
             if (ok && (theme == e_config->theme_default_border_style))
               {
                  /* Reset default border style to default */
                  eina_stringshare_replace(&e_config->theme_default_border_style, "default");
                  e_config_save_queue();
               }
          }
     }

   if (ok)
     {
        cw->frame_object = o;
        eina_stringshare_del(cw->frame_theme);
        cw->frame_theme = theme;
        evas_object_name_set(o, "cw->frame_object");

        if (cw->frame_name)
          edje_object_part_text_set(o, "e.text.title", cw->frame_name);

        if (pbg)
          {
             if (cw->frame_icon)
               {
                  if (!edje_object_part_swallow(cw->frame_object, "e.swallow.icon", cw->frame_icon))
                    E_FREE_FUNC(cw->frame_icon, evas_object_del);
               }
          }
        else
          {
             cw->ec->changes.icon = 1;
             EC_CHANGED(cw->ec);
          }
     }
   else
     {
        CRI("USER IS USING A SHITTY THEME! ABORT!!!!");
        evas_object_del(o);
        E_FREE_FUNC(cw->frame_icon, evas_object_del);
     }
reshadow:
   if (cw->shobj)
     _e_comp_object_shadow_setup(cw);
   do
     {
        _e_comp_smart_cb_frame_recalc(cw, cw->smart_obj, NULL);
        /* this guarantees that we won't get blocked by the NOP check in the interceptor */
        cw->y = cw->x = -99999;
        if (pbg)
          evas_object_move(obj, cw->ec->x, cw->ec->y);
        else if (cw->ec->placed || (!cw->ec->new_client))
          {
             /* if no previous frame:
              * - reapply client_inset
              * - clamp to zone
              */
             int x, y;

             if (cw->ec->changes.size)
               {
                  x = cw->ec->x;
                  y = cw->ec->y;
               }
             else
               {
                  x = cw->ec->client.x, y = cw->ec->client.y;
                  x = MAX(cw->ec->zone->x, cw->ec->client.x - cw->client_inset.l);
                  y = MAX(cw->ec->zone->y, cw->ec->client.y - cw->client_inset.t);
               }
             evas_object_move(obj, x, y);
          }
     } while (0);

   if (cw->ec->maximized)
     {
        cw->ec->changes.need_maximize = 1;
        EC_CHANGED(cw->ec);
     }
   evas_object_smart_callback_call(cw->smart_obj, "frame_changed", NULL);
   if (cw->frame_object)
     {
        cw->frame_extends = !!edje_object_data_get(cw->frame_object, "frame_extends");
        edje_object_signal_callback_add(cw->frame_object, "*", "*",
                                        _e_comp_object_cb_signal_bind, cw);
     }
   else
     cw->frame_extends = 0;
   evas_object_del(pbg);
   return EINA_TRUE;
}

EAPI void
e_comp_object_signal_emit(Evas_Object *obj, const char *sig, const char *src)
{
   E_Comp_Object_Mover *prov;

   API_ENTRY;
   //INF("EMIT %p: %s %s", cw->ec, sig, src);
   edje_object_signal_emit(cw->shobj, sig, src);
   if (cw->frame_object) edje_object_signal_emit(cw->frame_object, sig, src);
   if (cw->frame_icon && e_icon_edje_get(cw->frame_icon))
     edje_object_signal_emit(e_icon_edje_get(cw->frame_icon), sig, src);
   if ((cw->ec->override && e_comp_config_get()->match.disable_overrides) ||
       ((!cw->ec->override) && e_comp_config_get()->match.disable_borders))
     return;
   /* start with highest priority callback first */
   EINA_INLIST_REVERSE_FOREACH(_e_comp_object_movers, prov)
     {
        if (!e_util_glob_match(sig, prov->sig)) continue;
        if (prov->func(prov->data, obj, sig)) break;
     }
}

EAPI void
e_comp_object_signal_callback_add(Evas_Object *obj, const char *sig, const char *src, Edje_Signal_Cb cb, const void *data)
{
   /* FIXME: at some point I guess this should use eo to inherit
    * -EDJE_OBJ_SUB_ID_SIGNAL_CALLBACK_ADD
    * -EDJE_OBJ_SUB_ID_SIGNAL_CALLBACK_DEL
    */
   API_ENTRY;
   edje_object_signal_callback_add(cw->shobj, sig, src, cb, (void*)data);
}

EAPI void
e_comp_object_signal_callback_del(Evas_Object *obj, const char *sig, const char *src, Edje_Signal_Cb cb)
{
   API_ENTRY;
   edje_object_signal_callback_del(cw->shobj, sig, src, cb);
}

EAPI void
e_comp_object_signal_callback_del_full(Evas_Object *obj, const char *sig, const char *src, Edje_Signal_Cb cb, const void *data)
{
   API_ENTRY;
   edje_object_signal_callback_del_full(cw->shobj, sig, src, cb, (void*)data);
}

EAPI void
e_comp_object_damage(Evas_Object *obj, int x, int y, int w, int h)
{
   int tw, th;
   API_ENTRY;

   if (cw->ec->input_only || (!cw->updates)) return;
   if (cw->nocomp) return;
   if (cw->comp->nocomp)
     {
        cw->nocomp_need_update = EINA_TRUE;
        return;
     }
   /* ignore overdraw */
   if (cw->updates_full)
     {
        RENDER_DEBUG("IGNORED %p: %d,%d %dx%d", cw->ec, x, y, w, h);
        e_comp_object_render_update_add(obj);

        if ((cw->ec->visible) && (!evas_object_visible_get(cw->smart_obj)))
          evas_object_show(cw->smart_obj);

        return;
     }
   /* clip rect to client surface */
   RENDER_DEBUG("DAMAGE(%d,%d %dx%d) CLIP(%dx%d)", x, y, w, h, cw->ec->client.w, cw->ec->client.h);
   E_RECTS_CLIP_TO_RECT(x, y, w, h, 0, 0, cw->ec->client.w, cw->ec->client.h);
   /* if rect is the total size of the client after clip, clear the updates
    * since this is guaranteed to be the whole region anyway
    */
   eina_tiler_area_size_get(cw->updates, &tw, &th);
   if ((w > tw) || (h > th))
     {
        RENDER_DEBUG("DAMAGE RESIZE %p: %dx%d", cw->ec, cw->ec->client.w, cw->ec->client.h);
        eina_tiler_clear(cw->updates);
        eina_tiler_area_size_set(cw->updates, cw->ec->client.w, cw->ec->client.h);
        x = 0, y = 0;
        tw = cw->ec->client.w, th = cw->ec->client.h;
     }
   if ((!x) && (!y) && (w == tw) && (h == th))
     {
        eina_tiler_clear(cw->updates);
        RENDER_DEBUG("DAMAGE FULL: %p", cw->ec);
        cw->updates_full = 1;
        cw->update_count = 0;
     }
   cw->update_count++;
   if (cw->update_count > UPDATE_MAX)
     {
        /* this is going to get really dumb, so just update the whole thing */
        eina_tiler_clear(cw->updates);
        cw->update_count = cw->updates_full = 1;
        eina_tiler_rect_add(cw->updates, &(Eina_Rectangle){0, 0, tw, th});
        RENDER_DEBUG("DAMAGE MAX: %dx%d", tw, th);
     }
   else
     {
        eina_tiler_rect_add(cw->updates, &(Eina_Rectangle){x, y, w, h});
        RENDER_DEBUG("DAMAGE: %d,%d %dx%d", x, y, w, h);
     }
   cw->updates_exist = 1;
   e_comp_object_render_update_add(obj);

   if ((cw->ec->visible) && (!evas_object_visible_get(cw->smart_obj)))
     evas_object_show(cw->smart_obj);
}

EAPI Eina_Bool
e_comp_object_damage_exists(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;
   return cw->updates_exist;
}

EAPI void
e_comp_object_render_update_add(Evas_Object *obj)
{
   API_ENTRY;

   if (cw->ec->input_only || (!cw->updates) || (!cw->redirected)) return;
   if (e_object_is_del(E_OBJECT(cw->ec)))
     CRI("CAN'T RENDER A DELETED CLIENT!");
   if (!e_pixmap_usable_get(cw->ec->pixmap)) return;
   //if (e_client_util_resizing_get(cw->ec) && (e_pixmap_type_get(cw->ec->pixmap) == E_PIXMAP_TYPE_WL))
     //INF("WL RENDER UPDATE");
   if (!cw->update)
     {
        cw->update = 1;
        cw->comp->updates = eina_list_append(cw->comp->updates, cw->ec);
     }
   e_comp_render_queue(cw->comp);
}

EAPI void
e_comp_object_render_update_del(Evas_Object *obj)
{
   API_ENTRY;

   if (cw->ec->input_only || (!cw->updates)) return;
   if (!cw->update) return;
   cw->update = 0;
   /* this gets called during comp animating to clear the update flag */
   if (cw->comp->grabbed) return;
   cw->comp->updates = eina_list_remove(cw->comp->updates, cw->ec);
   if (!cw->comp->updates)
     {
        E_FREE_FUNC(cw->comp->update_job, ecore_job_del);
        if (cw->comp->render_animator)
          ecore_animator_freeze(cw->comp->render_animator);
     }
}

EAPI void
e_comp_object_shape_apply(Evas_Object *obj)
{
   Eina_List *l;
   Evas_Object *o;
   unsigned int i, *pix, *p;
   int w, h, px, py;

   API_ENTRY;
   if (!cw->ec) return; //NYI
   if (!_e_comp_shaped_check(cw->ec->client.w, cw->ec->client.h, cw->ec->shape_rects, cw->ec->shape_rects_num))
     {
        if (!cw->ec->shaped) return;
     }
   if (cw->native)
     {
        ERR("BUGGER: shape with native surface? cw=%p", cw);
        return;
     }
   evas_object_image_size_get(cw->obj, &w, &h);
   if ((w < 1) || (h < 1)) return;

   //INF("SHAPE RENDER %p", cw->ec);

   if (cw->ec->shape_rects) evas_object_image_native_surface_set(cw->obj, NULL);
   evas_object_image_alpha_set(cw->obj, !!cw->ec->shape_rects);
   EINA_LIST_FOREACH(cw->obj_mirror, l, o)
     {
        if (cw->ec->shape_rects) evas_object_image_native_surface_set(o, NULL);
        evas_object_image_alpha_set(o, 1);
     }

   p = pix = evas_object_image_data_get(cw->obj, 1);
   if (!pix)
     {
        evas_object_image_data_set(cw->obj, pix);
        return;
     }
   if (cw->ec->shape_rects)
     {
        unsigned char *spix, *sp;

        spix = calloc(w * h, sizeof(unsigned char));
        DBG("SHAPE [%p] rects %i", cw->ec, cw->ec->shape_rects_num);
        for (i = 0; i < cw->ec->shape_rects_num; i++)
          {
             int rx, ry, rw, rh;

             rx = cw->ec->shape_rects[i].x; ry = cw->ec->shape_rects[i].y;
             rw = cw->ec->shape_rects[i].w; rh = cw->ec->shape_rects[i].h;
             E_RECTS_CLIP_TO_RECT(rx, ry, rw, rh, 0, 0, w, h);
             sp = spix + (w * ry) + rx;
             for (py = 0; py < rh; py++)
               {
                  for (px = 0; px < rw; px++)
                    {
                       *sp = 0xff; sp++;
                    }
                  sp += w - rw;
               }
          }
        sp = spix;
        for (py = 0; py < h; py++)
          {
             for (px = 0; px < w; px++)
               {
                  unsigned int mask, imask;

                  mask = ((unsigned int)(*sp)) << 24;
                  imask = mask >> 8;
                  imask |= imask >> 8;
                  imask |= imask >> 8;
                  *p = mask | (*p & imask);
                  //if (*sp) *p = 0xff000000 | *p;
                  //else *p = 0x00000000;
                  sp++;
                  p++;
               }
          }
        free(spix);
     }
   else
      {
         for (py = 0; py < h; py++)
           {
              for (px = 0; px < w; px++)
                *p |= 0xff000000;
           }
      }
   evas_object_image_data_set(cw->obj, pix);
   evas_object_image_data_update_add(cw->obj, 0, 0, w, h);
   EINA_LIST_FOREACH(cw->obj_mirror, l, o)
     {
        evas_object_image_data_set(o, pix);
        evas_object_image_data_update_add(o, 0, 0, w, h);
     }
// dont need to fix alpha chanel as blending
// should be totally off here regardless of
// alpha channel content
}

/* helper function to simplify toggling of redirection for display servers which support it */
EAPI void
e_comp_object_redirected_set(Evas_Object *obj, Eina_Bool set)
{
   API_ENTRY;

   set = !!set;
   if (cw->redirected == set) return;
   cw->redirected = set;
   if (set)
     {
        if (cw->updates_exist)
          e_comp_object_render_update_add(obj);
        else
          e_comp_object_damage(obj, 0, 0, cw->w, cw->h);
     }
   else
     {
        Eina_List *l;
        Evas_Object *o;

        if (cw->ec->pixmap)
          e_pixmap_clear(cw->ec->pixmap);
        if (cw->native)
          evas_object_image_native_surface_set(cw->obj, NULL);
        evas_object_image_size_set(cw->obj, 1, 1);
        evas_object_image_data_set(cw->obj, NULL);
        EINA_LIST_FOREACH(cw->obj_mirror, l, o)
          {
             evas_object_image_size_set(o, 1, 1);
             evas_object_image_data_set(o, NULL);
             if (cw->native)
               evas_object_image_native_surface_set(o, NULL);
          }
        cw->native = 0;
        e_comp_object_render_update_del(obj);
     }
}

EAPI void
e_comp_object_native_surface_set(Evas_Object *obj, Eina_Bool set)
{
   Evas_Native_Surface ns;
   Eina_List *l;
   Evas_Object *o;

   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(cw->ec);
   if (cw->ec->input_only) return;
   set = !!set;

   if (set)
     {
        /* native requires gl enabled, texture from pixmap enabled, and a non-shaped client */
        set = (cw->ec->comp->gl && e_comp_config_get()->texture_from_pixmap && (!cw->ec->shaped));
        if (set)
          set = e_pixmap_native_surface_init(cw->ec->pixmap, &ns);
     }
   cw->native = set;

   evas_object_image_native_surface_set(cw->obj, set ? &ns : NULL);
   EINA_LIST_FOREACH(cw->obj_mirror, l, o)
     evas_object_image_native_surface_set(o, set ? &ns : NULL);
}

/* mark an object as dirty and setup damages */
EAPI void
e_comp_object_dirty(Evas_Object *obj)
{
   Eina_Iterator *it;
   Eina_Rectangle *r;
   Eina_List *l;
   Evas_Object *o;
   int w, h;
   Eina_Bool dirty, visible;

   API_ENTRY;
   /* only actually dirty if pixmap is available */
   dirty = e_pixmap_size_get(cw->ec->pixmap, &w, &h);
   visible = cw->visible;
   if (!dirty) w = h = 1;
   evas_object_image_pixels_dirty_set(cw->obj, dirty);
   if (!dirty)
     evas_object_image_data_set(cw->obj, NULL);
   evas_object_image_size_set(cw->obj, w, h);
   RENDER_DEBUG("SIZE [%p]: %dx%d", cw->ec, w, h);
   if (cw->pending_updates)
     eina_tiler_area_size_set(cw->pending_updates, w, h);
   EINA_LIST_FOREACH(cw->obj_mirror, l, o)
     {
        evas_object_image_pixels_dirty_set(o, dirty);
        if (!dirty)
          evas_object_image_data_set(o, NULL);
        evas_object_image_size_set(o, w, h);
     }
   if (!dirty)
     {
        ERR("ERROR FETCHING PIXMAP FOR %p", cw->ec);
        return;
     }
   e_comp_object_native_surface_set(obj, e_comp_gl_get());
   it = eina_tiler_iterator_new(cw->updates);
   EINA_ITERATOR_FOREACH(it, r)
     {
        RENDER_DEBUG("UPDATE ADD [%p]: %d %d %dx%d", cw->ec, r->x, r->y, r->w, r->h);
        evas_object_image_data_update_add(cw->obj, r->x, r->y, r->w, r->h);
        EINA_LIST_FOREACH(cw->obj_mirror, l, o)
          {
             evas_object_image_data_update_add(o, r->x, r->y, r->w, r->h);
             visible |= evas_object_visible_get(o);
          }
        if (cw->pending_updates)
          eina_tiler_rect_add(cw->pending_updates, r);
     }
   eina_iterator_free(it);
   if (cw->pending_updates)
     eina_tiler_clear(cw->updates);
   else
     {
        cw->pending_updates = cw->updates;
        cw->updates = eina_tiler_new(w, h);
        eina_tiler_tile_size_set(cw->updates, 1, 1);
     }
   cw->update_count = cw->updates_full = cw->updates_exist = 0;
   if (cw->visible || (!visible) || (!cw->pending_updates)) return;
   /* force render if main object is hidden but mirrors are visible */
   e_comp_object_render(obj);
}

EAPI Eina_Bool
e_comp_object_render(Evas_Object *obj)
{
   Eina_Iterator *it;
   Eina_Rectangle *r;
   Eina_List *l;
   Evas_Object *o;
   int stride, pw, ph;
   unsigned int *pix, *srcpix;
   Eina_Bool ret = EINA_FALSE;

   API_ENTRY EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(cw->ec, EINA_FALSE);
   if (cw->ec->input_only) return EINA_TRUE;
   e_comp_object_render_update_del(obj);
   if (!e_pixmap_size_get(cw->ec->pixmap, &pw, &ph)) return EINA_FALSE;
   //if (e_pixmap_type_get(cw->ec->pixmap) == E_PIXMAP_TYPE_WL)
     //INF("WL RENDER!");

   if (!cw->pending_updates)
     {
        WRN("RENDER [%p]: NO RECTS!", cw->ec);
        evas_object_image_data_set(cw->obj, NULL);
        EINA_LIST_FOREACH(cw->obj_mirror, l, o)
          evas_object_image_data_set(o, NULL);
        return EINA_FALSE;
     }

   evas_object_image_pixels_dirty_set(cw->obj, EINA_FALSE);

   RENDER_DEBUG("RENDER SIZE: %dx%d", pw, ph);
   it = eina_tiler_iterator_new(cw->pending_updates);
   if (e_pixmap_image_is_argb(cw->ec->pixmap))
     {
        pix = e_pixmap_image_data_get(cw->ec->pixmap);
        EINA_ITERATOR_FOREACH(it, r)
          {
             E_RECTS_CLIP_TO_RECT(r->x, r->y, r->w, r->h, 0, 0, pw, ph);
             /* get pixmap data from rect region on display server into memory */
             ret = e_pixmap_image_draw(cw->ec->pixmap, r);
             if (!ret)
               {
                  WRN("UPDATE [%p]: %i %i %ix%i FAIL!!!!!!!!!!!!!!!!!", cw->ec, r->x, r->y, r->w, r->h);
                  e_comp_object_damage(obj, 0, 0, pw, ph);
                  break;
               }
             RENDER_DEBUG("UPDATE [%p] %i %i %ix%i", cw->ec, r->x, r->y, r->w, r->h);
          }
        /* set pixel data */
        evas_object_image_data_set(cw->obj, pix);
        EINA_LIST_FOREACH(cw->obj_mirror, l, o)
          {
             evas_object_image_data_set(o, pix);
             evas_object_image_pixels_dirty_set(o, EINA_FALSE);
          }
        eina_iterator_free(it);
        E_FREE_FUNC(cw->pending_updates, eina_tiler_free);
        return ret;
     }

   pix = evas_object_image_data_get(cw->obj, EINA_TRUE);
   stride = evas_object_image_stride_get(cw->obj);
   srcpix = e_pixmap_image_data_get(cw->ec->pixmap);
   EINA_ITERATOR_FOREACH(it, r)
     {
        E_RECTS_CLIP_TO_RECT(r->x, r->y, r->w, r->h, 0, 0, pw, ph);
        ret = e_pixmap_image_draw(cw->ec->pixmap, r);
        if (!ret)
          {
             WRN("UPDATE [%p]: %i %i %ix%i FAIL!!!!!!!!!!!!!!!!!", cw->ec, r->x, r->y, r->w, r->h);
             e_comp_object_damage(obj, 0, 0, pw, ph);
             break;
          }
        e_pixmap_image_data_argb_convert(cw->ec->pixmap, pix, srcpix, r, stride);
        RENDER_DEBUG("UPDATE [%p]: %d %d %dx%d -- pix = %p", cw->ec, r->x, r->y, r->w, r->h, pix);
     }
   evas_object_image_data_set(cw->obj, pix);
   EINA_LIST_FOREACH(cw->obj_mirror, l, o)
     {
        evas_object_image_data_set(o, pix);
        evas_object_image_pixels_dirty_set(o, EINA_FALSE);
     }

   eina_iterator_free(it);
   E_FREE_FUNC(cw->pending_updates, eina_tiler_free);
   return EINA_TRUE;
}

/* create a duplicate of an evas object */
EAPI Evas_Object *
e_comp_object_util_mirror_add(Evas_Object *obj)
{
   Evas_Object *o;
   int w, h;
   unsigned int *pix = NULL;
   Eina_Bool argb = EINA_FALSE;

   SOFT_ENTRY(NULL);

   if (!cw)
     cw = evas_object_data_get(obj, "comp_mirror");
   if (!cw)
     {
        o = evas_object_image_filled_add(evas_object_evas_get(obj));
        evas_object_image_colorspace_set(o, EVAS_COLORSPACE_ARGB8888);
        evas_object_image_smooth_scale_set(o, e_comp_config_get()->smooth_windows);
        evas_object_image_alpha_set(o, 1);
        evas_object_image_source_set(o, obj);
        return o;
     }
   if ((!cw->ec) || (!e_pixmap_size_get(cw->ec->pixmap, &w, &h))) return NULL;
   if ((!cw->native) && (!e_pixmap_image_exists(cw->ec->pixmap))) return NULL;
   o = evas_object_image_filled_add(evas_object_evas_get(obj));
   evas_object_image_colorspace_set(o, EVAS_COLORSPACE_ARGB8888);
   evas_object_image_smooth_scale_set(o, e_comp_config_get()->smooth_windows);
   cw->obj_mirror = eina_list_append(cw->obj_mirror, o);
   evas_object_event_callback_add(o, EVAS_CALLBACK_DEL, _e_comp_object_cb_mirror_del, cw);
   evas_object_data_set(o, "E_Client", cw->ec);
   evas_object_data_set(o, "comp_mirror", cw);

   evas_object_image_alpha_set(o, cw->ec->argb || (!!cw->ec->shape_rects));
   evas_object_image_size_set(o, w, h);

   if (cw->ec->shaped)
     {
        if (!e_pixmap_image_exists(cw->ec->pixmap)) return o;
        pix = evas_object_image_data_get(cw->obj, 0);
     }
   else
     {
        if (cw->native)
          {
             Evas_Native_Surface ns;

             e_pixmap_native_surface_init(cw->ec->pixmap, &ns);
             evas_object_image_native_surface_set(o, &ns);
          }
        else
          {
             if (!e_pixmap_image_exists(cw->ec->pixmap)) return o;
             argb = e_pixmap_image_is_argb(cw->ec->pixmap);
             if (argb)
               pix = e_pixmap_image_data_get(cw->ec->pixmap);
             else
               pix = evas_object_image_data_get(cw->obj, EINA_FALSE);
          }
     }
   if (pix)
     {
        evas_object_image_data_set(o, pix);
        if (!argb)
          evas_object_image_data_set(cw->obj, pix);
     }
   evas_object_image_data_update_add(o, 0, 0, w, h);
   return o;
}

//////////////////////////////////////////////////////

/* setup an api effect for a client */
EAPI void
e_comp_object_effect_set(Evas_Object *obj, const char *effect)
{
   char buf[4096];
   Eina_Stringshare *grp;
   E_Comp_Config *config;
   Eina_Bool loaded = EINA_FALSE;

   API_ENTRY;
   if (!cw->shobj) return; //input window

   if (!effect) effect = "none";
   snprintf(buf, sizeof(buf), "e/comp/effects/%s", effect);

   config = e_comp_config_get();
   if ((config) && (config->effect_file))
     {
        if (edje_object_file_set(cw->effect_obj, config->effect_file, buf))
          loaded = EINA_TRUE;
     }

   if (!loaded)
     {
        edje_object_file_get(cw->effect_obj, NULL, &grp);
        if (!e_util_strcmp(buf, grp)) return;
        if (!e_theme_edje_object_set(cw->effect_obj, "base/theme/comp", buf))
          {
             snprintf(buf, sizeof(buf), "e/comp/effects/auto/%s", effect);
             if (!e_theme_edje_object_set(cw->effect_obj, "base/theme/comp", buf))
               if (!e_theme_edje_object_set(cw->effect_obj, "base/theme/comp", "e/comp/effects/none")) return;
          }
     }
   edje_object_part_swallow(cw->effect_obj, "e.swallow.content", cw->shobj);
   if (cw->effect_clip)
     {
        evas_object_clip_unset(cw->clip);
        cw->effect_clip = 0;
     }
   cw->effect_clip_able = !edje_object_data_get(cw->effect_obj, "noclip");
}

/* set params for embryo scripts in effect */
EAPI void
e_comp_object_effect_params_set(Evas_Object *obj, int id, int *params, unsigned int count)
{
   Edje_Message_Int_Set *msg;
   unsigned int x;

   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(params);
   EINA_SAFETY_ON_FALSE_RETURN(count);

   msg = alloca(sizeof(Edje_Message_Int_Set) + ((count - 1) * sizeof(int)));
   msg->count = (int)count;
   for (x = 0; x < count; x++)
      msg->val[x] = params[x];
   edje_object_message_send(cw->effect_obj, EDJE_MESSAGE_INT_SET, id, msg);
   edje_object_message_signal_process(cw->effect_obj);
}

static void
_e_comp_object_effect_end_cb(void *data, Evas_Object *obj, const char *emission, const char *source)
{
   Edje_Signal_Cb end_cb;
   void *end_data;
   E_Comp_Object *cw = data;

   edje_object_signal_callback_del_full(obj, "e,action,done", "e", _e_comp_object_effect_end_cb, NULL);
   if (cw->animating)
     {
        cw->comp->animating--;
        cw->animating--;
        e_object_unref(E_OBJECT(cw->ec));
     }

   end_cb = evas_object_data_get(obj, "_e_comp.end_cb");
   if (!end_cb) return;
   end_data = evas_object_data_get(obj, "_e_comp.end_data");
   end_cb(end_data, cw->smart_obj, emission, source);

}

/* clip effect to client's zone */
EAPI void
e_comp_object_effect_clip(Evas_Object *obj)
{
   API_ENTRY;
   if (!cw->ec->zone) return;
   if (cw->effect_clip) e_comp_object_effect_unclip(cw->smart_obj);
   if (!cw->effect_clip_able) return;
   evas_object_clip_set(cw->smart_obj, cw->ec->zone->bg_clip_object);
   cw->effect_clip = 1;
}

/* unclip effect from client's zone */
EAPI void
e_comp_object_effect_unclip(Evas_Object *obj)
{
   API_ENTRY;
   if (!cw->effect_clip) return;
   evas_object_clip_unset(cw->smart_obj);
   cw->effect_clip = 0;
}

/* start effect, running end_cb after */
EAPI void
e_comp_object_effect_start(Evas_Object *obj, Edje_Signal_Cb end_cb, const void *end_data)
{
   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(cw->ec); //NYI
   e_comp_object_effect_clip(obj);
   edje_object_signal_callback_del(cw->effect_obj, "e,action,done", "e", _e_comp_object_effect_end_cb);

   edje_object_signal_callback_add(cw->effect_obj, "e,action,done", "e", _e_comp_object_effect_end_cb, cw);
   evas_object_data_set(cw->effect_obj, "_e_comp.end_cb", end_cb);
   evas_object_data_set(cw->effect_obj, "_e_comp.end_data", end_data);

   edje_object_signal_emit(cw->effect_obj, "e,action,go", "e");
   if (cw->animating) return;
   cw->comp->animating++;
   cw->animating++;
   e_object_ref(E_OBJECT(cw->ec));
}

/* stop a currently-running effect immediately */
EAPI void
e_comp_object_effect_stop(Evas_Object *obj, Edje_Signal_Cb end_cb)
{
   API_ENTRY;
   if (evas_object_data_get(cw->effect_obj, "_e_comp.end_cb") != end_cb) return;
   e_comp_object_effect_unclip(obj);
   if (cw->effect_clip)
     {
        evas_object_clip_unset(cw->effect_obj);
        cw->effect_clip = 0;
     }
   edje_object_signal_emit(cw->effect_obj, "e,action,stop", "e");
   edje_object_signal_callback_del_full(cw->effect_obj, "e,action,done", "e", _e_comp_object_effect_end_cb, cw);
   if (cw->animating)
     {
        cw->animating--;
        cw->comp->animating--;
        e_object_unref(E_OBJECT(cw->ec));
     }
}

static int
_e_comp_object_effect_mover_sort_cb(E_Comp_Object_Mover *a, E_Comp_Object_Mover *b)
{
   return a->pri - b->pri;
}

/* add a function to trigger based on signal emissions for the purpose of modifying effects */
EAPI E_Comp_Object_Mover *
e_comp_object_effect_mover_add(int pri, const char *sig, E_Comp_Object_Mover_Cb provider, const void *data)
{
   E_Comp_Object_Mover *prov;

   prov = E_NEW(E_Comp_Object_Mover, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(prov, NULL);
   prov->func = provider;
   prov->data = (void*)data;
   prov->pri = pri;
   prov->sig = sig;
   _e_comp_object_movers = eina_inlist_sorted_insert(_e_comp_object_movers, EINA_INLIST_GET(prov),
     (Eina_Compare_Cb)_e_comp_object_effect_mover_sort_cb);
   return prov;
}

EAPI void
e_comp_object_effect_mover_del(E_Comp_Object_Mover *prov)
{
   EINA_SAFETY_ON_NULL_RETURN(prov);
   _e_comp_object_movers = eina_inlist_remove(_e_comp_object_movers, EINA_INLIST_GET(prov));
   free(prov);
}
////////////////////////////////////

static void
_e_comp_object_autoclose_cleanup(E_Comp *c, Eina_Bool already_del)
{
   if (c->autoclose.obj)
     {
        e_comp_ungrab_input(c, 0, 1);
        if (c->autoclose.del_cb)
          c->autoclose.del_cb(c->autoclose.data, c->autoclose.obj);
        else if (!already_del)
          {
             evas_object_hide(c->autoclose.obj);
             E_FREE_FUNC(c->autoclose.obj, evas_object_del);
          }
        E_FREE_FUNC(c->autoclose.rect, evas_object_del);
     }
   c->autoclose.obj = NULL;
   c->autoclose.data = NULL;
   c->autoclose.del_cb = NULL;
   c->autoclose.key_cb = NULL;
   E_FREE_FUNC(c->autoclose.key_handler, ecore_event_handler_del);
   e_comp_shape_queue(c);
}

static Eina_Bool
_e_comp_object_autoclose_key_down_cb(void *data, int type EINA_UNUSED, void *event)
{
   Ecore_Event_Key *ev = event;
   Eina_Bool del = EINA_TRUE;
   E_Comp *c = data;

   /* returning false in key_cb means delete the object */
   if (c->autoclose.key_cb)
     del = !c->autoclose.key_cb(c->autoclose.data, ev);
   if (del) _e_comp_object_autoclose_cleanup(data, 0);
   return ECORE_CALLBACK_RENEW;
}

static void
_e_comp_object_autoclose_mouse_up_cb(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   _e_comp_object_autoclose_cleanup(data, 0);
}

static void
_e_comp_object_autoclose_setup(Evas_Object *obj)
{
   E_Comp *c = e_comp_util_evas_object_comp_get(obj);

   if (!c->autoclose.rect)
     {
        /* create rect just below autoclose object to catch mouse events */
        c->autoclose.rect = evas_object_rectangle_add(c->evas);
        evas_object_move(c->autoclose.rect, 0, 0);
        evas_object_resize(c->autoclose.rect, c->man->w, c->man->h);
        evas_object_show(c->autoclose.rect);
        evas_object_name_set(c->autoclose.rect, "c->autoclose.rect");
        evas_object_color_set(c->autoclose.rect, 0, 0, 0, 0);
        evas_object_event_callback_add(c->autoclose.rect, EVAS_CALLBACK_MOUSE_UP, _e_comp_object_autoclose_mouse_up_cb, c);
        e_comp_grab_input(c, 0, 1);
     }
   evas_object_layer_set(c->autoclose.rect, evas_object_layer_get(obj) - 1);
   e_comp_shape_queue(c);
   if (!c->autoclose.key_handler)
     c->autoclose.key_handler = ecore_event_handler_add(ECORE_EVENT_KEY_DOWN, _e_comp_object_autoclose_key_down_cb, c);
}

static void
_e_comp_object_autoclose_show(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   _e_comp_object_autoclose_setup(obj);
   evas_object_event_callback_del(obj, EVAS_CALLBACK_SHOW, _e_comp_object_autoclose_show);
}

static void
_e_comp_object_autoclose_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   evas_object_event_callback_del(obj, EVAS_CALLBACK_SHOW, _e_comp_object_autoclose_show);
   _e_comp_object_autoclose_cleanup(data, 1);
   if (e_client_focused_get()) return;
   e_client_refocus();
}

EAPI void
e_comp_object_util_autoclose(Evas_Object *obj, E_Comp_Object_Autoclose_Cb del_cb, E_Comp_Object_Key_Cb cb, const void *data)
{
   E_Comp *c;

   SOFT_ENTRY();

   c = e_comp_util_evas_object_comp_get(obj);
   EINA_SAFETY_ON_NULL_RETURN(c);
   if (c->autoclose.obj)
     {
        if (c->autoclose.obj == obj) return;
        evas_object_event_callback_del_full(c->autoclose.obj, EVAS_CALLBACK_DEL, _e_comp_object_autoclose_del, c);
        c->autoclose.obj = obj;
        c->autoclose.del_cb = del_cb;
        c->autoclose.key_cb = cb;
        c->autoclose.data = (void*)data;
        if (evas_object_visible_get(obj))
          _e_comp_object_autoclose_setup(obj);
        else
          evas_object_event_callback_add(obj, EVAS_CALLBACK_SHOW, _e_comp_object_autoclose_show, c);
        evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _e_comp_object_autoclose_del, c);
        return;
     }
   c->autoclose.obj = obj;
   c->autoclose.del_cb = del_cb;
   c->autoclose.key_cb = cb;
   c->autoclose.data = (void*)data;
   if (evas_object_visible_get(obj))
     _e_comp_object_autoclose_setup(obj);
   else
     evas_object_event_callback_add(obj, EVAS_CALLBACK_SHOW, _e_comp_object_autoclose_show, c);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _e_comp_object_autoclose_del, c);
}
