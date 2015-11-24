#include "e.h"
#if defined(HAVE_WAYLAND_CLIENTS) || defined(HAVE_WAYLAND_ONLY)
# include "e_comp_wl.h"
#endif

#define OVER_FLOW 1
//#define SHAPE_DEBUG
//#define BORDER_ZOOMAPS
//////////////////////////////////////////////////////////////////////////
//
// TODO (no specific order):
//   1. abstract evas object and compwin so we can duplicate the object N times
//      in N canvases - for winlist, everything, pager etc. too
//   2. implement "unmapped composite cache" -> N pixels worth of unmapped
//      windows to be fully composited. only the most active/recent.
//   3. for unmapped windows - when window goes out of unmapped comp cache
//      make a miniature copy (1/4 width+height?) and set property on window
//      with pixmap id
//
//////////////////////////////////////////////////////////////////////////

static Eina_List *handlers = NULL;
static Eina_List *hooks = NULL;
EAPI E_Comp *e_comp = NULL;
static Eina_Hash *ignores = NULL;
static Eina_List *actions = NULL;

static E_Comp_Config *conf = NULL;
static E_Config_DD *conf_edd = NULL;
static E_Config_DD *conf_match_edd = NULL;

static Ecore_Timer *action_timeout = NULL;
static Eina_Bool gl_avail = EINA_FALSE;

static double ecore_frametime = 0;

static int _e_comp_log_dom = -1;

EAPI int E_EVENT_COMPOSITOR_RESIZE = -1;
EAPI int E_EVENT_COMPOSITOR_DISABLE = -1;
EAPI int E_EVENT_COMPOSITOR_ENABLE = -1;
EAPI int E_EVENT_COMPOSITOR_FPS_UPDATE = -1;

//////////////////////////////////////////////////////////////////////////
#undef DBG
#undef INF
#undef WRN
#undef ERR
#undef CRI

#if 1
# ifdef SHAPE_DEBUG
#  define SHAPE_DBG(...)            EINA_LOG_DOM_DBG(_e_comp_log_dom, __VA_ARGS__)
#  define SHAPE_INF(...)            EINA_LOG_DOM_INFO(_e_comp_log_dom, __VA_ARGS__)
#  define SHAPE_WRN(...)            EINA_LOG_DOM_WARN(_e_comp_log_dom, __VA_ARGS__)
#  define SHAPE_ERR(...)            EINA_LOG_DOM_ERR(_e_comp_log_dom, __VA_ARGS__)
#  define SHAPE_CRI(...)            EINA_LOG_DOM_CRIT(_e_comp_log_dom, __VA_ARGS__)
# else
#  define SHAPE_DBG(f, x ...)
#  define SHAPE_INF(f, x ...)
#  define SHAPE_WRN(f, x ...)
#  define SHAPE_ERR(f, x ...)
#  define SHAPE_CRI(f, x ...)
# endif

#define DBG(...)            EINA_LOG_DOM_DBG(_e_comp_log_dom, __VA_ARGS__)
#define INF(...)            EINA_LOG_DOM_INFO(_e_comp_log_dom, __VA_ARGS__)
#define WRN(...)            EINA_LOG_DOM_WARN(_e_comp_log_dom, __VA_ARGS__)
#define ERR(...)            EINA_LOG_DOM_ERR(_e_comp_log_dom, __VA_ARGS__)
#define CRI(...)            EINA_LOG_DOM_CRIT(_e_comp_log_dom, __VA_ARGS__)
#else
#define DBG(f, x ...)
#define INF(f, x ...)
#define WRN(f, x ...)
#define ERR(f, x ...)
#define CRI(f, x ...)
#endif

static Eina_Bool
_e_comp_visible_object_clip_is(Evas_Object *obj)
{
   Evas_Object *clip;
   int a;
   
   clip = evas_object_clip_get(obj);
   if (!evas_object_visible_get(clip)) return EINA_FALSE;
   evas_object_color_get(clip, NULL, NULL, NULL, &a);
   if (a <= 0) return EINA_FALSE;
   if (evas_object_clip_get(clip))
     return _e_comp_visible_object_clip_is(clip);
   return EINA_TRUE;
}

static Eina_Bool
_e_comp_visible_object_is(Evas_Object *obj, Evas_Coord x, Evas_Coord y, Evas_Coord w, Evas_Coord h)
{
   const char *type = evas_object_type_get(obj);
   Evas_Coord xx, yy, ww, hh;

   if ((!type) || (!e_util_strcmp(type, "e_comp_object"))) return EINA_FALSE;
   if (evas_object_data_get(obj, "comp_skip")) return EINA_FALSE;
   evas_object_geometry_get(obj, &xx, &yy, &ww, &hh);
   if (E_INTERSECTS(x, y, w, h, xx, yy, ww, hh))
     {
        if ((evas_object_visible_get(obj))
            && (!evas_object_clipees_get(obj))
           )
          {
             int a;
             
             evas_object_color_get(obj, NULL, NULL, NULL, &a);
             if (a > 0)
               {
                  if ((!strcmp(type, "rectangle")) ||
                      (!strcmp(type, "image")) ||
                      (!strcmp(type, "text")) ||
                      (!strcmp(type, "textblock")) ||
                      (!strcmp(type, "textgrid")) ||
                      (!strcmp(type, "polygon")) ||
                      (!strcmp(type, "line")))
                    {
                       if (evas_object_clip_get(obj))
                         return _e_comp_visible_object_clip_is(obj);
                       return EINA_TRUE;
                    }
                  else
                    {
                       Eina_List *children;
                       
                       if ((children = evas_object_smart_members_get(obj)))
                         {
                            Eina_List *l;
                            Evas_Object *o;
                            
                            EINA_LIST_FOREACH(children, l, o)
                              {
                                 if (_e_comp_visible_object_is(o, x, y, w, h))
                                   {
                                      if (evas_object_clip_get(o))
                                        {
                                           eina_list_free(children);
                                           return _e_comp_visible_object_clip_is(o);
                                        }
                                      eina_list_free(children);
                                      return !!evas_object_data_get(o, "comp_skip");
                                   }
                              }
                            eina_list_free(children);
                         }
                    }
               }
          }
     }
   return EINA_FALSE;
}

static Eina_Bool
_e_comp_visible_object_is_above(Evas_Object *obj, Evas_Coord x, Evas_Coord y, Evas_Coord w, Evas_Coord h)
{
   Evas_Object *above;
   
   for (above = evas_object_above_get(obj); above;
        above = evas_object_above_get(above))
     {
        if (_e_comp_visible_object_is(above, x, y, w, h)) return EINA_TRUE;
     }
   return EINA_FALSE;
}

static E_Client *
_e_comp_fullscreen_check(E_Comp *c)
{
   E_Client *ec;

   E_CLIENT_REVERSE_FOREACH(c, ec)
     {
        Evas_Object *o = ec->frame;

        if (ec->ignored || ec->input_only || (!evas_object_visible_get(ec->frame)))
          continue;
        if (!e_comp_util_client_is_fullscreen(ec))
          {
             if (evas_object_data_get(ec->frame, "comp_skip")) continue;
             return NULL;
          }
        while (o)
          {
             if (_e_comp_visible_object_is_above
                 (o, 0, 0, c->man->w, c->man->h)) return NULL;
             o = evas_object_smart_parent_get(o);
          }
        return ec;
     }
   return NULL;
}

