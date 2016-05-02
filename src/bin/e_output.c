#include "e.h"


/////////////////////////////////////////////////////////////////////////
static void                    _animated_apply_abort(void);
static Eina_Bool               _cb_delay_timer(void *data);
static Eina_Bool               _cb_fade_animator(void *data);
static void                    _animated_apply(void);
static void                    _do_apply(void);
static void                    _info_free(E_Drm_Output *r);
static char                   *_screens_fingerprint(E_Drm_Output *r);
static Eina_Bool               _screens_differ(E_Drm_Output *r1, E_Drm_Output *r2);
static Eina_Bool               _cb_screen_change_delay(void *data);
static void                    _screen_config_takeover(void);
static void                    _screen_config_eval(void);
static void                    _screen_config_maxsize(void);

/////////////////////////////////////////////////////////////////////////

static Eina_List     *_ev_handlers = NULL;
static Ecore_Timer   *_screen_delay_timer = NULL;
static Eina_Bool      event_screen = EINA_FALSE;
static Eina_Bool      event_ignore = EINA_FALSE;

/////////////////////////////////////////////////////////////////////////

E_API E_Drm_Output        *e_drm_output = NULL;

E_API int              E_EVENT_RANDR_CHANGE = 0;
E_API int              E_EVENT_SCREEN_CHANGE = 0;

/////////////////////////////////////////////////////////////////////////
EINTERN Eina_Bool
e_drm_output_init(void)
{
   if (!E_EVENT_SCREEN_CHANGE) E_EVENT_SCREEN_CHANGE = ecore_event_type_new();
   if (!e_comp_drm_available()) return EINA_FALSE;

   _do_apply();

   ecore_event_add(E_EVENT_SCREEN_CHANGE, NULL, NULL, NULL);

   return EINA_TRUE;
}

EINTERN int
e_drm_output_shutdown(void)
{
   _animated_apply_abort();
   // nuke any screen config delay handler
   if (_screen_delay_timer) ecore_timer_del(_screen_delay_timer);
   _screen_delay_timer = NULL;
   // clear up all event handlers
   E_FREE_LIST(_ev_handlers, ecore_event_handler_del);
   // free up screen info
   _info_free(e_drm_output);
   e_drm_output = NULL;

   return 1;
}

E_API void
e_drm_output_config_apply(void)
{
   _animated_apply();
}

E_API void
e_drm_output_screeninfo_update(void)
{
   // re-fetch/update current screen info
   _info_free(e_drm_output);
   e_drm_output = e_comp_drm_create();
   _screen_config_maxsize();
}

/////////////////////////////////////////////////////////////////////////
static double _start_time = 0.0;
static Ecore_Animator *_fade_animator = NULL;
static Ecore_Timer *_apply_delay = NULL;
Eina_Bool _applying = EINA_FALSE;
static int _target_from = 0;
static int _target_to = 0;
static Evas_Object *_fade_obj = NULL;

static void
_animated_apply_abort(void)
{
   if (_apply_delay) ecore_timer_del(_apply_delay);
   if (_fade_animator) ecore_animator_del(_fade_animator);
   _apply_delay = NULL;
   _fade_animator = NULL;
   _applying = EINA_FALSE;
   _fade_obj = NULL;
}

static Eina_Bool
_cb_delay_timer(void *data EINA_UNUSED)
{
   _apply_delay = NULL;
   _target_from = 255;
   _target_to = 0;
   _start_time = ecore_loop_time_get();
   _fade_animator = ecore_animator_add(_cb_fade_animator, NULL);
   return EINA_FALSE;
}

static Eina_Bool
_cb_fade_animator(void *data EINA_UNUSED)
{
   double t = ecore_loop_time_get() - _start_time;
   int v;

   t = t / 0.5;
   if (t < 0.0) t = 0.0;
   v = _target_from + ((_target_to - _target_from) * t);
   if (t >= 1.0) v = _target_to;
   evas_object_color_set(_fade_obj, 0, 0, 0, v);
   if (v == _target_to)
     {
        if (_target_to == 255)
          {
             _apply_delay = ecore_timer_add(3.0, _cb_delay_timer, NULL);
             _do_apply();
          }
        else
          {
             evas_object_del(_fade_obj);
             _fade_obj = NULL;
             _applying = EINA_FALSE;
          }
        _fade_animator = NULL;
        return EINA_FALSE;
     }
   return EINA_TRUE;
}

