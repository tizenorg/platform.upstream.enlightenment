#include "e.h"

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define INTERFACE "org.enlightenment.wm.Test"

static Ecore_Window tc_win = 0;
static E_Client *tc_ec = NULL;
static int tc_vis = -1;
static Eina_Bool deregistered = EINA_FALSE;

static Eldbus_Connection *_conn = NULL;
static Eldbus_Service_Interface *_iface;

static Eldbus_Message* _e_test_helper_cb_register_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message* _e_test_helper_cb_deregister_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message* _e_test_helper_cb_get_clients(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);

Eina_List *hdlrs = NULL;

enum
{
   E_TEST_HELPER_SIGNAL_NOTIFY_STACK = 0,
   E_TEST_HELPER_SIGNAL_CHANGE_VISIBILITY,
   E_TEST_HELPER_SIGNAL_ALLOW_DEREGISTER
};

static const Eldbus_Signal signals[] = {
   [E_TEST_HELPER_SIGNAL_NOTIFY_STACK] =
     {
        "NotifyStack",
        ELDBUS_ARGS({"ua(usiiiiibb)", "array of ec"}),
        0
     },
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
     INTERFACE, methods, signals
};

static void
_e_test_helper_send_notify_stack(void)
{
   Eldbus_Message *signal;
   Eldbus_Message_Iter *iter, *array_of_ec;
   E_Client *ec = NULL;
   Evas_Object *o = NULL;
   E_Comp *comp;

   comp = e_comp_get(NULL);
   if (!comp) return;

   signal = eldbus_service_signal_new(_iface, E_TEST_HELPER_SIGNAL_NOTIFY_STACK);
   iter = eldbus_message_iter_get(signal);
   eldbus_message_iter_arguments_append(iter, "ua(usiiiiibb)", tc_win, &array_of_ec);

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
   eldbus_service_signal_send(_iface, signal);
}

static void
_e_test_helper_send_change_visibility(Ecore_Window win, Eina_Bool vis)
{
   Eldbus_Message *signal;

   signal = eldbus_service_signal_new(_iface, E_TEST_HELPER_SIGNAL_CHANGE_VISIBILITY);
   eldbus_message_arguments_append(signal, "ub", win, vis);
   eldbus_service_signal_send(_iface, signal);
}

static Eldbus_Message *
_e_test_helper_cb_register_window(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                  const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window id;

   if (!eldbus_message_arguments_get(msg, "u", &id))
     {
        printf("Error on eldbus_message_arguments_get()\n");
        return reply;
     }
   printf("[%s:%d] register 0x%08x\n", __func__, __LINE__, id);

   eldbus_message_arguments_append(reply, "b", !tc_win);
   if (!tc_win) tc_win = id;

   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_deregister_window(const Eldbus_Service_Interface *iface EINA_UNUSED,
                           const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window id;

   if (!eldbus_message_arguments_get(msg, "u", &id))
     {
        printf("Error on eldbus_message_arguments_get()\n");
        return reply;
     }
   eldbus_message_arguments_append(reply, "b", tc_win == id && !tc_vis && tc_ec);

   if (tc_win != id) return reply;
   else deregistered = EINA_TRUE;

   if (tc_ec) evas_object_hide(tc_ec->frame);
   if (!tc_vis)
     {
        tc_win = 0;
        tc_vis = -1;
        tc_ec = NULL;
     }
   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_get_clients(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eldbus_Message_Iter *iter, *array_of_ec;
   E_Client *ec = NULL;
   Evas_Object *o = NULL;
   E_Comp *comp;

   comp = e_comp_get(NULL);
   if (!comp) return NULL;

   iter = eldbus_message_iter_get(reply);
   eldbus_message_iter_arguments_append(iter, "ua(usiiiiibb)", tc_win,  &array_of_ec);
   for (o = evas_object_top_get(comp->evas); o; o = evas_object_below_get(o))
     {
        Eldbus_Message_Iter* struct_of_ec;
        Ecore_Window win;
        ec = evas_object_data_get(o, "E_Client");

        if (!ec) continue;
        if (!e_util_strcmp(evas_object_name_get(o), "layer_obj")) continue;

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

   return reply;
}


static void
_e_test_helper_cb_name_request(void *data EINA_UNUSED, const Eldbus_Message *msg, Eldbus_Pending *pending EINA_UNUSED)
{
   unsigned int reply;
   const char *errname, *errmsg;

   if (eldbus_message_error_get(msg, &errname, &errmsg))
     {
        printf("error on _e_test_helper_cb_name_request %s %s\n", errname, errmsg);
        return;
     }

   if (!eldbus_message_arguments_get(msg, "u", &reply))
    {
       printf("error geting arguments on _e_test_helper_cb_name_request\n");
       return;
    }

   if (reply != ELDBUS_NAME_REQUEST_REPLY_PRIMARY_OWNER)
     {
        printf("error name already in use\n");
        return;
     }
}

static Eina_Bool
_e_test_helper_cb_visibility_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Client *ec;
   Ecore_Window win = 0;
   E_Event_Client *ev = event;

   ec = ev->ec;
   win = e_client_util_win_get(ec);
   if (win != tc_win) return ECORE_CALLBACK_PASS_ON;

   tc_ec = ec;
   
   if (!ec->visibility.obscured)
     _e_test_helper_send_notify_stack(); // will be replaced by just sending ChangeVisibility

   if ((tc_win) && (tc_vis != -1)) _e_test_helper_send_change_visibility(tc_win, !ec->visibility.obscured);
   tc_vis = !ec->visibility.obscured;

   if (deregistered)
     {
        tc_win = 0;
        tc_vis = -1;
        tc_ec = NULL;
        deregistered = EINA_FALSE;
     }
   return ECORE_CALLBACK_PASS_ON;
}

/* externally accessible functions */
EINTERN int
e_test_helper_init(void)
{
   eldbus_init();

   _conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   _iface = eldbus_service_interface_register(_conn, PATH, &iface_desc);
   eldbus_name_request(_conn, BUS, ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE, _e_test_helper_cb_name_request, _iface);

   E_LIST_HANDLER_APPEND(hdlrs, E_EVENT_CLIENT_VISIBILITY_CHANGE, _e_test_helper_cb_visibility_change, NULL);

   return 1;
}

EINTERN int
e_test_helper_shutdown(void)
{
   eldbus_service_interface_unregister(_iface);
   eldbus_connection_unref(_conn);
   eldbus_shutdown();

   return 1;
}