static void
_e_comp_fps_update(E_Comp *c)
{
   if (conf->fps_show)
     {
        if (c->fps_bg) return;

        c->fps_bg = evas_object_rectangle_add(c->evas);
        evas_object_color_set(c->fps_bg, 0, 0, 0, 128);
        evas_object_layer_set(c->fps_bg, E_LAYER_MAX);
        evas_object_name_set(c->fps_bg, "c->fps_bg");
        evas_object_lower(c->fps_bg);
        evas_object_show(c->fps_bg);

        c->fps_fg = evas_object_text_add(c->evas);
        evas_object_text_font_set(c->fps_fg, "Sans", 10);
        evas_object_text_text_set(c->fps_fg, "???");
        evas_object_color_set(c->fps_fg, 255, 255, 255, 255);
        evas_object_layer_set(c->fps_fg, E_LAYER_MAX);
        evas_object_name_set(c->fps_bg, "c->fps_fg");
        evas_object_stack_above(c->fps_fg, c->fps_bg);
        evas_object_show(c->fps_fg);
     }
   else
     {
        E_FREE_FUNC(c->fps_fg, evas_object_del);
        E_FREE_FUNC(c->fps_bg, evas_object_del);
     }
}

static void
_e_comp_cb_nocomp_begin(E_Comp *c)
{
   E_Client *ec, *ecf;

   if (c->nocomp) return;

   E_FREE_FUNC(c->nocomp_delay_timer, ecore_timer_del);

   ecf = _e_comp_fullscreen_check(c);
   if (!ecf) return;
   c->nocomp_ec = ecf;
   E_CLIENT_FOREACH(c, ec)
     if (ec != ecf) e_client_redirected_set(ec, 0);

   INF("NOCOMP %p: frame %p", ecf, ecf->frame);
   c->nocomp = 1;

   evas_object_raise(ecf->frame);
   e_client_redirected_set(ecf, 0);

   //ecore_evas_manual_render_set(c->ee, EINA_TRUE);
   ecore_evas_hide(c->ee);
   edje_file_cache_flush();
   edje_collection_cache_flush();
   evas_image_cache_flush(c->evas);
   evas_font_cache_flush(c->evas);
   evas_render_dump(c->evas);

   DBG("JOB2...");
   e_comp_render_queue(c);
   e_comp_shape_queue_block(c, 1);
   ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
}

static void
_e_comp_cb_nocomp_end(E_Comp *c)
{
   E_Client *ec;

   if (!c->nocomp) return;

   INF("COMP RESUME!");
   //ecore_evas_manual_render_set(c->ee, EINA_FALSE);
   ecore_evas_show(c->ee);
   E_CLIENT_FOREACH(c, ec)
     {
        e_client_redirected_set(ec, 1);
        if (ec->visible && (!ec->input_only))
          e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
     }
#ifndef HAVE_WAYLAND_ONLY
   e_comp_x_nocomp_end(c);
#endif
   e_comp_render_queue(c);
   e_comp_shape_queue_block(c, 0);
   ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
}

static Eina_Bool
_e_comp_cb_nocomp_begin_timeout(void *data)
{
   E_Comp *c = data;

   c->nocomp_delay_timer = NULL;
   if (c->nocomp_override == 0)
     {
        if (_e_comp_fullscreen_check(c)) c->nocomp_want = 1;
        _e_comp_cb_nocomp_begin(c);
     }
   return EINA_FALSE;
}


static Eina_Bool
_e_comp_client_update(E_Client *ec)
{
   int pw, ph;
   Eina_Bool post = EINA_FALSE;

   DBG("UPDATE [%p] pm = %p", ec, ec->pixmap);
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   e_pixmap_size_get(ec->pixmap, &pw, &ph);

   if (e_pixmap_dirty_get(ec->pixmap) && (!ec->comp->nocomp))
     {
        int w, h;

        if (e_pixmap_refresh(ec->pixmap) &&
            e_pixmap_size_get(ec->pixmap, &w, &h) &&
            e_pixmap_size_changed(ec->pixmap, pw, ph))
          {
             e_pixmap_image_clear(ec->pixmap, 0);
             post = EINA_TRUE;
             e_comp_object_render_update_del(ec->frame); //clear update
          }
        else if (!e_pixmap_size_get(ec->pixmap, NULL, NULL))
          {
             WRN("FAIL %p", ec);
             e_comp_object_redirected_set(ec->frame, 0);
             if (e_pixmap_failures_get(ec->pixmap) < 3)
               e_comp_object_render_update_add(ec->frame);
          }
     }
   if ((!ec->comp->saver) && e_pixmap_size_get(ec->pixmap, &pw, &ph))
     {
        //INF("PX DIRTY: PX(%dx%d) CLI(%dx%d)", pw, ph, ec->client.w, ec->client.h);
        e_pixmap_image_refresh(ec->pixmap);
        e_comp_object_dirty(ec->frame);
        if (e_pixmap_is_x(ec->pixmap) && (!ec->override))
          evas_object_resize(ec->frame, ec->w, ec->h);
     }
   return post;
}

static void
_e_comp_nocomp_end(E_Comp *c)
{
   c->nocomp_want = 0;
   E_FREE_FUNC(c->nocomp_delay_timer, ecore_timer_del);
   _e_comp_cb_nocomp_end(c);
   c->nocomp_ec = NULL;
}

static Eina_Bool
_e_comp_cb_update(E_Comp *c)
{
   E_Client *ec;
   Eina_List *l;
   //   static int doframeinfo = -1;

   if (!c) return EINA_FALSE;
   if (c->update_job)
     c->update_job = NULL;
   else
     ecore_animator_freeze(c->render_animator);
   DBG("UPDATE ALL");
   if (c->nocomp) goto nocomp;
   if (conf->grab && (!c->grabbed))
     {
        if (c->grab_cb) c->grab_cb(c);
        c->grabbed = 1;
     }
   l = c->updates;
   c->updates = NULL;
   EINA_LIST_FREE(l, ec)
     {
        /* clear update flag */
        e_comp_object_render_update_del(ec->frame);
        if (_e_comp_client_update(ec))
          e_comp_post_update_add(ec);
     }
   _e_comp_fps_update(c);
   if (conf->fps_show)
     {
        char buf[128];
        double fps = 0.0, t, dt;
        int i;
        Evas_Coord x = 0, y = 0, w = 0, h = 0;
        E_Zone *z;

        t = ecore_time_get();
        if (conf->fps_average_range < 1)
          conf->fps_average_range = 30;
        else if (conf->fps_average_range > 120)
          conf->fps_average_range = 120;
        dt = t - c->frametimes[conf->fps_average_range - 1];
        if (dt > 0.0) fps = (double)conf->fps_average_range / dt;
        else fps = 0.0;
        if (fps > 0.0) snprintf(buf, sizeof(buf), "FPS: %1.1f", fps);
        else snprintf(buf, sizeof(buf), "N/A");
        for (i = 121; i >= 1; i--)
          c->frametimes[i] = c->frametimes[i - 1];
        c->frametimes[0] = t;
        c->frameskip++;
        if (c->frameskip >= conf->fps_average_range)
          {
             c->frameskip = 0;
             evas_object_text_text_set(c->fps_fg, buf);
          }
        evas_object_geometry_get(c->fps_fg, NULL, NULL, &w, &h);
        w += 8;
        h += 8;
        z = e_zone_current_get(c);
        if (z)
          {
             switch (conf->fps_corner)
               {
                case 3: // bottom-right
                  x = z->x + z->w - w;
                  y = z->y + z->h - h;
                  break;

                case 2: // bottom-left
                  x = z->x;
                  y = z->y + z->h - h;
                  break;

                case 1: // top-right
                  x = z->x + z->w - w;
                  y = z->y;
                  break;
                default: // 0 // top-left
                  x = z->x;
                  y = z->y;
                  break;
               }
          }
        evas_object_move(c->fps_bg, x, y);
        evas_object_resize(c->fps_bg, w, h);
        evas_object_move(c->fps_fg, x + 4, y + 4);
     }
   else
     {
        if (c->calc_fps)
          {
             double fps = 0.0, dt;
             double t = ecore_time_get();
             int i, avg_range = 60;

             dt = t - c->frametimes[avg_range - 1];

             if (dt > 0.0) fps = (double)avg_range / dt;
             else fps = 0.0;

             if (fps > 60.0) fps = 60.0;
             if (fps < 0.0) fps = 0.0;

             for (i = avg_range; i >= 1; i--)
               c->frametimes[i] = c->frametimes[i - 1];

             c->frametimes[0] = t;
             c->frameskip++;
             if (c->frameskip >= avg_range)
               {
                  c->frameskip = 0;
				  c->fps = fps;
				  ecore_event_add(E_EVENT_COMPOSITOR_FPS_UPDATE, NULL, NULL, NULL);
               }
          }
     }
   if (conf->lock_fps)
     {
        DBG("MANUAL RENDER...");
        //        if (!c->nocomp) ecore_evas_manual_render(c->ee);
     }

   if (conf->grab && c->grabbed)
     {
        if (c->grab_cb) c->grab_cb(c);
        c->grabbed = 0;
     }
   if (c->updates && (!c->update_job))
     ecore_animator_thaw(c->render_animator);
   /*
      if (doframeinfo == -1)
      {
      doframeinfo = 0;
      if (getenv("DFI")) doframeinfo = 1;
      }
      if (doframeinfo)
      {
      static double t0 = 0.0;
      double td, t;

      t = ecore_time_get();
      td = t - t0;
      if (td > 0.0)
      {
      int fps, i;

      fps = 1.0 / td;
      for (i = 0; i < fps; i+= 2) putchar('=');
      printf(" : %3.3f", 1.0 / td);
      }
      t0 = t;
      }
    */
nocomp:
   ec = _e_comp_fullscreen_check(c);
   if (ec)
     {
        if (conf->nocomp_fs)
          {
             if (c->nocomp && c->nocomp_ec)
               {
                  E_Client *nec = NULL;
                  for (ec = e_client_top_get(c), nec = e_client_below_get(ec);
                       (ec && nec) && (ec != nec); ec = nec, nec = e_client_below_get(ec))
                    {
                       if (ec == c->nocomp_ec) break;
                       if (evas_object_layer_get(ec->frame) < evas_object_layer_get(c->nocomp_ec->frame)) break;
                       if (e_client_is_stacking(ec)) continue;
                       if (!ec->visible) continue;
                       if (evas_object_data_get(ec->frame, "comp_skip")) continue;
                       if (e_object_is_del(E_OBJECT(ec)) || (!e_client_util_desk_visible(ec, e_desk_current_get(ec->zone)))) continue;
                       if (ec->override || (e_config->allow_above_fullscreen && (!e_config->mode.presentation)))
                         {
                            _e_comp_nocomp_end(c);
                            break;
                         }
                       else
                         evas_object_stack_below(ec->frame, c->nocomp_ec->frame);
                    }
               }
             else if ((!c->nocomp) && (!c->nocomp_override > 0))
               {
                  if (!c->nocomp_delay_timer)
                    c->nocomp_delay_timer = ecore_timer_add(1.0, _e_comp_cb_nocomp_begin_timeout, c);
               }
          }
     }
   else
     _e_comp_nocomp_end(c);

   return ECORE_CALLBACK_RENEW;
}

