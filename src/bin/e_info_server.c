#define E_COMP_WL
#include "e.h"
#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <tdm_helper.h>
#ifdef HAVE_WAYLAND_ONLY
#include <wayland-tbm-server.h>
#include "e_comp_wl.h"
void wl_map_for_each(struct wl_map *map, void *func, void *data);
#endif
#ifdef HAVE_HWC
#include "e_comp_hwc.h"
#endif

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define IFACE "org.enlightenment.wm.info"

E_API int E_EVENT_INFO_ROTATION_MESSAGE = -1;

typedef struct _E_Info_Server
{
   Eldbus_Connection *conn;
   Eldbus_Service_Interface *iface;
} E_Info_Server;

typedef struct _E_Info_Transform
{
   E_Client         *ec;
   E_Util_Transform *transform;
   int               id;
   int               enable;
} E_Info_Transform;

static E_Info_Server e_info_server;
static Eina_List    *e_info_transform_list = NULL;

static Eina_List    *e_info_dump_hdlrs;
static char         *e_info_dump_path;
static int           e_info_dump_running;
static int           e_info_dump_count;

#define VALUE_TYPE_FOR_TOPVWINS "uuisiiiiibbiibbis"
#define VALUE_TYPE_REQUEST_RESLIST "ui"
#define VALUE_TYPE_REPLY_RESLIST "ssi"
#define VALUE_TYPE_FOR_INPUTDEV "ssi"

static E_Info_Transform *_e_info_transform_new(E_Client *ec, int id, int enable, int x, int y, int sx, int sy, int degree, int keep_ratio);
static E_Info_Transform *_e_info_transform_find(E_Client *ec, int id);
static void              _e_info_transform_set(E_Info_Transform *transform, int enable, int x, int y, int sx, int sy, int degree, int keep_ratio);
static void              _e_info_transform_del(E_Info_Transform *transform);
static void              _e_info_transform_del_with_id(E_Client *ec, int id);

