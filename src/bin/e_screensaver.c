#include "e.h"

E_API int E_EVENT_SCREENSAVER_ON = -1;
E_API int E_EVENT_SCREENSAVER_OFF = -1;
E_API int E_EVENT_SCREENSAVER_OFF_PRE = -1;

static Eina_List          *handlers = NULL;
static double              _idle_time = 0.0;
static Ecore_Idle_Enterer *_idle_before = NULL;
static Eina_Bool           _ev_update = EINA_FALSE;
static Ecore_Timer        *_event_idle_timer = NULL;
static Eina_Bool           _scrsaver_active = EINA_FALSE;

static Eina_Bool
_e_scrsaver_idle_timeout_cb(void *data EINA_UNUSED)
{
   _scrsaver_active = EINA_TRUE;
   _event_idle_timer = NULL;
   ecore_event_add(E_EVENT_SCREENSAVER_ON, NULL, NULL, NULL);
   return EINA_FALSE;
}

static Eina_Bool
_e_scrsaver_cb_idle_before(void *data EINA_UNUSED)
{
   if (!_ev_update) return ECORE_CALLBACK_RENEW;

   if (_event_idle_timer)
     {
        ecore_timer_del(_event_idle_timer);
        _event_idle_timer = NULL;
     }

   if (_idle_time != 0.0)
     {
        _event_idle_timer =
           ecore_timer_add(_idle_time,
                           _e_scrsaver_idle_timeout_cb,
                           NULL);
     }

   if (_scrsaver_active)
     {
        ecore_event_add(E_EVENT_SCREENSAVER_OFF, NULL, NULL, NULL);
        _scrsaver_active = EINA_FALSE;
     }

   _ev_update = EINA_FALSE;

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_scrsaver_cb_input(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   _ev_update = EINA_TRUE;
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
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_KEY_DOWN,          _e_scrsaver_cb_input, NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_KEY_UP,            _e_scrsaver_cb_input, NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_MOUSE_MOVE,        _e_scrsaver_cb_input, NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_MOUSE_BUTTON_DOWN, _e_scrsaver_cb_input, NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_MOUSE_BUTTON_UP,   _e_scrsaver_cb_input, NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_MOUSE_WHEEL,       _e_scrsaver_cb_input, NULL);

   _idle_before = ecore_idle_enterer_before_add(_e_scrsaver_cb_idle_before, NULL);

   return 1;
}

EINTERN int
e_screensaver_shutdown(void)
{
   E_FREE_LIST(handlers, ecore_event_handler_del);

   if (_event_idle_timer) ecore_timer_del(_event_idle_timer);
   _event_idle_timer = NULL;

   if (_idle_before) ecore_idle_enterer_del(_idle_before);
   _idle_before = NULL;

   handlers = NULL;

   return 1;
}

E_API void
e_screensaver_timeout_set(double time)
{
   _idle_time = time;
}

E_API double
e_screensaver_timeout_get(void)
{
   return _idle_time;
}
