#include "e.h"
#include "e_info_shared_types.h"
#include <time.h>
#include <dirent.h>

typedef void (*E_Info_Message_Cb)(const Eldbus_Message *msg);

typedef struct _E_Info_Client
{
   /* eldbus */
   int                eldbus_init;
   Eldbus_Proxy      *proxy;
   Eldbus_Connection *conn;
   Eldbus_Object     *obj;

   /* topvwins */
   Eina_List         *win_list;
} E_Info_Client;

typedef struct _E_Win_Info
{
   Ecore_Window     id;         // native window id
   uint32_t      res_id;
   int           pid;
   const char  *name;       // name of client window
   int          x, y, w, h; // geometry
   int          layer;      // value of E_Layer
   int          vis;        // visibility
   int          alpha;      // alpha window
   int          visibility;
   int          iconic;
   int          focused;
   const char  *layer_name; // layer name
} E_Win_Info;

#define VALUE_TYPE_FOR_TOPVWINS "uuisiiiiibbibbs"

static E_Info_Client e_info_client;

static Eina_Bool _e_info_client_eldbus_message(const char *method, E_Info_Message_Cb cb);
static Eina_Bool _e_info_client_eldbus_message_with_args(const char *method, E_Info_Message_Cb cb, const char *signature, ...);

static E_Win_Info *
_e_win_info_new(Ecore_Window id, uint32_t res_id, int pid, Eina_Bool alpha, const char *name, int x, int y, int w, int h, int layer, int visible, int visibility, int iconic, int focused, const char *layer_name)
{
   E_Win_Info *win = NULL;

   win = E_NEW(E_Win_Info, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(win, NULL);

   win->id = id;
   win->res_id = res_id;
   win->pid = pid;
   win->name = eina_stringshare_add(name);
   win->x = x;
   win->y = y;
   win->w = w;
   win->h = h;
   win->layer = layer;
   win->alpha = alpha;
   win->vis = visible;
   win->visibility = visibility;
   win->iconic = iconic;
   win->focused = focused;
   win->layer_name = eina_stringshare_add(layer_name);

   return win;
}

static void
_e_win_info_free(E_Win_Info *win)
{
   EINA_SAFETY_ON_NULL_RETURN(win);

   if (win->name)
     eina_stringshare_del(win->name);

   if (win->layer_name)
     eina_stringshare_del(win->layer_name);

   E_FREE(win);
}

static void
_cb_window_info_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *ec;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a("VALUE_TYPE_FOR_TOPVWINS")", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        const char *win_name;
        const char *layer_name;
        int x, y, w, h, layer, visibility;
        Eina_Bool visible, alpha, iconic, focused;
        Ecore_Window id;
        uint32_t res_id;
        int pid;
        E_Win_Info *win = NULL;
        res = eldbus_message_iter_arguments_get(ec,
                                                VALUE_TYPE_FOR_TOPVWINS,
                                                &id,
                                                &res_id,
                                                &pid,
                                                &win_name,
                                                &x,
                                                &y,
                                                &w,
                                                &h,
                                                &layer,
                                                &visible,
                                                &alpha,
                                                &visibility,
                                                &iconic,
                                                &focused,
                                                &layer_name);
        if (!res)
          {
             printf("Failed to get win info\n");
             continue;
          }

        win = _e_win_info_new(id, res_id, pid, alpha, win_name, x, y, w, h, layer, visible, visibility, iconic, focused, layer_name);
        e_info_client.win_list = eina_list_append(e_info_client.win_list, win);
     }

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_topvwins_info(int argc, char **argv)
{
   E_Win_Info *win;
   Eina_List *l;
   int i = 0;
   int prev_layer = -1;
   const char *prev_layer_name = NULL;

   if (!_e_info_client_eldbus_message("get_window_info", _cb_window_info_get))
     return;

   printf("%d Top level windows\n", eina_list_count(e_info_client.win_list));
   printf("--------------------------------------[ topvwins ]----------------------------------------------------------\n");
   printf(" No   Win_ID    RcsID    PID     w     h     x      y     Focus Depth Visi Icon  Map_State    Title              \n");
   printf("------------------------------------------------------------------------------------------------------------\n");

   if (!e_info_client.win_list)
     {
        printf("no window\n");
        return;
     }

   EINA_LIST_FOREACH(e_info_client.win_list, l, win)
     {
        if (!win) return;
        i++;
        if (win->layer != prev_layer)
          {
             if (prev_layer != -1)
                printf("------------------------------------------------------------------------------------------------------------[%s]\n",
                       prev_layer_name ? prev_layer_name : " ");
             prev_layer = win->layer;
             prev_layer_name = win->layer_name;
          }
        printf("%3d 0x%08x  %5d  %5d  %5d %5d %6d %6d     %c  %5d   ", i, win->id, win->res_id, win->pid, win->w, win->h, win->x, win->y, win->focused ? 'O':' ', win->alpha? 32:24);
        printf("%2d    %d   %-11s  %s\n", win->visibility, win->iconic, win->vis? "Viewable":"NotViewable", win->name?:"No Name");
     }

   if (prev_layer_name)
      printf("------------------------------------------------------------------------------------------------------------[%s]\n",
             prev_layer_name ? prev_layer_name : " ");

   E_FREE_LIST(e_info_client.win_list, _e_win_info_free);
}