static void
_msg_clients_append(Eldbus_Message_Iter *iter)
{
   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;
   Evas_Object *o;

   eldbus_message_iter_arguments_append(iter, "a("VALUE_TYPE_FOR_TOPVWINS")", &array_of_ec);

   // append clients.
   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        Eldbus_Message_Iter* struct_of_ec;
        Ecore_Window win;
        uint32_t res_id = 0;
        pid_t pid = -1;
        char layer_name[32];
        int hwc = -1;

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
        if (e_comp->hwc)
          {
             // TODO: print plane number
             if (!e_comp->nocomp_ec)
               hwc = 1; // comp mode
             else if (e_comp->nocomp_ec == ec)
               hwc = 2; // a client occupied scanout buff
             else
               hwc = 0;
          }

        eldbus_message_iter_arguments_append(array_of_ec, "("VALUE_TYPE_FOR_TOPVWINS")", &struct_of_ec);

        eldbus_message_iter_arguments_append
           (struct_of_ec, VALUE_TYPE_FOR_TOPVWINS,
            win,
            res_id,
            pid,
            e_client_util_name_get(ec) ?: "NO NAME",
            ec->x, ec->y, ec->w, ec->h, ec->layer,
            ec->visible, ec->argb, ec->visibility.opaque, ec->visibility.obscured, ec->iconic, ec->focused, hwc, layer_name);

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
_input_msg_clients_append(Eldbus_Message_Iter *iter)
{
   Eldbus_Message_Iter *array_of_input;
   Eina_List *l;
   E_Comp_Wl_Data *cdata;
   E_Comp_Wl_Input_Device *dev;

   eldbus_message_iter_arguments_append(iter, "a("VALUE_TYPE_FOR_INPUTDEV")", &array_of_input);

   cdata = e_comp->wl_comp_data;
   EINA_LIST_FOREACH(cdata->input_device_manager.device_list, l, dev)
     {
        Eldbus_Message_Iter *struct_of_input;

        eldbus_message_iter_arguments_append(array_of_input, "("VALUE_TYPE_FOR_INPUTDEV")", &struct_of_input);

        eldbus_message_iter_arguments_append
                     (struct_of_input, VALUE_TYPE_FOR_INPUTDEV,
                      dev->name, dev->identifier, dev->capability);

        eldbus_message_iter_container_close(array_of_input, struct_of_input);
     }
   eldbus_message_iter_container_close(iter, array_of_input);
}


static Eldbus_Message *
_e_info_server_cb_input_device_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   _input_msg_clients_append(eldbus_message_iter_get(reply));

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

#define wl_client_for_each(client, list)     \
   for (client = 0, client = wl_client_from_link((list)->next);   \
        wl_client_get_link(client) != (list);                     \
        client = wl_client_from_link(wl_client_get_link(client)->next))

static int resurceCnt = 0;

static void
_e_info_server_get_resource(void *element, void *data)
{
   struct wl_resource *resource = element;
   Eldbus_Message_Iter* array_of_res= data;
   Eldbus_Message_Iter* struct_of_res;

   eldbus_message_iter_arguments_append(array_of_res, "("VALUE_TYPE_REPLY_RESLIST")", &struct_of_res);
   eldbus_message_iter_arguments_append(struct_of_res, VALUE_TYPE_REPLY_RESLIST, "[resource]", wl_resource_get_name(resource), wl_resource_get_id(resource));
   eldbus_message_iter_container_close(array_of_res, struct_of_res);
   resurceCnt++;
}

static void
_msg_clients_res_list_append(Eldbus_Message_Iter *iter, uint32_t mode, int id)
{
   Eldbus_Message_Iter *array_of_res;

   struct wl_list * client_list;
   struct wl_client *client;
   struct wl_map *res_objs;
   //E_Comp_Data *cdata;
   E_Comp_Wl_Data *cdata;
   int pid = -1;

   enum {
   DEFAULT_SUMMARY,
   TREE,
   PID} type = mode;

   eldbus_message_iter_arguments_append(iter, "a("VALUE_TYPE_REPLY_RESLIST")", &array_of_res);

   if (!e_comp) return;
   if (!(cdata = e_comp->wl_comp_data)) return;
   if (!cdata->wl.disp) return;

   client_list = wl_display_get_client_list(cdata->wl.disp);

   wl_client_for_each(client, client_list)
     {
        Eldbus_Message_Iter* struct_of_res;

        wl_client_get_credentials(client, &pid, NULL, NULL);

        if ((type == PID) && (pid != id)) continue;

        eldbus_message_iter_arguments_append(array_of_res, "("VALUE_TYPE_REPLY_RESLIST")", &struct_of_res);

        eldbus_message_iter_arguments_append(struct_of_res, VALUE_TYPE_REPLY_RESLIST, "[client]", "pid", pid);
        eldbus_message_iter_container_close(array_of_res, struct_of_res);

        resurceCnt = 0;
        res_objs = wl_client_get_resources(client);
        wl_map_for_each(res_objs, _e_info_server_get_resource, array_of_res);

        eldbus_message_iter_arguments_append(array_of_res, "("VALUE_TYPE_REPLY_RESLIST")", &struct_of_res);
        eldbus_message_iter_arguments_append(struct_of_res, VALUE_TYPE_REPLY_RESLIST, "[count]", "resurceCnt", resurceCnt);
        eldbus_message_iter_container_close(array_of_res, struct_of_res);
     }
   eldbus_message_iter_container_close(iter, array_of_res);
}

static Eldbus_Message *
_e_info_server_cb_res_lists_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t mode = 0;
   int pid = -1;

   if (!eldbus_message_arguments_get(msg, VALUE_TYPE_REQUEST_RESLIST, &mode, &pid))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   _msg_clients_res_list_append(eldbus_message_iter_get(reply), mode, pid);

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
             strncat(transients, temp, sizeof(transients) - strlen(transients));
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
             strncat(shape_rects, temp, sizeof(shape_rects) - strlen(shape_rects));
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
             strncat(shape_input, temp, sizeof(shape_input) - strlen(shape_input));
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
   __WINDOW_PROP_ARG_APPEND_TYPE("Transform_count", "%d", e_client_transform_core_transform_count_get(target_ec));
   if (e_client_transform_core_transform_count_get(target_ec) > 0)
     {
        int i;
        int count = e_client_transform_core_transform_count_get(target_ec);

        __WINDOW_PROP_ARG_APPEND(" ", "[id] [move] [scale] [rotation] [keep_ratio]");
        for (i = 0 ; i < count ; ++i)
          {
             double dx, dy, dsx, dsy, drz;
             int x, y, rz;
             int keep_ratio;

             E_Util_Transform *transform = e_client_transform_core_transform_get(target_ec, i);
             if (!transform) continue;

             e_util_transform_move_get(transform, &dx, &dy, NULL);
             e_util_transform_scale_get(transform, &dsx, &dsy, NULL);
             e_util_transform_rotation_get(transform, NULL, NULL, &drz);
             keep_ratio = e_util_transform_keep_ratio_get(transform);

             x = (int)(dx + 0.5);
             y = (int)(dy + 0.5);
             rz = (int)(drz + 0.5);

             __WINDOW_PROP_ARG_APPEND_TYPE("Transform", "[%d] [%d, %d] [%2.1f, %2.1f] [%d] [%d]", i, x, y, dsx, dsy, rz, keep_ratio);
          }
     }
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

        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);
        snprintf(fname, sizeof(fname), "%s/0x%08x.png", dir, win);

        e_info_server_dump_client(ec, fname);
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

