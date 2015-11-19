#include "e.h"
#include "e_comp.h"

#include "Eldbus.h"
#include <Ecore.h>

#define BUS "efl.compositor"
#define PATH "/efl/compositor/gestures"
#define IFACE "efl.compositor.gestures"

#define A11Y_DBUS_NAME "org.a11y.Bus"
#define A11Y_DBUS_PATH "/org/a11y/bus"
#define A11Y_DBUS_INTERFACE "org.a11y.Bus"
#define A11Y_DBUS_STATUS_INTERFACE "org.a11y.Status"


#undef DBG
static int _e_comp_log_dom = -1;
#define DBG(...)  do EINA_LOG_DOM_DBG(_e_comp_log_dom, __VA_ARGS__); while(0)

Evas_Object *gesture_rectangle = NULL;
Evas_Object *gesture_layer = NULL;
Eldbus_Connection *conn = NULL;
Eldbus_Service_Interface *iface = NULL;

enum Elm_Extended_Gesture_Type {
	ELM_GESTURE_N_DOUBLE_TAPS_AND_HOLD = ELM_GESTURE_LAST + 1000,
	ELM_GESTURE_N_HOVERS
};

typedef struct {
	int gesture_n_long_taps;
	int gesture_n_double_taps;
	int gesture_n_triple_taps;
	int gesture_n_momentum;
	int gesture_n_double_taps_and_hold;
	int gesture_n_hovers;
} Aggregated_Gesture_Components_States;

Aggregated_Gesture_Components_States _aggregated_components = {0};

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
   DBG("Resizing to x: %i, y: %i, w: %i, h:%i", x, y, w, h);
   evas_object_geometry_set(gesture_rectangle, x, y, w, h);
}

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
      case ELM_GESTURE_N_DOUBLE_TAPS_AND_HOLD: return "ELM_GESTURE_N_DOUBLE_TAPS_AND_HOLD";
      case ELM_GESTURE_N_HOVERS: return "ELM_GESTURE_N_HOVERS";
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


static Aggregated_Gesture_Components_States _get_new_aggregated_components_state(Elm_Gesture_Type gesture_type, Elm_Gesture_State gesture_state, int timestamp)
{
  Aggregated_Gesture_Components_States ret = _aggregated_components;
  if (gesture_state == ELM_GESTURE_STATE_START || gesture_state == ELM_GESTURE_STATE_MOVE)
    {
       if (gesture_type == ELM_GESTURE_MOMENTUM)
         {
            ret.gesture_n_momentum = timestamp;
         }
       else if (gesture_type == ELM_GESTURE_N_LONG_TAPS)
         {
            if (ret.gesture_n_long_taps == 0)
               ret.gesture_n_long_taps = timestamp;
         }
       else if (gesture_type == ELM_GESTURE_N_DOUBLE_TAPS)
         {
            if (ret.gesture_n_double_taps == 0)
               ret.gesture_n_double_taps = timestamp;
         }
       else if (gesture_type == ELM_GESTURE_N_TRIPLE_TAPS)
         {
            ret.gesture_n_triple_taps = timestamp;
         }
    }
  else
    {
       if (gesture_type == ELM_GESTURE_MOMENTUM)
         {
            ret.gesture_n_momentum = 0;
         }
       else if (gesture_type == ELM_GESTURE_N_LONG_TAPS)
         {
            ret.gesture_n_long_taps = 0;
         }
       else if (gesture_type == ELM_GESTURE_N_DOUBLE_TAPS)
         {

            ret.gesture_n_double_taps = 0;
         }
       else if (gesture_type == ELM_GESTURE_N_TRIPLE_TAPS)
         {
            ret.gesture_n_triple_taps = 0;
         }

       if (ret.gesture_n_long_taps == 0 &&
           ret.gesture_n_double_taps == 0 &&
           ret.gesture_n_triple_taps == 0 &&
           ret.gesture_n_momentum == 0)
         {
            ret.gesture_n_hovers = 0;
            ret.gesture_n_double_taps_and_hold = 0;
         }
    }
  if (ret.gesture_n_long_taps > 0 && ret.gesture_n_hovers == 0 && timestamp - ret.gesture_n_long_taps > 500)
     ret.gesture_n_hovers = timestamp;
  if (ret.gesture_n_double_taps > 0 && ret.gesture_n_double_taps_and_hold == 0 && timestamp - ret.gesture_n_double_taps > 500)
     ret.gesture_n_double_taps_and_hold = timestamp;

   DBG("_get_new_aggregated_components_state "
       "old.gesture_n_momentum: %i, "
       "old.gesture_n_long_taps: %i, "
       "old.gesture_n_double_taps: %i, "
       "old.gesture_n_triple_taps: %i, "
       "old.gesture_n_hovers: %i, "
       "old.gesture_n_double_taps_and_hold: %i\n"
       "new.gesture_n_momentum: %i, "
       "new.gesture_n_long_taps: %i, "
       "new.gesture_n_double_taps: %i, "
       "new.gesture_n_triple_taps: %i, "
       "new.gesture_n_hovers: %i, "
       "new.gesture_n_double_taps_and_hold: %i",
       _aggregated_components.gesture_n_momentum,
       _aggregated_components.gesture_n_long_taps,
       _aggregated_components.gesture_n_double_taps,
       _aggregated_components.gesture_n_triple_taps,
       _aggregated_components.gesture_n_hovers,
       _aggregated_components.gesture_n_double_taps_and_hold,
       ret.gesture_n_momentum,
       ret.gesture_n_long_taps,
       ret.gesture_n_double_taps,
       ret.gesture_n_triple_taps,
       ret.gesture_n_hovers,
       ret.gesture_n_double_taps_and_hold);
  return ret;
}

