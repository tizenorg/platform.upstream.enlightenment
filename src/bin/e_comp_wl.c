#define E_COMP_WL
#include "e.h"

#include <wayland-tbm-server.h>

/* handle include for printing uint64_t */
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define COMPOSITOR_VERSION 3

/* Resource Data Mapping: (wl_resource_get_user_data)
 *
 * wl_surface == e_pixmap
 * wl_region == eina_tiler
 * wl_subsurface == e_client
 *
 */

static void _e_comp_wl_subsurface_parent_commit(E_Client *ec, Eina_Bool parent_synchronized);
static void _e_comp_wl_subsurface_restack(E_Client *ec);

/* local variables */
typedef struct _E_Comp_Wl_Transform_Context
{
   E_Client *ec;
   int direction;
   int degree;
} E_Comp_Wl_Transform_Context;

/* static Eina_Hash *clients_win_hash = NULL; */
static Eina_Hash *clients_buffer_hash = NULL;
static Eina_List *handlers = NULL;
static double _last_event_time = 0.0;

/* local functions */
static void
_e_comp_wl_transform_stay_within_canvas(E_Client *ec, int x, int y, int *new_x, int *new_y)
{
   int new_x_max, new_y_max;
   int zw, zh;
   Eina_Bool lw, lh;

   if (!ec->zone)
     {
        if (new_x) *new_x = x;
        if (new_y) *new_y = y;
        return;
     }

   zw = ec->zone->w;
   zh = ec->zone->h;

   new_x_max = zw - ec->w;
   new_y_max = zh - ec->h;
   lw = ec->w > zw ? EINA_TRUE : EINA_FALSE;
   lh = ec->h > zh ? EINA_TRUE : EINA_FALSE;

   if (new_x)
     {
        if (lw)
          {
             if (x <= new_x_max)
               *new_x = new_x_max;
             else if (x >= 0)
               *new_x = 0;
          }
        else
          {
             if (x >= new_x_max)
               *new_x = new_x_max;
             else if (x <= 0)
               *new_x = 0;
          }
     }

   if (new_y)
     {
        if (lh)
          {
             if (y <= new_y_max)
               *new_y = new_y_max;
             else if (y >= 0)
               *new_y = 0;
          }
        else
          {
             if (y >= new_y_max)
               *new_y = new_y_max;
             else if (y <= 0)
               *new_y = 0;
          }
     }
}

static void
_e_comp_wl_transform_pull_del(void *data,
                              Elm_Transit *trans EINA_UNUSED)
{
   E_Client *ec = data;
   if (!ec) return;

   e_object_unref(E_OBJECT(ec));
}

static void
_e_comp_wl_transform_pull(E_Client *ec)
{
   Elm_Transit *trans;
   int new_x, new_y;

   new_x = ec->client.x;
   new_y = ec->client.y;

   _e_comp_wl_transform_stay_within_canvas(ec,
                                           ec->client.x, ec->client.y,
                                           &new_x, &new_y);

   if ((ec->client.x == new_x) && (ec->client.y == new_y))
     return;

   e_object_ref(E_OBJECT(ec));

   trans = elm_transit_add();
   elm_transit_del_cb_set(trans, _e_comp_wl_transform_pull_del, ec);
   elm_transit_tween_mode_set(trans, ELM_TRANSIT_TWEEN_MODE_DECELERATE);
   elm_transit_effect_translation_add(trans,
                                      0, 0,
                                      new_x - ec->client.x,
                                      new_y - ec->client.y);
   elm_transit_object_add(trans, ec->frame);
   elm_transit_objects_final_state_keep_set(trans, EINA_TRUE);
   elm_transit_duration_set(trans, 0.4);
   elm_transit_go(trans);
}

static void
_e_comp_wl_transform_effect_end(Elm_Transit_Effect *context,
                                Elm_Transit *trans)
{
   E_Client *ec = NULL;
   E_Comp_Wl_Transform_Context *ctxt = context;

   if (!ctxt) return;
   ec = ctxt->ec;

   if ((ec) && (!e_object_is_del(E_OBJECT(ec))))
     {
        ec->comp_data->transform.start = 0;
        ec->comp_data->transform.cur_degree += ctxt->degree * ctxt->direction;
        if (ec->comp_data->transform.cur_degree < 0)
          ec->comp_data->transform.cur_degree += 360;
        else
          ec->comp_data->transform.cur_degree %= 360;
        ec->comp_data->transform.scount = 0;
        ec->comp_data->transform.stime = 0;
     }

   E_FREE(ctxt);
}

static void
_e_comp_wl_transform_effect_del(void *data,
                                Elm_Transit *trans EINA_UNUSED)
{
   E_Client *ec = data;
   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   e_client_transform_apply(ec, ec->comp_data->transform.cur_degree, -1.0, -1, -1);
}

static void
_e_comp_wl_transform_effect_run(Elm_Transit_Effect *context,
                                Elm_Transit *trans EINA_UNUSED,
                                double progress)
{
   E_Comp_Wl_Transform_Context *ctxt = context;
   E_Client *ec = NULL;
   int cx, cy;

   ec = ctxt->ec;
   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   cx = ec->client.x + ec->client.w / 2;
   cy = ec->client.y + ec->client.h / 2;

   e_client_transform_apply(ec, ec->comp_data->transform.cur_degree + ctxt->degree * ctxt->direction * progress, -1.0, cx, cy);
}

static void
_e_comp_wl_transform_unset(E_Client *ec)
{

   Elm_Transit *trans;
   E_Comp_Wl_Transform_Context *ctxt;
   ctxt = E_NEW(E_Comp_Wl_Transform_Context, 1);
   if (!ctxt) return;

   ec->comp_data->transform.start = 1;
   ctxt->direction = 1;
   ctxt->ec = ec;
   ctxt->degree = 360 - ec->comp_data->transform.cur_degree;

   trans = elm_transit_add();
   elm_transit_del_cb_set(trans, _e_comp_wl_transform_effect_del, ec);
   elm_transit_tween_mode_set(trans, ELM_TRANSIT_TWEEN_MODE_ACCELERATE);
   elm_transit_object_add(trans, ec->frame);
   elm_transit_effect_add(trans,
                          _e_comp_wl_transform_effect_run,
                          (void*)ctxt,
                          _e_comp_wl_transform_effect_end);
   elm_transit_duration_set(trans, 0.45);
   elm_transit_go(trans);
}

static Eina_Bool
_e_comp_wl_transform_set(E_Client *ec, 
                         int direction,
                         int count,
                         unsigned int time)
{
   Elm_Transit *trans;
   E_Comp_Wl_Transform_Context *ctxt;
   int degree;

   switch (count)
     {
      case 3:
         if (time <= 50)
           return EINA_FALSE;
         else if (time <= 100)
           degree = 100;
         else
           degree = 50;
         break;
      case 4:
         if (time <= 50)
           degree = 350;
         else if (time <= 100)
           degree = 250;
         else
           degree = 150;
         break;
      default:
         return EINA_FALSE;
     }

   ctxt = E_NEW(E_Comp_Wl_Transform_Context, 1);
   if (!ctxt) return EINA_FALSE;

   ec->comp_data->transform.start = 1;

   ctxt->direction = direction / abs(direction);
   ctxt->ec = ec;
   ctxt->degree = degree;

   trans = elm_transit_add();
   elm_transit_del_cb_set(trans, _e_comp_wl_transform_effect_del, ec);
   elm_transit_tween_mode_set(trans, ELM_TRANSIT_TWEEN_MODE_DECELERATE);
   elm_transit_object_add(trans, ec->frame);
   elm_transit_effect_add(trans,
                          _e_comp_wl_transform_effect_run,
                          (void*)ctxt,
                          _e_comp_wl_transform_effect_end);
   elm_transit_duration_set(trans, (double)count/10.0);
   elm_transit_go(trans);

   return EINA_TRUE;
}

static void
_e_comp_wl_focus_down_set(E_Client *ec)
{
   Ecore_Window win = 0;

   win = e_client_util_pwin_get(ec);
   e_bindings_mouse_grab(E_BINDING_CONTEXT_WINDOW, win);
   e_bindings_wheel_grab(E_BINDING_CONTEXT_WINDOW, win);
}

static void
_e_comp_wl_focus_check(E_Comp *comp)
{
   E_Client *ec;

   if (stopping) return;
   ec = e_client_focused_get();
   if ((!ec) || (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL))
     e_grabinput_focus(comp->ee_win, E_FOCUS_METHOD_PASSIVE);
}

static void
_e_comp_wl_log_cb_print(const char *format, va_list args)
{
   EINA_LOG_DOM_INFO(e_log_dom, format, args);
}

static Eina_Bool
_e_comp_wl_cb_read(void *data, Ecore_Fd_Handler *hdlr EINA_UNUSED)
{
   E_Comp_Data *cdata;

   if (!(cdata = data)) return ECORE_CALLBACK_RENEW;

   /* dispatch pending wayland events */
   wl_event_loop_dispatch(cdata->wl.loop, 0);

   return ECORE_CALLBACK_RENEW;
}

static void
_e_comp_wl_cb_prepare(void *data, Ecore_Fd_Handler *hdlr EINA_UNUSED)
{
   E_Comp_Data *cdata;

   if (!(cdata = data)) return;

   /* flush pending client events */
   wl_display_flush_clients(cdata->wl.disp);
}

static Eina_Bool
_e_comp_wl_cb_module_idle(void *data)
{
   E_Comp_Data *cdata;
   E_Module  *mod = NULL;

   if (!(cdata = data)) return ECORE_CALLBACK_RENEW;

   /* check if we are still loading modules */
   if (e_module_loading_get()) return ECORE_CALLBACK_RENEW;

   if (!(mod = e_module_find("wl_desktop_shell")))
     mod = e_module_new("wl_desktop_shell");

   if (mod)
     {
        e_module_enable(mod);

        /* FIXME: NB:
         * Do we need to dispatch pending wl events here ?? */

        return ECORE_CALLBACK_CANCEL;
     }

   return ECORE_CALLBACK_RENEW;
}

static void
_e_comp_wl_map_size_cal_from_buffer(E_Client *ec)
{
   E_Comp_Wl_Buffer_Viewport *vp = &ec->comp_data->scaler.buffer_viewport;
   int32_t width, height;

   if (!ec->comp_data->buffer_ref.buffer)
     {
        ec->comp_data->width_from_buffer = 0;
        ec->comp_data->height_from_buffer = 0;
        return;
     }

   switch (vp->buffer.transform)
     {
      case WL_OUTPUT_TRANSFORM_90:
      case WL_OUTPUT_TRANSFORM_270:
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        width = ec->comp_data->buffer_ref.buffer->h / vp->buffer.scale;
        height = ec->comp_data->buffer_ref.buffer->w / vp->buffer.scale;
        break;
      default:
        width = ec->comp_data->buffer_ref.buffer->w / vp->buffer.scale;
        height = ec->comp_data->buffer_ref.buffer->h / vp->buffer.scale;
        break;
     }

   ec->comp_data->width_from_buffer = width;
   ec->comp_data->height_from_buffer = height;
}

static void
_e_comp_wl_map_size_cal_from_viewport(E_Client *ec)
{
   E_Comp_Wl_Buffer_Viewport *vp = &ec->comp_data->scaler.buffer_viewport;
   int32_t width, height;

   width = ec->comp_data->width_from_buffer;
   height = ec->comp_data->height_from_buffer;

   if (width != 0 && vp->surface.width != -1)
     {
        ec->comp_data->width_from_viewport = vp->surface.width;
        ec->comp_data->height_from_viewport = vp->surface.height;
        return;
     }

   if (width != 0 && vp->buffer.src_width != wl_fixed_from_int(-1))
     {
        int32_t w = wl_fixed_to_int(wl_fixed_from_int(1) - 1 + vp->buffer.src_width);
        int32_t h = wl_fixed_to_int(wl_fixed_from_int(1) - 1 + vp->buffer.src_height);
        ec->comp_data->width_from_viewport = w ?: 1;
        ec->comp_data->height_from_viewport = h ?: 1;
        return;
     }

   ec->comp_data->width_from_viewport = width;
   ec->comp_data->height_from_viewport = height;
}

static void
_e_comp_wl_map_transform(int width, int height, uint32_t transform, int32_t scale,
                         int sx, int sy, int *dx, int *dy)
{
   switch (transform)
     {
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
        *dx = sx, *dy = sy;
        break;
      case WL_OUTPUT_TRANSFORM_FLIPPED:
        *dx = width - sx, *dy = sy;
        break;
      case WL_OUTPUT_TRANSFORM_90:
        *dx = height - sy, *dy = sx;
        break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        *dx = height - sy, *dy = width - sx;
        break;
      case WL_OUTPUT_TRANSFORM_180:
        *dx = width - sx, *dy = height - sy;
        break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        *dx = sx, *dy = height - sy;
        break;
      case WL_OUTPUT_TRANSFORM_270:
        *dx = sy, *dy = width - sx;
        break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        *dx = sy, *dy = sx;
        break;
     }

   *dx *= scale;
   *dy *= scale;
}

static void
_e_comp_wl_map_apply(E_Client *ec)
{
   E_Comp_Wl_Buffer_Viewport *vp = &ec->comp_data->scaler.buffer_viewport;
   E_Comp_Wl_Subsurf_Data *sdata;
   Evas_Map *map;
   int x1, y1, x2, y2, x, y;
   int dx, dy;

   sdata = ec->comp_data->sub.data;
   if (sdata)
     {
        if (sdata->parent)
          {
             dx = sdata->parent->x + sdata->position.x;
             dy = sdata->parent->y + sdata->position.y;
          }
        else
          {
             dx = sdata->position.x;
             dy = sdata->position.y;
          }
     }
   else
     {
        dx = ec->x;
        dy = ec->y;
     }

   map = evas_map_new(4);

   evas_map_util_points_populate_from_geometry(map,
                                               dx, dy,
                                               ec->comp_data->width_from_viewport,
                                               ec->comp_data->height_from_viewport,
                                               0);

   if (vp->buffer.src_width == wl_fixed_from_int(-1))
     {
        x1 = 0.0;
        y1 = 0.0;
        x2 = ec->comp_data->width_from_buffer;
        y2 = ec->comp_data->height_from_buffer;
     }
   else
     {
        x1 = wl_fixed_to_int(vp->buffer.src_x);
        y1 = wl_fixed_to_int(vp->buffer.src_y);
        x2 = wl_fixed_to_int(vp->buffer.src_x + vp->buffer.src_width);
        y2 = wl_fixed_to_int(vp->buffer.src_y + vp->buffer.src_height);
     }

   _e_comp_wl_map_transform(ec->comp_data->width_from_buffer, ec->comp_data->height_from_buffer,
                            vp->buffer.transform, vp->buffer.scale,
                            x1, y1, &x, &y);
   evas_map_point_image_uv_set(map, 0, x, y);

   _e_comp_wl_map_transform(ec->comp_data->width_from_buffer, ec->comp_data->height_from_buffer,
                            vp->buffer.transform, vp->buffer.scale,
                            x2, y1, &x, &y);
   evas_map_point_image_uv_set(map, 1, x, y);

   _e_comp_wl_map_transform(ec->comp_data->width_from_buffer, ec->comp_data->height_from_buffer,
                            vp->buffer.transform, vp->buffer.scale,
                            x2, y2, &x, &y);
   evas_map_point_image_uv_set(map, 2, x, y);

   _e_comp_wl_map_transform(ec->comp_data->width_from_buffer, ec->comp_data->height_from_buffer,
                            vp->buffer.transform, vp->buffer.scale,
                            x1, y2, &x, &y);
   evas_map_point_image_uv_set(map, 3, x, y);

   evas_object_map_set(ec->frame, map);
   evas_object_map_enable_set(ec->frame, map ? EINA_TRUE : EINA_FALSE);

   evas_map_free(map);
}

