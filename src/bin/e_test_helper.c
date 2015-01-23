#include "e.h"

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define IFACE "org.enlightenment.wm.Test"

typedef struct _Test_Helper_Data
{
   Eldbus_Connection *conn;
   Eldbus_Service_Interface *iface;

   Eina_List *hdlrs;

   struct
     {
        Ecore_Window win;
        E_Client *ec;
        int vis;
        Eina_Bool disuse;
     } target;

} Test_Helper_Data;

static Test_Helper_Data *th_data = NULL;

static Eldbus_Message* _e_test_helper_cb_register_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message* _e_test_helper_cb_deregister_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message* _e_test_helper_cb_get_clients(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);

enum
{
   E_TEST_HELPER_SIGNAL_CHANGE_VISIBILITY = 0,
};

static const Eldbus_Signal signals[] = {
   [E_TEST_HELPER_SIGNAL_CHANGE_VISIBILITY] =
     {
        "ChangeVisibility",
        ELDBUS_ARGS({"ub", "window id, visibility"}),
        0
     },
   { }
};

static const Eldbus_Method methods[] ={
     {
        "GetClients",
        NULL,
        ELDBUS_ARGS({"ua(usiiiiibb)", "array of ec"}),
        _e_test_helper_cb_get_clients, 0
     },
     {
        "RegisterWindow",
        ELDBUS_ARGS({"u", "window id to be registered"}),
        ELDBUS_ARGS({"b", "accept or not"}),
        _e_test_helper_cb_register_window, 0
     },
     {
        "DeregisterWindow",
        ELDBUS_ARGS({"u", "window id to be deregistered"}),
        ELDBUS_ARGS({"b", "accept or not"}),
        _e_test_helper_cb_deregister_window, 0
     },
     { }
};

static const Eldbus_Service_Interface_Desc iface_desc = {
     IFACE, methods, signals
};

static void
_e_test_helper_target_clear(void)
{
   EINA_SAFETY_ON_NULL_RETURN(th_data);

   th_data->target.win = 0;
   th_data->target.vis = -1;
   th_data->target.ec = NULL;
   th_data->target.disuse = EINA_FALSE;
}

static void
_e_test_helper_message_append_clients(Eldbus_Message_Iter *iter)
{
   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;
   Evas_Object *o;
   E_Comp *comp;

   EINA_SAFETY_ON_NULL_RETURN(th_data);

   if (!(comp = e_comp_get(NULL))) return;

   eldbus_message_iter_arguments_append(iter, "ua(usiiiiibb)", th_data->target.win, &array_of_ec);

   // append clients.
   for (o = evas_object_top_get(comp->evas); o; o = evas_object_below_get(o))
     {
        Eldbus_Message_Iter* struct_of_ec;
        Ecore_Window win;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_pixmap_window_get(ec->pixmap);
        eldbus_message_iter_arguments_append(array_of_ec, "(usiiiiibb)", &struct_of_ec);
        eldbus_message_iter_arguments_append
           (struct_of_ec, "usiiiiibb",
            win,
            e_client_util_name_get(ec) ?: ec->icccm.name,
            ec->x, ec->y, ec->w, ec->h, ec->layer,
            ec->visible, ec->argb);
        eldbus_message_iter_container_close(array_of_ec, struct_of_ec);
     }

   eldbus_message_iter_container_close(iter, array_of_ec);
}

/* Signal senders */
static void
_e_test_helper_send_change_visibility(Ecore_Window win, Eina_Bool vis)
{
   Eldbus_Message *signal;

   EINA_SAFETY_ON_NULL_RETURN(th_data);

   signal = eldbus_service_signal_new(th_data->iface,
                                      E_TEST_HELPER_SIGNAL_CHANGE_VISIBILITY);
   eldbus_message_arguments_append(signal, "ub", win, vis);
   eldbus_service_signal_send(th_data->iface, signal);
}

