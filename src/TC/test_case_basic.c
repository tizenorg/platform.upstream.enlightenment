#include "e.h"
#include "e_tc_main.h"
#include "test_case_basic.h"

/* Basic info of this test window */
E_TC_Basic_Info *_basic_info = NULL;

static E_Test_Case *_test_case = NULL;

static void test(void);

/* Method callbacks */
static void
_method_cb_register_window(void *data EINA_UNUSED, const Eldbus_Message *msg, Eldbus_Pending *pending EINA_UNUSED)
{
   Eina_Bool accepted = EINA_FALSE;
   const char *errname, *errmsg;

   if (eldbus_message_error_get(msg, &errname, &errmsg))
     {
        printf("%s %s\n", errname, errmsg);
        elm_exit();
        return;
     }

   if (!eldbus_message_arguments_get(msg, "b", &accepted))
     {
        printf("Error on eldbus_message_arguments_get()\n");
        elm_exit();
        return;
     }

   if (!accepted)
     {
        if ((_basic_info) && (_basic_info->client))
          printf("WM rejected the request for register!!(0x%08x) \n", _basic_info->client->win);
        elm_exit();
     }
   else if ((_basic_info) && (_basic_info->elm_win))
     {
        evas_object_resize(_basic_info->elm_win, 320, 320);
        evas_object_show(_basic_info->elm_win);
     }
}

static void
_method_cb_deregister_window(void *data EINA_UNUSED, const Eldbus_Message *msg, Eldbus_Pending *pending EINA_UNUSED)
{
   const char *errname, *errmsg;
   Eina_Bool allowed = EINA_TRUE;

   if (eldbus_message_error_get(msg, &errname, &errmsg))
     {
        printf("%s %s\n", errname, errmsg);
        return;
     }

   if (!eldbus_message_arguments_get(msg, "b", &allowed))
     {
        printf("Error on eldbus_message_arguments_get()\n");
        return;
     }

   if (!allowed)
     {
        _basic_info->wait_close = EINA_TRUE;
        return;
     }

   evas_object_hide(_basic_info->elm_win);
   evas_object_del(_basic_info->elm_win);
   elm_exit();
}

/* Signal Callbacks */
static void
_signal_cb_notify_stack(void *data EINA_UNUSED, const Eldbus_Message *msg)
{
   const char *errname, *errmsg;
   Eldbus_Message_Iter *array_of_ec, *struct_of_ec;
   Ecore_Window tc_win = 0;

   if (eldbus_message_error_get(msg, &errname, &errmsg))
     {
        ERR("%s %s", errname, errmsg);
        return;
     }

   if (!eldbus_message_arguments_get(msg, "ua(usiiiiibb)", &tc_win, &array_of_ec))
     {
        printf("Error on eldbus_message_arguments_get()\n");
        return;
     }

   while (eldbus_message_iter_get_and_next(array_of_ec, 'r', &struct_of_ec))
     {
        const char  *name;
        int x, y, w, h, layer;
        Eina_Bool visible, argb;
        Ecore_Window win;
        E_TC_Client *client = NULL;

        if (!eldbus_message_iter_arguments_get(struct_of_ec, "usiiiiibb", &win, &name, &x, &y, &w, &h, &layer, &visible, &argb))
          {
             printf("Error on eldbus_message_arguments_get()\n");
             continue;
          }

        client = E_NEW(E_TC_Client, 1);
        client->x = x;
        client->y = y;
        client->w = w;
        client->h = h;
        client->layer = layer;
        client->win = win;
        client->name = eina_stringshare_add(name);
        _basic_info->clients = eina_list_append(_basic_info->clients, client);
     }

   test();
}

static void
_signal_cb_change_visibility(void *data EINA_UNUSED, const Eldbus_Message *msg)
{
   const char *errname, *errmsg;
   Eina_Bool vis;
   Ecore_Window id;

   if (eldbus_message_error_get(msg, &errname, &errmsg))
     {
        ERR("%s %s", errname, errmsg);
        return;
     }

   if (!eldbus_message_arguments_get(msg, "ub", &id, &vis))
     {
        printf("Error on eldbus_message_arguments_get()\n");
        return;
     }

   if (_basic_info->client->win != id) return;
   if (_basic_info->wait_close)
     {
        _basic_info->wait_close = EINA_FALSE;
        evas_object_hide(_basic_info->elm_win);
        evas_object_del(_basic_info->elm_win);
        elm_exit();
     }
}

/* Main test */
static void
test(void)
{
   Eina_List *l;
   E_Test_Case *_tc;
   Eina_Bool pass;

   pass = !!_test_case->inner_tcs;

   EINA_LIST_FOREACH(_test_case->inner_tcs, l, _tc)
     {
        _tc->passed = _tc->test(NULL);
        printf("TEST \"%s\" : %s\n", _tc->name, _tc->passed?"PASS":"FAIL");
        pass = pass && _tc->passed;
     }
   _test_case->passed = pass;

   //request for quitting this test
   eldbus_proxy_call(dbus_proxy, "DeregisterWindow", _method_cb_deregister_window, NULL, -1, "u", _basic_info->client->win);
}

/* Window setup and request for register of the window */
Eina_Bool
test_case_basic(E_Test_Case *tc)
{
   Evas_Object *bg, *win;
   E_TC_Client client = { "test_case_basic", // name
                           0, 0, 320, 320, // geometry(x,y,w,h)
                           200, -1, -1, 0 // layer, visible, arg, win 
   };

   _test_case = tc;
   _basic_info = E_NEW(E_TC_Basic_Info, 1);

   win= elm_win_add(NULL, client.name, ELM_WIN_BASIC);
   client.win = elm_win_xwindow_get(win);

   eldbus_proxy_call(dbus_proxy,
                     "RegisterWindow", _method_cb_register_window, NULL, -1, "u", client.win);

   _basic_info->sig_hdlrs =
      eina_list_append(_basic_info->sig_hdlrs,
                       eldbus_proxy_signal_handler_add(dbus_proxy,
                                                       "ChangeVisibility",
                                                       _signal_cb_change_visibility,
                                                       NULL));
   _basic_info->sig_hdlrs =
      eina_list_append(_basic_info->sig_hdlrs,
                       eldbus_proxy_signal_handler_add(dbus_proxy,
                                                       "NotifyStack",
                                                       _signal_cb_notify_stack,
                                                       NULL));

   elm_win_title_set(win, client.name);
   elm_win_autodel_set(win, EINA_TRUE);

   bg = elm_bg_add(win);
   evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_win_resize_object_add(win, bg);
   elm_bg_color_set(bg, 0, 0, 255);
   evas_object_show(bg);

   _basic_info->elm_win = win;
   _basic_info->client = E_NEW(E_TC_Client, 1);
   memcpy(_basic_info->client, &client, sizeof(E_TC_Client));

   elm_run();

test_shutdown:
   if (_basic_info)
     {
        Eldbus_Signal_Handler *_sh;
        E_TC_Client *_client;

        EINA_LIST_FREE(_basic_info->clients, _client)
          {
             eina_stringshare_del(_client->name);
             E_FREE(_client);
             _client = NULL;
          }

        EINA_LIST_FREE(_basic_info->sig_hdlrs, _sh)
           eldbus_signal_handler_del(_sh);

        if (_basic_info->client) E_FREE(_basic_info->client);
     }

   return _test_case->passed;
}