static Eldbus_Message *
_e_info_server_cb_rotation_query(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   /* TODO: need implementation */

   return reply;
}

static void
_e_info_event_rotation_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Info_Rotation_Message *ev = event;

   e_object_unref(E_OBJECT(ev->zone));
   free(ev);
}

static Eldbus_Message *
_e_info_server_cb_rotation_message(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Event_Info_Rotation_Message *ev;
   E_Info_Rotation_Message rot_msg;
   E_Zone *z;
   Eina_List *l;
   uint32_t zone_num;
   uint32_t rval;

   if (!eldbus_message_arguments_get(msg, "iii", &rot_msg, &zone_num, &rval))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (rot_msg == E_INFO_ROTATION_MESSAGE_SET)
     {
        /* check if rval is valid */
        if ((rval > 270) || (rval % 90 != 0))
          return reply;
     }

   ev = E_NEW(E_Event_Info_Rotation_Message, 1);
   if (EINA_UNLIKELY(!ev))
     {
        ERR("Failed to allocate ""E_Event_Info_Rotation_Message""");
        return reply;
     }

   if (zone_num == -1)
     ev->zone = e_zone_current_get();
   else
     {
        EINA_LIST_FOREACH(e_comp->zones, l, z)
          {
             if (z->num == zone_num)
               ev->zone = z;
          }
     }

   if (!ev->zone)
     {
        ERR("Failed to found zone by given num: num %d", zone_num);
        free(ev);
        return reply;
     }

   e_object_ref(E_OBJECT(ev->zone));
   ev->message = rot_msg;
   ev->rotation = rval;

   ecore_event_add(E_EVENT_INFO_ROTATION_MESSAGE, ev, _e_info_event_rotation_free, NULL);

   return reply;
}

static Eldbus_Message *
_e_info_server_cb_fps_info_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   static double old_fps = 0;

   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   char buf[128] = {};

   if (!e_comp->calc_fps)
     {
        e_comp->calc_fps = 1;
     }

   if (old_fps == e_comp->fps)
     {
        snprintf(buf, sizeof(buf), "no_update");
     }
   else if (e_comp->fps > 0.0)
     {
        if(e_comp->nocomp && e_comp->nocomp_ec)
          snprintf(buf, sizeof(buf), "... FPS %3.1f(by 0x%x : %s)", e_comp->fps, e_client_util_win_get(e_comp->nocomp_ec), e_client_util_name_get(e_comp->nocomp_ec) ?: " ");
        else
          snprintf(buf, sizeof(buf), "... FPS %3.1f", e_comp->fps);
        old_fps = e_comp->fps;
     }
   else
     {
        snprintf(buf, sizeof(buf), "... FPS N/A");
     }

   eldbus_message_arguments_append(reply, "s", buf);
   return reply;
}

