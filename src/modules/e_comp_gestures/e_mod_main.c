#include "e.h"
#include "e_comp.h"

#include "Eldbus.h"
#include <Ecore.h>

#define BUS "efl.compositor"
#define PATH "/efl/compositor/gestures"
#define IFACE "efl.compositor.gestures"

#undef ERR
static int _e_comp_log_dom = -1;
#define ERR(...)            EINA_LOG_DOM_ERR(_e_comp_log_dom, __VA_ARGS__)

Evas_Object *gesture_rectangle = NULL;
Evas_Object *gesture_layer = NULL;
Eldbus_Connection *conn = NULL;
Eldbus_Service_Interface *iface = NULL;


enum _Gesture_Signals {
   _E_COMP_GESTURE_STATE_CHANGED_SIGNAL = 0
};


static const Eldbus_Signal signals[] = {
      {"GestureStateChanged", ELDBUS_ARGS({"iiv", NULL}), 0},
      { }
};

static const Eldbus_Method methods[] = {
      { }
};

static const Eldbus_Property properties[] = {
      { }
};

static const Eldbus_Service_Interface_Desc iface_desc = {
   IFACE, methods, signals, properties, NULL, NULL
};



EAPI E_Module_Api e_modapi = { E_MODULE_API_VERSION, "E_Comp_Gestures" };


static void
_resize_gesture_rectangle(Ecore_Evas *ee EINA_UNUSED)
{
   int x, y, w, h;
   E_Comp *e_comp = e_comp_get(NULL);
   ecore_evas_geometry_get(e_comp->ee, &x, &y, &w, &h);
   ERR("Resizing to x: %i, y: %i, w: %i, h:%i", x, y, w, h);
   evas_object_geometry_set(gesture_rectangle, x, y, w, h);
}

// ELM_GESTURE_N_TAPS, /**< N fingers single taps */
// ELM_GESTURE_N_LONG_TAPS, /**< N fingers single long-taps */
// ELM_GESTURE_N_DOUBLE_TAPS, /**< N fingers double-single taps */
// ELM_GESTURE_N_TRIPLE_TAPS, /**< N fingers triple-single taps */
// 
// ELM_GESTURE_MOMENTUM, /**< Reports momentum in the direction of move */
// 
// ELM_GESTURE_N_LINES, /**< N fingers line gesture */
// ELM_GESTURE_N_FLICKS, /**< N fingers flick gesture */
// 
// ELM_GESTURE_ZOOM, /**< Zoom */
// ELM_GESTURE_ROTATE, /**< Rotate */*/

// ELM_GESTURE_STATE_START, /**< Gesture STARTed     */
// ELM_GESTURE_STATE_MOVE, /**< Gesture is ongoing  */
// ELM_GESTURE_STATE_END, /**< Gesture completed   */
// ELM_GESTURE_STATE_ABORT /**< Ongoing gesture was ABORTed */


static const char * gesture_type_names(Elm_Gesture_Type gesture_type)
{
   switch(gesture_type) {
      case ELM_GESTURE_FIRST: return "ELM_GESTURE_LAST";
      case ELM_GESTURE_N_TAPS: return "ELM_GESTURE_N_TAPS";
      case ELM_GESTURE_N_LONG_TAPS: return "ELM_GESTURE_N_LONG_TAPS";
      case ELM_GESTURE_N_DOUBLE_TAPS: return "ELM_GESTURE_N_DOUBLE_TAPS";
      case ELM_GESTURE_N_TRIPLE_TAPS: return "ELM_GESTURE_N_TRIPLE_TAPS";
      case ELM_GESTURE_MOMENTUM: return "ELM_GESTURE_MOMENTUM";
      case ELM_GESTURE_N_LINES: return "ELM_GESTURE_N_LINES";
      case ELM_GESTURE_N_FLICKS: return "ELM_GESTURE_N_FLICKS";
      case ELM_GESTURE_ZOOM: return "ELM_GESTURE_ZOOM";
      case ELM_GESTURE_ROTATE: return "ELM_GESTURE_ROTATE";
      case ELM_GESTURE_LAST: return "ELM_GESTURE_LAST";
   }
   return "";
}

static const char *gesture_state_names(Elm_Gesture_State gesture_state)
{
  switch(gesture_state) {
     case ELM_GESTURE_STATE_START: return "ELM_GESTURE_STATE_START";
     case ELM_GESTURE_STATE_MOVE: return "ELM_GESTURE_STATE_MOVE";
     case ELM_GESTURE_STATE_END: return "ELM_GESTURE_STATE_END";
     case ELM_GESTURE_STATE_ABORT: return "ELM_GESTURE_STATE_ABORT";
     case ELM_GESTURE_STATE_UNDEFINED: return "ELM_GESTURE_STATE_UNDEFINED";
  }
  return "";
}