static void
_animated_apply(void)
{
   Evas *e;

   // fade out, config, wait 3 seconds, fade back in
   if (_applying) return;
   _applying = EINA_TRUE;
   _start_time = ecore_loop_time_get();
   e = e_comp->evas;
   _fade_obj = evas_object_rectangle_add(e);
   evas_object_pass_events_set(_fade_obj, EINA_TRUE);
   evas_object_color_set(_fade_obj, 0, 0, 0, 0);
   evas_object_move(_fade_obj, 0, 0);
   evas_object_resize(_fade_obj, 999999, 999999);
   evas_object_layer_set(_fade_obj, EVAS_LAYER_MAX);
   evas_object_show(_fade_obj);
   _target_from = 0;
   _target_to = 255;
   _fade_animator = ecore_animator_add(_cb_fade_animator, NULL);
}

static void
_do_apply(void)
{
   // take current screen config and apply it to the driver
   printf("OUTPUT: re-get info before applying..\n");
   _info_free(e_drm_output);
   e_drm_output = e_comp_drm_create();
   _screen_config_maxsize();
   printf("OUTPUT: takeover config...\n");
   _screen_config_takeover();
   printf("OUTPUT: eval config...\n");
   _screen_config_eval();
   printf("OUTPUT: really apply config...\n");
   e_comp_drm_apply();
   printf("OUTPUT: done config...\n");
}

static void
_info_free(E_Drm_Output *r)
{
   E_Output_Screen *s;
   E_Output_Mode *m;

   if (!r) return;
   // free up our randr screen data
   EINA_LIST_FREE(r->screens, s)
     {
        free(s->id);
        free(s->info.screen);
        free(s->info.name);
        free(s->info.edid);
        EINA_LIST_FREE(s->info.modes, m) free(m);
        free(s);
     }
   free(r);
}

static char *
_screens_fingerprint(E_Drm_Output *r)
{
   Eina_List *l;
   E_Output_Screen *s;
   Eina_Strbuf *buf;
   char *str;

   buf = eina_strbuf_new();
   if (!buf) return NULL;
   EINA_LIST_FOREACH(r->screens, l, s)
     {
        if (!s->id) eina_strbuf_append(buf, ":NULL:");
        else
          {
             eina_strbuf_append(buf, ":");
             eina_strbuf_append(buf, s->id);
             eina_strbuf_append(buf, ":");
             eina_strbuf_append(buf, ":LO:");
          }
     }
   str = eina_strbuf_string_steal(buf);
   eina_strbuf_free(buf);
   return str;
}

static Eina_Bool
_screens_differ(E_Drm_Output *r1, E_Drm_Output *r2)
{
   char *s1, *s2;
   Eina_Bool changed = EINA_FALSE;
   Eina_List *l, *ll;
   E_Output_Screen *s, *ss;

   // check monitor outputs and edids, plugged in things
   s1 = _screens_fingerprint(r1);
   s2 = _screens_fingerprint(r2);
   if ((!s1) && (!s2)) return EINA_FALSE;
   printf("OUTPUT: check fingerprint...\n");
   if ((s1) && (s2) && (strcmp(s1, s2))) changed = EINA_TRUE;
   printf("OUTPUT: ... fingerprint says %i\n", changed);
   free(s1);
   free(s2);
   // check screen config
   EINA_LIST_FOREACH(r2->screens, l, s)
     {
        if (!s->id) continue;
        EINA_LIST_FOREACH(r2->screens, ll, ss)
          {
             if ((ss->id) && (!strcmp(s->id, ss->id))) break;
             ss = NULL;
          }
        if (!ss) changed = EINA_TRUE;
        else if ((s->config.geom.x != ss->config.geom.x) ||
                 (s->config.geom.y != ss->config.geom.y) ||
                 (s->config.geom.w != ss->config.geom.w) ||
                 (s->config.geom.h != ss->config.geom.h) ||
                 (s->config.mode.w != ss->config.mode.w) ||
                 (s->config.mode.h != ss->config.mode.h) ||
                 (s->config.enabled != ss->config.enabled))
          changed = EINA_TRUE;
     }
   printf("OUTPUT: changed = %i\n", changed);
   return changed;
}