static Eldbus_Message *
e_info_server_cb_transform_message(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t enable, transform_id;
   uint32_t x, y, sx, sy, degree;
   uint32_t keep_ratio;
   const char *value = NULL;
   int32_t value_number;
   Evas_Object *o;
   E_Client *ec;

   if (!eldbus_message_arguments_get(msg, "siiiiiiii", &value, &transform_id, &enable, &x, &y, &sx, &sy, &degree, &keep_ratio))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (strlen(value) >= 2 && value[0] == '0' && value[1] == 'x')
      sscanf(value, "%x", &value_number);
   else
      sscanf(value, "%d", &value_number);

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        Ecore_Window win;
        E_Info_Transform *transform_info;

        if (!ec) continue;

        win = e_client_util_win_get(ec);

        if (win != value_number) continue;
        transform_info = _e_info_transform_find(ec, transform_id);

        if (transform_info)
          {
             _e_info_transform_set(transform_info, enable, x, y, sx, sy, degree, keep_ratio);

             if (!enable)
                _e_info_transform_del_with_id(ec, transform_id);
          }
        else
          {
             if (enable)
               {
                  _e_info_transform_new(ec, transform_id, enable, x, y, sx, sy, degree, keep_ratio);
               }
          }

        break;
     }

   return reply;
}

static Eina_Bool
_e_info_server_cb_buffer_change(void *data, int type, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;
   Ecore_Window event_win;
   Evas_Object *o;
   char fname[PATH_MAX];
   int count;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   /* dump buffer change call event buffer */
   ec = ev->ec;
   if (e_object_is_del(E_OBJECT(ec)))
     {
        ERR("%s: e_object_is_del(E_OBJECT(ec) return\n", __func__);
        return ECORE_CALLBACK_PASS_ON;
     }
   if (e_client_util_ignored_get(ec))
     {
        ERR("%s: e_client_util_ignored_get(ec) true. return\n", __func__);
        return ECORE_CALLBACK_PASS_ON;
     }
   event_win = e_client_util_win_get(ec);

   if (e_info_dump_count == 1000)
     e_info_dump_count = 1;
   count = e_info_dump_count++;
   snprintf(fname, sizeof(fname), "%s/%03d_0x%08x.png", e_info_dump_path, count, event_win);
   e_info_server_dump_client(ec, fname);

#if 0
   /* dump all buffers */
   char path[PATH_MAX];

   snprintf(path, sizeof(path), "%s/%d", e_info_dump_path, count);   
   if ((mkdir(path, 0755)) < 0)
     {
        printf("%s: mkdir '%s' fail\n", __func__, path);
        return ECORE_CALLBACK_PASS_ON;
     }
   
   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        E_Client *ec = evas_object_data_get(o, "E_Client");
        Ecore_Window win;

        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_client_util_win_get(ec);

        snprintf(fname, sizeof(fname), "%s/0x%08x.png", path, win);

        e_info_server_dump_client(ec, fname);
     }
#endif
   DBG("%d, %s dump excute\n", count, fname);

   return ECORE_CALLBACK_PASS_ON;
}