static void
_e_comp_cb_job(void *data)
{
   DBG("UPDATE ALL JOB...");
   _e_comp_cb_update(data);
}

static Eina_Bool
_e_comp_cb_animator(void *data)
{
   return _e_comp_cb_update(data);
}

//////////////////////////////////////////////////////////////////////////


#ifdef SHAPE_DEBUG
static void
_e_comp_shape_debug_rect(E_Comp *c, Eina_Rectangle *rect, E_Color *color)
{
   Evas_Object *o;

#define COLOR_INCREMENT 30
   o = evas_object_rectangle_add(c->evas);
   if (color->r < 256 - COLOR_INCREMENT)
     evas_object_color_set(o, (color->r += COLOR_INCREMENT), 0, 0, 255);
   else if (color->g < 256 - COLOR_INCREMENT)
     evas_object_color_set(o, 0, (color->g += COLOR_INCREMENT), 0, 255);
   else
     evas_object_color_set(o, 0, 0, (color->b += COLOR_INCREMENT), 255);
   evas_object_repeat_events_set(o, 1);
   evas_object_layer_set(o, E_LAYER_MENU - 1);
   evas_object_move(o, rect->x, rect->y);
   evas_object_resize(o, rect->w, rect->h);
   c->debug_rects = eina_list_append(c->debug_rects, o);
   evas_object_show(o);
}
#endif

static Eina_Bool
_e_comp_shapes_update_object_checker_function_thingy(E_Comp *c, Evas_Object *o)
{
   Eina_List *l;
   E_Zone *zone;

   if (o == c->bg_blank_object) return EINA_TRUE;
   EINA_LIST_FOREACH(c->zones, l, zone)
     {
        if ((o == zone->over) || (o == zone->base)) return EINA_TRUE;
        if ((o == zone->bg_object) || (o == zone->bg_event_object) ||
            (o == zone->bg_clip_object) || (o == zone->prev_bg_object) ||
            (o == zone->transition_object))
          return EINA_TRUE;
     }
   return EINA_FALSE;
}

static void
#ifdef SHAPE_DEBUG
_e_comp_shapes_update_comp_client_shape_comp_helper(E_Client *ec, Eina_Tiler *tb, Eina_List **rl)
#else
_e_comp_shapes_update_comp_client_shape_comp_helper(E_Client *ec, Eina_Tiler *tb)
#endif
{
   int x, y, w, h;

   /* ignore deleted shapes */
   if (e_object_is_del(E_OBJECT(ec)))
     {
        SHAPE_INF("IGNORING DELETED: %p", ec);
        return;
     }
   if ((!ec->visible) || (ec->hidden) || (!evas_object_visible_get(ec->frame)) || evas_object_pass_events_get(ec->frame))
     {
        SHAPE_DBG("SKIPPING SHAPE FOR %p", ec);
        return;
     }
#ifdef SHAPE_DEBUG
   INF("COMP EC: %p", ec);
#endif

   if (ec->shaped || ec->shaped_input)
     {
        int num, tot;
        int l, r, t, b;
        Eina_Rectangle *rect, *rects;

        /* add the frame */
        e_comp_object_frame_geometry_get(ec->frame, &l, &r, &t, &b);
        e_comp_object_frame_extends_get(ec->frame, &x, &y, &w, &h);
        if ((l + x) || (r + (w - ec->w + x)) || (t - y) || (b + (h - ec->h + y)))
          {
             if (t - y)
               {
                  eina_tiler_rect_add(tb, &(Eina_Rectangle){ec->x + x, ec->y + y, w, t - y});
                  SHAPE_INF("ADD: %d,%d@%dx%d", ec->x + x, ec->y + y, w, t - y);
               }
             if (l - x)
               {
                  eina_tiler_rect_add(tb, &(Eina_Rectangle){ec->x + x, ec->y + y, l - x, h});
                  SHAPE_INF("ADD: %d,%d@%dx%d", ec->x + x, ec->y + y, l - x, h);
               }
             if (r + (w - ec->w + x))
               {
                  eina_tiler_rect_add(tb, &(Eina_Rectangle){ec->x + l + ec->client.w + x, ec->y + y, r + (w - ec->w + x), h});
                  SHAPE_INF("ADD: %d,%d@%dx%d", ec->x + l + ec->client.w + x, ec->y + y, r + (w - ec->w + x), h);
               }
             if (b + (h - ec->h + y))
               {
                  eina_tiler_rect_add(tb, &(Eina_Rectangle){ec->x + x, ec->y + t + ec->client.h + y, w, b + (h - ec->h + y)});
                  SHAPE_INF("ADD: %d,%d@%dx%d", ec->x + x, ec->y + t + ec->client.h + y, w, b + (h - ec->h + y));
               }
          }
        rects = ec->shape_rects ?: ec->shape_input_rects;
        tot = ec->shape_rects_num ?: ec->shape_input_rects_num;
        for (num = 0, rect = rects; num < tot; num++, rect++)
          {
             x = rect->x, y = rect->y, w = rect->w, h = rect->h;
             x += ec->client.x, y += ec->client.y;
             E_RECTS_CLIP_TO_RECT(x, y, w, h, ec->comp->man->x, ec->comp->man->y, ec->comp->man->w, ec->comp->man->h);
             if ((w < 1) || (h < 1)) continue;
   //#ifdef SHAPE_DEBUG not sure we can shape check these?
             //r = E_NEW(Eina_Rectangle, 1);
             //EINA_RECTANGLE_SET(r, x, y, w, h);
             //rl = eina_list_append(rl, r);
   //#endif
             eina_tiler_rect_del(tb, &(Eina_Rectangle){x, y, w, h});
             SHAPE_INF("DEL: %d,%d@%dx%d", x, y, w, h);
          }
        return;
     }

#ifdef SHAPE_DEBUG
     {
        Eina_Rectangle *r;

        r = E_NEW(Eina_Rectangle, 1);
        EINA_RECTANGLE_SET(r, ec->client.x, ec->client.y, ec->client.w, ec->client.h);
        *rl = eina_list_append(*rl, r);
     }
#endif

   if (!e_client_util_borderless(ec))
     {
        e_comp_object_frame_extends_get(ec->frame, &x, &y, &w, &h);
        /* add the frame */
        eina_tiler_rect_add(tb, &(Eina_Rectangle){ec->x + x, ec->y + y, w, h});
        SHAPE_INF("ADD: %d,%d@%dx%d", ec->x + x, ec->y + y, w, h);
     }

   if ((!ec->shaded) && (!ec->shading))
     {
        /* delete the client if not shaded */
        eina_tiler_rect_del(tb, &(Eina_Rectangle){ec->client.x, ec->client.y, ec->client.w, ec->client.h});
        SHAPE_INF("DEL: %d,%d@%dx%d", ec->client.x, ec->client.y, ec->client.w, ec->client.h);
     }
}