static char *
_directory_make(char *path)
{
   char dir[PATH_MAX], curdir[PATH_MAX], stamp[PATH_MAX];
   time_t timer;
   struct tm *t, *buf;
   char *fullpath;
   DIR *dp;

   timer = time(NULL);

   buf = calloc (1, sizeof (struct tm));
   EINA_SAFETY_ON_NULL_RETURN_VAL(buf, NULL);

   t = localtime_r(&timer, buf);
   if (!t)
     {
        free(buf);
        printf("fail to get local time\n");
        return NULL;
     }

   fullpath = (char*) calloc(1, PATH_MAX*sizeof(char));
   if (!fullpath)
     {
        free(buf);
        printf("fail to alloc pathname memory\n");
        return NULL;
     }

   if (path && path[0] == '/')
     snprintf(dir, PATH_MAX, "%s", path);
   else
     {
        char *temp = getcwd(curdir, PATH_MAX);
        if (!temp)
          {
             free(buf);
             free(fullpath);
             return NULL;
          }
        if (path)
          {
             if (strlen(curdir) == 1 && curdir[0] == '/')
               snprintf(dir, PATH_MAX, "/%s", path);
             else
               snprintf(dir, PATH_MAX, "%s/%s", curdir, path);
          }
        else
          snprintf(dir, PATH_MAX, "%s", curdir);
     }

   if (!(dp = opendir (dir)))
     {
        free(buf);
        free(fullpath);
        printf("not exist: %s\n", dir);
        return NULL;
     }
   else
      closedir (dp);

   /* make the folder for the result of xwd files */
   snprintf(stamp, PATH_MAX, "%04d%02d%02d.%02d%02d%02d", t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

   if (strlen(dir) == 1 && dir[0] == '/')
     snprintf(fullpath, PATH_MAX, "/topvwins-%s", stamp);
   else
     snprintf(fullpath, PATH_MAX, "%s/topvwins-%s", dir, stamp);

   free (buf);

   if ((mkdir(fullpath, 0755)) < 0)
     {
        printf("fail: mkdir '%s'\n", fullpath);
        free(fullpath);
        return NULL;
     }

   printf("directory: %s\n", fullpath);

   return fullpath;
}

static void
_e_info_client_proc_topvwins_shot(int argc, char **argv)
{
   char *directory = _directory_make(argv[2]);
   EINA_SAFETY_ON_NULL_RETURN(directory);

   if (!_e_info_client_eldbus_message_with_args("dump_topvwins", NULL, "s", directory))
     {
        free(directory);
        return;
     }

   free(directory);
}

static void
_e_info_client_proc_eina_log_levels(int argc, char **argv)
{
   EINA_SAFETY_ON_FALSE_RETURN(argc == 3);
   EINA_SAFETY_ON_NULL_RETURN(argv[2]);

   if (!_e_info_client_eldbus_message_with_args("eina_log_levels", NULL, "s", argv[2]))
     {
        return;
     }
}

static void
_e_info_client_proc_eina_log_path(int argc, char **argv)
{
   char fd_name[PATH_MAX];
   int pid;
   char cwd[PATH_MAX];

   EINA_SAFETY_ON_FALSE_RETURN(argc == 3);
   EINA_SAFETY_ON_NULL_RETURN(argv[2]);

   pid = getpid();

   cwd[0] = '\0';
   if (!getcwd(cwd, sizeof(cwd)))
     snprintf(cwd, sizeof(cwd), "/tmp");

   if (!strncmp(argv[2], "console", 7))
     snprintf(fd_name, PATH_MAX, "/proc/%d/fd/1", pid);
   else
     {
        if (argv[2][0] == '/')
          snprintf(fd_name, PATH_MAX, "%s", argv[2]);
        else
          {
             if (strlen(cwd) > 0)
               snprintf(fd_name, PATH_MAX, "%s/%s", cwd, argv[2]);
             else
               snprintf(fd_name, PATH_MAX, "%s", argv[2]);
          }
     }

   printf("eina-log-path: %s\n", fd_name);

   if (!_e_info_client_eldbus_message_with_args("eina_log_path", NULL, "s", fd_name))
     {
        return;
     }
}

static void
_cb_window_prop_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *ec;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a(ss)", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   printf("--------------------------------------[ window prop ]-----------------------------------------------------\n");
   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        const char *title;
        const char *value;
        res = eldbus_message_iter_arguments_get(ec,
                                                "ss",
                                                &title,
                                                &value);
        if (!res)
          {
             printf("Failed to get win prop info\n");
             continue;
          }

        if (!strcmp(title, "[WINDOW PROP]"))
           printf("---------------------------------------------------------------------------------------------------------\n");
        else
           printf("%20s : %s\n", title, value);
     }
   printf("----------------------------------------------------------------------------------------------------------\n");

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_prop_prop_info(int argc, char **argv)
{
   const static int WINDOW_ID_MODE = 0;
   const static int WINDOW_PID_MODE = 1;
   const static int WINDOW_NAME_MODE = 2;
   const char *value;
   uint32_t mode = 0;

   if (argc < 3 || argv[2] == NULL)
     {
        printf("Error Check Args: enlightenment_info -prop [windowID]\n"
               "                  enlightenment_info -prop -id [windowID]\n"
               "                  enlightenment_info -prop -pid [PID]\n"
               "                  enlightenment_info -prop -name [name]\n");
        return;
     }

   if (strlen(argv[2]) > 2 && argv[2][0] == '-')
     {
        if (!strcmp(argv[2], "-id")) mode = WINDOW_ID_MODE;
        if (!strcmp(argv[2], "-pid")) mode = WINDOW_PID_MODE;
        if (!strcmp(argv[2], "-name")) mode = WINDOW_NAME_MODE;
        value = (argc >= 4 ? argv[3] : NULL);
     }
   else
     {
        mode = WINDOW_ID_MODE;
        value = argv[2];
     }

   if (!_e_info_client_eldbus_message_with_args("get_window_prop", _cb_window_prop_get, "us", mode, value))
     {
        printf("_e_info_client_eldbus_message_with_args error");
        return;
     }
}