static char *
_e_info_server_dump_directory_make(void)
{
   char *fullpath;
   time_t timer;
   struct tm *t, *buf;

   timer = time(NULL);

   buf = calloc (1, sizeof (struct tm));
   EINA_SAFETY_ON_NULL_RETURN_VAL(buf, NULL);
   t = localtime_r(&timer, buf);
   if (!t)
     {
        free(buf);
        ERR("fail to get local time\n");
        return NULL;
     }

   fullpath = (char *)calloc(1, PATH_MAX * sizeof(char));
   if (!fullpath)
     {
        free(buf);
        ERR("fail to alloc pathname memory\n");
        return NULL;
     }

   snprintf(fullpath, PATH_MAX, "/tmp/dump_%04d%02d%02d.%02d%02d%02d",
            t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

   free(buf);

   if ((mkdir(fullpath, 0755)) < 0)
     {
        ERR("%s: mkdir '%s' fail\n", __func__, fullpath);
        free(fullpath);
        return NULL;
     }

   return fullpath;
}

static Eldbus_Message *
_e_info_server_cb_buffer_dump(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   int start = 0;

   if (!eldbus_message_arguments_get(msg, "i", &start))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (start == 1)
     {
        if (e_info_dump_running == 1)
          return reply;
        e_info_dump_running = 1;
        e_info_dump_count = 1;
        e_info_dump_path = _e_info_server_dump_directory_make();
        if (e_info_dump_path == NULL)
          {
             e_info_dump_running = 0;
             e_info_dump_count = 0;
             ERR("dump_buffers start fail\n");
          }
        else
          {
             tdm_helper_dump_start(e_info_dump_path, &e_info_dump_count);
             E_LIST_HANDLER_APPEND(e_info_dump_hdlrs, E_EVENT_CLIENT_BUFFER_CHANGE,
                               _e_info_server_cb_buffer_change, NULL);
          }
     }
   else
     {
        if (e_info_dump_running == 0)
          return reply;
        tdm_helper_dump_stop();
        E_FREE_LIST(e_info_dump_hdlrs, ecore_event_handler_del);
        e_info_dump_hdlrs = NULL;
        if (e_info_dump_path)
          {
             free(e_info_dump_path);
             e_info_dump_path = NULL;
          }
        e_info_dump_count = 0;
        e_info_dump_running = 0;
     }
   return reply;
}

#ifdef HAVE_HWC
static Eldbus_Message *
e_info_server_cb_hwc_trace_message(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   uint32_t onoff;

   if (!eldbus_message_arguments_get(msg, "i", &onoff))
     {
        ERR("Error getting arguments.");
        return reply;
     }

   if (onoff == 1 || onoff == 0)
     e_comp_hwc_trace_debug(onoff);

   return reply;
}
#endif

static const Eldbus_Method methods[] = {
   { "get_window_info", NULL, ELDBUS_ARGS({"a("VALUE_TYPE_FOR_TOPVWINS")", "array of ec"}), _e_info_server_cb_window_info_get, 0 },
   { "dump_topvwins", ELDBUS_ARGS({"s", "directory"}), NULL, _e_info_server_cb_topvwins_dump, 0 },
   { "eina_log_levels", ELDBUS_ARGS({"s", "eina log levels"}), NULL, _e_info_server_cb_eina_log_levels, 0 },
   { "eina_log_path", ELDBUS_ARGS({"s", "eina log path"}), NULL, _e_info_server_cb_eina_log_path, 0 },
   { "get_window_prop", ELDBUS_ARGS({"us", "query_mode_value"}), ELDBUS_ARGS({"a(ss)", "array_of_ec"}), _e_info_server_cb_window_prop_get, 0},
   { "get_connected_clients", NULL, ELDBUS_ARGS({"a(ss)", "array of ec"}), _e_info_server_cb_connected_clients_get, 0 },
   { "rotation_query", ELDBUS_ARGS({"i", "query_rotation"}), NULL, _e_info_server_cb_rotation_query, 0},
   { "rotation_message", ELDBUS_ARGS({"iii", "rotation_message"}), NULL, _e_info_server_cb_rotation_message, 0},
   { "get_res_lists", ELDBUS_ARGS({VALUE_TYPE_REQUEST_RESLIST, "client resource"}), ELDBUS_ARGS({"a("VALUE_TYPE_REPLY_RESLIST")", "array of client resources"}), _e_info_server_cb_res_lists_get, 0 },
   { "get_input_devices", NULL, ELDBUS_ARGS({"a("VALUE_TYPE_FOR_INPUTDEV")", "array of input"}), _e_info_server_cb_input_device_info_get, 0},
   { "get_fps_info", NULL, ELDBUS_ARGS({"s", "fps request"}), _e_info_server_cb_fps_info_get, 0},
   { "transform_message", ELDBUS_ARGS({"siiiiiiii", "transform_message"}), NULL, e_info_server_cb_transform_message, 0},
   { "dump_buffers", ELDBUS_ARGS({"i", "start"}), NULL, _e_info_server_cb_buffer_dump, 0 },
#ifdef HAVE_HWC
   { "hwc_trace_message", ELDBUS_ARGS({"i", "hwc_trace_message"}), NULL, e_info_server_cb_hwc_trace_message, 0},
#endif
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

   E_EVENT_INFO_ROTATION_MESSAGE = ecore_event_type_new();

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

   if (e_info_transform_list)
     {
        E_Info_Transform *info;
        Eina_List *l, *l_next;

        EINA_LIST_FOREACH_SAFE(e_info_transform_list, l, l_next, info)
          {
             _e_info_transform_del(info);
          }

        eina_list_free(e_info_transform_list);
        e_info_transform_list = NULL;
     }

   if (e_info_dump_running == 1)
     tdm_helper_dump_stop();
   if (e_info_dump_hdlrs)
     {
        E_FREE_LIST(e_info_dump_hdlrs, ecore_event_handler_del);
        e_info_dump_hdlrs = NULL;
     }
   if (e_info_dump_path)
     {
        free(e_info_dump_path);
        e_info_dump_path = NULL;
     }
   e_info_dump_count = 0;
   e_info_dump_running = 0;

   eldbus_shutdown();

   return 1;
}

EINTERN void
e_info_server_dump_client(E_Client *ec, char *fname)
{
   void *data = NULL;
   int w = 0, h = 0;
   Ecore_Evas *ee = NULL;
   Evas_Object *img = NULL;

   if (!ec) return;
   if (e_client_util_ignored_get(ec)) return;

 #ifdef HAVE_WAYLAND_ONLY
   struct wl_shm_buffer *shmbuffer = NULL;
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(ec->pixmap);
   if (!buffer) return;

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
        tbm_surface_info_s surface_info;
        tbm_surface_h tbm_surface = wayland_tbm_server_get_surface(NULL, buffer->resource);

        memset(&surface_info, 0, sizeof(tbm_surface_info_s));
        tbm_surface_map(tbm_surface, TBM_SURF_OPTION_READ, &surface_info);

        data = surface_info.planes[0].ptr;
        w = surface_info.planes[0].stride/4;
        h = surface_info.height;
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

   evas_object_image_alpha_set(img, EINA_TRUE);
   evas_object_image_size_set(img, w, h);
   evas_object_image_data_set(img, data);

   if (!evas_object_image_save(img, fname, NULL, "compress=1 quality=100"))
     ERR("Cannot save window to '%s'", fname);

 err:
 #ifdef HAVE_WAYLAND_ONLY
   if (data && buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE)
     {
        tbm_surface_h tbm_surface = wayland_tbm_server_get_surface(NULL, buffer->resource);
        tbm_surface_unmap(tbm_surface);
     }
 #endif

   if (img) evas_object_del(img);
   if (ee) ecore_evas_free(ee);
}


static E_Info_Transform*
_e_info_transform_new(E_Client *ec, int id, int enable, int x, int y, int sx, int sy, int degree, int keep_ratio)
{
   E_Info_Transform *result = NULL;
   result = _e_info_transform_find(ec, id);

   if (!result)
     {
        result = (E_Info_Transform*)malloc(sizeof(E_Info_Transform));
        memset(result, 0, sizeof(E_Info_Transform));
        result->id = id;
        result->ec = ec;
        result->transform = e_util_transform_new();
        _e_info_transform_set(result, enable, x, y, sx, sy, degree, keep_ratio);
        e_info_transform_list = eina_list_append(e_info_transform_list, result);

     }

   return result;
}

static E_Info_Transform*
_e_info_transform_find(E_Client *ec, int id)
{
   Eina_List *l;
   E_Info_Transform *transform;
   E_Info_Transform *result = NULL;

   EINA_LIST_FOREACH(e_info_transform_list, l, transform)
     {
        if (transform->ec == ec && transform->id == id)
          {
             result =  transform;
             break;
          }
     }

   return result;
}

static void
_e_info_transform_set(E_Info_Transform *transform, int enable, int x, int y, int sx, int sy, int degree, int keep_ratio)
{
   if (!transform) return;
   if (!transform->transform) return;

   e_util_transform_move(transform->transform, (double)x, (double)y, 0.0);
   e_util_transform_scale(transform->transform, (double)sx / 100.0, (double)sy / 100.0, 1.0);
   e_util_transform_rotation(transform->transform, 0.0, 0.0, degree);
   e_util_transform_keep_ratio_set(transform->transform, keep_ratio);

   if (enable)
      e_client_transform_core_add(transform->ec, transform->transform);
   else
      e_client_transform_core_remove(transform->ec, transform->transform);

   e_client_transform_core_update(transform->ec);
}

static void
_e_info_transform_del(E_Info_Transform *transform)
{
   if (!transform) return;

   e_info_transform_list = eina_list_remove(e_info_transform_list, transform);
   e_client_transform_core_remove(transform->ec, transform->transform);
   e_util_transform_del(transform->transform);
   free(transform);
}

static void
_e_info_transform_del_with_id(E_Client *ec, int id)
{
   E_Info_Transform *transform = NULL;
   if (!ec) return;

   transform = _e_info_transform_find(ec, id);

   if (transform)
      _e_info_transform_del(transform);
}