static void
_e_comp_shapes_update_object_shape_comp_helper(E_Comp *c, Evas_Object *o, Eina_Tiler *tb)
{
   int x, y, w, h;

   /* ignore hidden and pass-event objects */
   if ((!evas_object_visible_get(o)) || evas_object_pass_events_get(o) || evas_object_repeat_events_get(o)) return;
   /* ignore canvas objects */
   if (_e_comp_shapes_update_object_checker_function_thingy(c, o)) return;
   SHAPE_INF("OBJ: %p:%s", o, evas_object_name_get(o));
   evas_object_geometry_get(o, &x, &y, &w, &h);
   eina_tiler_rect_add(tb, &(Eina_Rectangle){x, y, w, h});
   SHAPE_INF("ADD: %d,%d@%dx%d", x, y, w, h);
}

static void
_e_comp_shapes_update_job(E_Comp *c)
{
   Eina_Tiler *tb;
   E_Client *ec;
   Evas_Object *o = NULL;
   Eina_Rectangle *tr;
   Eina_Iterator *ti;
   Eina_Rectangle *exr;
   unsigned int i, tile_count;
#ifdef SHAPE_DEBUG
   Eina_Rectangle *r;
   Eina_List *rl = NULL;
   E_Color color = {0};

   INF("---------------------");
#endif

   E_FREE_LIST(c->debug_rects, evas_object_del);
   tb = eina_tiler_new(c->man->w, c->man->h);
   EINA_SAFETY_ON_NULL_GOTO(tb, tb_fail);

   eina_tiler_tile_size_set(tb, 1, 1);
   /* background */
   eina_tiler_rect_add(tb, &(Eina_Rectangle){0, 0, c->man->w, c->man->h});

   ec = e_client_bottom_get(c);
   if (ec) o = ec->frame;
   for (; o; o = evas_object_above_get(o))
     {
        int layer;

        layer = evas_object_layer_get(o);
        if (e_comp_canvas_client_layer_map(layer) == 9999) //not a client layer
          {
             _e_comp_shapes_update_object_shape_comp_helper(c, o, tb);
             continue;
          }
        ec = e_comp_object_client_get(o);
        if (ec && (!ec->no_shape_cut))
          _e_comp_shapes_update_comp_client_shape_comp_helper(ec, tb
#ifdef SHAPE_DEBUG
                                                           ,&rl
#endif
                                                          );

        else
          _e_comp_shapes_update_object_shape_comp_helper(c, o, tb);
     }

   ti = eina_tiler_iterator_new(tb);
   EINA_SAFETY_ON_NULL_GOTO(ti, ti_fail);
   tile_count = 128;

   exr = malloc(sizeof(Eina_Rectangle) * tile_count);
   EINA_SAFETY_ON_NULL_GOTO(exr, exr_fail);

   i = 0;
   EINA_ITERATOR_FOREACH(ti, tr)
     {
        exr[i++] = *(Eina_Rectangle*)((char*)tr);
        if (i == tile_count - 1)
          {
             exr = realloc(exr, sizeof(Eina_Rectangle) * (tile_count *= 2));
             EINA_SAFETY_ON_NULL_GOTO(exr, exr_fail);
          }
#ifdef SHAPE_DEBUG
        Eina_List *l;

        _e_comp_shape_debug_rect(c, &exr[i - 1], &color);
        INF("%d,%d @ %dx%d", exr[i - 1].x, exr[i - 1].y, exr[i - 1].w, exr[i - 1].h);
        EINA_LIST_FOREACH(rl, l, r)
          {
             if (E_INTERSECTS(r->x, r->y, r->w, r->h, tr->x, tr->y, tr->w, tr->h))
               ERR("POSSIBLE RECT FAIL!!!!");
          }
#endif
     }

#ifndef HAVE_WAYLAND_ONLY
   ecore_x_window_shape_input_rectangles_set(c->win, (Ecore_X_Rectangle*)exr, i);
#endif

exr_fail:
   free(exr);
ti_fail:
   eina_iterator_free(ti);
#ifdef SHAPE_DEBUG
   E_FREE_LIST(rl, free);
   printf("\n");
#endif
tb_fail:
   eina_tiler_free(tb);
   c->shape_job = NULL;
}

//////////////////////////////////////////////////////////////////////////