static Eina_Bool
_cb_screen_change_delay(void *data EINA_UNUSED)
{
   _screen_delay_timer = NULL;
   printf("OUTPUT: ... %i %i\n", event_screen, event_ignore);
   // if we had a screen plug/unplug etc. event and we shouldnt ignore it...
   if ((event_screen) && (!event_ignore))
     {
        E_Drm_Output *rtemp;
        Eina_Bool change = EINA_FALSE;

        printf("OUTPUT: reconfigure screens due to event...\n");
        rtemp = e_comp_drm_create();
        if (rtemp)
          {
             if (_screens_differ(e_drm_output, rtemp)) change = EINA_TRUE;
             _info_free(rtemp);
          }
        printf("OUTPUT: change = %i\n", change);
        // we plugged or unplugged some monitor - re-apply config so
        // known screens can be configured
        if (change) e_drm_output_config_apply();
     }
   // update screen info after the above apply or due to external changes
   e_drm_output_screeninfo_update();
   e_comp_canvas_resize(e_drm_output->w, e_drm_output->h);
   e_drm_output_screens_setup(e_comp->w, e_comp->h);
   e_comp_canvas_update();
   // tell the rest of e some screen reconfigure thing happened
   ecore_event_add(E_EVENT_SCREEN_CHANGE, NULL, NULL, NULL);
   event_screen = EINA_FALSE;
   event_ignore = EINA_FALSE;
   return EINA_FALSE;
}

static void
_screen_config_takeover(void)
{
   Eina_List *l;
   E_Output_Screen *s;
   EINA_LIST_FOREACH(e_drm_output->screens, l, s)
     {
        s->config.configured = EINA_TRUE;
        s->config.enabled = EINA_TRUE;
     }
}

static void
_screen_config_eval(void)
{
   Eina_List *l;
   E_Output_Screen *s;
   int minx, miny, maxx, maxy;

   minx = 65535;
   miny = 65535;
   maxx = -65536;
   maxy = -65536;

   EINA_LIST_FOREACH(e_drm_output->screens, l, s)
     {
        if (!s->config.enabled) continue;
        if (s->config.geom.x < minx) minx = s->config.geom.x;
        if (s->config.geom.y < miny) miny = s->config.geom.y;
        if ((s->config.geom.x + s->config.geom.w) > maxx)
          maxx = s->config.geom.x + s->config.geom.w;
        if ((s->config.geom.y + s->config.geom.h) > maxy)
          maxy = s->config.geom.y + s->config.geom.h;
        printf("OUTPUT: s: '%s' @ %i %i - %ix%i\n",
               s->info.name,
               s->config.geom.x, s->config.geom.y,
               s->config.geom.w, s->config.geom.h);
     }
   printf("OUTPUT:--- %i %i -> %i %i\n", minx, miny, maxx, maxy);
   EINA_LIST_FOREACH(e_drm_output->screens, l, s)
     {
        s->config.geom.x -= minx;
        s->config.geom.y -= miny;
     }
   e_drm_output->w = maxx - minx;
   e_drm_output->h = maxy - miny;
}

static void
_screen_config_maxsize(void)
{
   Eina_List *l;
   E_Output_Screen *s;
   int maxx, maxy;

   maxx = -65536;
   maxy = -65536;
   EINA_LIST_FOREACH(e_drm_output->screens, l, s)
     {
        if (!s->config.enabled) continue;
        if ((s->config.geom.x + s->config.geom.w) > maxx)
          maxx = s->config.geom.x + s->config.geom.w;
        if ((s->config.geom.y + s->config.geom.h) > maxy)
          maxy = s->config.geom.y + s->config.geom.h;
        printf("OUTPUT: '%s': %i %i %ix%i\n",
               s->info.name,
               s->config.geom.x, s->config.geom.y,
               s->config.geom.w, s->config.geom.h);
     }
   printf("OUTPUT: result max: %ix%i\n", maxx, maxy);
   e_drm_output->w = maxx;
   e_drm_output->h = maxy;
}

static int
_screen_sort_cb(const void *data1, const void *data2)
{
   const E_Output_Screen *s1 = data1, *s2 = data2;
   int dif;

   dif = -(s1->config.priority - s2->config.priority);
   if (dif == 0)
     {
        dif = s1->config.geom.x - s2->config.geom.x;
        if (dif == 0)
          dif = s1->config.geom.y - s2->config.geom.y;
     }
   return dif;
}

E_API void
e_drm_output_screen_refresh_queue(Eina_Bool lid_event)
{ 
   // delay handling of screen shances as they can come in in a series over
   // time and thus we can batch up responding to them by waiting 1.0 sec
   if (_screen_delay_timer)
     ecore_timer_reset(_screen_delay_timer);
   else
     _screen_delay_timer = ecore_timer_add(1.0, _cb_screen_change_delay, NULL);
   event_screen |= !!lid_event;
}

