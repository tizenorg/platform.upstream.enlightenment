#include "e.h"

static Ecore_Event_Handler *_e_screensaver_handler_on = NULL;
static Ecore_Event_Handler *_e_screensaver_handler_off = NULL;
static Ecore_Event_Handler *_e_screensaver_handler_config_mode = NULL;
static Ecore_Event_Handler *_e_screensaver_handler_border_fullscreen = NULL;
static Ecore_Event_Handler *_e_screensaver_handler_border_unfullscreen = NULL;
static Ecore_Event_Handler *_e_screensaver_handler_border_remove = NULL;
static Ecore_Event_Handler *_e_screensaver_handler_border_iconify = NULL;
static Ecore_Event_Handler *_e_screensaver_handler_border_uniconify = NULL;
static Ecore_Event_Handler *_e_screensaver_handler_border_desk_set = NULL;
static Ecore_Event_Handler *_e_screensaver_handler_desk_show = NULL;
static Ecore_Event_Handler *_e_screensaver_handler_powersave = NULL;
static int _e_screensaver_ask_presentation_count = 0;

static int _e_screensaver_timeout = 0;
//static int _e_screensaver_interval = 0;
static int _e_screensaver_blanking = 0;
static int _e_screensaver_expose = 0;

static Ecore_Timer *_e_screensaver_suspend_timer = NULL;
static Eina_Bool _e_screensaver_on = EINA_FALSE;

static Ecore_Timer *screensaver_idle_timer = NULL;

static Ecore_Timer *_e_screensaver_timer;
static Eina_Bool _e_screensaver_inhibited = EINA_FALSE;

E_API int E_EVENT_SCREENSAVER_ON = -1;
E_API int E_EVENT_SCREENSAVER_OFF = -1;
E_API int E_EVENT_SCREENSAVER_OFF_PRE = -1;

static Eina_Bool
_e_screensaver_idle_timeout_cb(void *d)
{
   e_screensaver_eval(!!d);
   _e_screensaver_timer = NULL;
   return EINA_FALSE;
}

E_API int
e_screensaver_timeout_get(Eina_Bool use_idle)
{
   return 0;
}

E_API void
e_screensaver_update(void)
{
   int timeout;
   Eina_Bool changed = EINA_FALSE;

   timeout = e_screensaver_timeout_get(EINA_TRUE);
   if (_e_screensaver_timeout != timeout)
     {
        _e_screensaver_timeout = timeout;
        changed = EINA_TRUE;
     }
   if (changed && (e_comp->comp_type == E_PIXMAP_TYPE_WL))
     {
        E_FREE_FUNC(_e_screensaver_timer, ecore_timer_del);
        if (timeout)
          _e_screensaver_timer = ecore_timer_add(timeout, _e_screensaver_idle_timeout_cb, (void*)1);
     }
}

static Eina_Bool
_e_screensaver_handler_config_mode_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   e_screensaver_update();
   return ECORE_CALLBACK_PASS_ON;
}

static double last_start = 0.0;

static Eina_Bool
_e_screensaver_handler_screensaver_on_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   _e_screensaver_on = EINA_TRUE;
   if (_e_screensaver_suspend_timer)
     {
        ecore_timer_del(_e_screensaver_suspend_timer);
        _e_screensaver_suspend_timer = NULL;
     }
   last_start = ecore_loop_time_get();
   _e_screensaver_ask_presentation_count = 0;
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_screensaver_handler_screensaver_off_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
//   e_dpms_force_update();

   _e_screensaver_on = EINA_FALSE;
   if (_e_screensaver_suspend_timer)
     {
        ecore_timer_del(_e_screensaver_suspend_timer);
        _e_screensaver_suspend_timer = NULL;
     }

   if (_e_screensaver_ask_presentation_count)
     _e_screensaver_ask_presentation_count = 0;
   if (_e_screensaver_timeout && (e_comp->comp_type == E_PIXMAP_TYPE_WL))
     _e_screensaver_timer = ecore_timer_add(_e_screensaver_timeout, _e_screensaver_idle_timeout_cb, (void*)1);
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_screensaver_handler_border_fullscreen_check_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   e_screensaver_update();
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_screensaver_handler_border_desk_set_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   e_screensaver_update();
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_screensaver_handler_desk_show_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   e_screensaver_update();
   return ECORE_CALLBACK_PASS_ON;
}