/* Method Handlers */
static Eldbus_Message *
_e_test_helper_cb_register_window(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                  const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window id;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, reply);

   if (!eldbus_message_arguments_get(msg, "u", &id))
     {
        ERR("Error on eldbus_message_arguments_get()\n");
        return reply;
     }

   eldbus_message_arguments_append(reply, "b", !th_data->target.win);
   if (!th_data->target.win) th_data->target.win = id;

   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_deregister_window(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                    const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window id;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, reply);

   if (!eldbus_message_arguments_get(msg, "u", &id))
     {
        ERR("Error on eldbus_message_arguments_get()\n");
        return reply;
     }
   eldbus_message_arguments_append(reply, "b", (th_data->target.win == id) && (!th_data->target.vis) && (th_data->target.ec));

   if (th_data->target.win != id) return reply;

   th_data->target.disuse = EINA_TRUE;

   if (th_data->target.ec) evas_object_hide(th_data->target.ec->frame);
   if (!th_data->target.vis) _e_test_helper_target_clear();

   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_get_clients(const Eldbus_Service_Interface *iface EINA_UNUSED,
                              const Eldbus_Message *msg)
{
   Eldbus_Message *reply;

   reply = eldbus_message_method_return_new(msg);
   _e_test_helper_message_append_clients(eldbus_message_iter_get(reply));

   return reply;
}

static void
_e_test_helper_cb_name_request(void *data EINA_UNUSED,
                               const Eldbus_Message *msg,
                               Eldbus_Pending *pending EINA_UNUSED)
{
   unsigned int flag;
   const char *errname, *errmsg;

   if (eldbus_message_error_get(msg, &errname, &errmsg))
     {
        ERR("error on _e_test_helper_cb_name_request %s %s\n", errname, errmsg);
        return;
     }

   if (!eldbus_message_arguments_get(msg, "u", &flag))
     {
        ERR("error geting arguments on _e_test_helper_cb_name_request\n");
        return;
     }

   if (flag != ELDBUS_NAME_REQUEST_REPLY_PRIMARY_OWNER)
     {
        ERR("error name already in use\n");
        return;
     }
}

static Eina_Bool
_e_test_helper_cb_visibility_change(void *data EINA_UNUSED,
                                    int type EINA_UNUSED,
                                    void *event)
{
   E_Client *ec;
   Ecore_Window win = 0;
   E_Event_Client *ev = event;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   win = e_client_util_win_get(ec);

   if (!th_data->target.win) return ECORE_CALLBACK_PASS_ON;
   if (win != th_data->target.win) return ECORE_CALLBACK_PASS_ON;

   th_data->target.ec = ec;

   if (th_data->target.vis != !ec->visibility.obscured)
     _e_test_helper_send_change_visibility(th_data->target.win, !ec->visibility.obscured);

   th_data->target.vis = !ec->visibility.obscured;

   if (th_data->target.disuse) _e_test_helper_target_clear();

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_test_helper_cb_client_remove(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Client *ec;
   Ecore_Window win = 0;
   E_Event_Client *ev = event;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   win = e_client_util_win_get(ec);

   if (!th_data->target.win) return ECORE_CALLBACK_PASS_ON;
   if (win != th_data->target.win) return ECORE_CALLBACK_PASS_ON;

   _e_test_helper_target_clear();

   return ECORE_CALLBACK_PASS_ON;
}

/* externally accessible functions */
EINTERN int
e_test_helper_init(void)
{
   eldbus_init();

   th_data = E_NEW(Test_Helper_Data, 1);

   th_data->conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   th_data->iface = eldbus_service_interface_register(th_data->conn,
                                                      PATH,
                                                      &iface_desc);
   eldbus_name_request(th_data->conn, BUS, ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE,
                       _e_test_helper_cb_name_request, th_data->iface);

   E_LIST_HANDLER_APPEND(th_data->hdlrs, E_EVENT_CLIENT_VISIBILITY_CHANGE,
                         _e_test_helper_cb_visibility_change, NULL);
   E_LIST_HANDLER_APPEND(th_data->hdlrs, E_EVENT_CLIENT_REMOVE,
                         _e_test_helper_cb_client_remove, NULL);

   th_data->target.vis = -1;

   return 1;
}

EINTERN int
e_test_helper_shutdown(void)
{
   if (th_data)
     {
        E_FREE_LIST(th_data->hdlrs, ecore_event_handler_del);

        eldbus_service_interface_unregister(th_data->iface);
        eldbus_connection_unref(th_data->conn);
        eldbus_shutdown();

        E_FREE(th_data);
     }

   return 1;
}