E_API void
e_drm_output_screens_setup(int rw, int rh)
{
   int i;
   E_Screen *screen;
   Eina_List *screens = NULL, *screens_rem;
   Eina_List *all_screens = NULL;
   Eina_List *l, *ll;
   E_Output_Screen *s, *s2, *s_chosen;
   Eina_Bool removed;

   if ((!e_drm_output) || (!e_drm_output->screens)) goto out;
   // put screens in tmp list
   EINA_LIST_FOREACH(e_drm_output->screens, l, s)
     {
        if ((s->config.enabled) &&
            (s->config.geom.w > 0) &&
            (s->config.geom.h > 0))
          {
             screens = eina_list_append(screens, s);
          }
     }
   // remove overlapping screens - if a set of screens overlap, keep the
   // smallest/lowest res
   do
     {
        removed = EINA_FALSE;

        EINA_LIST_FOREACH(screens, l, s)
          {
             screens_rem = NULL;

             EINA_LIST_FOREACH(l->next, ll, s2)
               {
                  if (E_INTERSECTS(s->config.geom.x, s->config.geom.y,
                                   s->config.geom.w, s->config.geom.h,
                                   s2->config.geom.x, s2->config.geom.y,
                                   s2->config.geom.w, s2->config.geom.h))
                    {
                       if (!screens_rem)
                         screens_rem = eina_list_append(screens_rem, s);
                       screens_rem = eina_list_append(screens_rem, s2);
                    }
               }
             // we have intersecting screens - choose the lowest res one
             if (screens_rem)
               {
                  removed = EINA_TRUE;
                  // find the smallest screen (chosen one)
                  s_chosen = NULL;
                  EINA_LIST_FOREACH(screens_rem, ll, s2)
                    {
                       if (!s_chosen) s_chosen = s2;
                       else
                         {
                            if ((s_chosen->config.geom.w *
                                 s_chosen->config.geom.h) >
                                (s2->config.geom.w *
                                 s2->config.geom.h))
                              s_chosen = s2;
                         }
                    }
                  // remove all from screens but the chosen one
                  EINA_LIST_FREE(screens_rem, s2)
                    {
                       if (s2 != s_chosen)
                         screens = eina_list_remove_list(screens, l);
                    }
                  // break our list walk and try again
                  break;
               }
          }
     }
   while (removed);
   // sort screens by priority etc.
   screens = eina_list_sort(screens, 0, _screen_sort_cb);
   i = 0;
   EINA_LIST_FOREACH(screens, l, s)
     {
        screen = E_NEW(E_Screen, 1);
        screen->escreen = screen->screen = i;
        screen->x = s->config.geom.x;
        screen->y = s->config.geom.y;
        screen->w = s->config.geom.w;
        screen->h = s->config.geom.h;
        if (s->id) screen->id = strdup(s->id);

        all_screens = eina_list_append(all_screens, screen);
        printf("xinerama screen %i %i %ix%i\n", screen->x, screen->y, screen->w, screen->h);
        INF("E INIT: XINERAMA SCREEN: [%i][%i], %ix%i+%i+%i",
            i, i, screen->w, screen->h, screen->x, screen->y);
        i++;
     }
   eina_list_free(screens);
   // if we have NO screens at all (above - i will be 0) AND we have no
   // existing screens set up in xinerama - then just say root window size
   // is the entire screen. this should handle the case where you unplug ALL
   // screens from an existing setup (unplug external monitors and/or close
   // laptop lid), in which case as long as at least one screen is configured
   // in xinerama, it will be left-as is until next time we re-eval screen
   // setup and have at least one screen
   printf("xinerama setup............... %i %p\n", i, e_xinerama_screens_all_get());
   if ((i == 0) && (!e_xinerama_screens_all_get()))
     {
out:
        screen = E_NEW(E_Screen, 1);
        screen->escreen = screen->screen = 0;
        screen->x = 0;
        screen->y = 0;
        if ((rw > 0) && (rh > 0))
          screen->w = rw, screen->h = rh;
        else
          ecore_evas_screen_geometry_get(e_comp->ee, NULL, NULL, &screen->w, &screen->h);
        all_screens = eina_list_append(all_screens, screen);
     }
   e_xinerama_screens_set(all_screens);
}
