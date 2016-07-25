#include "e_policy_wl_display.h"

typedef struct _E_Display_Dbus_Info
{
   Eldbus_Connection *conn;
} E_Display_Dbus_Info;

#define BUS_NAME "org.enlightenment.wm"

#define DEVICED_DEST "org.tizen.system.deviced"
#define DEVICED_PATH "/Org/Tizen/System/DeviceD/Display"
#define DEVICED_IFACE "org.tizen.system.deviced.display"
#define DEVICED_LOCK_STATE "lockstate"
#define DEVICED_UNLOCK_STATE "unlockstate"

#define DEVICED_LCDON "lcdon"
#define DEVICED_STAY_CUR_STATE "staycurstate"
#define DEVICED_SLEEP_MARGIN "sleepmargin"

/* static global variables */
static E_Display_Dbus_Info _e_display_dbus_info;
static Eina_List *_display_control_hooks = NULL;

/* for screen mode */
static Eina_List *_screen_mode_client_list = NULL;
static E_Display_Screen_Mode _e_display_screen_mode;

/* static functions */
static Eina_Bool _e_policy_display_dbus_init(void);
static void      _e_policy_display_dbus_shutdown(void);
static void      _e_policy_display_dbus_request_name_cb(void *data EINA_UNUSED, const Eldbus_Message *msg, Eldbus_Pending *pending EINA_UNUSED);

static Eina_Bool _e_policy_wl_display_client_add_to_list(Eina_List** list, E_Client *ec);
static Eina_Bool _e_policy_wl_display_client_remove_from_list(Eina_List** list, E_Client *ec);

static void _e_policy_wl_display_hook_client_del(void *d EINA_UNUSED, E_Client *ec);
static void _e_policy_wl_display_hook_client_visibility(void *d EINA_UNUSED, E_Client *ec);

/* for screen mode */
static Eina_Bool _e_policy_wl_display_screen_mode_find_visible_window(void);
static void      _e_policy_wl_display_screen_mode_send(E_Display_Screen_Mode mode);


static Eina_Bool
_e_policy_display_dbus_init(void)
{
   if (eldbus_init() == 0) return EINA_FALSE;

   _e_display_dbus_info.conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   EINA_SAFETY_ON_NULL_GOTO(_e_display_dbus_info.conn, failed);

   eldbus_name_request(_e_display_dbus_info.conn,
                       BUS_NAME,
                       ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE,
                       _e_policy_display_dbus_request_name_cb,
                       NULL);

   return EINA_TRUE;

failed:
   _e_policy_display_dbus_shutdown();
   return EINA_FALSE;
}

static void
_e_policy_display_dbus_shutdown(void)
{
   if (_e_display_dbus_info.conn)
     {
        eldbus_name_release(_e_display_dbus_info.conn, BUS_NAME, NULL, NULL);
        eldbus_connection_unref(_e_display_dbus_info.conn);
        _e_display_dbus_info.conn = NULL;
     }

   eldbus_shutdown();
}

static void
_e_policy_display_dbus_request_name_cb(void *data EINA_UNUSED, const Eldbus_Message *msg, Eldbus_Pending *pending EINA_UNUSED)
{
   unsigned int flag;

   if (eldbus_message_error_get(msg, NULL, NULL))
     {
        ERR("Could not request bus name");
        return;
     }

   if (!eldbus_message_arguments_get(msg, "u", &flag))
     {
        ERR("Could not get arguments on on_name_request");
        return;
     }

   if (!(flag & ELDBUS_NAME_REQUEST_REPLY_PRIMARY_OWNER))
     {
        WRN("Name already in use\n");
     }
}

static Eina_Bool
_e_policy_wl_display_client_add_to_list(Eina_List** list, E_Client *ec)
{
   if (!ec) return EINA_FALSE;

   if (eina_list_data_find(*list, ec) == ec)
     return EINA_TRUE;

   *list = eina_list_append(*list, ec);

   return EINA_TRUE;
}

static Eina_Bool
_e_policy_wl_display_client_remove_from_list(Eina_List** list, E_Client *ec)
{
   if (!ec) return EINA_FALSE;

   if (!eina_list_data_find(*list, ec))
     return EINA_FALSE;

   *list = eina_list_remove(*list, ec);

   return EINA_TRUE;
}

static void
_e_policy_wl_display_hook_client_del(void *d EINA_UNUSED, E_Client *ec)
{
   _e_policy_wl_display_client_remove_from_list(&_screen_mode_client_list, ec);
}

static void
_e_policy_wl_display_hook_client_visibility(void *d EINA_UNUSED, E_Client *ec)
{
   if (ec->visibility.changed)
     {
        e_policy_display_screen_mode_apply();
     }
}