static void
_cb_window_proc_connected_clients_get(const Eldbus_Message *msg)
{
   const char *name = NULL, *text = NULL;
   Eldbus_Message_Iter *array, *ec;
   Eina_Bool res;

   res = eldbus_message_error_get(msg, &name, &text);
   EINA_SAFETY_ON_TRUE_GOTO(res, finish);

   res = eldbus_message_arguments_get(msg, "a(ss)", &array);
   EINA_SAFETY_ON_FALSE_GOTO(res, finish);

   printf("--------------------------------------[ connected clients ]-----------------------------------------------------\n");
   int cnt = 0;
   while (eldbus_message_iter_get_and_next(array, 'r', &ec))
     {
        const char *title;
        const char *value;
        res = eldbus_message_iter_arguments_get(ec,
                                                "ss",
                                                &title,
                                                &value);
        if (!res)
          {
             printf("Failed to get connected clients info\n");
             continue;
          }

        if (!strcmp(title, "[Connected Clients]"))
          {
             printf("\n[%2d] %s\n", ++cnt, value);
          }
        else if (!strcmp(title, "[E_Client Info]"))
          {
             printf("      |----- %s :: %s\n", title, value);
          }
     }

finish:
   if ((name) || (text))
     {
        printf("errname:%s errmsg:%s\n", name, text);
     }
}

