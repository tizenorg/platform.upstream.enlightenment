#include "e.h"
#include <tbm_bufmgr.h>

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define IFACE "org.enlightenment.wm.info"

typedef struct _E_Info_Server
{
   Eldbus_Connection *conn;
   Eldbus_Service_Interface *iface;
} E_Info_Server;

static E_Info_Server e_info_server;

struct wl_drm;

struct wl_drm_buffer
{
   struct wl_resource *resource;
   struct wl_drm *drm;
   int32_t width, height;
   uint32_t format;
   const void *driver_format;
   int32_t offset[3];
   int32_t stride[3];
   void *driver_buffer;
};

static void
_msg_clients_append(Eldbus_Message_Iter *iter)
{
   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;
   Evas_Object *o;

   eldbus_message_iter_arguments_append(iter, "a(uuisiiiiibbs)", &array_of_ec);

   // append clients.
   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        Eldbus_Message_Iter* struct_of_ec;
        Ecore_Window win;
        uint32_t res_id = 0;
        pid_t pid = -1;
        char layer_name[32];

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);
        e_comp_layer_name_get(ec->layer, layer_name, sizeof(layer_name));

        if (ec->pixmap)
          res_id = e_pixmap_res_id_get(ec->pixmap);
#ifdef HAVE_WAYLAND_ONLY
        if (ec->comp_data)
          {
             E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
             if (cdata->surface)
               wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
          }
#endif
        eldbus_message_iter_arguments_append(array_of_ec, "(uuisiiiiibbs)", &struct_of_ec);

        eldbus_message_iter_arguments_append
           (struct_of_ec, "uuisiiiiibbs",
            win,
            res_id,
            pid,
            e_client_util_name_get(ec) ?: "NO NAME",
            ec->x, ec->y, ec->w, ec->h, ec->layer,
            ec->visible, ec->argb, layer_name);

        eldbus_message_iter_container_close(array_of_ec, struct_of_ec);
     }

   eldbus_message_iter_container_close(iter, array_of_ec);
}

/* Method Handlers */
static Eldbus_Message *
_e_info_server_cb_window_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   _msg_clients_append(eldbus_message_iter_get(reply));

   return reply;
}

static void
_msg_connected_clients_append(Eldbus_Message_Iter *iter)
{
   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;
   Evas_Object *o;

   eldbus_message_iter_arguments_append(iter, "a(ss)", &array_of_ec);

   Eina_List *l;
   E_Comp_Connected_Client_Info *cinfo;


   Eldbus_Message_Iter* struct_of_ec;

#define __CONNECTED_CLIENTS_ARG_APPEND_TYPE(title, str, x...) ({                           \
                                                               char __temp[128] = {0,};                                                     \
                                                               snprintf(__temp, sizeof(__temp), str, ##x);                                  \
                                                               eldbus_message_iter_arguments_append(array_of_ec, "(ss)", &struct_of_ec);    \
                                                               eldbus_message_iter_arguments_append(struct_of_ec, "ss", (title), (__temp)); \
                                                               eldbus_message_iter_container_close(array_of_ec, struct_of_ec);})

   EINA_LIST_FOREACH(e_comp->connected_clients, l, cinfo)
     {
        __CONNECTED_CLIENTS_ARG_APPEND_TYPE("[Connected Clients]", "name:%20s pid:%3d uid:%3d gid:%3d", cinfo->name ?: "NO_NAME", cinfo->pid, cinfo->uid, cinfo->gid);
        for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
          {
             Ecore_Window win;
             uint32_t res_id = 0;
             pid_t pid = -1;

             ec = evas_object_data_get(o, "E_Client");
             if (!ec) continue;
             if (e_client_util_ignored_get(ec)) continue;

             win = e_client_util_win_get(ec);

             if (ec->pixmap)
               res_id = e_pixmap_res_id_get(ec->pixmap);
#ifdef HAVE_WAYLAND_ONLY
             if (ec->comp_data)
               {
                  E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
                  if (cdata->surface)
                    wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
               }
#endif
             if (cinfo->pid == pid)
               {
                  __CONNECTED_CLIENTS_ARG_APPEND_TYPE("[E_Client Info]", "win:0x%08x res_id:%5d, name:%20s, geo:(%4d, %4d, %4dx%4d), layer:%5d, visible:%d, argb:%d",
                                                      win, res_id, e_client_util_name_get(ec) ?: "NO_NAME", ec->x, ec->y, ec->w, ec->h, ec->layer, ec->visible, ec->argb);
               }
          }
     }

   eldbus_message_iter_container_close(iter, array_of_ec);
}

