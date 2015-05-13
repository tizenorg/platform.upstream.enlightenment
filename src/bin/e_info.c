#include <Eina.h>
#include <Ecore.h>
#include <Elementary.h>
#include <Eldbus.h>
#include <inttypes.h>

#ifdef E_NEW
# undef E_NEW
#endif
# define E_NEW(s, n) (s *)calloc(n, sizeof(s))

#ifdef E_FREE
# undef E_FREE
#endif
# define E_FREE(p) do { free(p); p = NULL; } while (0)
#ifdef E_FREE_LIST
# undef E_FREE_LIST
#endif
# define E_FREE_LIST(list, free)   \
  do                               \
    {                              \
       void *_tmp_;                \
       EINA_LIST_FREE(list, _tmp_) \
         {                         \
            free(_tmp_);           \
         }                         \
    }                              \
  while (0)

typedef struct _E_Test_Info E_Test_Info;
typedef struct _E_Win_Info E_Win_Info;

struct _E_Test_Info
{
   Eina_List    *win_list;
   Eldbus_Proxy *proxy;
};

struct _E_Win_Info
{
   uint64_t     id;         // native window id
   const char  *name;       // name of client window
   int          x, y, w, h; // geometry
   int          layer;      // value of E_Layer
   int          vis;        // visibility
   int          alpha;      // alpha window
};

E_Win_Info *
e_win_info_add(Ecore_Window id,
               Eina_Bool alpha,
               const char *name,
               int x, int y,
               int w, int h,
               int layer,
               int visible)
{
   E_Win_Info *win = NULL;

   win = E_NEW(E_Win_Info, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(win, NULL);

   win->id = id;
   win->name = eina_stringshare_add(name);
   win->x = x;
   win->y = y;
   win->w = w;
   win->h = h;
   win->layer = layer;
   win->alpha = alpha;
   win->vis = visible;

   return win;
}

void
e_win_info_del(E_Win_Info *win)
{
   EINA_SAFETY_ON_NULL_RETURN(win);

   if (win->name)
     eina_stringshare_del(win->name);

   E_FREE(win);
}

static void
_cb_method_get_clients(void *data,
                       const Eldbus_Message *msg,
                       Eldbus_Pending *p EINA_UNUSED)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *ec;
   uint64_t target_win = 0;
   E_Test_Info *test_info = data;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "ua(usiiiiibb)", &target_win, &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        const char *win_name;
        int x, y, w, h, layer;
        Eina_Bool visible, alpha;
        Ecore_Window id;
        E_Win_Info *win = NULL;
        res = eldbus_message_iter_arguments_get(ec,
                                                "usiiiiibb",
                                                &id,
                                                &win_name,
                                                &x,
                                                &y,
                                                &w,
                                                &h,
                                                &layer,
                                                &visible,
                                                &alpha);
        if (!res)
          {
             printf("Failed to get win info\n");
             continue;
          }

        win = e_win_info_add(id, alpha, win_name, x, y, w, h, layer, visible);
        test_info->win_list = eina_list_append(test_info->win_list, win);
     }
   data = test_info;

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }

   elm_exit();
}

Eina_List *
e_win_info_list_get(E_Test_Info *test_info)
{
   Eldbus_Pending *p;
   p = eldbus_proxy_call(test_info->proxy,
                         "GetClients",
                         _cb_method_get_clients,
                         test_info,
                         -1,
                         "");
   EINA_SAFETY_ON_NULL_RETURN_VAL(p, NULL);

   elm_run();

   return test_info->win_list;
}

static void
_print_usage()
{
   printf("Options:\n"
        "\t-topvwins\n"
        "\t\tPrint top visible windows\n");
   exit(0);
}

static void
_print_stack_info(E_Test_Info *test_info)
{
   E_Win_Info *win;
   Eina_List *list = NULL, *l;

   EINA_SAFETY_ON_NULL_RETURN(test_info);

   list = e_win_info_list_get(test_info);
   EINA_SAFETY_ON_NULL_GOTO(list, cleanup);

   printf("%d Top level windows\n", eina_list_count(list));
   printf("-------------------------[ topvwins ]------------------------------\n");
   printf("No   PID     w     h     x     y   Depth         Title    map state\n");
   printf("-------------------------------------------------------------------\n");
   int i=0;
   EINA_LIST_FOREACH(list, l, win)
     {
        if (!win) return;
        i++;
        printf("%3d %"PRIu64" %5d %5d %5d %5d %5d ", i, win->id, win->w, win->h, win->x, win->y, win->alpha? 32:24);
        printf("%15s %11s\n", win->name?:"No Name", win->vis? "Viewable":"NotViewable");
     }
cleanup:
   E_FREE_LIST(list, e_win_info_del);
}

int
main(int argc, char **argv)
{
   int res;
   E_Test_Info * test_info;
   Eldbus_Connection *conn;
   Eldbus_Object     *obj;

   test_info = E_NEW(E_Test_Info, 1);
   EINA_SAFETY_ON_NULL_GOTO(test_info, err);

   res = eldbus_init();
   EINA_SAFETY_ON_FALSE_GOTO((res > 0), err);

   conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   if (!conn)
     {
        printf("Can't get dbus connection.");
        goto err;
     }

   obj = eldbus_object_get(conn,
                           "org.enlightenment.wm",
                           "/org/enlightenment/wm");
   if (!obj)
     {
        printf("Can't get dbus object.");
        goto err;
     }

   test_info->proxy = eldbus_proxy_get(obj, "org.enlightenment.wm.Test");
   if (!test_info->proxy)
     {
        printf("Can't get dbus proxy.");
        goto err;
     }

   if (argv[1] && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "-help") || !strcmp(argv[1], "--help")))
     _print_usage();
   else if (!strcmp(argv[1], "-topvwins"))
     _print_stack_info(test_info);

   eldbus_proxy_unref(test_info->proxy);
   eldbus_object_unref(obj);
   eldbus_connection_unref(conn);
   eldbus_shutdown();

   E_FREE(test_info);
   return 0;

err:
   E_FREE(test_info);

   return -1;
}
