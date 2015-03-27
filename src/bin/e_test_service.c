#include "e.h"

typedef struct _E_Test_Service
{
   Eldbus_Connection *conn;
   Eldbus_Service_Interface *iface;
   Eina_List *hdlrs;
   struct
   {
      Ecore_Window win;
      E_Client *ec;
      int vis;
   } registrant; /* TODO: Eina_List */

} E_Test_Service;

static E_Test_Service *_testsrv = NULL;

static Eina_Bool       _e_test_srv_cb_property_get(const Eldbus_Service_Interface *iface, const char *name, Eldbus_Message_Iter *iter, const Eldbus_Message *msg, Eldbus_Message **err);
static Eldbus_Message *_e_test_srv_cb_register_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_srv_cb_deregister_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_srv_cb_set_window_stack(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_srv_cb_get_window_info(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);

static const Eldbus_Method methods[] =
{
   {
      "RegisterWindow",
      ELDBUS_ARGS({"u", "window id to be registered"}),
      ELDBUS_ARGS({"b", "accept or not"}),
      _e_test_srv_cb_register_window, 0
   },
   {
      "DeregisterWindow",
      ELDBUS_ARGS({"u", "window id to be deregistered"}),
      ELDBUS_ARGS({"b", "accept or not"}),
      _e_test_srv_cb_deregister_window, 0
   },
   {
      "SetWindowStack",
      ELDBUS_ARGS({"uui", "window id to change stack, sibling window id, stack change type"}),
      NULL,
      _e_test_srv_cb_set_window_stack, 0
   },
   {
      "GetWindowInfo",
      NULL,
      ELDBUS_ARGS({"ua(usiiiiibb)", "array of ec"}),
      _e_test_srv_cb_get_window_info, 0
   },
   {}
};

enum
{
   E_TEST_SERVICE_SIGNAL_CHANGE_VISIBILITY = 0,
   E_TEST_SERVICE_SIGNAL_CHANGE_STACK,
};

static const Eldbus_Signal signals[] =
{
   [E_TEST_SERVICE_SIGNAL_CHANGE_VISIBILITY] =
   {
      "VisibilityChanged",
      ELDBUS_ARGS({"ub", "window id, visibility"}),
      0
   },
   [E_TEST_SERVICE_SIGNAL_CHANGE_STACK] =
   {
      "StackChanged",
      ELDBUS_ARGS({"u", "window id was restacked"}),
      0
   },
   {}
};

static const Eldbus_Property properties[] =
{
   { "Registrant", "u", NULL, NULL, 0 },
   {}
};

static const Eldbus_Service_Interface_Desc iface_desc =
{
   "org.enlightenment.wm.Test",
   methods,
   signals,
   properties,
   _e_test_srv_cb_property_get,
   NULL
};

static void
_e_test_srv_registrant_clear(void)
{
   EINA_SAFETY_ON_NULL_RETURN(_testsrv);

   _testsrv->registrant.win = 0;
   _testsrv->registrant.vis = 0;
   _testsrv->registrant.ec = NULL;
}

/* Method Handlers */
static Eldbus_Message *
_e_test_srv_cb_register_window(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   Ecore_Window id;
   Eina_Bool res;
   int approve = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_testsrv, NULL);

   reply = eldbus_message_method_return_new(msg);
   EINA_SAFETY_ON_NULL_GOTO(_testsrv, finish);

   res = eldbus_message_arguments_get(msg, "u", &id);
   if (res)
     {
        _testsrv->registrant.win = id;
        approve = 1;
     }

finish:
   eldbus_message_arguments_append(reply, "b", approve);
   return reply;
}

static Eldbus_Message *
_e_test_srv_cb_deregister_window(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   Ecore_Window id;
   Eina_Bool res;
   int approve = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_testsrv, NULL);

   reply = eldbus_message_method_return_new(msg);
   EINA_SAFETY_ON_NULL_GOTO(_testsrv, finish);

   res = eldbus_message_arguments_get(msg, "u", &id);
   if ((res) && (id == _testsrv->registrant.win))
     {
        _e_test_srv_registrant_clear();
        approve = 1;
     }

finish:
   eldbus_message_arguments_append(reply, "b", approve);
   return reply;
}

static Eldbus_Message *
_e_test_srv_cb_set_window_stack(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   Ecore_Window win, sibling_win = 0;
   E_Client *ec, *sibling_ec;
   int type = -1;
   Eina_Bool res;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_testsrv, NULL);

   reply = eldbus_message_method_return_new(msg);
   EINA_SAFETY_ON_NULL_GOTO(_testsrv, finish);

   res = eldbus_message_arguments_get(msg,
                                      "uiu",
                                      &win,
                                      &sibling_win,
                                      &type);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   ec = e_pixmap_find_client(E_PIXMAP_TYPE_X, win);
   sibling_ec = e_pixmap_find_client(E_PIXMAP_TYPE_X, sibling_win);
   EINA_SAFETY_ON_NULL_GOTO(ec, finish);

   /* TODO: focus setting */
   switch (type)
     {
      case 0: evas_object_raise(ec->frame); break;
      case 1: evas_object_lower(ec->frame); break;
      case 2: if (sibling_ec) evas_object_stack_above(ec->frame, sibling_ec->frame); break;
      case 3: if (sibling_ec) evas_object_stack_below(ec->frame, sibling_ec->frame); break;
      default: break;
     }

finish:
   return reply;
}