static Eldbus_Message *
_e_info_server_cb_connected_clients_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   _msg_connected_clients_append(eldbus_message_iter_get(reply));

   return reply;
}

static void
_msg_window_prop_client_append(Eldbus_Message_Iter *iter, E_Client *target_ec)
{
   Eldbus_Message_Iter* struct_of_ec;
   pid_t pid = -1;
   char win_resid[16] = {0,};
   char char_True[] = "TRUE";
   char char_False[] = "FALSE";
   char layer_name[48] = {0,};
   char layer[64] = {0,};
   char transients[128] = {0,};
   char shape_rects[128] = {0,};
   char shape_input[128] = {0,};

   if (!target_ec) return;

   if (target_ec->pixmap)
      snprintf(win_resid, sizeof(win_resid), "%d", e_pixmap_res_id_get(target_ec->pixmap));

   e_comp_layer_name_get(target_ec->layer, layer_name, sizeof(layer_name));
   snprintf(layer, sizeof(layer), "[%d, %s]",  target_ec->layer, layer_name);

   if (target_ec->transients)
     {
        E_Client *child;
        const Eina_List *l;

        EINA_LIST_FOREACH(target_ec->transients, l, child)
          {
             char temp[16];
             snprintf(temp, sizeof(temp), "0x%x", e_client_util_win_get(child));
             strncat( transients, temp, sizeof(transients) - strlen(transients));
          }
     }

   if (target_ec->shape_rects && target_ec->shape_rects_num > 0)
     {
        int i = 0;
        for (i = 0 ; i < target_ec->shape_rects_num ; ++i)
          {
             char temp[32];
             snprintf(temp, sizeof(temp), "[%d,%d,%d,%d] ", target_ec->shape_rects[i].x, target_ec->shape_rects[i].y,
                      target_ec->shape_rects[i].w, target_ec->shape_rects[i].h);
             strncat( shape_rects, temp, sizeof(shape_rects) - strlen(shape_rects));
          }
     }

   if (target_ec->shape_input_rects && target_ec->shape_input_rects_num > 0)
     {
        int i = 0;
        for (i = 0 ; i < target_ec->shape_input_rects_num ; ++i)
          {
             char temp[32];
             snprintf(temp, sizeof(temp), "[%d,%d,%d,%d] ", target_ec->shape_input_rects[i].x, target_ec->shape_input_rects[i].y,
                      target_ec->shape_input_rects[i].w, target_ec->shape_input_rects[i].h);
             strncat( shape_input, temp, sizeof(shape_input) - strlen(shape_input));
          }
     }

#ifdef HAVE_WAYLAND_ONLY
   if (target_ec->comp_data)
     {

        E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)target_ec->comp_data;
        if (cdata->surface)
          {
             wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
          }
     }
#endif

#define __WINDOW_PROP_ARG_APPEND(title, value) ({                                    \
                                                eldbus_message_iter_arguments_append(iter, "(ss)", &struct_of_ec);    \
                                                eldbus_message_iter_arguments_append(struct_of_ec, "ss", (title), (value));  \
                                                eldbus_message_iter_container_close(iter, struct_of_ec);})