static Eina_Bool
_e_comp_key_down(void *data EINA_UNUSED, int type EINA_UNUSED, Ecore_Event_Key *ev)
{
   if ((!strcasecmp(ev->key, "f")) &&
       (ev->modifiers & ECORE_EVENT_MODIFIER_SHIFT) &&
       (ev->modifiers & ECORE_EVENT_MODIFIER_CTRL) &&
       (ev->modifiers & ECORE_EVENT_MODIFIER_ALT))
     {
        e_comp_canvas_fps_toggle();
     }
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_comp_signal_user(void *data EINA_UNUSED, int type EINA_UNUSED, Ecore_Event_Signal_User *ev)
{
   if (ev->number == 1)
     {
        // e uses this to pop up config panel
     }
   else if (ev->number == 2)
     {
        e_comp_canvas_fps_toggle();
     }
   return ECORE_CALLBACK_PASS_ON;
}

//////////////////////////////////////////////////////////////////////////

static void
_e_comp_free(E_Comp *c)
{
   E_FREE_LIST(c->zones, e_object_del);

   e_comp_canvas_clear(c);

   ecore_evas_free(c->ee);
   eina_stringshare_del(c->name);

   if (c->render_animator) ecore_animator_del(c->render_animator);
   if (c->update_job) ecore_job_del(c->update_job);
   if (c->screen_job) ecore_job_del(c->screen_job);
   if (c->nocomp_delay_timer) ecore_timer_del(c->nocomp_delay_timer);
   if (c->nocomp_override_timer) ecore_timer_del(c->nocomp_override_timer);
   ecore_job_del(c->shape_job);

   free(c);
}

//////////////////////////////////////////////////////////////////////////

static Eina_Bool
_e_comp_override_expire(void *data)
{
   E_Comp *c = data;

   c->nocomp_override_timer = NULL;
   c->nocomp_override--;

   if (c->nocomp_override <= 0)
     {
        c->nocomp_override = 0;
        if (c->nocomp_want) _e_comp_cb_nocomp_begin(c);
     }
   return EINA_FALSE;
}

//////////////////////////////////////////////////////////////////////////

static Eina_Bool
_e_comp_screensaver_on(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   Eina_List *l;
   E_Zone *zone;

   printf("_e_comp_screensaver_on\n");
   ecore_frametime = ecore_animator_frametime_get();
   if (e_comp->saver) return ECORE_CALLBACK_RENEW;
   e_comp_override_add(e_comp);
   e_comp->saver = EINA_TRUE;
   if (e_comp->render_animator)
     ecore_animator_freeze(e_comp->render_animator);
   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        e_zone_fade_handle(zone, 1, 3.0);
        edje_object_signal_emit(zone->base, "e,state,screensaver,on", "e");
        edje_object_signal_emit(zone->over, "e,state,screensaver,on", "e");
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_comp_screensaver_off(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   Eina_List *l;
   E_Zone *zone;
   E_Client *ec;

   printf("_e_comp_screensaver_off\n");
   ecore_animator_frametime_set(ecore_frametime);
   if (!e_comp->saver) return ECORE_CALLBACK_RENEW;
   e_comp_override_del(e_comp);
   e_comp->saver = EINA_FALSE;
   if (!e_comp->nocomp)
     ecore_evas_manual_render_set(e_comp->ee, EINA_FALSE);
   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        edje_object_signal_emit(zone->base, "e,state,screensaver,off", "e");
        edje_object_signal_emit(zone->over, "e,state,screensaver,off", "e");
        e_zone_fade_handle(zone, 0, 0.5);
     }
   E_CLIENT_FOREACH(e_comp, ec)
     if (e_comp_object_damage_exists(ec->frame))
       e_comp_object_render_update_add(ec->frame);

   return ECORE_CALLBACK_PASS_ON;
}

static Evas_Object *
_e_comp_act_opacity_obj_finder(E_Object *obj)
{
   E_Client *ec;

   switch (obj->type)
     {
      case E_CLIENT_TYPE:
        return ((E_Client*)obj)->frame;
      case E_ZONE_TYPE:
      case E_MANAGER_TYPE:
      case E_MENU_TYPE:
        ec = e_client_focused_get();
        return ec ? ec->frame : NULL;
     }
   if (e_obj_is_win(obj))
     {
        ec = e_win_client_get((Evas_Object *)obj);
        return ec ? ec->frame : NULL;
     }
   ec = e_client_focused_get();
   return ec ? ec->frame : NULL;
}

static void
_e_comp_act_opacity_change_go(E_Object *obj, const char *params)
{
   int opacity, cur;
   Evas_Object *o;

   if ((!params) || (!params[0])) return;
   o = _e_comp_act_opacity_obj_finder(obj);
   if (!o) return;
   opacity = atoi(params);
   opacity = E_CLAMP(opacity, -255, 255);
   evas_object_color_get(o, NULL, NULL, NULL, &cur);
   opacity += cur;
   opacity = E_CLAMP(opacity, 0, 255);
   evas_object_color_set(o, opacity, opacity, opacity, opacity);
}

static void
_e_comp_act_opacity_set_go(E_Object * obj __UNUSED__, const char *params)
{
   int opacity;
   Evas_Object *o;

   if ((!params) || (!params[0])) return;
   o = _e_comp_act_opacity_obj_finder(obj);
   if (!o) return;
   opacity = atoi(params);
   opacity = E_CLAMP(opacity, 0, 255);
   evas_object_color_set(o, opacity, opacity, opacity, opacity);
}

static void
_e_comp_act_redirect_toggle_go(E_Object * obj EINA_UNUSED, const char *params EINA_UNUSED)
{
   e_comp_client_redirect_toggle(e_client_focused_get());
}

//////////////////////////////////////////////////////////////////////////

EINTERN Eina_Bool
e_comp_init(void)
{
   _e_comp_log_dom = eina_log_domain_register("e_comp", EINA_COLOR_YELLOW);
   eina_log_domain_level_set("e_comp", EINA_LOG_LEVEL_INFO);

   ecore_frametime = ecore_animator_frametime_get();

   E_EVENT_COMPOSITOR_RESIZE = ecore_event_type_new();
   E_EVENT_COMP_OBJECT_ADD = ecore_event_type_new();
   E_EVENT_COMPOSITOR_DISABLE = ecore_event_type_new();
   E_EVENT_COMPOSITOR_ENABLE = ecore_event_type_new();
   E_EVENT_COMPOSITOR_FPS_UPDATE = ecore_event_type_new();

   ignores = eina_hash_pointer_new(NULL);

   e_comp_cfdata_edd_init(&conf_edd, &conf_match_edd);
   conf = e_config_domain_load("e_comp", conf_edd);
   if (conf)
     {
        conf->max_unmapped_pixels = 32 * 1024;
        conf->keep_unmapped = 1;
     }
   else
     conf = e_comp_cfdata_config_new();

   // comp config versioning - add this in. over time add epochs etc. if
   // necessary, but for now a simple version number will do
   if (conf->version < E_COMP_VERSION)
     {
        switch (conf->version)
          {
           case 0:
             // going from version 0 we should disable grab for smoothness
             conf->grab = 0;
             /* fallthrough */
           default:
             break;
          }
        e_config_save_queue();
        conf->version = E_COMP_VERSION;
     }

   {
      E_Action *act;

      if ((act = e_action_add("opacity_change")))
        {
           act->func.go = _e_comp_act_opacity_change_go;
           e_action_predef_name_set(N_("Compositor"),
                                    N_("Change current window opacity"), "opacity_change",
                                    NULL, "syntax: +/- the amount to change opacity by (>0 for more opaque)", 1);
           actions = eina_list_append(actions, act);
        }

      if ((act = e_action_add("opacity_set")))
        {
           act->func.go = _e_comp_act_opacity_set_go;
           e_action_predef_name_set(N_("Compositor"),
                                    N_("Set current window opacity"), "opacity_set",
                                    "255", "syntax: number between 0-255 to set for transparent-opaque", 1);
           actions = eina_list_append(actions, act);
        }

      if ((act = e_action_add("redirect_toggle")))
        {
           act->func.go = _e_comp_act_redirect_toggle_go;
           e_action_predef_name_set(N_("Compositor"),
                                    N_("Toggle focused client's redirect state"), "redirect_toggle",
                                    NULL, NULL, 0);
           actions = eina_list_append(actions, act);
        }
   }

   {
      const char *eng;
      
      eng = getenv("E_WL_FORCE");
      if (eng)
        {
           char buf[128];

           snprintf(buf, sizeof(buf), "wl_%s", eng);
           if (e_module_enable(e_module_new(buf)))
             goto out;
        }
   }

#ifndef HAVE_WAYLAND_ONLY
   if (!e_comp_x_init())
#endif
     {
        const char **test, *eng[] =
        {
#ifdef HAVE_WL_DRM
           "wl_drm",
#endif
/* probably add other engines here; fb should be last? */
#ifdef HAVE_WL_FM
           "wl_fb",
#endif
           "wl_desktop_shell",
           NULL
        };

        e_util_env_set("HYBRIS_EGLPLATFORM", "wayland");
        for (test = eng; *test; test++)
          {
             if (!e_module_enable(e_module_new(*test)))
               return EINA_FALSE;
          }
        goto out;
     }
#if defined(HAVE_WAYLAND_CLIENTS) || defined(HAVE_WAYLAND_ONLY)
   e_comp_wl_init();
#endif
   if (!e_comp) return EINA_FALSE;
out:
   e_comp->elm = elm_win_fake_add(e_comp->ee);
   evas_object_show(e_comp->elm);
   e_util_env_set("HYBRIS_EGLPLATFORM", NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_SCREENSAVER_ON, _e_comp_screensaver_on, NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_SCREENSAVER_OFF, _e_comp_screensaver_off, NULL);

   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_KEY_DOWN, _e_comp_key_down, NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_SIGNAL_USER, _e_comp_signal_user, NULL);

   return EINA_TRUE;
}