static void _send_taps_signal(int gesture_type, int gesture_state, int x, int y, int n, int timestamp)
{
   DBG("Sending signal for gesture <%s> and state <%s>", gesture_type_names(gesture_type), gesture_state_names(gesture_state));
   Eldbus_Message_Iter *iter;
   Eldbus_Message *msg;
   msg = eldbus_message_signal_new(PATH, IFACE, signals[_E_COMP_GESTURE_STATE_CHANGED_SIGNAL].name);

   iter = eldbus_message_iter_get(msg);
   eldbus_message_iter_arguments_append(iter, "ii", gesture_type, gesture_state);

   Eldbus_Message_Iter *variant_iter = eldbus_message_iter_container_new(iter, 'v', "(iiuu)");
   Eldbus_Message_Iter *struct_iter = eldbus_message_iter_container_new(variant_iter, 'r', NULL);
   eldbus_message_iter_arguments_append(struct_iter, "iiuu", x, y, n, timestamp);
   eldbus_message_iter_container_close(variant_iter, struct_iter);
   eldbus_message_iter_container_close(iter, variant_iter);
   eldbus_connection_send(conn, msg, NULL, NULL, -1);
}

static Evas_Event_Flags
_send_gesture_signal(void *data, void *event_info)
{
   int tmp = (int)data;
   Elm_Gesture_Type gesture_type = tmp % 10000;
   Elm_Gesture_State gesture_state = tmp / 10000;

   DBG("Received signal for gesture <%s> and state <%s>", gesture_type_names(gesture_type), gesture_state_names(gesture_state));
   int agg_gesture_x, agg_gesture_y, agg_gesture_n, agg_gesture_timestamp;
   Aggregated_Gesture_Components_States new_state;

   if (gesture_type == ELM_GESTURE_N_TAPS ||
       gesture_type == ELM_GESTURE_N_LONG_TAPS ||
       gesture_type == ELM_GESTURE_N_DOUBLE_TAPS ||
       gesture_type == ELM_GESTURE_N_TRIPLE_TAPS)
     {
        Elm_Gesture_Taps_Info * ginfo = event_info;
        _send_taps_signal(gesture_type, gesture_state, ginfo->x, ginfo->y, ginfo->n, ginfo->timestamp);
        agg_gesture_x = ginfo->x;
        agg_gesture_y = ginfo->y;
        agg_gesture_n = ginfo->n;
        agg_gesture_timestamp = ginfo->timestamp;
        new_state = _get_new_aggregated_components_state(gesture_type, gesture_state, agg_gesture_timestamp);
     }
   else if (gesture_type == ELM_GESTURE_N_FLICKS || gesture_type == ELM_GESTURE_N_LINES)
     {
        DBG("Sending signal for gesture <%s> and state <%s>", gesture_type_names(gesture_type), gesture_state_names(gesture_state));
        Eldbus_Message_Iter *iter;
        Eldbus_Message *msg;
        msg = eldbus_message_signal_new(PATH, IFACE, signals[_E_COMP_GESTURE_STATE_CHANGED_SIGNAL].name);

        iter = eldbus_message_iter_get(msg);
        eldbus_message_iter_arguments_append(iter, "ii", gesture_type, gesture_state);
        Elm_Gesture_Line_Info *ginfo = (Elm_Gesture_Line_Info *) event_info;
        agg_gesture_x = ginfo->momentum.x2;
        agg_gesture_y = ginfo->momentum.y2;
        agg_gesture_n = ginfo->momentum.n;
        agg_gesture_timestamp = ginfo->momentum.tx;

        Eldbus_Message_Iter *variant_iter = eldbus_message_iter_container_new(iter, 'v', "((iiiiuuiiu)d)");
        Eldbus_Message_Iter *struct_iter = eldbus_message_iter_container_new(variant_iter, 'r', NULL);
        Eldbus_Message_Iter *struct_iter_momentum = eldbus_message_iter_container_new(struct_iter, 'r', NULL);
        eldbus_message_iter_arguments_append(struct_iter_momentum, "iiiiuuiiu",
                                             ginfo->momentum.x1, ginfo->momentum.y1,
                                             ginfo->momentum.x2, ginfo->momentum.y2,
                                             ginfo->momentum.tx, ginfo->momentum.ty,
                                             ginfo->momentum.mx, ginfo->momentum.my,
                                             ginfo->momentum.n);
        eldbus_message_iter_container_close(struct_iter, struct_iter_momentum);
        eldbus_message_iter_arguments_append(struct_iter, "d", ginfo->angle);
        eldbus_message_iter_container_close(variant_iter, struct_iter);
        eldbus_message_iter_container_close(iter, variant_iter);
        eldbus_connection_send(conn, msg, NULL, NULL, -1);
     }
   else if (gesture_type == ELM_GESTURE_MOMENTUM)
   {
      Elm_Gesture_Momentum_Info *ginfo = (Elm_Gesture_Momentum_Info *) event_info;
      agg_gesture_x = ginfo->x2;
      agg_gesture_y = ginfo->y2;
      agg_gesture_n = ginfo->n;
      agg_gesture_timestamp = ginfo->tx;
   } else {
      DBG("ERROR - unhangled gesture state");
      return;
   }

   new_state = _get_new_aggregated_components_state(gesture_type, gesture_state, agg_gesture_timestamp);

   if (_aggregated_components.gesture_n_hovers == 0 && new_state.gesture_n_hovers > 0)
      _send_taps_signal(ELM_GESTURE_N_HOVERS, ELM_GESTURE_STATE_START, agg_gesture_x, agg_gesture_y, agg_gesture_n, agg_gesture_timestamp);
   else if (_aggregated_components.gesture_n_hovers > 0 && new_state.gesture_n_hovers == 0)
      _send_taps_signal(ELM_GESTURE_N_HOVERS, ELM_GESTURE_STATE_END, agg_gesture_x, agg_gesture_y, agg_gesture_n, agg_gesture_timestamp);
   else if (new_state.gesture_n_hovers > 0)
      _send_taps_signal(ELM_GESTURE_N_HOVERS, ELM_GESTURE_STATE_MOVE, agg_gesture_x, agg_gesture_y, agg_gesture_n, agg_gesture_timestamp);

   if (_aggregated_components.gesture_n_double_taps_and_hold == 0 && new_state.gesture_n_double_taps_and_hold > 0)
      _send_taps_signal(ELM_GESTURE_N_DOUBLE_TAPS_AND_HOLD, ELM_GESTURE_STATE_START, agg_gesture_x, agg_gesture_y, agg_gesture_n, agg_gesture_timestamp);
   else if (_aggregated_components.gesture_n_double_taps_and_hold > 0 && new_state.gesture_n_double_taps_and_hold == 0)
      _send_taps_signal(ELM_GESTURE_N_DOUBLE_TAPS_AND_HOLD, ELM_GESTURE_STATE_END, agg_gesture_x, agg_gesture_y, agg_gesture_n, agg_gesture_timestamp);
   else if (new_state.gesture_n_double_taps_and_hold > 0)
      _send_taps_signal(ELM_GESTURE_N_DOUBLE_TAPS_AND_HOLD, ELM_GESTURE_STATE_MOVE, agg_gesture_x, agg_gesture_y, agg_gesture_n, agg_gesture_timestamp);

   _aggregated_components = new_state;

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
        DBG("Could not request bus name. Error: %s. Message: %s", errname, errmsg);
        return;
     }

   if (!eldbus_message_arguments_get(msg, "u", &flag))
     {
        DBG("Could not get arguments on on_name_request");
        return;
     }

   if (!(flag & ELDBUS_NAME_REQUEST_REPLY_PRIMARY_OWNER))
      DBG("Name already in use\n");
}