static Eldbus_Message *
_e_test_srv_cb_get_window_info(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   Eldbus_Message_Iter *iter, *array_of_ec;
   E_Client *ec;
   Evas_Object *o;
   E_Comp *comp;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_testsrv, NULL);
   if (!(comp = e_comp_get(NULL))) return NULL;

   reply = eldbus_message_method_return_new(msg);

   iter = eldbus_message_iter_get(reply);

   eldbus_message_iter_arguments_append(iter,
                                        "ua(usiiiiibb)",
                                        _testsrv->registrant.win,
                                        &array_of_ec);

   for (o = evas_object_top_get(comp->evas); o; o = evas_object_below_get(o))
     {
        Eldbus_Message_Iter *struct_of_ec;
        Ecore_Window win;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);
        eldbus_message_iter_arguments_append(array_of_ec, "(usiiiiibb)", &struct_of_ec);
        eldbus_message_iter_arguments_append(struct_of_ec,
                                             "usiiiiibb",
                                             win,
                                             e_client_util_name_get(ec) ? e_client_util_name_get(ec) : "NO NAME",
                                             ec->x,
                                             ec->y,
                                             ec->w,
                                             ec->h,
                                             ec->layer,
                                             ec->visible,
                                             ec->argb);
        eldbus_message_iter_container_close(array_of_ec, struct_of_ec);
     }

   eldbus_message_iter_container_close(iter, array_of_ec);

   return reply;
}

static void
_e_test_srv_cb_name_request(void *data EINA_UNUSED, const Eldbus_Message *msg, Eldbus_Pending *pending EINA_UNUSED)
{
   const char *name = NULL, *text = NULL;
   unsigned int flag;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, err);

   res = eldbus_message_arguments_get(msg, "u", &flag);
   EINA_SAFETY_ON_FALSE_GOTO(res, err);
   EINA_SAFETY_ON_FALSE_GOTO((flag == ELDBUS_NAME_REQUEST_REPLY_PRIMARY_OWNER), err);

   return;

err:
   ERR("errname:%s errmsg:%s\n", name, text);
   e_test_service_shutdown();
}

static Eina_Bool
_e_test_srv_cb_ev_visibility_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Client *ec;
   Ecore_Window win = 0;
   E_Event_Client *ev = event;
   Eldbus_Message *sig;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_testsrv, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   win = e_client_util_win_get(ec);

   if (!_testsrv->registrant.win) return ECORE_CALLBACK_PASS_ON;
   if (win != _testsrv->registrant.win) return ECORE_CALLBACK_PASS_ON;

   if (!_testsrv->registrant.ec)
     _testsrv->registrant.ec = ec;

   if ((_testsrv->registrant.vis) != (!ec->visibility.obscured))
     {
        sig = eldbus_service_signal_new(_testsrv->iface,
                                        E_TEST_SERVICE_SIGNAL_CHANGE_VISIBILITY);
        eldbus_message_arguments_append(sig,
                                        "ub",
                                        _testsrv->registrant.win,
                                        !ec->visibility.obscured);
        eldbus_service_signal_send(_testsrv->iface, sig);
     }

   _testsrv->registrant.vis = !ec->visibility.obscured;

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_test_srv_cb_ev_client_remove(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_testsrv, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;

   if (!_testsrv->registrant.ec) return ECORE_CALLBACK_PASS_ON;
   if (ec != _testsrv->registrant.ec) return ECORE_CALLBACK_PASS_ON;

   _e_test_srv_registrant_clear();

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_test_srv_cb_ev_client_restack(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   Eldbus_Message *sig;
   Ecore_Window win;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_testsrv, ECORE_CALLBACK_PASS_ON);
   if (!_testsrv->registrant.ec) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;

   if ((win = e_client_util_win_get(ec)))
     {
        sig = eldbus_service_signal_new(_testsrv->iface,
                                        E_TEST_SERVICE_SIGNAL_CHANGE_STACK);
        eldbus_message_arguments_append(sig, "u", win);
        eldbus_service_signal_send(_testsrv->iface, sig);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_test_srv_cb_property_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const char *name, Eldbus_Message_Iter *iter, const Eldbus_Message *msg EINA_UNUSED, Eldbus_Message **err EINA_UNUSED)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(_testsrv, EINA_FALSE);

   if (!e_util_strcmp(name, "Registrant"))
     {
        eldbus_message_iter_basic_append(iter, 'u', _testsrv->registrant.win);
     }

   return EINA_TRUE;
}

/* externally accessible functions */
EINTERN int
e_test_service_init(void)
{
   eldbus_init();

   _testsrv = E_NEW(E_Test_Service, 1);

   _testsrv->conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   _testsrv->iface = eldbus_service_interface_register(_testsrv->conn,
                                                       "/org/enlightenment/wm",
                                                       &iface_desc);

   eldbus_name_request(_testsrv->conn,
                       "org.enlightenment.wm",
                       ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE,
                       _e_test_srv_cb_name_request,
                       _testsrv->iface);

   E_LIST_HANDLER_APPEND(_testsrv->hdlrs, E_EVENT_CLIENT_VISIBILITY_CHANGE, _e_test_srv_cb_ev_visibility_change, NULL);
   E_LIST_HANDLER_APPEND(_testsrv->hdlrs, E_EVENT_CLIENT_REMOVE,            _e_test_srv_cb_ev_client_remove,     NULL);
   E_LIST_HANDLER_APPEND(_testsrv->hdlrs, E_EVENT_CLIENT_STACK,             _e_test_srv_cb_ev_client_restack,    NULL);

   _e_test_srv_registrant_clear();

   return 1;
}

EINTERN int
e_test_service_shutdown(void)
{
   if (!_testsrv) return 1;

   E_FREE_LIST(_testsrv->hdlrs, ecore_event_handler_del);

   eldbus_service_interface_unregister(_testsrv->iface);
   eldbus_connection_unref(_testsrv->conn);
   eldbus_shutdown();

   E_FREE(_testsrv);

   return 1;
}