static Eina_Bool
_e_policy_wl_display_screen_mode_find_visible_window(void)
{
   Eina_List *l = NULL;
   E_Client *ec = NULL;
   Eina_Bool find = EINA_FALSE;
   int ec_visibility;

   if (_screen_mode_client_list == NULL) return EINA_FALSE;

   EINA_LIST_FOREACH(_screen_mode_client_list, l, ec)
     {
        if (e_object_is_del(E_OBJECT(ec)))
          ec_visibility = E_VISIBILITY_FULLY_OBSCURED;
        else
          ec_visibility = ec->visibility.obscured;

        if ((ec_visibility == E_VISIBILITY_UNOBSCURED) ||
            (ec_visibility == E_VISIBILITY_PARTIALLY_OBSCURED))
          {
             find = EINA_TRUE;
             break;
          }
     }

   return find;
}

static void
_e_policy_wl_display_screen_mode_send(E_Display_Screen_Mode mode)
{
   Eldbus_Message *msg;
   Eina_Bool ret;
   unsigned int timeout = 0;

   if (!_e_display_dbus_info.conn) return;

   if (mode == E_DISPLAY_SCREEN_MODE_ALWAYS_ON)
     {
        msg = eldbus_message_method_call_new(DEVICED_DEST,
                                             DEVICED_PATH,
                                             DEVICED_IFACE,
                                             DEVICED_LOCK_STATE);
        if (!msg) return;

        ret = eldbus_message_arguments_append(msg, "sssi",
                                              DEVICED_LCDON,
                                              DEVICED_STAY_CUR_STATE,
                                              "",
                                              timeout);
     }
   else
     {
        msg = eldbus_message_method_call_new(DEVICED_DEST,
                                             DEVICED_PATH,
                                             DEVICED_IFACE,
                                             DEVICED_UNLOCK_STATE);
        if (!msg) return;

        ret = eldbus_message_arguments_append(msg, "ss",
                                              DEVICED_LCDON,
                                              DEVICED_SLEEP_MARGIN);
     }

   if (!ret)
     {
        if (msg)
          eldbus_message_unref(msg);

        return;
     }

   _e_display_screen_mode = mode;
   DBG("[SCREEN_MODE] Request screen mode:%d\n", mode);

   eldbus_connection_send(_e_display_dbus_info.conn, msg, NULL, NULL, -1);
}

#undef E_CLIENT_HOOK_APPEND
#define E_CLIENT_HOOK_APPEND(l, t, cb, d) \
  do                                      \
    {                                     \
       E_Client_Hook *_h;                 \
       _h = e_client_hook_add(t, cb, d);  \
       assert(_h);                        \
       l = eina_list_append(l, _h);       \
    }                                     \
  while (0)

Eina_Bool
e_policy_display_init(void)
{
   if (!_e_policy_display_dbus_init()) return EINA_FALSE;

   _e_display_screen_mode = E_DISPLAY_SCREEN_MODE_DEFAULT;

   /* hook functions */
   E_CLIENT_HOOK_APPEND(_display_control_hooks, E_CLIENT_HOOK_DEL, _e_policy_wl_display_hook_client_del, NULL);
   E_CLIENT_HOOK_APPEND(_display_control_hooks, E_CLIENT_HOOK_EVAL_VISIBILITY, _e_policy_wl_display_hook_client_visibility, NULL);

   return EINA_TRUE;
}

void
e_policy_display_shutdown(void)
{
   E_FREE_LIST(_display_control_hooks, e_client_hook_del);

   if (_screen_mode_client_list) eina_list_free(_screen_mode_client_list);

   _e_policy_display_dbus_shutdown();
}

void
e_policy_display_screen_mode_set(E_Client *ec, int mode)
{
   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (mode == 0)
     {
        _e_policy_wl_display_client_remove_from_list(&_screen_mode_client_list, ec);
        e_policy_display_screen_mode_apply();
     }
   else
     {
        _e_policy_wl_display_client_add_to_list(&_screen_mode_client_list, ec);
        e_policy_display_screen_mode_apply();
     }
}

void
e_policy_display_screen_mode_apply(void)
{
   /* check the _screen_mode_client_list and update the lcd locked status */
   if (_e_policy_wl_display_screen_mode_find_visible_window())
     {
        if (_e_display_screen_mode == E_DISPLAY_SCREEN_MODE_DEFAULT)
          _e_policy_wl_display_screen_mode_send(E_DISPLAY_SCREEN_MODE_ALWAYS_ON);
     }
   else
     {
        if (_e_display_screen_mode == E_DISPLAY_SCREEN_MODE_ALWAYS_ON)
          _e_policy_wl_display_screen_mode_send(E_DISPLAY_SCREEN_MODE_DEFAULT);
     }
}