static void _enable_gesture_service() {
   DBG("Enabling gesture service");

   if(conn)
      return;

   conn = eldbus_address_connection_get("unix:path=/var/run/dbus/system_bus_socket");
   eldbus_name_request(conn, "efl.compositor.gestures", 0, _e_msgbus_request_name_cb, NULL);

   iface = eldbus_service_interface_register(conn, PATH, &iface_desc);

   E_Comp *e_comp = e_comp_get(NULL);

   gesture_rectangle = evas_object_rectangle_add(e_comp->evas);
   evas_object_layer_set(gesture_rectangle, E_LAYER_MAX);
   _resize_gesture_rectangle(NULL);
   evas_object_color_set(gesture_rectangle, 0, 0, 255, 128);
   evas_object_show(gesture_rectangle);

   gesture_layer = elm_gesture_layer_add(gesture_rectangle);
   elm_gesture_layer_hold_events_set(gesture_layer, EINA_FALSE);
   elm_gesture_layer_attach(gesture_layer, gesture_rectangle);
   elm_gesture_layer_hold_events_set(gesture_layer, EINA_FALSE);
   ecore_evas_callback_resize_set(e_comp->ee, _resize_gesture_rectangle);

   //518 taken from /etc/config/model-config.xml on Tizen 3.0 Mobile on Note 4
   elm_gesture_layer_tap_finger_size_set(gesture_layer, 518*0.15);//ration taken from Qt sources for Android

   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_TAPS, ELM_GESTURE_STATE_START, _send_gesture_signal, (void *)(ELM_GESTURE_N_TAPS + 10000*ELM_GESTURE_STATE_START));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_TAPS, ELM_GESTURE_STATE_END,   _send_gesture_signal, (void *)(ELM_GESTURE_N_TAPS + 10000*ELM_GESTURE_STATE_END));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_TAPS, ELM_GESTURE_STATE_ABORT, _send_gesture_signal, (void *)(ELM_GESTURE_N_TAPS + 10000*ELM_GESTURE_STATE_ABORT));

   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_DOUBLE_TAPS, ELM_GESTURE_STATE_START, _send_gesture_signal, (void *)(ELM_GESTURE_N_DOUBLE_TAPS + 10000*ELM_GESTURE_STATE_START));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_DOUBLE_TAPS, ELM_GESTURE_STATE_END, _send_gesture_signal, (void *)(ELM_GESTURE_N_DOUBLE_TAPS + 10000*ELM_GESTURE_STATE_END));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_DOUBLE_TAPS, ELM_GESTURE_STATE_ABORT, _send_gesture_signal, (void *)(ELM_GESTURE_N_DOUBLE_TAPS + 10000*ELM_GESTURE_STATE_ABORT));

   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_TRIPLE_TAPS, ELM_GESTURE_STATE_START,   _send_gesture_signal, (void *)(ELM_GESTURE_N_TRIPLE_TAPS + 10000*ELM_GESTURE_STATE_START));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_TRIPLE_TAPS, ELM_GESTURE_STATE_END,   _send_gesture_signal, (void *)(ELM_GESTURE_N_TRIPLE_TAPS + 10000*ELM_GESTURE_STATE_END));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_TRIPLE_TAPS, ELM_GESTURE_STATE_ABORT,   _send_gesture_signal, (void *)(ELM_GESTURE_N_TRIPLE_TAPS + 10000*ELM_GESTURE_STATE_ABORT));

   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_FLICKS, ELM_GESTURE_STATE_START,   _send_gesture_signal, (void *)(ELM_GESTURE_N_FLICKS + 10000*ELM_GESTURE_STATE_START));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_FLICKS, ELM_GESTURE_STATE_END,   _send_gesture_signal, (void *)(ELM_GESTURE_N_FLICKS + 10000*ELM_GESTURE_STATE_END));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_FLICKS, ELM_GESTURE_STATE_ABORT, _send_gesture_signal, (void *)(ELM_GESTURE_N_FLICKS + 10000*ELM_GESTURE_STATE_ABORT));

   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_LONG_TAPS, ELM_GESTURE_STATE_START,   _send_gesture_signal, (void *)(ELM_GESTURE_N_LONG_TAPS + 10000*ELM_GESTURE_STATE_START));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_LONG_TAPS, ELM_GESTURE_STATE_ABORT,   _send_gesture_signal, (void *)(ELM_GESTURE_N_LONG_TAPS + 10000*ELM_GESTURE_STATE_ABORT));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_N_LONG_TAPS, ELM_GESTURE_STATE_END,   _send_gesture_signal, (void *)(ELM_GESTURE_N_LONG_TAPS + 10000*ELM_GESTURE_STATE_END));

   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_MOMENTUM, ELM_GESTURE_STATE_START,   _send_gesture_signal, (void *)(ELM_GESTURE_MOMENTUM + 10000*ELM_GESTURE_STATE_START));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_MOMENTUM, ELM_GESTURE_STATE_MOVE,   _send_gesture_signal, (void *)(ELM_GESTURE_MOMENTUM + 10000*ELM_GESTURE_STATE_MOVE));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_MOMENTUM, ELM_GESTURE_STATE_ABORT,   _send_gesture_signal, (void *)(ELM_GESTURE_MOMENTUM + 10000*ELM_GESTURE_STATE_ABORT));
   elm_gesture_layer_cb_set(gesture_layer, ELM_GESTURE_MOMENTUM, ELM_GESTURE_STATE_END,   _send_gesture_signal, (void *)(ELM_GESTURE_MOMENTUM + 10000*ELM_GESTURE_STATE_END));

   DBG("gesture service enabled");

}