static Eina_Bool
_style_demo(void *data)
{
   Eina_List *style_shadows, *l;
   int demo_state;
   const E_Comp_Demo_Style_Item *it;

   demo_state = (long)evas_object_data_get(data, "style_demo_state");
   demo_state = (demo_state + 1) % 4;
   evas_object_data_set(data, "style_demo_state", (void *)(long)demo_state);

   style_shadows = evas_object_data_get(data, "style_shadows");
   EINA_LIST_FOREACH(style_shadows, l, it)
     {
        Evas_Object *ob = it->preview;
        Evas_Object *of = it->frame;

        switch (demo_state)
          {
           case 0:
             edje_object_signal_emit(ob, "e,state,visible", "e");
             edje_object_signal_emit(ob, "e,state,focused", "e");
             edje_object_part_text_set(of, "e.text.label", _("Visible"));
             break;

           case 1:
             edje_object_signal_emit(ob, "e,state,unfocused", "e");
             edje_object_part_text_set(of, "e.text.label", _("Focus-Out"));
             break;

           case 2:
             edje_object_signal_emit(ob, "e,state,focused", "e");
             edje_object_part_text_set(of, "e.text.label", _("Focus-In"));
             break;

           case 3:
             edje_object_signal_emit(ob, "e,state,hidden", "e");
             edje_object_part_text_set(of, "e.text.label", _("Hidden"));
             break;

           default:
             break;
          }
     }
   return ECORE_CALLBACK_RENEW;
}

static void
_style_selector_del(void *data       __UNUSED__,
                    Evas *e,
                    Evas_Object *o,
                    void *event_info __UNUSED__)
{
   Eina_List *style_shadows, *style_list;
   Ecore_Timer *timer;
   Evas_Object *orec0;

   orec0 = evas_object_name_find(e, "style_shadows");
   style_list = evas_object_data_get(orec0, "list");

   style_shadows = evas_object_data_get(o, "style_shadows");
   if (style_shadows)
     {
        E_Comp_Demo_Style_Item *ds_it;

        EINA_LIST_FREE(style_shadows, ds_it)
          {
             style_list = eina_list_remove(style_list, ds_it);

             evas_object_del(ds_it->frame);
             evas_object_del(ds_it->livethumb);
             free(ds_it);
          }
        evas_object_data_set(o, "style_shadows", NULL);
     }

   timer = evas_object_data_get(o, "style_timer");
   if (timer)
     {
        ecore_timer_del(timer);
        evas_object_data_set(o, "style_timer", NULL);
     }

   evas_object_data_set(orec0, "list", style_list);
}

EINTERN Evas_Object *
e_comp_style_selector_create(Evas *evas, const char **source)
{
   Evas_Object *oi, *ob, *oo, *obd, *orec, *oly, *orec0;
   Eina_List *styles, *l, *style_shadows = NULL, *style_list;
   char *style;
   const char *str;
   int n, sel;
   Evas_Coord wmw, wmh;
   Ecore_Timer *timer;

   orec0 = evas_object_name_find(evas, "style_shadows");
   style_list = evas_object_data_get(orec0, "list");
   oi = e_widget_ilist_add(evas, 80, 80, source);
   evas_object_event_callback_add(oi, EVAS_CALLBACK_DEL,
                                  _style_selector_del, oi);
   sel = 0;
   styles = e_theme_comp_frame_list();
   n = 0;
   EINA_LIST_FOREACH(styles, l, style)
     {
        E_Comp_Demo_Style_Item *ds_it;
        char buf[4096];

        ds_it = malloc(sizeof(E_Comp_Demo_Style_Item));

        ob = e_livethumb_add(evas);
        ds_it->livethumb = ob;
        e_livethumb_vsize_set(ob, 240, 240);

        oly = e_layout_add(e_livethumb_evas_get(ob));
        ds_it->layout = ob;
        e_layout_virtual_size_set(oly, 240, 240);
        e_livethumb_thumb_set(ob, oly);
        evas_object_show(oly);

        oo = edje_object_add(e_livethumb_evas_get(ob));
        ds_it->preview = oo;
        snprintf(buf, sizeof(buf), "e/comp/frame/%s", style);
        e_theme_edje_object_set(oo, "base/theme/borders", buf);
        e_layout_pack(oly, oo);
        e_layout_child_move(oo, 39, 39);
        e_layout_child_resize(oo, 162, 162);
        edje_object_signal_emit(oo, "e,state,visible", "e");
        edje_object_signal_emit(oo, "e,state,focused", "e");
        evas_object_show(oo);

        ds_it->frame = edje_object_add(evas);
        e_theme_edje_object_set
          (ds_it->frame, "base/theme/comp", "e/comp/preview");
        edje_object_part_swallow(ds_it->frame, "e.swallow.preview", ob);
        evas_object_show(ds_it->frame);
        style_shadows = eina_list_append(style_shadows, ds_it);

        obd = edje_object_add(e_livethumb_evas_get(ob));
        ds_it->border = obd;
        e_theme_edje_object_set(obd, "base/theme/borders",
                                "e/widgets/border/default/border");
        edje_object_part_text_set(obd, "e.text.title", _("Title"));
        edje_object_signal_emit(obd, "e,state,shadow,on", "e");
        edje_object_part_swallow(oo, "e.swallow.content", obd);
        evas_object_show(obd);

        orec = evas_object_rectangle_add(e_livethumb_evas_get(ob));
        ds_it->client = orec;
        evas_object_color_set(orec, 0, 0, 0, 128);
        edje_object_part_swallow(obd, "e.swallow.client", orec);
        evas_object_show(orec);

        e_widget_ilist_append(oi, ds_it->frame, style, NULL, NULL, style);
        evas_object_show(ob);
        if (*source)
          {
             if (!strcmp(*source, style)) sel = n;
          }
        n++;

        style_list = eina_list_append(style_list, ds_it);
     }
   evas_object_data_set(orec0, "list", style_list);
   evas_object_data_set(oi, "style_shadows", style_shadows);
   timer = ecore_timer_add(3.0, _style_demo, oi);
   evas_object_data_set(oi, "style_timer", timer);
   evas_object_data_set(oi, "style_demo_state", (void *)1);
   e_widget_size_min_get(oi, &wmw, &wmh);
   e_widget_size_min_set(oi, 160, 100);
   e_widget_ilist_selected_set(oi, sel);
   e_widget_ilist_go(oi);

   EINA_LIST_FREE(styles, str)
     eina_stringshare_del(str);

   return oi;
}

EAPI E_Comp *
e_comp_new(void)
{
   E_Comp *c;

   if (e_comp)
     CRI("CANNOT REPLACE EXISTING COMPOSITOR");
   c = E_OBJECT_ALLOC(E_Comp, E_COMP_TYPE, _e_comp_free);
   if (!c) return NULL;

   c->name = eina_stringshare_add(_("Compositor"));
   c->render_animator = ecore_animator_add(_e_comp_cb_animator, c);
   ecore_animator_freeze(c->render_animator);
   e_comp = c;
   return c;
}

EAPI int
e_comp_internal_save(void)
{
   return e_config_domain_save("e_comp", conf_edd, conf);
}