static void
_e_comp_wl_evas_cb_show(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec, *tmp;
   Eina_List *l;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!ec->override) e_hints_window_visible_set(ec);

   if ((!ec->override) && (!ec->re_manage) && (!ec->comp_data->reparented) &&
       (!ec->comp_data->need_reparent))
     {
        ec->comp_data->need_reparent = EINA_TRUE;
        ec->visible = EINA_TRUE;
     }
   if (!e_client_util_ignored_get(ec))
     {
        ec->take_focus = !starting;
        EC_CHANGED(ec);
     }

   if (!ec->comp_data->need_reparent)
     {
        if ((ec->hidden) || (ec->iconic))
          {
             evas_object_hide(ec->frame);
             e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
          }
        else if (!ec->internal_elm_win)
          evas_object_show(ec->frame);
     }

   EINA_LIST_FOREACH(ec->e.state.video_child, l, tmp)
     evas_object_show(tmp->frame);

   if (ec->comp_data->sub.below_obj)
     evas_object_show(ec->comp_data->sub.below_obj);
}

static void
_e_comp_wl_evas_cb_hide(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec, *tmp;
   Eina_List *l;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   EINA_LIST_FOREACH(ec->e.state.video_child, l, tmp)
     evas_object_hide(tmp->frame);

   if (ec->comp_data->sub.below_obj)
     evas_object_hide(ec->comp_data->sub.below_obj);
}

static void
_e_comp_wl_evas_cb_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Client *ec;
   E_Client *subc;
   Eina_List *l;
   int x, y;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     {
        x = ec->x + subc->comp_data->sub.data->position.x;
        y = ec->y + subc->comp_data->sub.data->position.y;
        evas_object_move(subc->frame, x, y);
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        x = ec->x + subc->comp_data->sub.data->position.x;
        y = ec->y + subc->comp_data->sub.data->position.y;
        evas_object_move(subc->frame, x, y);
     }

   if (ec->comp_data->sub.below_obj)
     evas_object_move(ec->comp_data->sub.below_obj, ec->x, ec->y);
}

static void
_e_comp_wl_evas_cb_restack(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Client *ec;
   E_Client *parent = NULL;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   /* return if ec isn't both a parent of a subsurface and a subsurface itself */
   if (!ec->comp_data->sub.list && !ec->comp_data->sub.below_list && !ec->comp_data->sub.data)
     {
        if (ec->comp_data->sub.below_obj) _e_comp_wl_subsurface_restack(ec);
        return;
     }

   if (ec->comp_data->sub.data)
     parent = ec->comp_data->sub.data->parent;
   else
     parent = ec;

   EINA_SAFETY_ON_NULL_RETURN(parent);

   /* return if parent is null or is in restacking progress */
   if (parent->comp_data->sub.restacking) return;

   _e_comp_wl_subsurface_restack(parent);
}

static void
_e_comp_wl_evas_cb_mouse_in(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;
   Evas_Event_Mouse_In *ev;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial;

   ev = event;
   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!ec->comp_data->surface) return;
   if (ec->comp_data->transform.start) return;

   if (e_comp->wl_comp_data->dnd.enabled)
     {
        e_comp_wl_data_dnd_focus(ec);
        return;
     }

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(ec->comp->wl_comp_data->wl.disp);
   EINA_LIST_FOREACH(ec->comp->wl_comp_data->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;

        wl_pointer_send_enter(res, serial, ec->comp_data->surface,
                              wl_fixed_from_int(ev->canvas.x - ec->client.x),
                              wl_fixed_from_int(ev->canvas.y - ec->client.y));
     }
}

static void
_e_comp_wl_evas_cb_mouse_out(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   Evas_Event_Mouse_Out *ev;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial;
   Eina_Bool inside_check;

   ev = event;

   if (!(ec = data)) return;
   inside_check = E_INSIDE(ev->canvas.x, ev->canvas.y,
                          ec->client.x, ec->client.y, ec->client.w, ec->client.h);
   if (ec->cur_mouse_action && inside_check) return;
   if (e_object_is_del(E_OBJECT(e_comp))) return;
   /* FIXME? this is a hack to just reset the cursor whenever we mouse out. not sure if accurate */
   {
      Evas_Object *o;

      ecore_evas_cursor_get(e_comp->ee, &o, NULL, NULL, NULL);
      if ((e_comp->pointer->o_ptr != o) && (e_comp->wl_comp_data->ptr.enabled))
        e_pointer_object_set(e_comp->pointer, NULL, 0, 0);
   }
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!ec->comp_data->surface) return;
   if (ec->comp_data->transform.start) return;

   if (e_comp->wl_comp_data->dnd.enabled)
     {
        if (e_comp->wl_comp_data->dnd.focus == ec->comp_data->surface)
          e_comp_wl_data_dnd_focus(NULL);
     }

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(ec->comp->wl_comp_data->wl.disp);
   EINA_LIST_FOREACH(ec->comp->wl_comp_data->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_leave(res, serial, ec->comp_data->surface);
     }
}

static void
_e_comp_wl_evas_cb_mouse_move(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   Evas_Event_Mouse_Move *ev;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;

   ev = event;

   e_comp->wl_comp_data->ptr.x = wl_fixed_from_int(ev->cur.canvas.x);
   e_comp->wl_comp_data->ptr.y = wl_fixed_from_int(ev->cur.canvas.y);

   if (!(ec = data)) return;

   if (ec->cur_mouse_action) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_client_util_ignored_get(ec)) return;
   if (!ec->comp_data->surface) return;

   if (e_comp->wl_comp_data->dnd.enabled)
     {
        e_comp_wl_data_dnd_motion(ec, ev->timestamp,
                                  ev->cur.canvas.x - ec->client.x,
                                  ev->cur.canvas.y - ec->client.y);
        return;
     }

   wc = wl_resource_get_client(ec->comp_data->surface);
   EINA_LIST_FOREACH(ec->comp->wl_comp_data->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_motion(res, ev->timestamp,
                               wl_fixed_from_int(ev->cur.canvas.x - ec->client.x),
                               wl_fixed_from_int(ev->cur.canvas.y - ec->client.y));
     }
}

static Eina_Bool
_e_comp_wl_evas_handle_mouse_button(E_Client *ec, uint32_t timestamp, uint32_t button_id, uint32_t state)
{
   Eina_List *l;
   struct wl_client *wc;
   uint32_t serial, btn;
   struct wl_resource *res;

   if (ec->cur_mouse_action || ec->border_menu) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;
   if (e_client_util_ignored_get(ec)) return EINA_FALSE;

   switch (button_id)
     {
      case 1:
        btn = BTN_LEFT;
        break;
      case 2:
        btn = BTN_MIDDLE;
        break;
      case 3:
        btn = BTN_RIGHT;
        break;
      default:
        btn = button_id;
        break;
     }

   ec->comp->wl_comp_data->ptr.button = btn;

   if (!ec->comp_data->surface) return EINA_FALSE;

   if ((ec->comp_data->transform.cur_degree != 0) &&
       (btn == BTN_MIDDLE))
     {
        _e_comp_wl_transform_unset(ec);
        return EINA_TRUE;
     }

   if (e_comp->wl_comp_data->dnd.enabled)
     {
        e_comp_wl_data_dnd_drop(ec, timestamp, btn, state);
        return EINA_FALSE;
     }

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(ec->comp->wl_comp_data->wl.disp);

   EINA_LIST_FOREACH(ec->comp->wl_comp_data->ptr.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        if (!e_comp_wl_input_pointer_check(res)) continue;
        wl_pointer_send_button(res, serial, timestamp, btn, state);
     }
   return EINA_TRUE;
}

static void
_e_comp_wl_evas_cb_mouse_down(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec = data;
   Evas_Event_Mouse_Down *ev = event;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   _e_comp_wl_evas_handle_mouse_button(ec, ev->timestamp, ev->button,
                                       WL_POINTER_BUTTON_STATE_PRESSED);
}

static void
_e_comp_wl_evas_cb_mouse_up(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec = data;
   Evas_Event_Mouse_Up *ev = event;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   _e_comp_wl_evas_handle_mouse_button(ec, ev->timestamp, ev->button,
                                       WL_POINTER_BUTTON_STATE_RELEASED);
}

static void
_e_comp_wl_evas_cb_mouse_wheel(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   Evas_Event_Mouse_Wheel *ev;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t axis, dir;

   ev = event;
   if (!(ec = data)) return;
   if (ec->cur_mouse_action) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_client_util_ignored_get(ec)) return;

   if (ev->direction == 0)
     axis = WL_POINTER_AXIS_VERTICAL_SCROLL;
   else
     axis = WL_POINTER_AXIS_HORIZONTAL_SCROLL;

   if (ev->z < 0)
     dir = -wl_fixed_from_int(abs(ev->z));
   else
     dir = wl_fixed_from_int(ev->z);

   if (!ec->comp_data->surface) return;

   if (ec->comp_data->transform.start) return;
   if (ec->comp_data->transform.enabled)
     {
        if (ec->comp_data->transform.stime == 0)
          {
             ec->comp_data->transform.stime = ev->timestamp;
             return;
          }

        if (_e_comp_wl_transform_set(ec, ev->z,
                                     ++ec->comp_data->transform.scount,
                                     ev->timestamp - ec->comp_data->transform.stime))
          {
             ec->comp_data->transform.scount = 0;
             ec->comp_data->transform.stime = 0;
          }

        /* do not send wheel event to client */
        return;
     }

   wc = wl_resource_get_client(ec->comp_data->surface);
   EINA_LIST_FOREACH(ec->comp->wl_comp_data->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_axis(res, ev->timestamp, axis, dir);
     }
}

static void
_e_comp_wl_evas_cb_multi_down(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   Eina_List *l;
   struct wl_client *wc;
   uint32_t serial;
   struct wl_resource *res;
   E_Client *ec = data;
   Evas_Event_Multi_Down *ev = event;
   wl_fixed_t x, y;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data->surface) return;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(e_comp->wl_comp_data->wl.disp);

   x = wl_fixed_from_int(ev->canvas.x - ec->client.x);
   y = wl_fixed_from_int(ev->canvas.y - ec->client.y);

   EINA_LIST_FOREACH(e_comp->wl_comp_data->touch.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        if (!e_comp_wl_input_touch_check(res)) continue;
        wl_touch_send_down(res, serial, ev->timestamp, ec->comp_data->surface, ev->device, x, y);
     }
}

static void
_e_comp_wl_evas_cb_multi_up(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   Eina_List *l;
   struct wl_client *wc;
   uint32_t serial;
   struct wl_resource *res;
   E_Client *ec = data;
   Evas_Event_Multi_Up *ev = event;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data->surface) return;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(e_comp->wl_comp_data->wl.disp);

   EINA_LIST_FOREACH(e_comp->wl_comp_data->touch.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        if (!e_comp_wl_input_touch_check(res)) continue;
        wl_touch_send_up(res, serial, ev->timestamp, ev->device);
     }
}

static void
_e_comp_wl_evas_cb_multi_move(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   Eina_List *l;
   struct wl_client *wc;
   struct wl_resource *res;
   E_Client *ec = data;
   Evas_Event_Multi_Move *ev = event;
   wl_fixed_t x, y;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data->surface) return;

   wc = wl_resource_get_client(ec->comp_data->surface);

   x = wl_fixed_from_int(ev->cur.canvas.x - ec->client.x);
   y = wl_fixed_from_int(ev->cur.canvas.y - ec->client.y);

   EINA_LIST_FOREACH(e_comp->wl_comp_data->touch.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        if (!e_comp_wl_input_touch_check(res)) continue;
        wl_touch_send_motion(res, ev->timestamp, ev->device, x, y);
     }
}


static void
_e_comp_wl_client_priority_adjust(int pid, int set, int adj, Eina_Bool use_adj, Eina_Bool adj_child, Eina_Bool do_child)
{
   Eina_List *files;
   char *file, buff[PATH_MAX];
   FILE *f;
   int pid2, ppid;
   int num_read;
   int n;

   if (use_adj)
      n = (getpriority(PRIO_PROCESS, pid) + adj);
   else
      n = set;
   setpriority(PRIO_PROCESS, pid, n);

   if (adj_child)
      use_adj = EINA_TRUE;

   if (!do_child)
      return;

   files = ecore_file_ls("/proc");
   EINA_LIST_FREE(files, file)
      {
         if (!isdigit(file[0]))
            continue;

         snprintf(buff, sizeof(buff), "/proc/%s/stat", file);
         if ((f = fopen(buff, "r")))
            {
               pid2 = -1;
               ppid = -1;
               num_read = fscanf(f, "%i %*s %*s %i %*s", &pid2, &ppid);
               fclose(f);
               if (num_read == 2 && ppid == pid)
                  _e_comp_wl_client_priority_adjust(pid2, set,
                                                    adj, use_adj,
                                                    adj_child, do_child);
            }

         free(file);
      }
}

static void
_e_comp_wl_client_priority_raise(E_Client *ec)
{
   if (ec->netwm.pid <= 0) return;
   if (ec->netwm.pid == getpid()) return;
   _e_comp_wl_client_priority_adjust(ec->netwm.pid,
                                     e_config->priority - 1, -1,
                                     EINA_FALSE, EINA_TRUE, EINA_FALSE);
}

static void
_e_comp_wl_client_priority_normal(E_Client *ec)
{
   if (ec->netwm.pid <= 0) return;
   if (ec->netwm.pid == getpid()) return;
   _e_comp_wl_client_priority_adjust(ec->netwm.pid, e_config->priority, 1,
                                     EINA_FALSE, EINA_TRUE, EINA_FALSE);
}

static void
_e_comp_wl_client_focus(E_Client *ec)
{
   struct wl_resource *res;
   struct wl_client *wc;
   uint32_t serial, *k;
   Eina_List *l;

   /* update keyboard modifier state */
   wl_array_for_each(k, &e_comp->wl_comp_data->kbd.keys)
     e_comp_wl_input_keyboard_state_update(e_comp->wl_comp_data, *k, EINA_TRUE);

   ec->comp_data->focus_update = 1;
   if (!ec->comp_data->surface) return;

   /* send keyboard_enter to all keyboard resources */
   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(e_comp->wl_comp_data->wl.disp);
   EINA_LIST_FOREACH(e_comp->wl_comp_data->kbd.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        wl_keyboard_send_enter(res, serial, ec->comp_data->surface,
                               &e_comp->wl_comp_data->kbd.keys);
        ec->comp_data->focus_update = 0;
     }
}

static void
_e_comp_wl_evas_cb_focus_in(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec, *focused;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (ec->iconic) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   /* block spurious focus events */
   focused = e_client_focused_get();
   if ((focused) && (ec != focused)) return;

   /* raise client priority */
   _e_comp_wl_client_priority_raise(ec);

   _e_comp_wl_client_focus(ec);
}

static void
_e_comp_wl_evas_cb_focus_out(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;
   E_Comp_Data *cdata;
   struct wl_resource *res;
   struct wl_client *wc;
   uint32_t serial, *k;
   Eina_List *l;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   /* lower client priority */
   _e_comp_wl_client_priority_normal(ec);

   cdata = ec->comp->wl_comp_data;

   /* update keyboard modifier state */
   wl_array_for_each(k, &cdata->kbd.keys)
     e_comp_wl_input_keyboard_state_update(cdata, *k, EINA_FALSE);

   if (!ec->comp_data->surface) return;

   /* send keyboard_leave to all keyboard resources */
   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(cdata->wl.disp);
   EINA_LIST_FOREACH(cdata->kbd.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        wl_keyboard_send_leave(res, serial, ec->comp_data->surface);
     }
   ec->comp_data->focus_update = 0;
}