static void _disable_gesture_service() {
   DBG("Disabling gesture service");
   if (iface)
     {
        eldbus_service_object_unregister(iface);
        iface = NULL;
     }
   if (conn)
     {
        eldbus_connection_unref(conn);
        conn = NULL;
     }

   if (gesture_layer)
      evas_object_del(gesture_layer);
   if (gesture_rectangle)
      evas_object_del(gesture_rectangle);
}

static void
_properties_changed_cb(void *data, Eldbus_Proxy *proxy EINA_UNUSED, void *event)
{
   DBG("");
   Eldbus_Proxy_Event_Property_Changed *ev = event;
   Eo *bridge = data;
   Eina_Bool val;
   const char *ifc = eldbus_proxy_interface_get(ev->proxy);
   if (ev->name && !strcmp(ev->name, "ScreenReaderEnabled" ) &&
       ifc && !strcmp(A11Y_DBUS_STATUS_INTERFACE, ifc))
     {
        if (!eina_value_get(ev->value, &val))
          {
             DBG("Unable to get ScreenReaderEnabled property value");
             return;
          }
         if (val)
            _enable_gesture_service();
         else
            _disable_gesture_service();
     }
}

static int _check_if_screen_reader_running(void *data) {
   DBG("");
   const char* cmd = "ps -ef | grep screen-reader | grep -v grep -c";

   FILE* app = popen(cmd, "r");
   char instances = '0';

   if (app)
     {
        fread(&instances, sizeof(instances), 1, app);
        pclose(app);
     }
   DBG("Screen reader running %c", instances);
   if (instances == '0' && conn)
      _disable_gesture_service();
   else if (instances != '0' && !conn)
      _enable_gesture_service();
//    Eldbus_Proxy *proxy;
//    Eldbus_Pending *req;
//    Eldbus_Object *bus_obj;
//    Eldbus_Connection *session_bus;
//    int ret = ECORE_CALLBACK_DONE;
//    if (!(session_bus = eldbus_address_connection_get("unix:path=/run/user/5001/dbus/user_bus_socket")))
//      {
//         DBG("Unable to connect to Session Bus");
// 	ret = ECORE_CALLBACK_RENEW;
//      }
//    else if (!(bus_obj = eldbus_object_get(session_bus, A11Y_DBUS_NAME, A11Y_DBUS_PATH)))
//      {
//         DBG("Could not get /org/a11y/bus object");
// 	ret = ECORE_CALLBACK_RENEW;
//      }
//    else if (!(proxy = eldbus_proxy_get(bus_obj, A11Y_DBUS_STATUS_INTERFACE)))
//      {
//         DBG("Could not get proxy object for %s interface", A11Y_DBUS_STATUS_INTERFACE);
// 	ret = ECORE_CALLBACK_RENEW;
//      } else {
// 	eldbus_proxy_properties_monitor(proxy, EINA_TRUE);
// 	eldbus_proxy_event_callback_add(proxy, ELDBUS_PROXY_EVENT_PROPERTY_CHANGED, _properties_changed_cb, NULL);
//      }
//    DBG("ret:%i", ret);
   return ECORE_CALLBACK_RENEW;
}

EAPI void *
e_modapi_init(E_Module *m)
{
   _e_comp_log_dom = eina_log_domain_register("e_comp_gestures", EINA_COLOR_YELLOW);
   ecore_init();
   eldbus_init();

   ecore_timer_add(2, _check_if_screen_reader_running, NULL);

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   eldbus_shutdown();
   ecore_shutdown();
   eina_log_domain_unregister(_e_comp_log_dom);

   return 1;
}