EINTERN int
e_comp_shutdown(void)
{
   E_FREE_FUNC(action_timeout, ecore_timer_del);
   while (e_comp->clients)
     e_object_del(eina_list_data_get(e_comp->clients));
#if defined(HAVE_WAYLAND_CLIENTS) || defined(HAVE_WAYLAND_ONLY)
   e_comp_wl_shutdown();
#endif
   e_object_del(E_OBJECT(e_comp));
   E_FREE_LIST(handlers, ecore_event_handler_del);
   E_FREE_LIST(actions, e_object_del);
   E_FREE_LIST(hooks, e_client_hook_del);

   gl_avail = EINA_FALSE;
   e_comp_cfdata_config_free(conf);
   E_CONFIG_DD_FREE(conf_match_edd);
   E_CONFIG_DD_FREE(conf_edd);
   conf = NULL;
   conf_match_edd = NULL;
   conf_edd = NULL;

   E_FREE_FUNC(ignores, eina_hash_free);

   return 1;
}

EAPI void
e_comp_render_queue(E_Comp *c)
{
   E_OBJECT_CHECK(c);
   E_OBJECT_TYPE_CHECK(c, E_COMP_TYPE);

   if (conf->lock_fps)
     {
        ecore_animator_thaw(c->render_animator);
     }
   else
     {
        if (c->update_job)
          {
             DBG("UPDATE JOB DEL...");
             E_FREE_FUNC(c->update_job, ecore_job_del);
          }
        DBG("UPDATE JOB ADD...");
        c->update_job = ecore_job_add(_e_comp_cb_job, c);
     }
}

EAPI void
e_comp_shape_queue(E_Comp *c)
{
   EINA_SAFETY_ON_NULL_RETURN(c);

   if (c->comp_type != E_PIXMAP_TYPE_X) return;
   if (!c->shape_job)
     c->shape_job = ecore_job_add((Ecore_Cb)_e_comp_shapes_update_job, c);
}

EAPI void
e_comp_shape_queue_block(E_Comp *c, Eina_Bool block)
{
   EINA_SAFETY_ON_NULL_RETURN(c);

   c->shape_queue_blocked = !!block;
   if (block)
     E_FREE_FUNC(c->shape_job, ecore_job_del);
   else
     e_comp_shape_queue(c);
}

EAPI E_Comp_Config *
e_comp_config_get(void)
{
   return conf;
}

EAPI void
e_comp_shadows_reset(void)
{
   E_Client *ec;

   _e_comp_fps_update(e_comp);
   E_LIST_FOREACH(e_comp->zones, e_comp_canvas_zone_update);
   E_CLIENT_FOREACH(e_comp, ec)
     e_comp_object_frame_theme_set(ec->frame, E_COMP_OBJECT_FRAME_RESHADOW);
}

EAPI E_Comp *
e_comp_get(const void *o EINA_UNUSED)
{
   return e_comp;
}


EAPI Ecore_Window
e_comp_top_window_at_xy_get(E_Comp *c, Evas_Coord x, Evas_Coord y)
{
   E_Client *ec;
   Evas_Object *o;

   EINA_SAFETY_ON_NULL_RETURN_VAL(c, 0);
   o = evas_object_top_at_xy_get(c->evas, x, y, 0, 0);
   if (!o) return c->ee_win;
   ec = evas_object_data_get(o, "E_Client");
   if (ec) return e_client_util_pwin_get(ec);
   return c->ee_win;
}

EAPI void
e_comp_util_wins_print(const E_Comp *c EINA_UNUSED)
{
   Evas_Object *o;

   o = evas_object_top_get(e_comp->evas);
   while (o)
     {
        E_Client *ec;
        int x, y, w, h;

        ec = evas_object_data_get(o, "E_Client");
        evas_object_geometry_get(o, &x, &y, &w, &h);
        fprintf(stderr, "LAYER %d  ", evas_object_layer_get(o));
        if (ec)
          fprintf(stderr, "EC%s%s:  %p - '%s:%s' || %d,%d @ %dx%d\n",
                  ec->override ? "O" : "", ec->focused ? "*" : "", ec,
                  e_client_util_name_get(ec) ?: ec->icccm.name, ec->icccm.class, x, y, w, h);
        else
          fprintf(stderr, "OBJ: %p - %s || %d,%d @ %dx%d\n", o, evas_object_name_get(o), x, y, w, h);
        o = evas_object_below_get(o);
     }
   fputc('\n', stderr);
}

EAPI void
e_comp_ignore_win_add(E_Pixmap_Type type, Ecore_Window win)
{
   E_Client *ec;

   eina_hash_add(ignores, &win, (void*)1);
   ec = e_pixmap_find_client(type, win);
   if (!ec) return;
   ec->ignored = 1;
   if (ec->visible) evas_object_hide(ec->frame);
}

EAPI void
e_comp_ignore_win_del(E_Pixmap_Type type, Ecore_Window win)
{
   E_Client *ec;

   eina_hash_del_by_key(ignores, &win);
   ec = e_pixmap_find_client(type, win);
   if ((!ec) || (e_object_is_del(E_OBJECT(ec)))) return;
   ec->ignored = 0;
   if (ec->visible) evas_object_show(ec->frame);
}

EAPI Eina_Bool
e_comp_ignore_win_find(Ecore_Window win)
{
   return !!eina_hash_find(ignores, &win);
}

EAPI void
e_comp_override_del(E_Comp *c)
{
   c->nocomp_override--;
   if (c->nocomp_override <= 0)
     {
        c->nocomp_override = 0;
        if (c->nocomp_want) _e_comp_cb_nocomp_begin(c);
     }
}

EAPI void
e_comp_override_add(E_Comp *c)
{
   c->nocomp_override++;
   if ((c->nocomp_override > 0) && (c->nocomp)) _e_comp_cb_nocomp_end(c);
}

#if 0
FIXME
EAPI void
e_comp_block_window_add(void)
{
   e_comp->block_count++;
   if (e_comp->block_win) return;
   e_comp->block_win = ecore_x_window_new(e_comp->man->root, e_comp->man->x, e_comp->man->y, e_comp->man->w, e_comp->man->h);
   INF("BLOCK WIN: %x", e_comp->block_win);
   ecore_x_window_background_color_set(e_comp->block_win, 0, 0, 0);
   e_comp_ignore_win_add(e_comp->block_win);
   ecore_x_window_configure(e_comp->block_win,
     ECORE_X_WINDOW_CONFIGURE_MASK_SIBLING | ECORE_X_WINDOW_CONFIGURE_MASK_STACK_MODE,
     0, 0, 0, 0, 0, ((E_Comp_Win*)e_comp->wins)->win, ECORE_X_WINDOW_STACK_ABOVE);
   ecore_x_window_show(e_comp->block_win);
}

EAPI void
e_comp_block_window_del(void)
{
   if (!e_comp->block_count) return;
   e_comp->block_count--;
   if (e_comp->block_count) return;
   if (e_comp->block_win) ecore_x_window_free(e_comp->block_win);
   e_comp->block_win = 0;
}
#endif

EAPI E_Comp *
e_comp_find_by_window(Ecore_Window win)
{
   if ((e_comp->win == win) || (e_comp->ee_win == win) || (e_comp->man->root == win)) return e_comp;
   return NULL;
}

EAPI void
e_comp_override_timed_pop(E_Comp *c)
{
   EINA_SAFETY_ON_NULL_RETURN(c);
   if (c->nocomp_override <= 0) return;
   if (c->nocomp_override_timer)
     c->nocomp_override--;
   else
     c->nocomp_override_timer = ecore_timer_add(1.0, _e_comp_override_expire, c);
}

EAPI unsigned int
e_comp_e_object_layer_get(const E_Object *obj)
{
   E_Gadcon *gc = NULL;
   E_Client *ec = NULL;

   if (!obj) return 0;

   switch (obj->type)
     {
      case E_GADCON_CLIENT_TYPE:
        gc = ((E_Gadcon_Client *)(obj))->gadcon;
        EINA_SAFETY_ON_NULL_RETURN_VAL(gc, 0);
        /* no break */
      case E_GADCON_TYPE:
        if (!gc) gc = (E_Gadcon *)obj;
        if (gc->shelf) return gc->shelf->layer;
        if (!gc->toolbar) return E_LAYER_DESKTOP;
        return e_win_client_get(gc->toolbar->fwin)->layer;

      case E_ZONE_TYPE:
        return E_LAYER_DESKTOP;

      case E_CLIENT_TYPE:
        return ((E_Client *)(obj))->layer;

      /* FIXME: add more types as needed */
      default:
        break;
     }
   if (e_obj_is_win(obj))
     {
        ec = e_win_client_get((void*)obj);
        if (ec)
          return ec->layer;
     }
   return 0;
}