static void
_e_comp_wl_evas_cb_resize(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   if ((ec->shading) || (ec->shaded)) return;
   if (!ec->comp_data->shell.configure_send) return;

   /* TODO: calculate x, y with transfrom object */
   if ((e_client_util_resizing_get(ec)) && (!ec->transformed))
     {
        int x, y, ax, ay;

        x = ec->mouse.last_down[ec->moveinfo.down.button - 1].w;
        y = ec->mouse.last_down[ec->moveinfo.down.button - 1].h;
        if (ec->comp_data->shell.window.w && ec->comp_data->shell.window.h)
          {
             ax = ec->client.w - ec->comp_data->shell.window.w;
             ay = ec->client.h - ec->comp_data->shell.window.h;
          }
        else
          ax = ay = 0;

        switch (ec->resize_mode)
          {
           case E_POINTER_RESIZE_TL:
           case E_POINTER_RESIZE_L:
           case E_POINTER_RESIZE_BL:
             x += ec->mouse.last_down[ec->moveinfo.down.button - 1].mx -
               ec->mouse.current.mx - ec->comp_data->shell.window.x;
             break;
           case E_POINTER_RESIZE_TR:
           case E_POINTER_RESIZE_R:
           case E_POINTER_RESIZE_BR:
             x += ec->mouse.current.mx - ec->mouse.last_down[ec->moveinfo.down.button - 1].mx -
               ec->comp_data->shell.window.x;
             break;
           default:
             x -= ax;
          }
        switch (ec->resize_mode)
          {
           case E_POINTER_RESIZE_TL:
           case E_POINTER_RESIZE_T:
           case E_POINTER_RESIZE_TR:
             y += ec->mouse.last_down[ec->moveinfo.down.button - 1].my -
               ec->mouse.current.my - ec->comp_data->shell.window.y;
             break;
           case E_POINTER_RESIZE_BL:
           case E_POINTER_RESIZE_B:
           case E_POINTER_RESIZE_BR:
             y += ec->mouse.current.my - ec->mouse.last_down[ec->moveinfo.down.button - 1].my -
               ec->comp_data->shell.window.y;
             break;
           default:
             y -= ay;
          }

        ec->comp_data->shell.configure_send(ec->comp_data->shell.surface,
                                 ec->comp->wl_comp_data->resize.edges,
                                 x, y);
     }
   else
     ec->comp_data->shell.configure_send(ec->comp_data->shell.surface,
                                 ec->comp->wl_comp_data->resize.edges,
                                 ec->client.w, ec->client.h);

   if (ec->comp_data->sub.below_obj)
     evas_object_resize(ec->comp_data->sub.below_obj, ec->w, ec->h);
}

static void
_e_comp_wl_evas_cb_state_update(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec = data;

   if (e_object_is_del(E_OBJECT(ec))) return;

   /* check for wayland pixmap */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   if (ec->comp_data->shell.configure_send)
     ec->comp_data->shell.configure_send(ec->comp_data->shell.surface, 0, 0, 0);
}

static void
_e_comp_wl_evas_cb_delete_request(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;
   if (ec->netwm.ping) e_client_ping(ec);

   e_comp_ignore_win_del(E_PIXMAP_TYPE_WL, e_pixmap_window_get(ec->pixmap));

   e_object_del(E_OBJECT(ec));

   _e_comp_wl_focus_check(ec->comp);

   /* TODO: Delete request send ??
    * NB: No such animal wrt wayland */
}

static void
_e_comp_wl_evas_cb_kill_request(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;
   /* if (ec->netwm.ping) e_client_ping(ec); */

   e_comp_ignore_win_del(E_PIXMAP_TYPE_WL, e_pixmap_window_get(ec->pixmap));
   if (ec->comp_data)
     {
        if (ec->comp_data->reparented)
          e_client_comp_hidden_set(ec, EINA_TRUE);
     }

   evas_object_pass_events_set(ec->frame, EINA_TRUE);
   if (ec->visible) evas_object_hide(ec->frame);
   if (!ec->internal) e_object_del(E_OBJECT(ec));

   _e_comp_wl_focus_check(ec->comp);
}

static void
_e_comp_wl_evas_cb_ping(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;
   if (!(ec->comp_data->shell.ping)) return;
   if (!(ec->comp_data->shell.surface)) return;

   ec->comp_data->shell.ping(ec->comp_data->shell.surface);
}

static void
_e_comp_wl_evas_cb_color_set(void *data, Evas_Object *obj, void *event EINA_UNUSED)
{
   E_Client *ec;
   int a = 0;

   if (!(ec = data)) return;
   evas_object_color_get(obj, NULL, NULL, NULL, &a);
   if (ec->netwm.opacity == a) return;
   ec->netwm.opacity = a;
   ec->netwm.opacity_changed = EINA_TRUE;
}

static void
_e_comp_wl_buffer_reference_cb_destroy(struct wl_listener *listener, void *data)
{
   E_Comp_Wl_Buffer_Ref *ref;

   ref = container_of(listener, E_Comp_Wl_Buffer_Ref, destroy_listener);
   if ((E_Comp_Wl_Buffer *)data != ref->buffer) return;
   ref->buffer = NULL;
}

static void
_e_comp_wl_buffer_cb_destroy(struct wl_listener *listener, void *data EINA_UNUSED)
{
   E_Comp_Wl_Buffer *buffer;
   E_Client *ec;

   buffer = container_of(listener, E_Comp_Wl_Buffer, destroy_listener);

   if ((ec = eina_hash_find(clients_buffer_hash, &buffer->resource)))
     {
        eina_hash_del_by_key(clients_buffer_hash, &buffer->resource);
        if (e_object_is_del(E_OBJECT(ec)))
          {
             /* clear comp object immediately */
             e_comp_object_redirected_set(ec->frame, 0);
             evas_object_del(ec->frame);
             ec->frame = NULL;
          }
        e_object_unref(E_OBJECT(ec));
     }

   wl_signal_emit(&buffer->destroy_signal, buffer);
   free(buffer);
}

static void
_e_comp_wl_client_evas_init(E_Client *ec)
{
   if (ec->comp_data->evas_init) return;

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW,        _e_comp_wl_evas_cb_show,        ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE,        _e_comp_wl_evas_cb_hide,        ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOVE,        _e_comp_wl_evas_cb_move,        ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESTACK,     _e_comp_wl_evas_cb_restack,     ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOUSE_IN,    _e_comp_wl_evas_cb_mouse_in,    ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOUSE_OUT,   _e_comp_wl_evas_cb_mouse_out,   ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOUSE_MOVE,  _e_comp_wl_evas_cb_mouse_move,  ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOUSE_DOWN,  _e_comp_wl_evas_cb_mouse_down,  ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOUSE_UP,    _e_comp_wl_evas_cb_mouse_up,    ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOUSE_WHEEL, _e_comp_wl_evas_cb_mouse_wheel, ec);

   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MULTI_DOWN, EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_multi_down, ec);
   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MULTI_UP,   EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_multi_up,   ec);
   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MULTI_MOVE, EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_multi_move, ec);

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_FOCUS_IN,    _e_comp_wl_evas_cb_focus_in,    ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_FOCUS_OUT,   _e_comp_wl_evas_cb_focus_out,   ec);

   if (!ec->override)
     {
        evas_object_smart_callback_add(ec->frame, "client_resize",   _e_comp_wl_evas_cb_resize,       ec);
        evas_object_smart_callback_add(ec->frame, "maximize_done",   _e_comp_wl_evas_cb_state_update, ec);
        evas_object_smart_callback_add(ec->frame, "unmaximize_done", _e_comp_wl_evas_cb_state_update, ec);
        evas_object_smart_callback_add(ec->frame, "fullscreen",      _e_comp_wl_evas_cb_state_update, ec);
        evas_object_smart_callback_add(ec->frame, "unfullscreen",    _e_comp_wl_evas_cb_state_update, ec);
     }

   /* setup delete/kill callbacks */
   evas_object_smart_callback_add(ec->frame, "delete_request", _e_comp_wl_evas_cb_delete_request, ec);
   evas_object_smart_callback_add(ec->frame, "kill_request",   _e_comp_wl_evas_cb_kill_request,   ec);

   /* setup ping callback */
   evas_object_smart_callback_add(ec->frame, "ping",           _e_comp_wl_evas_cb_ping,           ec);
   evas_object_smart_callback_add(ec->frame, "color_set",      _e_comp_wl_evas_cb_color_set,      ec);

   ec->comp_data->evas_init = EINA_TRUE;
}

#ifndef HAVE_WAYLAND_ONLY
static Eina_Bool
_e_comp_wl_cb_randr_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   Eina_List *l;
   E_Randr2_Screen *screen;
   unsigned int transform = WL_OUTPUT_TRANSFORM_NORMAL;

   EINA_LIST_FOREACH(e_randr2->screens, l, screen)
     {
        if (!screen->config.enabled) continue;
        switch (screen->config.rotation)
          {
           case 90:
             transform = WL_OUTPUT_TRANSFORM_90;
             break;
           case 180:
             transform = WL_OUTPUT_TRANSFORM_180;
             break;
           case 270:
             transform = WL_OUTPUT_TRANSFORM_270;
             break;
           case 0:
           default:
             transform = WL_OUTPUT_TRANSFORM_NORMAL;
             break;
          }

        e_comp_wl_output_init(screen->id, screen->info.screen,
                              screen->info.name,
                              screen->config.geom.x, screen->config.geom.y,
                              screen->config.geom.w, screen->config.geom.h,
                              screen->info.size.w, screen->info.size.h,
                              screen->config.mode.refresh, 0, transform);
     }

   return ECORE_CALLBACK_RENEW;
}
#endif

static Eina_Bool
_e_comp_wl_cb_comp_object_add(void *data EINA_UNUSED, int type EINA_UNUSED, E_Event_Comp_Object *ev)
{
   E_Client *ec;

   /* try to get the client from the object */
   if (!(ec = e_comp_object_client_get(ev->comp_object)))
     return ECORE_CALLBACK_RENEW;

   /* check for client being deleted */
   if (e_object_is_del(E_OBJECT(ec))) return ECORE_CALLBACK_RENEW;

   /* check for wayland pixmap */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return ECORE_CALLBACK_RENEW;

   /* if we have not setup evas callbacks for this client, do it */
   if (!ec->comp_data->evas_init) _e_comp_wl_client_evas_init(ec);

   return ECORE_CALLBACK_RENEW;
}

