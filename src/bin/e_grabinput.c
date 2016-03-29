#include "e.h"

/* local subsystem functions */
static void      _e_grabinput_focus(Ecore_Window win, E_Focus_Method method);

/* local subsystem globals */
static Ecore_Window grab_mouse_win = 0;
static Ecore_Window grab_key_win = 0;
static Ecore_Window focus_win = 0;
static E_Focus_Method focus_method = E_FOCUS_METHOD_NO_INPUT;
static double last_focus_time = 0.0;

static Ecore_Window focus_fix_win = 0;

/* externally accessible functions */
EINTERN int
e_grabinput_init(void)
{
   return 1;
}

EINTERN int
e_grabinput_shutdown(void)
{
   return 1;
}

// TODO: should be removed - yigl
E_API int
e_grabinput_get(Ecore_Window mouse_win, int confine_mouse EINA_UNUSED, Ecore_Window key_win)
{
   if (grab_mouse_win)
     {
        grab_mouse_win = 0;
     }
   if (grab_key_win)
     {
        grab_key_win = 0;
        focus_win = 0;
     }
   if (mouse_win)
     {
        grab_mouse_win = mouse_win;
     }
   if (key_win)
     {
        grab_key_win = key_win;
     }
   return 1;
}

E_API void
e_grabinput_release(Ecore_Window mouse_win, Ecore_Window key_win)
{
   if (mouse_win == grab_mouse_win)
     {
        grab_mouse_win = 0;
     }
   if (key_win == grab_key_win)
     {
        grab_key_win = 0;
        if (focus_win != 0)
          {
             _e_grabinput_focus(focus_win, focus_method);
             focus_win = 0;
             focus_method = E_FOCUS_METHOD_NO_INPUT;
          }
     }
}

E_API void
e_grabinput_focus(Ecore_Window win, E_Focus_Method method)
{
   if (grab_key_win != 0)
     {
        focus_win = win;
        focus_method = method;
     }
   else
     _e_grabinput_focus(win, method);
}

E_API double
e_grabinput_last_focus_time_get(void)
{
   return last_focus_time;
}

E_API Ecore_Window
e_grabinput_last_focus_win_get(void)
{
   return focus_fix_win;
}

E_API Ecore_Window
e_grabinput_key_win_get(void)
{
   return grab_key_win;
}

E_API Ecore_Window
e_grabinput_mouse_win_get(void)
{
   return grab_mouse_win;
}

static void
_e_grabinput_focus(Ecore_Window win, E_Focus_Method method)
{
   last_focus_time = ecore_loop_time_get();
}