EAPI void
e_comp_layer_name_get(unsigned int layer, char *buff, int buff_size)
{
   if (!buff) return;

   switch(layer)
     {
      case E_LAYER_BOTTOM: strncpy(buff, "E_LAYER_BOTTOM", buff_size); break;
      case E_LAYER_BG: strncpy(buff, "E_LAYER_BG", buff_size); break;
      case E_LAYER_DESKTOP: strncpy(buff, "E_LAYER_DESKTOP", buff_size); break;
      case E_LAYER_DESKTOP_TOP: strncpy(buff, "E_LAYER_DESKTOP_TOP", buff_size); break;
      case E_LAYER_CLIENT_DESKTOP: strncpy(buff, "E_LAYER_CLIENT_DESKTOP", buff_size); break;
      case E_LAYER_CLIENT_BELOW: strncpy(buff, "E_LAYER_CLIENT_BELOW", buff_size); break;
      case E_LAYER_CLIENT_NORMAL: strncpy(buff, "E_LAYER_CLIENT_NORMAL", buff_size); break;
      case E_LAYER_CLIENT_ABOVE: strncpy(buff, "E_LAYER_CLIENT_ABOVE", buff_size); break;
      case E_LAYER_CLIENT_EDGE: strncpy(buff, "E_LAYER_CLIENT_EDGE", buff_size); break;
      case E_LAYER_CLIENT_FULLSCREEN: strncpy(buff, "E_LAYER_CLIENT_FULLSCREEN", buff_size); break;
      case E_LAYER_CLIENT_EDGE_FULLSCREEN: strncpy(buff, "E_LAYER_CLIENT_EDGE_FULLSCREEN", buff_size); break;
      case E_LAYER_CLIENT_POPUP: strncpy(buff, "E_LAYER_CLIENT_POPUP", buff_size); break;
      case E_LAYER_CLIENT_TOP: strncpy(buff, "E_LAYER_CLIENT_TOP", buff_size); break;
      case E_LAYER_CLIENT_DRAG: strncpy(buff, "E_LAYER_CLIENT_DRAG", buff_size); break;
      case E_LAYER_CLIENT_PRIO: strncpy(buff, "E_LAYER_CLIENT_PRIO", buff_size); break;
      case E_LAYER_CLIENT_NOTIFICATION_LOW: strncpy(buff, "E_LAYER_CLIENT_NOTIFICATION_LOW", buff_size); break;
      case E_LAYER_CLIENT_NOTIFICATION_NORMAL: strncpy(buff, "E_LAYER_CLIENT_NOTIFICATION_NORMAL", buff_size); break;
      case E_LAYER_CLIENT_NOTIFICATION_HIGH: strncpy(buff, "E_LAYER_CLIENT_NOTIFICATION_HIGH", buff_size); break;
      case E_LAYER_CLIENT_NOTIFICATION_TOP: strncpy(buff, "E_LAYER_CLIENT_NOTIFICATION_TOP", buff_size); break;
      case E_LAYER_CLIENT_ALERT: strncpy(buff, "E_LAYER_CLIENT_ALERT", buff_size); break;
      case E_LAYER_POPUP: strncpy(buff, "E_LAYER_POPUP", buff_size); break;
      case E_LAYER_MENU: strncpy(buff, "E_LAYER_MENU", buff_size); break;
      case E_LAYER_DESKLOCK: strncpy(buff, "E_LAYER_DESKLOCK", buff_size); break;
      case E_LAYER_MAX: strncpy(buff, "E_LAYER_MAX", buff_size); break;
      default:strncpy(buff, "E_LAYER_NONE", buff_size); break;
     }
}

EAPI Eina_Bool
e_comp_grab_input(E_Comp *c, Eina_Bool mouse, Eina_Bool kbd)
{
   Eina_Bool ret = EINA_FALSE;
   Ecore_Window mwin = 0, kwin = 0;

   mouse = !!mouse;
   kbd = !!kbd;
   if (mouse || c->input_mouse_grabs)
     mwin = c->ee_win;
   if (kbd || c->input_mouse_grabs)
     kwin = c->ee_win;
   e_comp_override_add(c);
   if ((c->input_mouse_grabs && c->input_key_grabs) ||
       e_grabinput_get(mwin, 0, kwin))
     {
        ret = EINA_TRUE;
        c->input_mouse_grabs += mouse;
        c->input_key_grabs += kbd;
     }
   return ret;
}

EAPI void
e_comp_ungrab_input(E_Comp *c, Eina_Bool mouse, Eina_Bool kbd)
{
   Ecore_Window mwin = 0, kwin = 0;

   mouse = !!mouse;
   kbd = !!kbd;
   if (mouse && (c->input_mouse_grabs == 1))
     mwin = c->ee_win;
   if (kbd && (c->input_key_grabs == 1))
     kwin = c->ee_win;
   if (c->input_mouse_grabs)
     c->input_mouse_grabs -= mouse;
   if (c->input_key_grabs)
     c->input_key_grabs -= kbd;
   e_comp_override_timed_pop(c);
   if ((!mwin) && (!kwin)) return;
   e_grabinput_release(mwin, kwin);
   evas_event_feed_mouse_out(c->evas, 0, NULL);
   evas_event_feed_mouse_in(c->evas, 0, NULL);
   if (e_client_focused_get()) return;
   if (e_config->focus_policy != E_FOCUS_MOUSE)
     e_client_refocus();
}

EAPI void
e_comp_gl_set(Eina_Bool set)
{
   gl_avail = !!set;
}

EAPI Eina_Bool
e_comp_gl_get(void)
{
   return gl_avail;
}

EAPI E_Comp *
e_comp_evas_find(const Evas *e)
{
   if (e_comp->evas == e) return e_comp;
   return NULL;
}

EAPI void
e_comp_button_bindings_ungrab_all(void)
{
   if (e_comp->bindings_ungrab_cb)
     e_comp->bindings_ungrab_cb(e_comp);
}

EAPI void
e_comp_button_bindings_grab_all(void)
{
   if (e_comp->bindings_grab_cb)
     e_comp->bindings_grab_cb(e_comp);
}

EAPI void
e_comp_client_redirect_toggle(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (!conf->enable_advanced_features) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_X) return;
   ec->unredirected_single = !ec->unredirected_single;
   e_client_redirected_set(ec, !ec->redirected);
   ec->no_shape_cut = !ec->redirected;
   e_comp_shape_queue(ec->comp);
}

EAPI Eina_Bool
e_comp_util_object_is_above_nocomp(Evas_Object *obj)
{
   E_Comp *comp;
   Evas_Object *o;
   int cl, ol;

   EINA_SAFETY_ON_NULL_RETURN_VAL(obj, EINA_FALSE);
   if (!evas_object_visible_get(obj)) return EINA_FALSE;
   comp = e_comp_util_evas_object_comp_get(obj);
   if ((!comp) || (!comp->nocomp_ec)) return EINA_FALSE;
   cl = evas_object_layer_get(comp->nocomp_ec->frame);
   ol = evas_object_layer_get(obj);
   if (cl > ol) return EINA_FALSE;
   o = evas_object_above_get(comp->nocomp_ec->frame);
   if ((cl == ol) && (evas_object_layer_get(o) == cl))
     {
        do {
           if (o == obj)
             return EINA_TRUE;
           o = evas_object_above_get(o);
        } while (o && (evas_object_layer_get(o) == cl));
     }
   else
     return EINA_TRUE;
   return EINA_FALSE;
}