#define __WINDOW_PROP_ARG_APPEND_TYPE(title, str, x...) ({                           \
                                                         char __temp[128] = {0,};                                                     \
                                                         snprintf(__temp, sizeof(__temp), str, ##x);                                  \
                                                         eldbus_message_iter_arguments_append(iter, "(ss)", &struct_of_ec);    \
                                                         eldbus_message_iter_arguments_append(struct_of_ec, "ss", (title), (__temp)); \
                                                         eldbus_message_iter_container_close(iter, struct_of_ec);})

   __WINDOW_PROP_ARG_APPEND("[WINDOW PROP]", "[WINDOW PROP]");
   __WINDOW_PROP_ARG_APPEND_TYPE("Window_ID", "0x%x", e_client_util_win_get(target_ec));
   __WINDOW_PROP_ARG_APPEND_TYPE("PID", "%d", pid);
   __WINDOW_PROP_ARG_APPEND("ResourceID", win_resid);
   __WINDOW_PROP_ARG_APPEND("Window_Name", e_client_util_name_get(target_ec) ?: "NO NAME");
   __WINDOW_PROP_ARG_APPEND("Role", target_ec->icccm.window_role ?: "NO ROLE");
   __WINDOW_PROP_ARG_APPEND_TYPE("Geometry", "[%d, %d, %d, %d]", target_ec->x, target_ec->y, target_ec->w, target_ec->h);
   __WINDOW_PROP_ARG_APPEND_TYPE("ParentWindowID", "0x%x", target_ec->parent ? e_client_util_win_get(target_ec->parent) : 0);
   __WINDOW_PROP_ARG_APPEND("Transients", transients);
   __WINDOW_PROP_ARG_APPEND("Shape_rects", shape_rects);
   __WINDOW_PROP_ARG_APPEND("Shape_input", shape_input);
   __WINDOW_PROP_ARG_APPEND("Layer", layer);
   __WINDOW_PROP_ARG_APPEND("Visible",  target_ec->visible ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("32bit",  target_ec->argb ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Hidden", target_ec->hidden ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Moving", target_ec->moving ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Focused", target_ec->focused ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Iconic", target_ec->iconic ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Sticky", target_ec->sticky ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Urgent", target_ec->urgent ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Fullscreen", target_ec->fullscreen ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Re_manage", target_ec->re_manage ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Take_focus", target_ec->take_focus ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Want_focus", target_ec->want_focus ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND_TYPE("E_Maximize_Policy", "0x%x", target_ec->maximized);
   __WINDOW_PROP_ARG_APPEND_TYPE("E_FullScreen_Policy", "%d", target_ec->fullscreen_policy);
   __WINDOW_PROP_ARG_APPEND_TYPE("E_Transient_Policy", "%d", target_ec->transient_policy);
   __WINDOW_PROP_ARG_APPEND("Override", target_ec->override ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Input_only", target_ec->input_only ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Dialog", target_ec->dialog ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Tooltip", target_ec->tooltip ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Redirected", target_ec->redirected ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Unredirected_single", target_ec->unredirected_single ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Shape_changed", target_ec->shape_changed ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Layer_block", target_ec->layer_block ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Ignored", target_ec->ignored ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("No_shape_cut", target_ec->no_shape_cut ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Maximize_override", target_ec->maximize_override ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND("Transformed", target_ec->transformed ? char_True : char_False);
   __WINDOW_PROP_ARG_APPEND_TYPE("Ignore_first_unmap", "%c", target_ec->ignore_first_unmap);

#undef __WINDOW_PROP_ARG_APPEND
#undef __WINDOW_PROP_ARG_APPEND_TYPE
}

static void
_msg_window_prop_append(Eldbus_Message_Iter *iter, uint32_t mode, const char *value)
{
   const static int WINDOW_ID_MODE = 0;
   const static int WINDOW_PID_MODE = 1;
   const static int WINDOW_NAME_MODE = 2;

   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;
   Evas_Object *o;
   int32_t value_number = 0;

   eldbus_message_iter_arguments_append(iter, "a(ss)", &array_of_ec);

   if (mode == WINDOW_ID_MODE || mode == WINDOW_PID_MODE)
     {
        if (!value) value_number = 0;
        else
          {
             if (strlen(value) >= 2 && value[0] == '0' && value[1] == 'x')
                sscanf(value, "%x", &value_number);
             else
                sscanf(value, "%d", &value_number);
          }
     }

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;

        if (mode == WINDOW_ID_MODE)
          {
             Ecore_Window win = e_client_util_win_get(ec);

             if (win == value_number)
               {
                  _msg_window_prop_client_append(array_of_ec, ec);
                  break;
               }
          }
        else if (mode == WINDOW_PID_MODE)
          {
             pid_t pid = -1;
#ifdef HAVE_WAYLAND_ONLY
             if (ec->comp_data)
               {
                  E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
                  if (cdata->surface)
                    {
                       wl_client_get_credentials(wl_resource_get_client(cdata->surface), &pid, NULL, NULL);
                    }
               }
#endif
             if (pid == value_number)
               {
                  _msg_window_prop_client_append(array_of_ec, ec);
               }
          }
        else if (mode == WINDOW_NAME_MODE)
          {
             const char *name = e_client_util_name_get(ec) ?: "NO NAME";

             if (name != NULL && value != NULL)
               {
                  const char *find = strstr(name, value);

                  if (find)
                     _msg_window_prop_client_append(array_of_ec, ec);
               }
          }
     }

   eldbus_message_iter_container_close(iter, array_of_ec);
}

static Eldbus_Message *
_e_info_server_cb_window_prop_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t mode = 0;
   const char *value = NULL;

   if (!eldbus_message_arguments_get(msg, "us", &mode, &value))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   _msg_window_prop_append(eldbus_message_iter_get(reply), mode, value);
   return reply;
}

static Eldbus_Message *
_e_info_server_cb_topvwins_dump(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *dir;
   Evas_Object *o;

   if (!eldbus_message_arguments_get(msg, "s", &dir))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        E_Client *ec = evas_object_data_get(o, "E_Client");
        char fname[PATH_MAX];
        Ecore_Window win;
        void *data = NULL;
        int w = 0, h = 0;
        Ecore_Evas *ee = NULL;
        Evas_Object *img = NULL;

        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);
        snprintf(fname, sizeof(fname), "%s/0x%08x.png", dir, win);

#ifdef HAVE_WAYLAND_ONLY
        struct wl_shm_buffer *shmbuffer = NULL;
        E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(ec->pixmap);
        if (!buffer) continue;

        if (buffer->type == E_COMP_WL_BUFFER_TYPE_SHM)
          {
             shmbuffer = wl_shm_buffer_get(buffer->resource);
             if (shmbuffer)
               {
                  data = wl_shm_buffer_get_data(shmbuffer);
                  w = wl_shm_buffer_get_stride(shmbuffer)/4;
                  h = wl_shm_buffer_get_height(shmbuffer);
               }
          }
        else if (buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE)
          {
             struct wl_drm_buffer *drm_buffer = wl_resource_get_user_data(buffer->resource);
             data = tbm_bo_map((tbm_bo)drm_buffer->driver_buffer, TBM_DEVICE_CPU, TBM_OPTION_READ).ptr;
             w = drm_buffer->stride[0]/4;
             h = drm_buffer->height;
          }
        else
          {
             ERR("Invalid resource:%u", wl_resource_get_id(buffer->resource));
          }
#endif

        EINA_SAFETY_ON_NULL_GOTO(data, err);

        ee = ecore_evas_buffer_new(1, 1);
        EINA_SAFETY_ON_NULL_GOTO(ee, err);

        img = evas_object_image_add(ecore_evas_get(ee));
        EINA_SAFETY_ON_NULL_GOTO(img, err);

        evas_object_image_alpha_set(img, ec->argb);
        evas_object_image_size_set(img, w, h);
        evas_object_image_data_set(img, data);

        if (!evas_object_image_save(img, fname, NULL, "compress=1 quality=100"))
          ERR("Cannot save window to '%s'", fname);

err:
#ifdef HAVE_WAYLAND_ONLY
        if (data && buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE)
          {
             struct wl_drm_buffer *drm_buffer = wl_resource_get_user_data(buffer->resource);
             tbm_bo_unmap((tbm_bo)(drm_buffer->driver_buffer));
          }
#endif

        if (img) evas_object_del(img);
        if (ee) ecore_evas_free(ee);
     }

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_eina_log_levels(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *start = NULL;

   if (!eldbus_message_arguments_get(msg, "s", &start) || !start)
     {
        ERR("Error getting arguments.");
        return reply;
     }

   while (1)
     {
        char module_name[256];
        char *end = NULL;
        char *tmp = NULL;
        int level;

        end = strchr(start, ':');
        if (!end)
           break;

        // Parse level, keep going if failed
        level = (int)strtol((char *)(end + 1), &tmp, 10);
        if (tmp == (end + 1))
           goto parse_end;

        // Parse name
        strncpy(module_name, start, MIN(end - start, (sizeof module_name) - 1));
        module_name[end - start] = '\0';

		  eina_log_domain_level_set((const char*)module_name, level);

parse_end:
        start = strchr(tmp, ',');
        if (start)
           start++;
        else
           break;
     }

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_eina_log_path(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   const char *path = NULL;
   static int old_stderr = -1;
   int  log_fd = -1;
   FILE *log_fl;

   if (!eldbus_message_arguments_get(msg, "s", &path) || !path)
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (old_stderr == -1)
     old_stderr = dup(STDOUT_FILENO);

   log_fl = fopen(path, "a");
   if (!log_fl)
     {
        ERR("failed: open file(%s)\n", path);
        return reply;
     }

   fflush(stderr);
   close(STDOUT_FILENO);

   setvbuf(log_fl, NULL, _IOLBF, 512);
   log_fd = fileno(log_fl);

   dup2(log_fd, STDOUT_FILENO);
   fclose(log_fl);

   return reply;
}

static const Eldbus_Method methods[] = {
   { "get_window_info", NULL, ELDBUS_ARGS({"a(uuisiiiiibbs)", "array of ec"}), _e_info_server_cb_window_info_get, 0 },
   { "dump_topvwins", ELDBUS_ARGS({"s", "directory"}), NULL, _e_info_server_cb_topvwins_dump, 0 },
   { "eina_log_levels", ELDBUS_ARGS({"s", "eina log levels"}), NULL, _e_info_server_cb_eina_log_levels, 0 },
   { "eina_log_path", ELDBUS_ARGS({"s", "eina log path"}), NULL, _e_info_server_cb_eina_log_path, 0 },
   { "get_window_prop", ELDBUS_ARGS({"us", "query_mode_value"}), ELDBUS_ARGS({"a(ss)", "array_of_ec"}), _e_info_server_cb_window_prop_get, 0},
   { "get_connected_clients", NULL, ELDBUS_ARGS({"a(ss)", "array of ec"}), _e_info_server_cb_connected_clients_get, 0 },
   { NULL, NULL, NULL, NULL, 0 }
};

static const Eldbus_Service_Interface_Desc iface_desc = {
     IFACE, methods, NULL, NULL, NULL, NULL
};

EINTERN int
e_info_server_init(void)
{
   eldbus_init();

   e_info_server.conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   EINA_SAFETY_ON_NULL_GOTO(e_info_server.conn, err);

   e_info_server.iface = eldbus_service_interface_register(e_info_server.conn,
                                                           PATH,
                                                           &iface_desc);
   EINA_SAFETY_ON_NULL_GOTO(e_info_server.iface, err);

   return 1;

err:
   e_info_server_shutdown();
   return 0;
}

EINTERN int
e_info_server_shutdown(void)
{
   if (e_info_server.iface)
     {
        eldbus_service_interface_unregister(e_info_server.iface);
        e_info_server.iface = NULL;
     }

   if (e_info_server.conn)
     {
        eldbus_connection_unref(e_info_server.conn);
        e_info_server.conn = NULL;
     }

   eldbus_shutdown();

   return 1;
}