static void
_e_info_client_proc_connected_clients(int argc, char **argv)
{
   if (!_e_info_client_eldbus_message("get_connected_clients", _cb_window_proc_connected_clients_get))
     {
        printf("_e_info_client_eldbus_message error");
        return;
     }
}

#define ROTATION_USAGE \
   "[COMMAND] [ARG]...\n" \
   "\tset     : Set the orientation of zone (Usage: set [zone-no] [rval(0|90|180|270)]\n" \
   "\tinfo    : Get the information of zone's rotation (Usage: info [zone-no]) (Not Implemented)\n" \
   "\tenable  : Enable the rotation of zone (Usage: enable [zone-no] (Not Implemented)\n" \
   "\tdisable : Disable the rotation of zone (Usage: disable [zone-no] (Not Implemented)\n"

static void
_cb_rotation_query(const Eldbus_Message *msg)
{
   (void)msg;
   /* TODO: need implementation */
}

static void
_e_info_client_proc_rotation(int argc, char **argv)
{
   E_Info_Rotation_Message req;
   int32_t zone_num = -1;
   int32_t rval = -1;
   const int off_len = 2, cmd_len = 1;
   Eina_Bool res = EINA_FALSE;

   if (argc < off_len + cmd_len)
     goto arg_err;

   if (eina_streq(argv[off_len], "info"))
     {
        if (argc > off_len + cmd_len)
          zone_num = atoi(argv[off_len + 1]);

        res = _e_info_client_eldbus_message_with_args("rotation_query",
                                                      _cb_rotation_query,
                                                      "i", zone_num);
     }
   else
     {
        if (eina_streq(argv[off_len], "set"))
          {
             if (argc < off_len + cmd_len + 1)
               goto arg_err;
             else if (argc > off_len + cmd_len + 1)
               {
                  zone_num = atoi(argv[off_len + 1]);
                  rval = atoi(argv[off_len + 2]);
               }
             else
               rval = atoi(argv[off_len + 1]);

             if ((rval < 0) || (rval > 270) || (rval % 90 != 0))
               goto arg_err;

             req = E_INFO_ROTATION_MESSAGE_SET;
          }
        else
          {
             if (argc > off_len + cmd_len)
               zone_num = atoi(argv[off_len + 1]);

             if (eina_streq(argv[off_len], "enable"))
               req = E_INFO_ROTATION_MESSAGE_ENABLE;
             else if (eina_streq(argv[off_len], "disable"))
               req = E_INFO_ROTATION_MESSAGE_DISABLE;
             else
               goto arg_err;
          }

        res = _e_info_client_eldbus_message_with_args("rotation_message",
                                                      NULL, "iii",
                                                      req, zone_num, rval);
     }

   if (!res)
     printf("_e_info_client_eldbus_message_with_args error");

   return;
arg_err:
   printf("Usage: enlightenment_info -rotation %s", ROTATION_USAGE);
}

static struct
{
   const char *option;
   const char *params;
   const char *description;
   void (*func)(int argc, char **argv);
} procs[] =
{
   {
      "topvwins", NULL,
      "Print top visible windows",
      _e_info_client_proc_topvwins_info
   },
   {
      "dump_topvwins", "[directory_path]",
      "Dump top-level visible windows (default directory_path : current working directory)",
      _e_info_client_proc_topvwins_shot
   },
   {
      "eina_log_levels", "[mymodule1:5,mymodule2:2]",
      "Set EINA_LOG_LEVELS in runtime",
      _e_info_client_proc_eina_log_levels
   },
   {
      "eina_log_path", "[console|file_path]",
      "Set eina-log path in runtime",
      _e_info_client_proc_eina_log_path
   },
   {
      "prop", "[id]",
      "print window infomation",
      _e_info_client_prop_prop_info
   },
   {
      "connected_clients", NULL,
      "print connected clients on Enlightenment",
      _e_info_client_proc_connected_clients
   },
   {
      "rotation",
      ROTATION_USAGE,
      "Send a message about rotation",
      _e_info_client_proc_rotation
   },
};

static void
_e_info_client_eldbus_message_cb(void *data, const Eldbus_Message *msg, Eldbus_Pending *p EINA_UNUSED)
{
   E_Info_Message_Cb cb = (E_Info_Message_Cb)data;

   if (cb) cb(msg);

   ecore_main_loop_quit();
}