static void _send_gesture_signal(Elm_Gesture_Type gesture_type, Elm_Gesture_State gesture_state, void *event_info)
{
   ERR("send signal for gesture <%s> and state <%s>", gesture_type_names(gesture_type), gesture_state_names(gesture_state));
   Eldbus_Message_Iter *iter;
   Eldbus_Message *msg;

   msg = eldbus_message_signal_new(PATH, IFACE, signals[_E_COMP_GESTURE_STATE_CHANGED_SIGNAL].name);

   iter = eldbus_message_iter_get(msg);
   eldbus_message_iter_arguments_append(iter, "ii", gesture_type, gesture_state);
   if (gesture_type == ELM_GESTURE_N_DOUBLE_TAPS)
   {
      Elm_Gesture_Taps_Info * ginfo = event_info;
      eldbus_message_iter_arguments_append(iter, "iiiiuu", gesture_type, gesture_state, ginfo->x, ginfo->y, ginfo->n, ginfo->timestamp);
   }

   eldbus_connection_send(conn, msg, NULL, NULL, -1);
}

static Evas_Event_Flags
gesture_n_taps_start(void *data, void *event_info)
{
   ERR("GESTURE MOMENTUM START DETECTED");
   _send_gesture_signal(ELM_GESTURE_N_TAPS, ELM_GESTURE_STATE_START, event_info);
   return EVAS_EVENT_FLAG_NONE;
}

static Evas_Event_Flags
gesture_n_taps_move(void *data, void *event_info)
{
   ERR("GESTURE MOMENTUM MOVE DETECTED");
   _send_gesture_signal(ELM_GESTURE_N_TAPS, ELM_GESTURE_STATE_MOVE, event_info);
   return EVAS_EVENT_FLAG_NONE;
}

static Evas_Event_Flags
gesture_n_taps_end(void *data, void *event_info)
{
   ERR("GESTURE MOMENTUM END DETECTED");
   _send_gesture_signal(ELM_GESTURE_N_TAPS, ELM_GESTURE_STATE_END, event_info);
   return EVAS_EVENT_FLAG_NONE;
}

static Evas_Event_Flags
gesture_n_taps_abort(void *data, void *event_info)
{
   ERR("GESTURE MOMENTUM ABORT DETECTED");
   _send_gesture_signal(ELM_GESTURE_N_TAPS, ELM_GESTURE_STATE_ABORT, event_info);
   return EVAS_EVENT_FLAG_NONE;
}

static void
_e_msgbus_request_name_cb(void *data __UNUSED__, const Eldbus_Message *msg,
                          Eldbus_Pending *pending __UNUSED__)
{
   unsigned int flag;
   const char *errname, *errmsg;
   if (eldbus_message_error_get(msg, &errname, &errmsg))
     {
        ERR("Could not request bus name. Error: %s. Message: %s", errname, errmsg);
        return;
     }

   if (!eldbus_message_arguments_get(msg, "u", &flag))
     {
        ERR("Could not get arguments on on_name_request");
        return;
     }

   if (!(flag & ELDBUS_NAME_REQUEST_REPLY_PRIMARY_OWNER))
     ERR("Name already in use\n");
}


EAPI void *
e_modapi_init(E_Module *m)
{
   _e_comp_log_dom = eina_log_domain_register("e_comp_gestures", EINA_COLOR_YELLOW);
   setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/user/5001/dbus/user_bus_socket", 1);
   ERR("BEFORE ECORE INIT");
   ecore_init();
   ERR("BEFORE ELDBUS INIT");
   eldbus_init();


   ERR("BEFORE ELDBUS CONNECTION GET");

   conn = eldbus_address_connection_get("unix:path=/var/run/dbus/system_bus_socket");
   eldbus_name_request(conn, "efl.compositor.gestures", 0, _e_msgbus_request_name_cb, NULL);
   ERR("BEFORE INTERFACE REGISTER INIT");
   iface = eldbus_service_interface_register(conn, PATH, &iface_desc);

   E_Comp *e_comp = e_comp_get(NULL);

   gesture_rectangle = evas_object_rectangle_add(e_comp->evas);
   evas_object_layer_set(gesture_rectangle, E_LAYER_MAX);
   _resize_gesture_rectangle(NULL);
   evas_object_color_set(gesture_rectangle, 0, 0, 255, 128);
   evas_object_show(gesture_rectangle);

   gesture_layer = elm_gesture_layer_add(gesture_rectangle);
   elm_gesture_layer_attach(gesture_layer, gesture_rectangle);
   ecore_evas_callback_resize_set(e_comp->ee, _resize_gesture_rectangle);

   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_TAPS, ELM_GESTURE_STATE_START, gesture_n_taps_start, NULL);
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_TAPS, ELM_GESTURE_STATE_MOVE,  gesture_n_taps_move, NULL);
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_TAPS, ELM_GESTURE_STATE_END,   gesture_n_taps_end, NULL);
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_TAPS, ELM_GESTURE_STATE_ABORT, gesture_n_taps_abort, NULL);

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   eldbus_shutdown();
   ecore_shutdown();
   eldbus_service_object_unregister(iface);
   eldbus_connection_unref(conn);
   if (gesture_layer)
      evas_object_del(gesture_layer);
   if (gesture_rectangle)
      evas_object_del(gesture_rectangle);

   return 1;
}