EINTERN void
e_screensaver_preinit(void)
{
   E_EVENT_SCREENSAVER_ON = ecore_event_type_new();
   E_EVENT_SCREENSAVER_OFF = ecore_event_type_new();
   E_EVENT_SCREENSAVER_OFF_PRE = ecore_event_type_new();
}

EINTERN int
e_screensaver_init(void)
{
   _e_screensaver_handler_on = ecore_event_handler_add
       (E_EVENT_SCREENSAVER_ON, _e_screensaver_handler_screensaver_on_cb, NULL);
   _e_screensaver_handler_off = ecore_event_handler_add
       (E_EVENT_SCREENSAVER_OFF, _e_screensaver_handler_screensaver_off_cb, NULL);
   _e_screensaver_handler_config_mode = ecore_event_handler_add
       (E_EVENT_CONFIG_MODE_CHANGED, _e_screensaver_handler_config_mode_cb, NULL);

   _e_screensaver_handler_border_fullscreen = ecore_event_handler_add
       (E_EVENT_CLIENT_FULLSCREEN, _e_screensaver_handler_border_fullscreen_check_cb, NULL);
   _e_screensaver_handler_border_unfullscreen = ecore_event_handler_add
       (E_EVENT_CLIENT_UNFULLSCREEN, _e_screensaver_handler_border_fullscreen_check_cb, NULL);
   _e_screensaver_handler_border_remove = ecore_event_handler_add
       (E_EVENT_CLIENT_REMOVE, _e_screensaver_handler_border_fullscreen_check_cb, NULL);
   _e_screensaver_handler_border_iconify = ecore_event_handler_add
       (E_EVENT_CLIENT_ICONIFY, _e_screensaver_handler_border_fullscreen_check_cb, NULL);
   _e_screensaver_handler_border_uniconify = ecore_event_handler_add
       (E_EVENT_CLIENT_UNICONIFY, _e_screensaver_handler_border_fullscreen_check_cb, NULL);
   _e_screensaver_handler_border_desk_set = ecore_event_handler_add
       (E_EVENT_CLIENT_DESK_SET, _e_screensaver_handler_border_desk_set_cb, NULL);
   _e_screensaver_handler_desk_show = ecore_event_handler_add
       (E_EVENT_DESK_SHOW, _e_screensaver_handler_desk_show_cb, NULL);

   return 1;
}