static Eina_Bool
_e_info_client_eldbus_message(const char *method, E_Info_Message_Cb cb)
{
   Eldbus_Pending *p;

   p = eldbus_proxy_call(e_info_client.proxy, method,
                         _e_info_client_eldbus_message_cb,
                         cb, -1, "");
   EINA_SAFETY_ON_NULL_RETURN_VAL(p, EINA_FALSE);

   ecore_main_loop_begin();
   return EINA_TRUE;
}

static Eina_Bool
_e_info_client_eldbus_message_with_args(const char *method, E_Info_Message_Cb cb, const char *signature, ...)
{
   Eldbus_Pending *p;
   va_list ap;

   va_start(ap, signature);
   p = eldbus_proxy_vcall(e_info_client.proxy, method,
                          _e_info_client_eldbus_message_cb,
                          cb, -1, signature, ap);
   va_end(ap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(p, EINA_FALSE);

   ecore_main_loop_begin();
   return EINA_TRUE;
}

static void
_e_info_client_eldbus_disconnect(void)
{
   if (e_info_client.proxy)
     {
        eldbus_proxy_unref(e_info_client.proxy);
        e_info_client.proxy = NULL;
     }

   if (e_info_client.obj)
     {
        eldbus_object_unref(e_info_client.obj);
        e_info_client.obj = NULL;
     }

   if (e_info_client.conn)
     {
        eldbus_connection_unref(e_info_client.conn);
        e_info_client.conn = NULL;
     }

   if (e_info_client.eldbus_init)
     {
        eldbus_shutdown();
        e_info_client.eldbus_init = 0;
     }
}

static Eina_Bool
_e_info_client_eldbus_connect(void)
{
   e_info_client.eldbus_init = eldbus_init();
   EINA_SAFETY_ON_FALSE_GOTO(e_info_client.eldbus_init > 0, err);

   e_info_client.conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   EINA_SAFETY_ON_NULL_GOTO(e_info_client.conn, err);

   e_info_client.obj = eldbus_object_get(e_info_client.conn,
                                         "org.enlightenment.wm",
                                         "/org/enlightenment/wm");
   EINA_SAFETY_ON_NULL_GOTO(e_info_client.obj, err);

   e_info_client.proxy = eldbus_proxy_get(e_info_client.obj, "org.enlightenment.wm.info");
   EINA_SAFETY_ON_NULL_GOTO(e_info_client.proxy, err);

   return EINA_TRUE;

err:
   _e_info_client_eldbus_disconnect();
   return EINA_FALSE;
}

static Eina_Bool
_e_info_client_process(int argc, char **argv)
{
   int nproc = sizeof(procs) / sizeof(procs[0]);
   int i;

   for (i = 0; i < nproc; i++)
     {
        if (!strncmp(argv[1]+1, procs[i].option, strlen(procs[i].option)))
          {
             if (procs[i].func)
               procs[i].func(argc, argv);

             return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static void
_e_info_client_print_usage(const char *exec)
{
   int nproc = sizeof(procs) / sizeof(procs[0]);
   int i;

   printf("\nUsage:\n");

   for (i = 0; i < nproc; i++)
     printf("  %s -%s %s\n", exec, procs[i].option, (procs[i].params)?procs[i].params:"");

   printf("\nOptions:\n");

   for (i = 0; i < nproc; i++)
     {
        printf("  -%s\n", procs[i].option);
        printf("      %s\n", (procs[i].description)?procs[i].description:"");
     }

   printf("\n");
}

int
main(int argc, char **argv)
{
   if (argc < 2 || argv[1][0] != '-')
     {
        _e_info_client_print_usage(argv[0]);
        return 0;
     }

   /* connecting dbus */
   if (!_e_info_client_eldbus_connect())
     goto err;

   if (!strcmp(argv[1], "-h") ||
       !strcmp(argv[1], "-help") ||
       !strcmp(argv[1], "--help"))
     {
        _e_info_client_print_usage(argv[0]);
     }
   else
     {
        /* handling a client request */
        if (!_e_info_client_process(argc, argv))
          {
             printf("unknown option: %s\n", argv[1]);
             _e_info_client_print_usage(argv[0]);
          }
     }

   /* disconnecting dbus */
   _e_info_client_eldbus_disconnect();

   return 0;

err:
   _e_info_client_eldbus_disconnect();
   return -1;
}