static void
_e_comp_wl_cb_key_down(void *event)
{
   E_Comp_Data *cdata;
   E_Client *ec;
   Ecore_Event_Key *ev;
   uint32_t serial, *end, *k, keycode;

   ev = event;
   keycode = (ev->keycode - 8);
   if (!(cdata = e_comp->wl_comp_data)) return;

#ifdef HAVE_WAYLAND_ONLY
 #ifndef E_RELEASE_BUILD
   if ((ev->modifiers & ECORE_EVENT_MODIFIER_CTRL) &&
       ((ev->modifiers & ECORE_EVENT_MODIFIER_ALT) ||
       (ev->modifiers & ECORE_EVENT_MODIFIER_ALTGR)) &&
       eina_streq(ev->key, "BackSpace"))
     exit(0);
 #endif
#endif

   end = (uint32_t *)cdata->kbd.keys.data + (cdata->kbd.keys.size / sizeof(*k));

   for (k = cdata->kbd.keys.data; k < end; k++)
     {
        /* ignore server-generated key repeats */
        if (*k == keycode) return;
     }

   /* update modifier state */
   e_comp_wl_input_keyboard_state_update(cdata, keycode, EINA_TRUE);

   if ((!e_client_action_get()) && (!e_comp->input_key_grabs) && (!e_menu_grab_window_get()))
     {
        if ((ec = e_client_focused_get()))
          {
             if (ec->comp_data->surface)
               {
                  struct wl_client *wc;
                  struct wl_resource *res;
                  Eina_List *l;

                  wc = wl_resource_get_client(ec->comp_data->surface);
                  serial = wl_display_next_serial(cdata->wl.disp);
                  EINA_LIST_FOREACH(cdata->kbd.resources, l, res)
                    {
                       if (wl_resource_get_client(res) != wc) continue;
                       wl_keyboard_send_key(res, serial, ev->timestamp,
                                            keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
                    }

                  /* A key only sent to clients is added to the list */
                  cdata->kbd.keys.size = (const char *)end - (const char *)cdata->kbd.keys.data;
                  k = wl_array_add(&cdata->kbd.keys, sizeof(*k));
                  *k = keycode;
               }
          }
     }

   if (cdata->kbd.mod_changed)
     {
        e_comp_wl_input_keyboard_modifiers_update(cdata);
        cdata->kbd.mod_changed = 0;
     }
}

static void
_e_comp_wl_cb_key_up(void *event)
{
   E_Client *ec;
   E_Comp_Data *cdata;
   Ecore_Event_Key *ev;
   uint32_t serial, *end, *k, keycode;
   uint32_t delivered_key;

   ev = event;
   keycode = (ev->keycode - 8);
   delivered_key = 0;
   if (!(cdata = e_comp->wl_comp_data)) return;

   end = (uint32_t *)cdata->kbd.keys.data + (cdata->kbd.keys.size / sizeof(*k));
   for (k = cdata->kbd.keys.data; k < end; k++)
     {
        if (*k == keycode)
          {
             *k = *--end;
             delivered_key = 1;
          }
     }

   cdata->kbd.keys.size = (const char *)end - (const char *)cdata->kbd.keys.data;

   /* update modifier state */
   e_comp_wl_input_keyboard_state_update(cdata, keycode, EINA_FALSE);

   /* If a key down event have been sent to clients, send a key up event to client for garantee key event sequence pair. (down/up) */
   if (delivered_key || ((!e_client_action_get()) && (!e_comp->input_key_grabs) && (!e_menu_grab_window_get())))
     {
        if ((ec = e_client_focused_get()))
          {
             if (ec->comp_data->surface)
               {
                  struct wl_client *wc;
                  struct wl_resource *res;
                  Eina_List *l;

                  wc = wl_resource_get_client(ec->comp_data->surface);
                  serial = wl_display_next_serial(cdata->wl.disp);
                  EINA_LIST_FOREACH(cdata->kbd.resources, l, res)
                    {
                       if (wl_resource_get_client(res) != wc) continue;
                       wl_keyboard_send_key(res, serial, ev->timestamp,
                                            keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
                    }
               }
          }
     }

   if (cdata->kbd.mod_changed)
     {
        e_comp_wl_input_keyboard_modifiers_update(cdata);
        cdata->kbd.mod_changed = 0;
     }
}

static Eina_Bool
_e_comp_wl_cb_input_event(void *data EINA_UNUSED, int type, void *ev)
{
   _last_event_time = ecore_loop_time_get();

   if (type == ECORE_EVENT_KEY_DOWN)
     _e_comp_wl_cb_key_down(ev);
   else if (type == ECORE_EVENT_KEY_UP)
     _e_comp_wl_cb_key_up(ev);

   return ECORE_CALLBACK_RENEW;
}

static void
_e_comp_wl_subsurface_restack(E_Client *ec)
{
   E_Client *subc, *temp;
   Eina_List *l;

   ec->comp_data->sub.restacking = EINA_TRUE;

   temp = ec;
   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     {
        evas_object_stack_above(subc->frame, temp->frame);
        temp = subc;
     }

   temp = ec;
   EINA_LIST_REVERSE_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        evas_object_stack_below(subc->frame, temp->frame);
        temp = subc;
     }

   if (ec->comp_data->sub.below_obj)
     evas_object_stack_below(ec->comp_data->sub.below_obj, temp->frame);

   ec->comp_data->sub.restacking = EINA_FALSE;
}

static void
_e_comp_wl_surface_subsurface_order_commit(E_Client *ec)
{
   E_Client *subc;
   Eina_List *l;

   if (!ec->comp_data->sub.list_changed) return;
   ec->comp_data->sub.list_changed = EINA_FALSE;

   /* TODO: need to check more complicated subsurface tree */
   EINA_LIST_FOREACH(ec->comp_data->sub.list_pending, l, subc)
     {
        ec->comp_data->sub.list = eina_list_remove(ec->comp_data->sub.list, subc);
        ec->comp_data->sub.list = eina_list_append(ec->comp_data->sub.list, subc);

        _e_comp_wl_surface_subsurface_order_commit(subc);
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list_pending, l, subc)
     {
        ec->comp_data->sub.below_list = eina_list_remove(ec->comp_data->sub.below_list, subc);
        ec->comp_data->sub.below_list = eina_list_append(ec->comp_data->sub.below_list, subc);

        _e_comp_wl_surface_subsurface_order_commit(subc);
     }

   _e_comp_wl_subsurface_restack(ec);
}

static void
_e_comp_wl_surface_state_size_update(E_Client *ec, E_Comp_Wl_Surface_State *state)
{
   int w = 0, h = 0;
   /* double scale = 0.0; */

   if (!ec->comp_data->buffer_ref.buffer)
     {
        state->bw = 0;
        state->bh = 0;
        return;
     }

   /* scale = e_comp->wl_comp_data->output.scale; */
   /* switch (e_comp->wl_comp_data->output.transform) */
   /*   { */
   /*    case WL_OUTPUT_TRANSFORM_90: */
   /*    case WL_OUTPUT_TRANSFORM_270: */
   /*    case WL_OUTPUT_TRANSFORM_FLIPPED_90: */
   /*    case WL_OUTPUT_TRANSFORM_FLIPPED_270: */
   /*      w = ec->comp_data->buffer_ref.buffer->h / scale; */
   /*      h = ec->comp_data->buffer_ref.buffer->w / scale; */
   /*      break; */
   /*    default: */
   /*      w = ec->comp_data->buffer_ref.buffer->w / scale; */
   /*      h = ec->comp_data->buffer_ref.buffer->h / scale; */
   /*      break; */
   /*   } */

   w = ec->comp_data->buffer_ref.buffer->w;
   h = ec->comp_data->buffer_ref.buffer->h;

   state->bw = w;
   state->bh = h;
}

static void
_e_comp_wl_surface_state_cb_buffer_destroy(struct wl_listener *listener, void *data EINA_UNUSED)
{
   E_Comp_Wl_Surface_State *state;

   state =
     container_of(listener, E_Comp_Wl_Surface_State, buffer_destroy_listener);
   state->buffer = NULL;
}

static void
_e_comp_wl_surface_state_init(E_Comp_Wl_Surface_State *state, int w, int h)
{
   state->new_attach = EINA_FALSE;
   state->buffer = NULL;
   state->buffer_destroy_listener.notify =
     _e_comp_wl_surface_state_cb_buffer_destroy;
   state->sx = state->sy = 0;

   state->input = eina_tiler_new(w, h);
   eina_tiler_tile_size_set(state->input, 1, 1);

   state->opaque = eina_tiler_new(w, h);
   eina_tiler_tile_size_set(state->opaque, 1, 1);

   state->buffer_viewport.buffer.transform = WL_OUTPUT_TRANSFORM_NORMAL;
   state->buffer_viewport.buffer.scale = 1;
   state->buffer_viewport.buffer.src_width = wl_fixed_from_int(-1);
   state->buffer_viewport.surface.width = -1;
   state->buffer_viewport.changed = 0;
}

static void
_e_comp_wl_surface_state_finish(E_Comp_Wl_Surface_State *state)
{
   struct wl_resource *cb;
   Eina_Rectangle *dmg;

   EINA_LIST_FREE(state->frames, cb)
     wl_resource_destroy(cb);

   EINA_LIST_FREE(state->damages, dmg)
     eina_rectangle_free(dmg);

   if (state->opaque) eina_tiler_free(state->opaque);
   state->opaque = NULL;

   if (state->input) eina_tiler_free(state->input);
   state->input = NULL;

   if (state->buffer) wl_list_remove(&state->buffer_destroy_listener.link);
   state->buffer = NULL;
}

static void
_e_comp_wl_surface_state_buffer_set(E_Comp_Wl_Surface_State *state, E_Comp_Wl_Buffer *buffer)
{
   if (state->buffer == buffer) return;
   if (state->buffer)
     wl_list_remove(&state->buffer_destroy_listener.link);
   state->buffer = buffer;
   if (state->buffer)
     wl_signal_add(&state->buffer->destroy_signal,
                   &state->buffer_destroy_listener);
}

static void
_e_comp_wl_surface_state_commit(E_Client *ec, E_Comp_Wl_Surface_State *state)
{
   Eina_Bool first = EINA_FALSE;
   Eina_Rectangle *dmg;
   Eina_List *l;
   struct wl_resource *cb;
   Eina_Bool placed = EINA_TRUE;
   int x = 0, y = 0;

   first = !e_pixmap_usable_get(ec->pixmap);

   ec->comp_data->scaler.buffer_viewport = state->buffer_viewport;

   if (state->new_attach)
     e_comp_wl_surface_attach(ec, state->buffer);

   _e_comp_wl_surface_state_buffer_set(state, NULL);

   if (state->new_attach || state->buffer_viewport.changed)
     {
        _e_comp_wl_surface_state_size_update(ec, state);
        _e_comp_wl_map_size_cal_from_viewport(ec);

        if (ec->changes.pos)
          e_comp_object_frame_xy_adjust(ec->frame, ec->x, ec->y, &x, &y);
        else
          x = ec->client.x, y = ec->client.y;

        if (ec->new_client) placed = ec->placed;

        if (!ec->lock_client_size)
          {
             ec->w = ec->client.w = state->bw;
             ec->h = ec->client.h = state->bh;
          }
     }
   if (!e_pixmap_usable_get(ec->pixmap))
     {
        if (ec->comp_data->mapped)
          {
             if ((ec->comp_data->shell.surface) && (ec->comp_data->shell.unmap))
               ec->comp_data->shell.unmap(ec->comp_data->shell.surface);
             else
               {
                  evas_object_hide(ec->frame);
                  ec->comp_data->mapped = EINA_FALSE;
               }
          }

        if (ec->comp_data->sub.below_obj && evas_object_visible_get(ec->comp_data->sub.below_obj))
          evas_object_hide(ec->comp_data->sub.below_obj);
     }
   else
     {
        if (!ec->comp_data->mapped)
          {
             if ((ec->comp_data->shell.surface) && (ec->comp_data->shell.map))
               ec->comp_data->shell.map(ec->comp_data->shell.surface);
             else
               {
                  evas_object_show(ec->frame);
                  ec->comp_data->mapped = EINA_TRUE;
               }
          }

        if (ec->comp_data->sub.below_obj && !evas_object_visible_get(ec->comp_data->sub.below_obj))
          evas_object_show(ec->comp_data->sub.below_obj);
     }

   if (state->new_attach || state->buffer_viewport.changed)
     {
        if ((ec->comp_data->shell.surface) && (ec->comp_data->shell.configure))
          ec->comp_data->shell.configure(ec->comp_data->shell.surface,
                                         x, y, ec->w, ec->h);
        else
          e_client_util_move_resize_without_frame(ec, x, y, ec->w, ec->h);

        if (ec->new_client)
          ec->placed = placed;
        else if ((first) && (ec->placed))
          {
             ec->x = ec->y = 0;
             ec->placed = EINA_FALSE;
             ec->new_client = EINA_TRUE;
          }
     }

   if (state->buffer_viewport.changed)
     _e_comp_wl_map_apply(ec);

   /* resize transform object */
   if (ec->transformed)
     e_client_transform_update(ec);

   state->sx = 0;
   state->sy = 0;
   state->new_attach = EINA_FALSE;
   state->buffer_viewport.changed = 0;

   if (!ec->comp_data->mapped) goto unmapped;

   /* put state damages into surface */
   if ((!ec->comp->nocomp) && (ec->frame))
     {
        /* FIXME: workaround for bad wayland egl driver which doesn't send damage request */
        if (!eina_list_count(state->damages))
          {
             if (ec->comp_data->buffer_ref.buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE)
               {
                  e_comp_object_damage(ec->frame,
                                       0, 0,
                                       ec->comp_data->buffer_ref.buffer->w,
                                       ec->comp_data->buffer_ref.buffer->h);
               }
          }
        else
          {
             EINA_LIST_FREE(state->damages, dmg)
               {
                  /* not creating damage for ec that shows a underlay video */
                  if (ec->comp_data->buffer_ref.buffer->type != E_COMP_WL_BUFFER_TYPE_VIDEO ||
                      !e_comp->wl_comp_data->available_hw_accel.underlay)
                    e_comp_object_damage(ec->frame, dmg->x, dmg->y, dmg->w, dmg->h);

                  eina_rectangle_free(dmg);
               }
          }
     }

   /* put state opaque into surface */
   if (state->opaque)
     {
        Eina_Rectangle *rect;
        Eina_Iterator *itr;

        itr = eina_tiler_iterator_new(state->opaque);
        EINA_ITERATOR_FOREACH(itr, rect)
          {
             e_pixmap_image_opaque_set(ec->pixmap, rect->x, rect->y,
                                       rect->w, rect->h);
             break;
          }

        eina_iterator_free(itr);
     }
   else
     e_pixmap_image_opaque_set(ec->pixmap, 0, 0, 0, 0);

   /* put state input into surface */
   if (state->input)
     {
        Eina_Tiler *src, *tmp;

        tmp = eina_tiler_new(state->bw, state->bh);
        eina_tiler_tile_size_set(tmp, 1, 1);
        eina_tiler_rect_add(tmp,
                            &(Eina_Rectangle){0, 0, state->bw, state->bh});
        if ((src = eina_tiler_intersection(state->input, tmp)))
          {
             Eina_Rectangle *rect;
             Eina_Iterator *itr;

             itr = eina_tiler_iterator_new(src);
             EINA_ITERATOR_FOREACH(itr, rect)
               e_comp_object_input_area_set(ec->frame, rect->x, rect->y,
                                            rect->w, rect->h);

             eina_iterator_free(itr);
             eina_tiler_free(src);
          }
        else
          e_comp_object_input_area_set(ec->frame, 0, 0, ec->w, ec->h);

        eina_tiler_free(tmp);
     }

   /* insert state frame callbacks into comp_data->frames
    * NB: This clears state->frames list */
   EINA_LIST_FOREACH(state->frames, l, cb)
     eina_list_move(&ec->comp_data->frames, &state->frames, cb);

   if (ec->comp_data->buffer_ref.buffer->type == E_COMP_WL_BUFFER_TYPE_VIDEO &&
       e_comp->wl_comp_data->available_hw_accel.underlay)
     e_pixmap_image_clear(ec->pixmap, 1);

   return;

unmapped:
   /* clear pending damages */
   EINA_LIST_FREE(state->damages, dmg)
     eina_rectangle_free(dmg);

   /* clear input tiler */
   if (state->input)
     eina_tiler_clear(state->input);
}

static void
_e_comp_wl_surface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   DBG("Surface Cb Destroy: %d", wl_resource_get_id(resource));
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_surface_cb_attach(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *buffer_resource, int32_t sx, int32_t sy)
{
   E_Pixmap *ep;
   E_Client *ec;
   E_Comp_Wl_Buffer *buffer = NULL;

   if (!(ep = wl_resource_get_user_data(resource))) return;
   if (!(ec = e_pixmap_client_get(ep))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (buffer_resource)
     {
        if (!(buffer = e_comp_wl_buffer_get(buffer_resource, resource)))
          {
             ERR("Could not get buffer from resource");
             wl_client_post_no_memory(client);
             return;
          }

        /* ref only if it's first buffer of client */
        if (!eina_hash_del_by_data(clients_buffer_hash, ec))
          e_object_ref(E_OBJECT(ec));
        eina_hash_add(clients_buffer_hash, &buffer_resource, ec);
     }

   _e_comp_wl_surface_state_buffer_set(&ec->comp_data->pending, buffer);

   ec->comp_data->pending.sx = sx;
   ec->comp_data->pending.sy = sy;
   ec->comp_data->pending.new_attach = EINA_TRUE;
}

static void
_e_comp_wl_surface_cb_damage(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   E_Pixmap *ep;
   E_Client *ec;
   Eina_Rectangle *dmg = NULL;

   if (!(ep = wl_resource_get_user_data(resource))) return;
   if (!(ec = e_pixmap_client_get(ep))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!(dmg = eina_rectangle_new(x, y, w, h))) return;

   ec->comp_data->pending.damages =
     eina_list_append(ec->comp_data->pending.damages, dmg);
}

static void
_e_comp_wl_frame_cb_destroy(struct wl_resource *resource)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   ec->comp_data->frames =
     eina_list_remove(ec->comp_data->frames, resource);

   ec->comp_data->pending.frames =
     eina_list_remove(ec->comp_data->pending.frames, resource);
}

static void
_e_comp_wl_surface_cb_frame(struct wl_client *client, struct wl_resource *resource, uint32_t callback)
{
   E_Pixmap *ep;
   E_Client *ec;
   struct wl_resource *res;

   if (!(ep = wl_resource_get_user_data(resource))) return;
   if (!(ec = e_pixmap_client_get(ep))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   /* create frame callback */
   if (!(res =
         wl_resource_create(client, &wl_callback_interface, 1, callback)))
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   wl_resource_set_implementation(res, NULL, ec, _e_comp_wl_frame_cb_destroy);

   ec->comp_data->pending.frames =
     eina_list_prepend(ec->comp_data->pending.frames, res);
}

static void
_e_comp_wl_surface_cb_opaque_region_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *region_resource)
{
   E_Pixmap *ep;
   E_Client *ec;

   if (!(ep = wl_resource_get_user_data(resource))) return;
   if (!(ec = e_pixmap_client_get(ep))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (region_resource)
     {
        Eina_Tiler *tmp;

        if (!(tmp = wl_resource_get_user_data(region_resource)))
          return;

        eina_tiler_union(ec->comp_data->pending.opaque, tmp);

        if (!eina_tiler_empty(ec->comp_data->pending.opaque))
          {
             if (ec->argb)
               {
                  ec->argb = EINA_FALSE;
                  e_comp_object_alpha_set(ec->frame, EINA_FALSE);
               }
          }
     }
   else
     {
        if (ec->comp_data->pending.opaque)
          {
             eina_tiler_clear(ec->comp_data->pending.opaque);
             /* eina_tiler_free(ec->comp_data->pending.opaque); */
          }
        if (!ec->argb)
          {
             ec->argb = EINA_TRUE;
             e_comp_object_alpha_set(ec->frame, EINA_TRUE);
          }
     }
}

static void
_e_comp_wl_surface_cb_input_region_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *region_resource)
{
   E_Pixmap *ep;
   E_Client *ec;

   if (!(ep = wl_resource_get_user_data(resource))) return;
   if (!(ec = e_pixmap_client_get(ep))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (region_resource)
     {
        Eina_Tiler *tmp;

        if (!(tmp = wl_resource_get_user_data(region_resource)))
          return;

        eina_tiler_union(ec->comp_data->pending.input, tmp);
     }
   else
     {
        eina_tiler_rect_add(ec->comp_data->pending.input,
                            &(Eina_Rectangle){0, 0, ec->client.w, ec->client.h});
     }
}

static void
_e_comp_wl_surface_cb_commit(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Pixmap *ep;
   E_Client *ec, *subc;
   Eina_List *l;

   if (!(ep = wl_resource_get_user_data(resource))) return;
   if (!(ec = e_pixmap_client_get(ep))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (e_comp_wl_subsurface_commit(ec)) return;

   e_comp_wl_surface_commit(ec);

   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     {
        if (ec != subc)
          _e_comp_wl_subsurface_parent_commit(subc, EINA_FALSE);
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        if (ec != subc)
          _e_comp_wl_subsurface_parent_commit(subc, EINA_FALSE);
     }
}

static void
_e_comp_wl_surface_cb_buffer_transform_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t transform)
{
   E_Pixmap *ep;
   E_Client *ec;

   if (!(ep = wl_resource_get_user_data(resource))) return;
   if (!(ec = e_pixmap_client_get(ep))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (transform < 0 || transform > WL_OUTPUT_TRANSFORM_FLIPPED_270)
     {
        wl_resource_post_error(resource,
                               WL_SURFACE_ERROR_INVALID_TRANSFORM,
                               "buffer transform must be a valid transform "
                               "('%d' specified)", transform);
        return;
     }

   ec->comp_data->pending.buffer_viewport.buffer.transform = transform;
   ec->comp_data->pending.buffer_viewport.changed = 1;
}

static void
_e_comp_wl_surface_cb_buffer_scale_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t scale)
{
   E_Pixmap *ep;
   E_Client *ec;

   if (!(ep = wl_resource_get_user_data(resource))) return;
   if (!(ec = e_pixmap_client_get(ep))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (scale < 1)
{
        wl_resource_post_error(resource,
                               WL_SURFACE_ERROR_INVALID_SCALE,
                               "buffer scale must be at least one "
                               "('%d' specified)", scale);
        return;
     }

   ec->comp_data->pending.buffer_viewport.buffer.scale = scale;
   ec->comp_data->pending.buffer_viewport.changed = 1;
}

static const struct wl_surface_interface _e_surface_interface =
{
   _e_comp_wl_surface_cb_destroy,
   _e_comp_wl_surface_cb_attach,
   _e_comp_wl_surface_cb_damage,
   _e_comp_wl_surface_cb_frame,
   _e_comp_wl_surface_cb_opaque_region_set,
   _e_comp_wl_surface_cb_input_region_set,
   _e_comp_wl_surface_cb_commit,
   _e_comp_wl_surface_cb_buffer_transform_set,
   _e_comp_wl_surface_cb_buffer_scale_set
};

static void
_e_comp_wl_surface_destroy(struct wl_resource *resource)
{
   E_Pixmap *ep;
   E_Client *ec;

   if (!(ep = wl_resource_get_user_data(resource))) return;

   /* try to get the e_client from this pixmap */
   if (!(ec = e_pixmap_client_get(ep)))
     {
        e_pixmap_free(ep);
        return;
     }
   else
     e_pixmap_del(ep);

   evas_object_hide(ec->frame);
   e_object_del(E_OBJECT(ec));
}

static void
_e_comp_wl_compositor_cb_surface_create(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   E_Comp *comp;
   struct wl_resource *res;
   E_Pixmap *ep = NULL;
   pid_t pid;

   if (!(comp = wl_resource_get_user_data(resource))) return;

   DBG("Compositor Cb Surface Create: %d", id);

   /* try to create an internal surface */
   if (!(res = wl_resource_create(client, &wl_surface_interface,
                                  wl_resource_get_version(resource), id)))
     {
        ERR("Could not create compositor surface");
        wl_client_post_no_memory(client);
        return;
     }

   DBG("\tCreated Resource: %d", wl_resource_get_id(res));

   /* set implementation on resource */
   wl_resource_set_implementation(res, &_e_surface_interface, NULL,
                                  _e_comp_wl_surface_destroy);

   wl_client_get_credentials(client, &pid, NULL, NULL);
   if (pid == getpid())
     {
        /* pixmap of internal win was supposed to be created at trap show */
        ep = e_pixmap_find(E_PIXMAP_TYPE_WL, (uintptr_t)id);
     }
   else
     {
        if ((ep = e_pixmap_find(E_PIXMAP_TYPE_WL, (uintptr_t)res)))
          {
             ERR("There is e_pixmap already, Delete old e_pixmap %p", ep);
             e_pixmap_del(ep);
             ep = NULL;
          }
     }

   if (!ep)
     {
        /* try to create new pixmap */
        if (!(ep = e_pixmap_new(E_PIXMAP_TYPE_WL, res)))
          {
             ERR("Could not create new pixmap");
             wl_resource_destroy(res);
             wl_client_post_no_memory(client);
             return;
          }
     }
   DBG("\tUsing Pixmap: %p", ep);

   /* set reference to pixmap so we can fetch it later */
   wl_resource_set_user_data(res, ep);

   E_Comp_Wl_Client_Data *cdata = e_pixmap_cdata_get(ep);
   if (cdata)
     cdata->wl_surface = res;

   /* emit surface create signal */
   wl_signal_emit(&comp->wl_comp_data->signals.surface.create, res);
}

static void
_e_comp_wl_region_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   DBG("Region Destroy: %d", wl_resource_get_id(resource));
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_region_cb_add(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   Eina_Tiler *tiler;

   DBG("Region Add: %d", wl_resource_get_id(resource));
   DBG("\tGeom: %d %d %d %d", x, y, w, h);

   /* get the tiler from the resource */
   if ((tiler = wl_resource_get_user_data(resource)))
     {
        Eina_Tiler *src;

        src = eina_tiler_new(w, h);
        eina_tiler_tile_size_set(src, 1, 1);
        eina_tiler_rect_add(src, &(Eina_Rectangle){x, y, w, h});
        eina_tiler_union(tiler, src);
        eina_tiler_free(src);
     }
}

static void
_e_comp_wl_region_cb_subtract(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   Eina_Tiler *tiler;

   DBG("Region Subtract: %d", wl_resource_get_id(resource));
   DBG("\tGeom: %d %d %d %d", x, y, w, h);

   /* get the tiler from the resource */
   if ((tiler = wl_resource_get_user_data(resource)))
     {
        Eina_Tiler *src;

        src = eina_tiler_new(w, h);
        eina_tiler_tile_size_set(src, 1, 1);
        eina_tiler_rect_add(src, &(Eina_Rectangle){x, y, w, h});

        eina_tiler_subtract(tiler, src);
        eina_tiler_free(src);
     }
}

static const struct wl_region_interface _e_region_interface =
{
   _e_comp_wl_region_cb_destroy,
   _e_comp_wl_region_cb_add,
   _e_comp_wl_region_cb_subtract
};

static void
_e_comp_wl_compositor_cb_region_destroy(struct wl_resource *resource)
{
   Eina_Tiler *tiler;

   DBG("Compositor Region Destroy: %d", wl_resource_get_id(resource));

   /* try to get the tiler from the region resource */
   if ((tiler = wl_resource_get_user_data(resource)))
     eina_tiler_free(tiler);
}

static void
_e_comp_wl_compositor_cb_region_create(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   E_Comp *comp;
   Eina_Tiler *tiler;
   struct wl_resource *res;

   /* get the compositor from the resource */
   if (!(comp = wl_resource_get_user_data(resource))) return;

   DBG("Region Create: %d", wl_resource_get_id(resource));

   /* try to create new tiler */
   if (!(tiler = eina_tiler_new(comp->man->w, comp->man->h)))
     {
        ERR("Could not create Eina_Tiler");
        wl_resource_post_no_memory(resource);
        return;
     }

   /* set tiler size */
   eina_tiler_tile_size_set(tiler, 1, 1);

   if (!(res = wl_resource_create(client, &wl_region_interface, 1, id)))
     {
        ERR("\tFailed to create region resource");
        wl_resource_post_no_memory(resource);
        return;
     }

   wl_resource_set_implementation(res, &_e_region_interface, tiler,
                                  _e_comp_wl_compositor_cb_region_destroy);
}

static const struct wl_compositor_interface _e_comp_interface =
{
   _e_comp_wl_compositor_cb_surface_create,
   _e_comp_wl_compositor_cb_region_create
};

static void
_e_comp_wl_pname_print(pid_t pid)
{
   FILE *h;
   char proc[512], pname[512];
   size_t len;

   snprintf(proc, 512,"/proc/%d/cmdline", pid);

   h = fopen(proc, "r");
   if (!h) return;

   len = fread(pname, sizeof(char), 512, h);
   if (len > 0)
     {
        if ('\n' == pname[len - 1])
          pname[len - 1] = '\0';
     }

   fclose(h);

   ELOGF("COMP", "         |%s", NULL, NULL, pname);
}


static void
_e_comp_wl_compositor_cb_unbind(struct wl_resource *res_comp)
{
   struct wl_client *client;
   pid_t pid = 0;
   uid_t uid = 0;
   gid_t gid = 0;

   client = wl_resource_get_client(res_comp);
   if (client)
     wl_client_get_credentials(client,
                               &pid,
                               &uid,
                               &gid);

   ELOGF("COMP",
         "UNBIND   |res_comp:0x%08x|client:0x%08x|%d|%d|%d",
         NULL, NULL,
         (unsigned int)res_comp,
         (unsigned int)client,
         pid, uid, gid);
}

static void
_e_comp_wl_compositor_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp *comp;
   struct wl_resource *res;
   pid_t pid = 0;
   uid_t uid = 0;
   gid_t gid = 0;

   if (!(comp = data)) return;

   if (!(res =
         wl_resource_create(client, &wl_compositor_interface,
                            MIN(version, COMPOSITOR_VERSION), id)))
     {
        ERR("Could not create compositor resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res,
                                  &_e_comp_interface,
                                  comp,
                                  _e_comp_wl_compositor_cb_unbind);

   wl_client_get_credentials(client, &pid, &uid, &gid);

   ELOGF("COMP",
         "BIND     |res_comp:0x%08x|client:0x%08x|%d|%d|%d",
         NULL, NULL,
         (unsigned int)res,
         (unsigned int)client,
         pid, uid, gid);

   _e_comp_wl_pname_print(pid);
}

static void
_e_comp_wl_compositor_cb_del(E_Comp *comp)
{
   E_Comp_Data *cdata;
   E_Comp_Wl_Output *output;

   /* get existing compositor data */
   if (!(cdata = comp->wl_comp_data)) return;

   EINA_LIST_FREE(cdata->outputs, output)
     {
        if (output->id) eina_stringshare_del(output->id);
        if (output->make) eina_stringshare_del(output->make);
        if (output->model) eina_stringshare_del(output->model);
        free(output);
     }

   /* delete fd handler */
   if (cdata->fd_hdlr) ecore_main_fd_handler_del(cdata->fd_hdlr);

   /* free allocated data structure */
   free(cdata);
}

static void
_e_comp_wl_subsurface_destroy(struct wl_resource *resource)
{
   E_Client *ec;
   E_Comp_Wl_Subsurf_Data *sdata;

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;

   if (!ec->comp_data) return;

   if (!(sdata = ec->comp_data->sub.data)) return;

   if (sdata->parent)
     {
        /* remove this client from parents sub list */
        sdata->parent->comp_data->sub.list =
          eina_list_remove(sdata->parent->comp_data->sub.list, ec);
        sdata->parent->comp_data->sub.list_pending =
          eina_list_remove(sdata->parent->comp_data->sub.list_pending, ec);
        sdata->parent->comp_data->sub.below_list =
          eina_list_remove(sdata->parent->comp_data->sub.below_list, ec);
        sdata->parent->comp_data->sub.below_list_pending =
          eina_list_remove(sdata->parent->comp_data->sub.below_list_pending, ec);
     }

   _e_comp_wl_surface_state_finish(&sdata->cached);
   e_comp_wl_buffer_reference(&sdata->cached_buffer_ref, NULL);

   /* the client is getting deleted, which means the pixmap will be getting
    * freed. We need to unset the surface user data */
   /* wl_resource_set_user_data(ec->comp_data->surface, NULL); */

   E_FREE(sdata);

   ec->comp_data->sub.data = NULL;
}

static Eina_Bool
_e_comp_wl_subsurface_synchronized_get(E_Comp_Wl_Subsurf_Data *sdata)
{
   while (sdata)
     {
        if (sdata->synchronized) return EINA_TRUE;
        if (!sdata->parent) return EINA_FALSE;
        sdata = sdata->parent->comp_data->sub.data;
     }

   return EINA_FALSE;
}

static void
_e_comp_wl_subsurface_bg_evas_cb_resize(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data) return;

   if (ec->comp_data->sub.below_obj)
      evas_object_resize(ec->comp_data->sub.below_obj, ec->w, ec->h);
}

static void
_e_comp_wl_subsurface_create_below_bg_rectangle(E_Client *ec)
{
   short layer;

   if (ec->comp_data->sub.below_obj) return;

   ec->comp_data->sub.below_obj = evas_object_rectangle_add(ec->comp->evas);
   EINA_SAFETY_ON_NULL_RETURN(ec->comp_data->sub.below_obj);

   layer = evas_object_layer_get(ec->frame);
   evas_object_layer_set(ec->comp_data->sub.below_obj, layer);
   evas_object_render_op_set(ec->comp_data->sub.below_obj, EVAS_RENDER_COPY);
   evas_object_color_set(ec->comp_data->sub.below_obj, 0x00, 0x00, 0x00, 0xff);
   evas_object_move(ec->comp_data->sub.below_obj, ec->x, ec->y);
   evas_object_resize(ec->comp_data->sub.below_obj, ec->w, ec->h);
   evas_object_name_set(ec->comp_data->sub.below_obj, "below_bg_rectangle");

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESIZE,
                                  _e_comp_wl_subsurface_bg_evas_cb_resize, ec);

   _e_comp_wl_subsurface_restack(ec);
}

static void
_e_comp_wl_subsurface_commit_to_cache(E_Client *ec)
{
   E_Comp_Client_Data *cdata;
   E_Comp_Wl_Subsurf_Data *sdata;
   struct wl_resource *cb;
   Eina_List *l;
   Eina_Iterator *itr;
   Eina_Rectangle *rect;

   if (!(cdata = ec->comp_data)) return;
   if (!(sdata = cdata->sub.data)) return;

   DBG("Subsurface Commit to Cache");

   /* move pending damage to cached */
   EINA_LIST_FOREACH(cdata->pending.damages, l, rect)
     eina_list_move(&sdata->cached.damages, &cdata->pending.damages, rect);

   if (cdata->pending.new_attach)
     {
        sdata->cached.new_attach = EINA_TRUE;
        _e_comp_wl_surface_state_buffer_set(&sdata->cached,
                                            cdata->pending.buffer);
        e_comp_wl_buffer_reference(&sdata->cached_buffer_ref,
                                   cdata->pending.buffer);
     }

   sdata->cached.sx = cdata->pending.sx;
   sdata->cached.sy = cdata->pending.sy;
   /* sdata->cached.buffer = cdata->pending.buffer; */
   sdata->cached.new_attach = cdata->pending.new_attach;

   sdata->cached.buffer_viewport.changed |= cdata->pending.buffer_viewport.changed;
   sdata->cached.buffer_viewport.buffer =cdata->pending.buffer_viewport.buffer;
   sdata->cached.buffer_viewport.surface = cdata->pending.buffer_viewport.surface;

   _e_comp_wl_surface_state_buffer_set(&cdata->pending, NULL);
   cdata->pending.sx = 0;
   cdata->pending.sy = 0;
   cdata->pending.new_attach = EINA_FALSE;
   cdata->pending.buffer_viewport.changed = 0;

   /* copy cdata->pending.opaque into sdata->cached.opaque */
   itr = eina_tiler_iterator_new(cdata->pending.opaque);
   EINA_ITERATOR_FOREACH(itr, rect)
     eina_tiler_rect_add(sdata->cached.opaque, rect);
   eina_iterator_free(itr);

   /* repeat for input */
   itr = eina_tiler_iterator_new(cdata->pending.input);
   EINA_ITERATOR_FOREACH(itr, rect)
     eina_tiler_rect_add(sdata->cached.input, rect);
   eina_iterator_free(itr);

   EINA_LIST_FOREACH(cdata->pending.frames, l, cb)
     eina_list_move(&sdata->cached.frames, &cdata->pending.frames, cb);

   sdata->cached.has_data = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_commit_from_cache(E_Client *ec)
{
   E_Comp_Client_Data *cdata;
   E_Comp_Wl_Subsurf_Data *sdata;

   if (!(cdata = ec->comp_data)) return;
   if (!(sdata = cdata->sub.data)) return;

   DBG("Subsurface Commit from Cache");

   _e_comp_wl_surface_state_commit(ec, &sdata->cached);

   e_comp_wl_buffer_reference(&sdata->cached_buffer_ref, NULL);

   _e_comp_wl_surface_subsurface_order_commit(ec);

   /* schedule repaint */
   if (e_pixmap_refresh(ec->pixmap))
     e_comp_post_update_add(ec);
}

static void
_e_comp_wl_subsurface_parent_commit(E_Client *ec, Eina_Bool parent_synchronized)
{
   E_Client *parent;
   E_Comp_Wl_Subsurf_Data *sdata;

   if (!(sdata = ec->comp_data->sub.data)) return;
   if (!(parent = sdata->parent)) return;

   if (sdata->position.set)
     {
        evas_object_move(ec->frame, parent->x + sdata->position.x,
                         parent->y + sdata->position.y);
        sdata->position.set = EINA_FALSE;
     }

   if ((parent_synchronized) || (sdata->synchronized))
     {
        E_Client *subc;
        Eina_List *l;

        if (sdata->cached.has_data)
          _e_comp_wl_subsurface_commit_from_cache(ec);

        EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_TRUE);
          }
        EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_TRUE);
          }
     }
}