EINTERN int
e_screensaver_shutdown(void)
{
   if (_e_screensaver_handler_on)
     {
        ecore_event_handler_del(_e_screensaver_handler_on);
        _e_screensaver_handler_on = NULL;
     }

   if (_e_screensaver_handler_off)
     {
        ecore_event_handler_del(_e_screensaver_handler_off);
        _e_screensaver_handler_off = NULL;
     }

   if (_e_screensaver_suspend_timer)
     {
        ecore_timer_del(_e_screensaver_suspend_timer);
        _e_screensaver_suspend_timer = NULL;
     }

   if (_e_screensaver_handler_powersave)
     {
        ecore_event_handler_del(_e_screensaver_handler_powersave);
        _e_screensaver_handler_powersave = NULL;
     }

   if (_e_screensaver_handler_config_mode)
     {
        ecore_event_handler_del(_e_screensaver_handler_config_mode);
        _e_screensaver_handler_config_mode = NULL;
     }

   if (_e_screensaver_handler_border_fullscreen)
     {
        ecore_event_handler_del(_e_screensaver_handler_border_fullscreen);
        _e_screensaver_handler_border_fullscreen = NULL;
     }

   if (_e_screensaver_handler_border_unfullscreen)
     {
        ecore_event_handler_del(_e_screensaver_handler_border_unfullscreen);
        _e_screensaver_handler_border_unfullscreen = NULL;
     }

   if (_e_screensaver_handler_border_remove)
     {
        ecore_event_handler_del(_e_screensaver_handler_border_remove);
        _e_screensaver_handler_border_remove = NULL;
     }

   if (_e_screensaver_handler_border_iconify)
     {
        ecore_event_handler_del(_e_screensaver_handler_border_iconify);
        _e_screensaver_handler_border_iconify = NULL;
     }

   if (_e_screensaver_handler_border_uniconify)
     {
        ecore_event_handler_del(_e_screensaver_handler_border_uniconify);
        _e_screensaver_handler_border_uniconify = NULL;
     }

   if (_e_screensaver_handler_border_desk_set)
     {
        ecore_event_handler_del(_e_screensaver_handler_border_desk_set);
        _e_screensaver_handler_border_desk_set = NULL;
     }

   if (_e_screensaver_handler_desk_show)
     {
        ecore_event_handler_del(_e_screensaver_handler_desk_show);
        _e_screensaver_handler_desk_show = NULL;
     }

   return 1;
}

E_API void
e_screensaver_attrs_set(int timeout, int blanking, int expose)
{
   _e_screensaver_timeout = timeout;
//   _e_screensaver_interval = ecore_x_screensaver_interval_get();
   _e_screensaver_blanking = blanking;
   _e_screensaver_expose = expose;
}

E_API Eina_Bool
e_screensaver_on_get(void)
{
   return _e_screensaver_on;
}

E_API void
e_screensaver_activate(void)
{
   if (e_screensaver_on_get()) return;

   E_FREE_FUNC(screensaver_idle_timer, ecore_timer_del);
   if (e_comp->comp_type == E_PIXMAP_TYPE_WL)
     e_screensaver_eval(1);
   E_FREE_FUNC(_e_screensaver_timer, ecore_timer_del);
}

E_API void
e_screensaver_deactivate(void)
{
   if (!e_screensaver_on_get()) return;

   E_FREE_FUNC(screensaver_idle_timer, ecore_timer_del);
   e_screensaver_notidle();
}

E_API void
e_screensaver_eval(Eina_Bool saver_on)
{
   if (saver_on)
     {
        if (!e_screensaver_on_get())
          ecore_event_add(E_EVENT_SCREENSAVER_ON, NULL, NULL, NULL);
        return;
     }
   if (screensaver_idle_timer)
     {
        E_FREE_FUNC(screensaver_idle_timer, ecore_timer_del);
        return;
     }
   if (e_screensaver_on_get())
     ecore_event_add(E_EVENT_SCREENSAVER_OFF, NULL, NULL, NULL);
}

E_API void
e_screensaver_notidle(void)
{
   if (_e_screensaver_inhibited || (e_comp->comp_type != E_PIXMAP_TYPE_WL)) return;
   E_FREE_FUNC(_e_screensaver_timer, ecore_timer_del);
   if (e_screensaver_on_get())
     {
        ecore_event_add(E_EVENT_SCREENSAVER_OFF_PRE, NULL, NULL, NULL);
        _e_screensaver_timer = ecore_timer_add(0.2, _e_screensaver_idle_timeout_cb, NULL);
     }
   else if (_e_screensaver_timeout)
     _e_screensaver_timer = ecore_timer_add(_e_screensaver_timeout, _e_screensaver_idle_timeout_cb, (void*)1);
}

E_API void
e_screensaver_inhibit_toggle(Eina_Bool inhibit)
{
   if (e_comp->comp_type != E_PIXMAP_TYPE_WL) return;
   E_FREE_FUNC(_e_screensaver_timer, ecore_timer_del);
   _e_screensaver_inhibited = !!inhibit;
   if (inhibit)
     e_screensaver_eval(0);
   else
     e_screensaver_notidle();
}