static void
_e_comp_wl_subsurface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_subsurface_cb_position_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y)
{
   E_Client *ec;
   E_Comp_Wl_Subsurf_Data *sdata;

   DBG("Subsurface Cb Position Set: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;

   if (!(sdata = ec->comp_data->sub.data)) return;

   sdata->position.x = x;
   sdata->position.y = y;
   sdata->position.set = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_place_above(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *sibling_resource)
{
   E_Client *ec, *ecs;
   E_Client *parent;

   DBG("Subsurface Cb Place Above: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;

   if (!ec->comp_data->sub.data) return;

   /* try to get the client from the sibling resource */
   if (!(ecs = wl_resource_get_user_data(sibling_resource))) return;

   if (!ecs->comp_data->sub.data) return;

   if (!(parent = ec->comp_data->sub.data->parent)) return;

   parent->comp_data->sub.list_pending =
     eina_list_remove(parent->comp_data->sub.list_pending, ec);

   parent->comp_data->sub.list_pending =
     eina_list_append_relative(parent->comp_data->sub.list_pending, ec, ecs);

   parent->comp_data->sub.list_changed = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_place_below(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *sibling_resource)
{
   E_Client *ec, *ecs;
   E_Client *parent;

   DBG("Subsurface Cb Place Below: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;

   if (!ec->comp_data->sub.data) return;

   /* try to get the client from the sibling resource */
   if (!(ecs = wl_resource_get_user_data(sibling_resource))) return;

   if (!ecs->comp_data->sub.data) return;

   if (!(parent = ec->comp_data->sub.data->parent)) return;

   parent->comp_data->sub.list_pending =
     eina_list_remove(parent->comp_data->sub.list_pending, ec);

   parent->comp_data->sub.list_pending =
     eina_list_prepend_relative(parent->comp_data->sub.list_pending, ec, ecs);

   parent->comp_data->sub.list_changed = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_sync_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;
   E_Comp_Wl_Subsurf_Data *sdata;

   DBG("Subsurface Cb Sync Set: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;

   if (!(sdata = ec->comp_data->sub.data)) return;

   sdata->synchronized = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_desync_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;
   E_Comp_Wl_Subsurf_Data *sdata;

   DBG("Subsurface Cb Desync Set: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;

   if (!(sdata = ec->comp_data->sub.data)) return;

   sdata->synchronized = EINA_FALSE;
}

static const struct wl_subsurface_interface _e_subsurface_interface =
{
   _e_comp_wl_subsurface_cb_destroy,
   _e_comp_wl_subsurface_cb_position_set,
   _e_comp_wl_subsurface_cb_place_above,
   _e_comp_wl_subsurface_cb_place_below,
   _e_comp_wl_subsurface_cb_sync_set,
   _e_comp_wl_subsurface_cb_desync_set
};

static Eina_Bool
_e_comp_wl_subsurface_create(E_Client *ec, E_Client *epc, uint32_t id, struct wl_resource *surface_resource)
{
   struct wl_client *client;
   struct wl_resource *res;
   E_Comp_Wl_Subsurf_Data *sdata;

   /* try to get the wayland client from the surface resource */
   if (!(client = wl_resource_get_client(surface_resource)))
     {
        ERR("Could not get client from resource %d",
            wl_resource_get_id(surface_resource));
        return EINA_FALSE;
     }

   /* try to allocate subsurface data */
   if (!(sdata = E_NEW(E_Comp_Wl_Subsurf_Data, 1)))
     {
        ERR("Could not allocate space for subsurface data");
        goto dat_err;
     }

   /* try to create the subsurface resource */
   if (!(res = wl_resource_create(client, &wl_subsurface_interface, 1, id)))
     {
        ERR("Failed to create subsurface resource");
        wl_resource_post_no_memory(surface_resource);
        goto res_err;
     }

   /* set resource implementation */
   wl_resource_set_implementation(res, &_e_subsurface_interface, ec,
                                  _e_comp_wl_subsurface_destroy);

   _e_comp_wl_surface_state_init(&sdata->cached, ec->w, ec->h);

   /* set subsurface data properties */
   sdata->cached_buffer_ref.buffer = NULL;
   sdata->resource = res;
   sdata->synchronized = EINA_TRUE;
   sdata->parent = epc;

   /* set subsurface client properties */
   ec->borderless = EINA_TRUE;
   ec->argb = EINA_TRUE;
   ec->lock_border = EINA_TRUE;
   ec->lock_focus_in = ec->lock_focus_out = EINA_TRUE;
   ec->netwm.state.skip_taskbar = EINA_TRUE;
   ec->netwm.state.skip_pager = EINA_TRUE;
   ec->no_shape_cut = EINA_TRUE;
   ec->border_size = 0;

   if (epc)
     {
        if (epc->frame)
          {
            short layer = evas_object_layer_get(epc->frame);
            evas_object_layer_set(ec->frame, layer);
          }

        if (epc->comp_data)
          {
             /* append this client to the parents subsurface list */
             epc->comp_data->sub.list_pending =
               eina_list_append(epc->comp_data->sub.list_pending, ec);
             epc->comp_data->sub.list_changed = EINA_TRUE;
          }

        /* TODO: add callbacks ?? */
     }

   ec->comp_data->surface = surface_resource;
   ec->comp_data->sub.data = sdata;

   ec->lock_user_location = 0;
   ec->lock_client_location = 0;
   ec->lock_user_size = 0;
   ec->lock_client_size = 0;
   ec->lock_client_stacking = 0;
   ec->lock_user_shade = 0;
   ec->lock_client_shade = 0;
   ec->lock_user_maximize = 0;
   ec->lock_client_maximize = 0;
   ec->changes.need_maximize = 0;
   ec->maximized = E_MAXIMIZE_NONE;
   EC_CHANGED(ec);

   return EINA_TRUE;

res_err:
   free(sdata);
dat_err:
   return EINA_FALSE;
}

static void
_e_comp_wl_subcompositor_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_subcompositor_cb_subsurface_get(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent_resource)
{
   E_Pixmap *ep, *epp;
   E_Client *ec, *epc = NULL;
   static const char where[] = "get_subsurface: wl_subsurface@";

   if (!(ep = wl_resource_get_user_data(surface_resource))) return;
   if (!(epp = wl_resource_get_user_data(parent_resource))) return;

   if (ep == epp)
     {
        wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "%s%d: wl_surface@%d cannot be its own parent",
                               where, id, wl_resource_get_id(surface_resource));
        return;
     }

   if (!(ec = e_pixmap_client_get(ep)))
     {
        if (!(ec = e_client_new(NULL, ep, 0, 0)))
          {
             wl_resource_post_no_memory(resource);
             return;
          }

        if (ec->comp_data)
          ec->comp_data->surface = surface_resource;
     }

   if (e_object_is_del(E_OBJECT(ec))) return;

   if ((epc = e_pixmap_client_get(epp)))
     {
        if (e_object_is_del(E_OBJECT(epc))) return;
     }

   /* check if this surface is already a sub-surface */
   if ((ec->comp_data) && (ec->comp_data->sub.data))
     {
        wl_resource_post_error(resource,
                               WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "%s%d: wl_surface@%d is already a sub-surface",
                               where, id, wl_resource_get_id(surface_resource));
        return;
     }

   /* try to create a new subsurface */
   if (!_e_comp_wl_subsurface_create(ec, epc, id, surface_resource))
     ERR("Failed to create subsurface for surface %d",
         wl_resource_get_id(surface_resource));
}

static const struct wl_subcompositor_interface _e_subcomp_interface =
{
   _e_comp_wl_subcompositor_cb_destroy,
   _e_comp_wl_subcompositor_cb_subsurface_get
};

static void
_e_comp_wl_subcompositor_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp *comp;
   struct wl_resource *res;

   if (!(comp = data)) return;

   if (!(res =
         wl_resource_create(client, &wl_subcompositor_interface,
                            MIN(version, 1), id)))
     {
        ERR("Could not create subcompositor resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_subcomp_interface, comp, NULL);

   /* TODO: add handlers for client iconify/uniconify */
}

static void
_e_comp_wl_client_cb_new(void *data EINA_UNUSED, E_Client *ec)
{
   Ecore_Window win;

   /* make sure this is a wayland client */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   /* get window id from pixmap */
   win = e_pixmap_window_get(ec->pixmap);

   /* ignore fake root windows */
   if ((ec->override) && ((ec->x == -77) && (ec->y == -77)))
     {
        e_comp_ignore_win_add(E_PIXMAP_TYPE_WL, win);
        e_object_del(E_OBJECT(ec));
        return;
     }

   if (!(ec->comp_data = E_NEW(E_Comp_Client_Data, 1)))
     {
        ERR("Could not allocate new client data structure");
        return;
     }

   wl_signal_init(&ec->comp_data->destroy_signal);

   _e_comp_wl_surface_state_init(&ec->comp_data->pending, ec->w, ec->h);

   /* set initial client properties */
   ec->argb = EINA_TRUE;
   ec->no_shape_cut = EINA_TRUE;
   ec->ignored = e_comp_ignore_win_find(win);
   ec->border_size = 0;

   /* NB: could not find a better place to do this, BUT for internal windows,
    * we need to set delete_request else the close buttons on the frames do
    * basically nothing */
   if ((ec->internal) || (ec->internal_elm_win))
     ec->icccm.delete_request = EINA_TRUE;

   /* set initial client data properties */
   ec->comp_data->mapped = EINA_FALSE;
   ec->comp_data->first_damage = ec->internal;

   ec->comp_data->need_reparent = !ec->internal;

   ec->comp_data->scaler.buffer_viewport.buffer.transform = WL_OUTPUT_TRANSFORM_NORMAL;
   ec->comp_data->scaler.buffer_viewport.buffer.scale = 1;
   ec->comp_data->scaler.buffer_viewport.buffer.src_width = wl_fixed_from_int(-1);
   ec->comp_data->scaler.buffer_viewport.surface.width = -1;

   E_Comp_Client_Data *p_cdata = e_pixmap_cdata_get(ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN(p_cdata);
   ec->comp_data->accepts_focus = p_cdata->accepts_focus;
   ec->comp_data->conformant = p_cdata->conformant;
   ec->comp_data->aux_hint.hints = p_cdata->aux_hint.hints;
   ec->comp_data->win_type = p_cdata->win_type;
   ec->comp_data->layer = p_cdata->layer;
   ec->comp_data->fetch.win_type = p_cdata->fetch.win_type;
   ec->comp_data->fetch.layer = p_cdata->fetch.layer;
   ec->comp_data->video_client = p_cdata->video_client;

   /* add this client to the hash */
   /* eina_hash_add(clients_win_hash, &win, ec); */
   e_hints_client_list_set();

   e_pixmap_cdata_set(ec->pixmap, ec->comp_data);
}

static void
_e_comp_wl_client_cb_del(void *data EINA_UNUSED, E_Client *ec)
{
   /* Eina_Rectangle *dmg; */
   struct wl_resource *cb;
   E_Client *subc;

   /* make sure this is a wayland client */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   if ((!ec->already_unparented) && (ec->comp_data->reparented))
     _e_comp_wl_focus_down_set(ec);

   ec->already_unparented = EINA_TRUE;
   if (ec->comp_data->reparented)
     {
        /* reset pixmap parent window */
        e_pixmap_parent_window_set(ec->pixmap, 0);
     }

   if (ec->comp_data->sub.data)
     {
        E_Comp_Wl_Subsurf_Data *sdata = ec->comp_data->sub.data;
        if (sdata->parent && sdata->parent->comp_data)
          {
             /* remove this client from parents sub list */
             sdata->parent->comp_data->sub.list =
               eina_list_remove(sdata->parent->comp_data->sub.list, ec);
             sdata->parent->comp_data->sub.list_pending =
               eina_list_remove(sdata->parent->comp_data->sub.list_pending, ec);
             sdata->parent->comp_data->sub.below_list =
               eina_list_remove(sdata->parent->comp_data->sub.below_list, ec);
             sdata->parent->comp_data->sub.below_list_pending =
               eina_list_remove(sdata->parent->comp_data->sub.below_list_pending, ec);
          }
     }

   /* remove sub list */
   EINA_LIST_FREE(ec->comp_data->sub.list, subc)
     if (subc->comp_data && subc->comp_data->sub.data) subc->comp_data->sub.data->parent = NULL;
   EINA_LIST_FREE(ec->comp_data->sub.list_pending, subc)
     if (subc->comp_data && subc->comp_data->sub.data) subc->comp_data->sub.data->parent = NULL;
   EINA_LIST_FREE(ec->comp_data->sub.below_list, subc)
     if (subc->comp_data && subc->comp_data->sub.data) subc->comp_data->sub.data->parent = NULL;
   EINA_LIST_FREE(ec->comp_data->sub.below_list_pending, subc)
     if (subc->comp_data && subc->comp_data->sub.data) subc->comp_data->sub.data->parent = NULL;

   if ((ec->parent) && (ec->parent->modal == ec))
     {
        ec->parent->lock_close = EINA_FALSE;
        ec->parent->modal = NULL;
     }

   wl_signal_emit(&ec->comp_data->destroy_signal, &ec->comp_data->surface);

   _e_comp_wl_surface_state_finish(&ec->comp_data->pending);

   e_comp_wl_buffer_reference(&ec->comp_data->buffer_ref, NULL);

   EINA_LIST_FREE(ec->comp_data->frames, cb)
     wl_resource_destroy(cb);

   if (ec->comp_data->surface)
     wl_resource_set_user_data(ec->comp_data->surface, NULL);

   if (ec->comp_data->aux_hint.hints)
     {
        E_Comp_Wl_Aux_Hint *hint;
        EINA_LIST_FREE(ec->comp_data->aux_hint.hints, hint)
          {
             eina_stringshare_del(hint->hint);
             eina_stringshare_del(hint->val);
             E_FREE(hint);
          }
     }

   e_pixmap_cdata_set(ec->pixmap, NULL);

   E_FREE(ec->comp_data);

   _e_comp_wl_focus_check(ec->comp);
}

static void
_e_comp_wl_client_cb_post_new(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   ec->need_shape_merge = EINA_FALSE;

   if (ec->need_shape_export)
     {
//        ec->shape_changed = EINA_TRUE;
        e_comp_shape_queue(ec->comp);
        ec->need_shape_export = EINA_FALSE;
     }

   if (ec->argb && ec->frame && !e_util_strcmp("video", ec->icccm.window_role))
     _e_comp_wl_subsurface_create_below_bg_rectangle(ec);
}

#if 0
static void
_e_comp_wl_client_cb_pre_frame(void *data EINA_UNUSED, E_Client *ec)
{
   Ecore_Window parent;

   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;
   if (!ec->comp_data->need_reparent) return;

   DBG("Client Pre Frame: %d", wl_resource_get_id(ec->comp_data->surface));

   parent = e_client_util_pwin_get(ec);

   /* set pixmap parent window */
   e_pixmap_parent_window_set(ec->pixmap, parent);

   ec->border_size = 0;
   ec->border.changed = EINA_TRUE;
   ec->changes.shape = EINA_TRUE;
   ec->changes.shape_input = EINA_TRUE;
   EC_CHANGED(ec);

   if (ec->visible)
     {
        if ((ec->comp_data->set_win_type) && (ec->internal_elm_win))
          {
             int type = ECORE_WL_WINDOW_TYPE_TOPLEVEL;

             switch (ec->netwm.type)
               {
                case E_WINDOW_TYPE_DIALOG:
                  /* NB: If there is No transient set, then dialogs get
                   * treated as Normal Toplevel windows */
                  if (ec->icccm.transient_for)
                    type = ECORE_WL_WINDOW_TYPE_TRANSIENT;
                  break;
                case E_WINDOW_TYPE_DESKTOP:
                  type = ECORE_WL_WINDOW_TYPE_FULLSCREEN;
                  break;
                case E_WINDOW_TYPE_DND:
                  type = ECORE_WL_WINDOW_TYPE_DND;
                  break;
                case E_WINDOW_TYPE_MENU:
                case E_WINDOW_TYPE_DROPDOWN_MENU:
                case E_WINDOW_TYPE_POPUP_MENU:
                  type = ECORE_WL_WINDOW_TYPE_MENU;
                  break;
                case E_WINDOW_TYPE_NORMAL:
                default:
                    break;
               }

             ecore_evas_wayland_type_set(e_win_ee_get(ec->internal_elm_win), type);
             ec->comp_data->set_win_type = EINA_FALSE;
          }
     }

   e_bindings_mouse_grab(E_BINDING_CONTEXT_WINDOW, parent);
   e_bindings_wheel_grab(E_BINDING_CONTEXT_WINDOW, parent);

   _e_comp_wl_client_evas_init(ec);

   /* if ((ec->netwm.ping) && (!ec->ping_poller)) */
   /*   e_client_ping(ec); */

   if (ec->visible) evas_object_show(ec->frame);

   ec->comp_data->need_reparent = EINA_FALSE;
   ec->redirected = EINA_TRUE;

   if (ec->comp_data->change_icon)
     {
        ec->comp_data->change_icon = EINA_FALSE;
        ec->changes.icon = EINA_TRUE;
        EC_CHANGED(ec);
     }

   ec->comp_data->reparented = EINA_TRUE;
}
#endif

static void
_e_comp_wl_client_cb_focus_set(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   /* send configure */
   if (ec->comp_data->shell.configure_send)
     {
        if (ec->comp_data->shell.surface)
          ec->comp_data->shell.configure_send(ec->comp_data->shell.surface,
                                              0, 0, 0);
     }

   if ((ec->icccm.take_focus) && (ec->icccm.accepts_focus))
     e_grabinput_focus(e_client_util_win_get(ec),
                       E_FOCUS_METHOD_LOCALLY_ACTIVE);
   else if (!ec->icccm.accepts_focus)
     e_grabinput_focus(e_client_util_win_get(ec),
                       E_FOCUS_METHOD_GLOBALLY_ACTIVE);
   else if (!ec->icccm.take_focus)
     e_grabinput_focus(e_client_util_win_get(ec), E_FOCUS_METHOD_PASSIVE);

   if (ec->comp->wl_comp_data->kbd.focus != ec->comp_data->surface)
     {
        ec->comp->wl_comp_data->kbd.focus = ec->comp_data->surface;
        e_comp_wl_data_device_keyboard_focus_set(ec->comp->wl_comp_data);
     }
}

static void
_e_comp_wl_client_cb_focus_unset(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   /* send configure */
   if (ec->comp_data->shell.configure_send)
     {
        if (ec->comp_data->shell.surface)
          ec->comp_data->shell.configure_send(ec->comp_data->shell.surface,
                                              0, 0, 0);
     }

   _e_comp_wl_focus_check(ec->comp);

   if (ec->comp->wl_comp_data->kbd.focus == ec->comp_data->surface)
     ec->comp->wl_comp_data->kbd.focus = NULL;
}

static void
_e_comp_wl_client_cb_resize_begin(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   switch (ec->resize_mode)
     {
      case E_POINTER_RESIZE_T: // 1
        ec->comp->wl_comp_data->resize.edges = 1;
        break;
      case E_POINTER_RESIZE_B: // 2
        ec->comp->wl_comp_data->resize.edges = 2;
        break;
      case E_POINTER_RESIZE_L: // 4
        ec->comp->wl_comp_data->resize.edges = 4;
        break;
      case E_POINTER_RESIZE_R: // 8
        ec->comp->wl_comp_data->resize.edges = 8;
        break;
      case E_POINTER_RESIZE_TL: // 5
        ec->comp->wl_comp_data->resize.edges = 5;
        break;
      case E_POINTER_RESIZE_TR: // 9
        ec->comp->wl_comp_data->resize.edges = 9;
        break;
      case E_POINTER_RESIZE_BL: // 6
        ec->comp->wl_comp_data->resize.edges = 6;
        break;
      case E_POINTER_RESIZE_BR: // 10
        ec->comp->wl_comp_data->resize.edges = 10;
        break;
      default:
        ec->comp->wl_comp_data->resize.edges = 0;
        break;
     }
}

static void
_e_comp_wl_client_cb_resize_end(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   ec->comp->wl_comp_data->resize.edges = 0;
   ec->comp->wl_comp_data->resize.resource = NULL;

   if (ec->pending_resize)
     {
        ec->changes.pos = EINA_TRUE;
        ec->changes.size = EINA_TRUE;
        EC_CHANGED(ec);
     }

   E_FREE_LIST(ec->pending_resize, free);
}

static void
_e_comp_wl_client_cb_move_end(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   if (ec->comp_data->transform.enabled)
     _e_comp_wl_transform_pull(ec);
}

static void
_e_comp_wl_cb_output_unbind(struct wl_resource *resource)
{
   E_Comp_Wl_Output *output;

   if (!(output = wl_resource_get_user_data(resource))) return;

   output->resources = eina_list_remove(output->resources, resource);
}

static void
_e_comp_wl_cb_output_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp_Wl_Output *output;
   struct wl_resource *resource;

   if (!(output = data)) return;

   resource =
     wl_resource_create(client, &wl_output_interface, MIN(version, 2), id);
   if (!resource)
     {
        wl_client_post_no_memory(client);
        return;
     }

   output->resources = eina_list_append(output->resources, resource);

   wl_resource_set_implementation(resource, NULL, output,
                                  _e_comp_wl_cb_output_unbind);
   wl_resource_set_user_data(resource, output);

   wl_output_send_geometry(resource, output->x, output->y,
                           output->phys_width, output->phys_height,
                           output->subpixel, output->make, output->model,
                           output->transform);

   if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
     wl_output_send_scale(resource, output->scale);

   /* 3 == preferred + current */
   wl_output_send_mode(resource, 3, output->w, output->h, output->refresh);

   if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
     wl_output_send_done(resource);
}

static void
_e_comp_wl_gl_init(E_Comp_Data *cdata)
{
   Evas_GL *evasgl = NULL;
   Evas_GL_API *glapi = NULL;
   Evas_GL_Context *ctx = NULL;
   Evas_GL_Surface *sfc = NULL;
   Evas_GL_Config *cfg = NULL;
   Eina_Bool res;
   const char *name;

   if (!e_comp_gl_get()) return;

   name = ecore_evas_engine_name_get(e_comp->ee);
   if (!name) return;
   if (strcmp(name, "gl_drm")) return;

   /* create dummy evas gl to bind wayland display of enlightenment to egl display */
   evasgl = evas_gl_new(e_comp->evas);
   EINA_SAFETY_ON_NULL_GOTO(evasgl, err);

   glapi = evas_gl_api_get(evasgl);
   EINA_SAFETY_ON_NULL_GOTO(glapi, err);
   EINA_SAFETY_ON_NULL_GOTO(glapi->evasglBindWaylandDisplay, err);

   cfg = evas_gl_config_new();
   EINA_SAFETY_ON_NULL_GOTO(cfg, err);

   sfc = evas_gl_surface_create(evasgl, cfg, 1, 1);
   EINA_SAFETY_ON_NULL_GOTO(sfc, err);

   ctx = evas_gl_context_create(evasgl, NULL);
   EINA_SAFETY_ON_NULL_GOTO(ctx, err);

   res = evas_gl_make_current(evasgl, sfc, ctx);
   EINA_SAFETY_ON_FALSE_GOTO(res, err);

   res = glapi->evasglBindWaylandDisplay(evasgl, cdata->wl.disp);
   EINA_SAFETY_ON_FALSE_GOTO(res, err);

   evas_gl_config_free(cfg);

   cdata->gl.evasgl = evasgl;
   cdata->gl.api = glapi;
   cdata->gl.sfc = sfc;
   cdata->gl.ctx = ctx;

   /* for native surface */
   e_comp->gl = 1;

   return;

err:
   evas_gl_config_free(cfg);
   evas_gl_make_current(evasgl, NULL, NULL);
   evas_gl_context_destroy(evasgl, ctx);
   evas_gl_surface_destroy(evasgl, sfc);
   evas_gl_free(evasgl);
}

static void
_e_comp_wl_gl_shutdown(E_Comp_Data *cdata)
{
   if (!cdata->gl.evasgl) return;

   cdata->gl.api->evasglUnbindWaylandDisplay(cdata->gl.evasgl, cdata->wl.disp);

   evas_gl_make_current(cdata->gl.evasgl, NULL, NULL);
   evas_gl_context_destroy(cdata->gl.evasgl, cdata->gl.ctx);
   evas_gl_surface_destroy(cdata->gl.evasgl, cdata->gl.sfc);
   evas_gl_free(cdata->gl.evasgl);

   cdata->gl.sfc = NULL;
   cdata->gl.ctx = NULL;
   cdata->gl.api = NULL;
   cdata->gl.evasgl = NULL;
}

static void
_e_comp_wl_gl_popup_cb_close(void *data,
                             Evas_Object *obj EINA_UNUSED,
                             void *event_info EINA_UNUSED)
{
   evas_object_del(data);
}

static void
_e_comp_wl_gl_popup_cb_focus(void *data,
                             Evas_Object *obj EINA_UNUSED,
                             void *event_info EINA_UNUSED)
{
   elm_object_focus_set(data, EINA_TRUE);
}

static Eina_Bool
_e_comp_wl_gl_idle(void *data EINA_UNUSED)
{
   if (!e_comp->gl)
     {
        /* show warning window to notify failure of gl init */
        Evas_Object *win, *bg, *popup, *btn;

        win = elm_win_add(NULL, "compositor warning", ELM_WIN_BASIC);
        elm_win_title_set(win, "Compositor Warning");
        elm_win_autodel_set(win, EINA_TRUE);
        elm_win_borderless_set(win, EINA_TRUE);
        elm_win_role_set(win, "notification-low");
        elm_win_alpha_set(win, EINA_TRUE);

        bg = evas_object_rectangle_add(evas_object_evas_get(win));
        evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        elm_win_resize_object_add(win, bg);
        evas_object_color_set(bg, 125, 125, 125, 125);
        evas_object_show(bg);

        popup = elm_popup_add(win);
        elm_object_text_set(popup,
                            _( "Your screen does not support OpenGL.<br>"
                               "Falling back to software engine."));
        elm_object_part_text_set(popup, "title,text", "Compositor Warning");

        btn = elm_button_add(popup);
        elm_object_text_set(btn, "Close");
        elm_object_part_content_set(popup, "button1", btn);
        evas_object_show(btn);

        evas_object_smart_callback_add(win, "focus,in", _e_comp_wl_gl_popup_cb_focus, popup);
        evas_object_smart_callback_add(btn, "unpressed", _e_comp_wl_gl_popup_cb_close, win);

        evas_object_show(popup);
        evas_object_show(win);
     }

   return ECORE_CALLBACK_CANCEL;
}



static Eina_Bool
_e_comp_wl_compositor_create(void)
{
   E_Comp *comp;
   E_Comp_Data *cdata;
   const char *name;
   int fd = 0;

   /* check for existing compositor. create if needed */
   if (!(comp = e_comp))
     {
        comp = e_comp_new();
        comp->comp_type = E_PIXMAP_TYPE_WL;
        E_OBJECT_DEL_SET(comp, _e_comp_wl_compositor_cb_del);
     }

   /* create new compositor data */
   if (!(cdata = E_NEW(E_Comp_Data, 1)))
     {
       ERR("Could not create compositor data: %m");
       return EINA_FALSE;
     }

   /* set compositor wayland data */
   comp->wl_comp_data = cdata;

   /* set wayland log handler */
   wl_log_set_handler_server(_e_comp_wl_log_cb_print);

   /* try to create a wayland display */
   if (!(cdata->wl.disp = wl_display_create()))
     {
        ERR("Could not create a Wayland display: %m");
        goto disp_err;
     }

   /* try to setup wayland socket */
   if (!(name = wl_display_add_socket_auto(cdata->wl.disp)))
     {
        ERR("Could not create Wayland display socket: %m");
        goto sock_err;
     }

   /* set wayland display environment variable */
   e_env_set("WAYLAND_DISPLAY", name);

   /* initialize compositor signals */
   wl_signal_init(&cdata->signals.surface.create);
   wl_signal_init(&cdata->signals.surface.activate);
   wl_signal_init(&cdata->signals.surface.kill);

   /* cdata->output.transform = WL_OUTPUT_TRANSFORM_NORMAL; */
   /* cdata->output.scale = e_scale; */

   /* try to add compositor to wayland globals */
   if (!wl_global_create(cdata->wl.disp, &wl_compositor_interface,
                         COMPOSITOR_VERSION, comp,
                         _e_comp_wl_compositor_cb_bind))
     {
        ERR("Could not add compositor to wayland globals: %m");
        goto comp_global_err;
     }

   /* try to add subcompositor to wayland globals */
   if (!wl_global_create(cdata->wl.disp, &wl_subcompositor_interface, 1,
                         comp, _e_comp_wl_subcompositor_cb_bind))
     {
        ERR("Could not add subcompositor to wayland globals: %m");
        goto comp_global_err;
     }

   /* initialize shm mechanism */
   wl_display_init_shm(cdata->wl.disp);

#ifndef HAVE_WAYLAND_ONLY
   _e_comp_wl_cb_randr_change(NULL, 0, NULL);
#endif

   /* try to init data manager */
   if (!e_comp_wl_data_manager_init(cdata))
     {
        ERR("Could not initialize data manager");
        goto data_err;
     }

   /* try to init input */
   if (!e_comp_wl_input_init(cdata))
     {
        ERR("Could not initialize input");
        goto input_err;
     }

   _e_comp_wl_gl_init(cdata);

#ifndef HAVE_WAYLAND_ONLY
   if (getenv("DISPLAY"))
     {
        E_Config_XKB_Layout *ekbd;
        Ecore_X_Atom xkb = 0;
        Ecore_X_Window root = 0;
        int len = 0;
        unsigned char *dat;
        char *rules, *model, *layout;

        if ((ekbd = e_xkb_layout_get()))
          {
             model = strdup(ekbd->model);
             layout = strdup(ekbd->name);
          }

        root = ecore_x_window_root_first_get();
        xkb = ecore_x_atom_get("_XKB_RULES_NAMES");
        ecore_x_window_prop_property_get(root, xkb, ECORE_X_ATOM_STRING,
                                         1024, &dat, &len);
        if ((dat) && (len > 0))
          {
             rules = (char *)dat;
             dat += strlen((const char *)dat) + 1;
             if (!model) model = strdup((const char *)dat);
             dat += strlen((const char *)dat) + 1;
             if (!layout) layout = strdup((const char *)dat);
          }

        /* fallback */
        if (!rules) rules = strdup("evdev");
        if (!model) model = strdup("pc105");
        if (!layout) layout = strdup("us");

        /* update compositor keymap */
        e_comp_wl_input_keymap_set(cdata, rules, model, layout);
     }
#endif

   /* get the wayland display loop */
   cdata->wl.loop = wl_display_get_event_loop(cdata->wl.disp);

   /* get the file descriptor of the wayland event loop */
   fd = wl_event_loop_get_fd(cdata->wl.loop);

   /* create a listener for wayland main loop events */
   cdata->fd_hdlr =
     ecore_main_fd_handler_add(fd, (ECORE_FD_READ | ECORE_FD_ERROR),
                               _e_comp_wl_cb_read, cdata, NULL, NULL);
   ecore_main_fd_handler_prepare_callback_set(cdata->fd_hdlr,
                                              _e_comp_wl_cb_prepare, cdata);

   /* setup module idler to load shell mmodule */
   ecore_idler_add(_e_comp_wl_cb_module_idle, cdata);

#ifndef ENABLE_QUICK_INIT
   /* check if gl init succeded */
   ecore_idler_add(_e_comp_wl_gl_idle, cdata);

#endif
   if (comp->comp_type == E_PIXMAP_TYPE_X)
     {
        e_comp_wl_input_pointer_enabled_set(EINA_TRUE);
        e_comp_wl_input_keyboard_enabled_set(EINA_TRUE);
        e_comp_wl_input_touch_enabled_set(EINA_TRUE);
     }

   return EINA_TRUE;

input_err:
   e_comp_wl_data_manager_shutdown(cdata);
data_err:
comp_global_err:
   e_env_unset("WAYLAND_DISPLAY");
sock_err:
   wl_display_destroy(cdata->wl.disp);
disp_err:
   free(cdata);
   return EINA_FALSE;
}

/* public functions */

/**
 * Creates and initializes a Wayland compositor with ecore.
 * Registers callback handlers for keyboard and mouse activity
 * and other client events.
 *
 * @returns true on success, false if initialization failed.
 */
EAPI Eina_Bool
e_comp_wl_init(void)
{
   /* set gl available if we have ecore_evas support */
   if (ecore_evas_engine_type_supported_get(ECORE_EVAS_ENGINE_WAYLAND_EGL) ||
       ecore_evas_engine_type_supported_get(ECORE_EVAS_ENGINE_OPENGL_DRM))
     e_comp_gl_set(EINA_TRUE);

   /* try to create a wayland compositor */
   if (!_e_comp_wl_compositor_create())
     {
        e_error_message_show(_("Enlightenment cannot create a Wayland Compositor!\n"));
        return EINA_FALSE;
     }

   /* try to init ecore_wayland */
   if (!ecore_wl_init(NULL))
     {
        e_error_message_show(_("Enlightenment cannot initialize Ecore_Wayland!\n"));
        return EINA_FALSE;
     }

   wl_event_loop_dispatch(e_comp->wl_comp_data->wl.loop, -1); //server calls wl_client_create()

   ecore_wl_flush(); // client sendmsg wl_display.get_registry request
   wl_event_loop_dispatch(e_comp->wl_comp_data->wl.loop, -1); // server calls display_get_registry()
   wl_display_flush_clients(e_comp->wl_comp_data->wl.disp); // server flushes wl_registry.global events

   ecore_wl_display_iterate(); // client handles global events and make 'bind' requests to each global interfaces

   ecore_wl_flush(); // client sendmsg wl_registry.bind requests
   wl_event_loop_dispatch(e_comp->wl_comp_data->wl.loop, -1); // server calls registry_bind() using given interfaces' name

   /* create hash to store clients */
   /* clients_win_hash = eina_hash_int64_new(NULL); */
   clients_buffer_hash = eina_hash_pointer_new(NULL);

#ifdef HAVE_WAYLAND_TBM
   e_comp_wl_tbm_init();
#endif

   /* add event handlers to catch E events */
#ifndef HAVE_WAYLAND_ONLY
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_RANDR_CHANGE,          _e_comp_wl_cb_randr_change,    NULL);
#endif
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_COMP_OBJECT_ADD,       _e_comp_wl_cb_comp_object_add, NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_KEY_DOWN,          _e_comp_wl_cb_input_event,     NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_KEY_UP,            _e_comp_wl_cb_input_event,     NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_MOUSE_BUTTON_DOWN, _e_comp_wl_cb_input_event,     NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_MOUSE_BUTTON_UP,   _e_comp_wl_cb_input_event,     NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_MOUSE_MOVE,        _e_comp_wl_cb_input_event,     NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_MOUSE_WHEEL,       _e_comp_wl_cb_input_event,     NULL);

   /* add hooks to catch e_client events */
   e_client_hook_add(E_CLIENT_HOOK_NEW_CLIENT,           _e_comp_wl_client_cb_new,          NULL);
   e_client_hook_add(E_CLIENT_HOOK_DEL,                  _e_comp_wl_client_cb_del,          NULL);
   e_client_hook_add(E_CLIENT_HOOK_EVAL_POST_NEW_CLIENT, _e_comp_wl_client_cb_post_new,     NULL);
   e_client_hook_add(E_CLIENT_HOOK_FOCUS_SET,            _e_comp_wl_client_cb_focus_set,    NULL);
   e_client_hook_add(E_CLIENT_HOOK_FOCUS_UNSET,          _e_comp_wl_client_cb_focus_unset,  NULL);
   e_client_hook_add(E_CLIENT_HOOK_RESIZE_BEGIN,         _e_comp_wl_client_cb_resize_begin, NULL);
   e_client_hook_add(E_CLIENT_HOOK_RESIZE_END,           _e_comp_wl_client_cb_resize_end,   NULL);
   e_client_hook_add(E_CLIENT_HOOK_MOVE_END,             _e_comp_wl_client_cb_move_end,     NULL);

   _last_event_time = ecore_loop_time_get();

   return EINA_TRUE;
}

EAPI void
e_comp_wl_deferred_job(void)
{
   ecore_idle_enterer_add(_e_comp_wl_gl_idle, NULL);
}

/**
 * Get the signal that is fired for the creation of a Wayland surface.
 *
 * @returns the corresponding Wayland signal
 */
EAPI struct wl_signal
e_comp_wl_surface_create_signal_get(E_Comp *comp)
{
   return comp->wl_comp_data->signals.surface.create;
}

/* internal functions */
EINTERN void
e_comp_wl_shutdown(void)
{
   /* free evas gl */
   E_Comp_Data *cdata;
   if ((cdata = e_comp->wl_comp_data))
     _e_comp_wl_gl_shutdown(cdata);

#ifndef HAVE_WAYLAND_ONLY
   _e_comp_wl_compositor_cb_del(e_comp);
#endif
   /* free buffer hash */
   E_FREE_FUNC(clients_buffer_hash, eina_hash_free);

   /* free handlers */
   E_FREE_LIST(handlers, ecore_event_handler_del);

#ifdef HAVE_WAYLAND_TBM
   e_comp_wl_tbm_shutdown();
#endif

   /* shutdown ecore_wayland */
   ecore_wl_shutdown();
}

EINTERN struct wl_resource *
e_comp_wl_surface_create(struct wl_client *client, int version, uint32_t id)
{
   struct wl_resource *ret = NULL;

   if ((ret = wl_resource_create(client, &wl_surface_interface, version, id)))
     DBG("Created Surface: %d", wl_resource_get_id(ret));

   return ret;
}

static void
e_comp_wl_surface_event_simple_free(void *d EINA_UNUSED, E_Event_Client *ev)
{
   e_object_unref(E_OBJECT(ev->ec));
   free(ev);
}

EINTERN void
e_comp_wl_surface_attach(E_Client *ec, E_Comp_Wl_Buffer *buffer)
{
   E_Event_Client *ev;
   ev = E_NEW(E_Event_Client, 1);
   if (!ev) return;

   e_comp_wl_buffer_reference(&ec->comp_data->buffer_ref, buffer);

   /* set usable early because shell module checks this */
   e_pixmap_usable_set(ec->pixmap, (buffer != NULL));
   e_pixmap_resource_set(ec->pixmap, buffer);
   e_pixmap_dirty(ec->pixmap);

   _e_comp_wl_surface_state_size_update(ec, &ec->comp_data->pending);
   _e_comp_wl_map_size_cal_from_buffer(ec);

   ev->ec = ec;
   e_object_ref(E_OBJECT(ec));
   ecore_event_add(E_EVENT_CLIENT_BUFFER_CHANGE, ev,
                   (Ecore_End_Cb)e_comp_wl_surface_event_simple_free, NULL);
}

EINTERN Eina_Bool
e_comp_wl_surface_commit(E_Client *ec)
{
   _e_comp_wl_surface_state_commit(ec, &ec->comp_data->pending);

   if (ec->comp_data->sub.below_list || ec->comp_data->sub.below_list_pending)
     {
        if (!ec->comp_data->sub.below_obj)
          _e_comp_wl_subsurface_create_below_bg_rectangle(ec);
     }

   _e_comp_wl_surface_subsurface_order_commit(ec);

   /* schedule repaint */
   if (e_pixmap_refresh(ec->pixmap))
     e_comp_post_update_add(ec);

   if (!e_pixmap_usable_get(ec->pixmap))
     {
        if (ec->comp_data->mapped)
          {
             if ((ec->comp_data->shell.surface) && (ec->comp_data->shell.unmap))
               ec->comp_data->shell.unmap(ec->comp_data->shell.surface);
             else
               {
                  evas_object_hide(ec->frame);
                  ec->comp_data->mapped = EINA_FALSE;
               }
          }

        if (ec->comp_data->sub.below_obj && evas_object_visible_get(ec->comp_data->sub.below_obj))
          evas_object_hide(ec->comp_data->sub.below_obj);
     }
   else
     {
        if (!ec->comp_data->mapped)
          {
             if ((ec->comp_data->shell.surface) && (ec->comp_data->shell.map))
               ec->comp_data->shell.map(ec->comp_data->shell.surface);
             else
               {
                  evas_object_show(ec->frame);
                  ec->comp_data->mapped = EINA_TRUE;
               }
          }

        if (ec->comp_data->sub.below_obj && !evas_object_visible_get(ec->comp_data->sub.below_obj))
          evas_object_show(ec->comp_data->sub.below_obj);
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_subsurface_commit(E_Client *ec)
{
   E_Comp_Wl_Subsurf_Data *sdata;

   /* check for valid subcompositor data */
   if (!(sdata = ec->comp_data->sub.data)) return EINA_FALSE;

   if (_e_comp_wl_subsurface_synchronized_get(sdata))
     _e_comp_wl_subsurface_commit_to_cache(ec);
   else
     {
        E_Client *subc;
        Eina_List *l;

        if (sdata->cached.has_data)
          {
             _e_comp_wl_subsurface_commit_to_cache(ec);
             _e_comp_wl_subsurface_commit_from_cache(ec);
          }
        else
          e_comp_wl_surface_commit(ec);

        EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_FALSE);
          }
        EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_FALSE);
          }
     }

   return EINA_TRUE;
}

EAPI void
e_comp_wl_buffer_reference(E_Comp_Wl_Buffer_Ref *ref, E_Comp_Wl_Buffer *buffer)
{
   if ((ref->buffer) && (buffer != ref->buffer))
     {
        ref->buffer->busy--;
        if (ref->buffer->busy == 0)
          {
             if (!wl_resource_get_client(ref->buffer->resource)) return;
             wl_buffer_send_release(ref->buffer->resource);
          }
        wl_list_remove(&ref->destroy_listener.link);
     }

   if ((buffer) && (buffer != ref->buffer))
     {
        buffer->busy++;
        wl_signal_add(&buffer->destroy_signal, &ref->destroy_listener);
     }

   ref->buffer = buffer;
   ref->destroy_listener.notify = _e_comp_wl_buffer_reference_cb_destroy;
}

/**
 * Get the buffer for a given resource.
 *
 * Retrieves the Wayland SHM buffer for the resource and
 * uses it to create a new E_Comp_Wl_Buffer object.  This
 * buffer will be freed when the resource is destroyed.
 *
 * @param resource that owns the desired buffer
 * @returns a new E_Comp_Wl_Buffer object
 */
EAPI E_Comp_Wl_Buffer *
e_comp_wl_buffer_get(struct wl_resource *resource, struct wl_resource *surface)
{
   E_Comp_Wl_Buffer *buffer = NULL;
   struct wl_listener *listener;
   struct wl_shm_buffer *shmbuff;
   E_Comp_Data *cdata;
   Eina_Bool res;
   tbm_surface_h tbm_surf;

   listener =
     wl_resource_get_destroy_listener(resource, _e_comp_wl_buffer_cb_destroy);
   if (listener)
     return container_of(listener, E_Comp_Wl_Buffer, destroy_listener);

   if (!(buffer = E_NEW(E_Comp_Wl_Buffer, 1))) return NULL;

   shmbuff = wl_shm_buffer_get(resource);

   if (shmbuff)
     {
        buffer->type = E_COMP_WL_BUFFER_TYPE_SHM;

        buffer->w = wl_shm_buffer_get_width(shmbuff);
        buffer->h = wl_shm_buffer_get_height(shmbuff);
     }
   else
     {
        E_Pixmap *ep = wl_resource_get_user_data(surface);
        E_Comp_Wl_Client_Data *p_cdata = e_pixmap_cdata_get(ep);

        cdata = e_comp->wl_comp_data;
        if (p_cdata && p_cdata->video_client)
          {
             tbm_surf = wayland_tbm_server_get_surface(cdata->tbm.server, resource);
             buffer->type = E_COMP_WL_BUFFER_TYPE_VIDEO;
             buffer->w = tbm_surface_get_width(tbm_surf);
             buffer->h = tbm_surface_get_height(tbm_surf);
          }
        else if (cdata->gl.api)
          {
             buffer->type = E_COMP_WL_BUFFER_TYPE_NATIVE;

             res = cdata->gl.api->evasglQueryWaylandBuffer(cdata->gl.evasgl,
                                                           resource,
                                                           EVAS_GL_WIDTH,
                                                           &buffer->w);
             EINA_SAFETY_ON_FALSE_GOTO(res, err);

             res = cdata->gl.api->evasglQueryWaylandBuffer(cdata->gl.evasgl,
                                                           resource,
                                                           EVAS_GL_HEIGHT,
                                                           &buffer->h);
             EINA_SAFETY_ON_FALSE_GOTO(res, err);
          }
        else
          goto err;
     }

   buffer->resource = resource;
   wl_signal_init(&buffer->destroy_signal);
   buffer->destroy_listener.notify = _e_comp_wl_buffer_cb_destroy;
   wl_resource_add_destroy_listener(resource, &buffer->destroy_listener);

   return buffer;

err:
   ERR("Invalid resource:%u", wl_resource_get_id(resource));
   E_FREE(buffer);
   return NULL;
}

/**
 * Computes the time since the last input event.
 *
 * @returns time in seconds.
 */
EAPI double
e_comp_wl_idle_time_get(void)
{
   return (ecore_loop_time_get() - _last_event_time);
}

static E_Comp_Wl_Output *
_e_comp_wl_output_get(Eina_List *outputs, const char *id)
{
   Eina_List *l;
   E_Comp_Wl_Output *output;

   EINA_LIST_FOREACH(outputs, l, output)
     {
       if (!strcmp(output->id, id))
         return output;
     }

   return NULL;
}

/**
 * Initializes information about one display output.
 *
 * Adds or updates the given data about a single display output,
 * with an id matching the provided id.
 *
 * @param id         identification of output to be added or changed
 * @param make       manufacturer name of the display output
 * @param model      model name of the display output
 * @param x          output's top left corner x coordinate
 * @param y          output's top left corner y coordinate
 * @param w          output's width in pixels
 * @param h          output's height in pixels
 * @param pw         output's physical width in millimeters
 * @param ph         output's physical height in millimeters
 * @param refresh    output's refresh rate in mHz
 * @param subpixel   output's subpixel layout
 * @param transform  output's rotation and/or mirror transformation
 */
EAPI void
e_comp_wl_output_init(const char *id, const char *make, const char *model, int x, int y, int w, int h, int pw, int ph, unsigned int refresh, unsigned int subpixel, unsigned int transform)
{
   E_Comp_Data *cdata;
   E_Comp_Wl_Output *output;
   Eina_List *l2;
   struct wl_resource *resource;

   if (!(cdata = e_comp->wl_comp_data)) return;

   /* retrieve named output; or create it if it doesn't exist */
   output = _e_comp_wl_output_get(cdata->outputs, id);
   if (!output)
     {
        if (!(output = E_NEW(E_Comp_Wl_Output, 1))) return;
        if (id) output->id = eina_stringshare_add(id);
        if (make)
          output->make = eina_stringshare_add(make);
        else
          output->make = eina_stringshare_add("unknown");
        if (model)
          output->model = eina_stringshare_add(model);
        else
          output->model = eina_stringshare_add("unknown");

        cdata->outputs = eina_list_append(cdata->outputs, output);

        output->global = wl_global_create(cdata->wl.disp, &wl_output_interface,
                                          2, output, _e_comp_wl_cb_output_bind);
        output->resources = NULL;
        output->scale = e_scale;
     }

   /* update the output details */
   output->x = x;
   output->y = y;
   output->w = w;
   output->h = h;
   output->phys_width = pw;
   output->phys_height = ph;
   output->refresh = refresh;
   output->subpixel = subpixel;
   output->transform = transform;

   if (output->scale <= 0)
     output->scale = e_scale;

   /* if we have bound resources, send updates */
   EINA_LIST_FOREACH(output->resources, l2, resource)
     {
        wl_output_send_geometry(resource,
                                output->x, output->y,
                                output->phys_width,
                                output->phys_height,
                                output->subpixel,
                                output->make, output->model,
                                output->transform);

        if (wl_resource_get_version(resource) >= WL_OUTPUT_SCALE_SINCE_VERSION)
          wl_output_send_scale(resource, output->scale);

        /* 3 == preferred + current */
        wl_output_send_mode(resource, 3, output->w, output->h, output->refresh);

        if (wl_resource_get_version(resource) >= WL_OUTPUT_DONE_SINCE_VERSION)
          wl_output_send_done(resource);
     }
}
