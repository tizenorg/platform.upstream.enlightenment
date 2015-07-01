#define EXECUTIVE_MODE_ENABLED
#define E_COMP_WL
#include "e.h"
#include "e_desktop_shell_protocol.h"
#include "e_scaler.h"
#include "tizen_extension_server_protocol.h"
#include "tizen_subsurface_server_protocol.h"
#include "tizen_transient_for_server_protocol.h"

#define XDG_SERVER_VERSION 4

static void
_e_shell_surface_parent_set(E_Client *ec, struct wl_resource *parent_resource)
{
   E_Pixmap *pp;
   E_Client *pc;
   uint64_t pwin = 0;

   if (!parent_resource)
     {
        ec->icccm.fetch.transient_for = EINA_FALSE;
        ec->icccm.transient_for = 0;
        if (ec->parent)
          {
             ec->parent->transients =
                eina_list_remove(ec->parent->transients, ec);
             if (ec->parent->modal == ec) ec->parent->modal = NULL;
             ec->parent = NULL;
          }
        return;
     }
   else if (!(pp = wl_resource_get_user_data(parent_resource)))
     {
        ERR("Could not get parent resource pixmap");
        return;
     }

   pwin = e_pixmap_window_get(pp);

   /* find the parent client */
   if (!(pc = e_pixmap_client_get(pp)))
     pc = e_pixmap_find_client(E_PIXMAP_TYPE_WL, pwin);

   e_pixmap_parent_window_set(ec->pixmap, pwin);

   /* If we already have a parent, remove it */
   if (ec->parent)
     {
        if (pc != ec->parent)
          {
             ec->parent->transients =
                eina_list_remove(ec->parent->transients, ec);
             if (ec->parent->modal == ec) ec->parent->modal = NULL;
             ec->parent = NULL;
          }
        else
          pc = NULL;
     }

   if ((pc) && (pc != ec) &&
       (eina_list_data_find(pc->transients, ec) != ec))
     {
        pc->transients = eina_list_append(pc->transients, ec);
        ec->parent = pc;
     }

   ec->icccm.fetch.transient_for = EINA_TRUE;
   ec->icccm.transient_for = pwin;
}

static void
_e_shell_surface_mouse_down_helper(E_Client *ec, E_Binding_Event_Mouse_Button *ev, Eina_Bool move)
{
   if (move)
     {
        /* tell E to start moving the client */
        e_client_act_move_begin(ec, ev);

        /* we have to get a reference to the window_move action here, or else
         * when e_client stops the move we will never get notified */
        ec->cur_mouse_action = e_action_find("window_move");
        if (ec->cur_mouse_action)
          e_object_ref(E_OBJECT(ec->cur_mouse_action));
     }
   else
     {
        /* tell E to start resizing the client */
        e_client_act_resize_begin(ec, ev);

        /* we have to get a reference to the window_resize action here,
         * or else when e_client stops the resize we will never get notified */
        ec->cur_mouse_action = e_action_find("window_resize");
        if (ec->cur_mouse_action)
          e_object_ref(E_OBJECT(ec->cur_mouse_action));
     }

   e_focus_event_mouse_down(ec);
}

static void
_e_shell_surface_destroy(struct wl_resource *resource)
{
   E_Client *ec;

   /* DBG("Shell Surface Destroy: %d", wl_resource_get_id(resource)); */

   /* get the client for this resource */
   if ((ec = wl_resource_get_user_data(resource)))
     {
        if (e_object_is_del(E_OBJECT(ec))) return;

        if (ec->comp_data)
          {
             if (ec->comp_data->mapped)
               {
                  if ((ec->comp_data->shell.surface) &&
                      (ec->comp_data->shell.unmap))
                    ec->comp_data->shell.unmap(ec->comp_data->shell.surface);
               }
             if (ec->parent)
               {
                  ec->parent->transients =
                    eina_list_remove(ec->parent->transients, ec);
               }
             /* wl_resource_destroy(ec->comp_data->shell.surface); */
             ec->comp_data->shell.surface = NULL;
          }
     }
}

static void
_e_shell_surface_cb_destroy(struct wl_resource *resource)
{
   /* DBG("Shell Surface Destroy: %d", wl_resource_get_id(resource)); */

   _e_shell_surface_destroy(resource);
}

static void
_e_shell_surface_cb_pong(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t serial EINA_UNUSED)
{
   E_Client *ec;

   if ((ec = wl_resource_get_user_data(resource)))
     {
        ec->ping_ok = EINA_TRUE;
        ec->hung = EINA_FALSE;
     }
}

static void
_e_shell_surface_cb_move(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial EINA_UNUSED)
{
   E_Client *ec;
   E_Comp_Data *cdata;
   E_Binding_Event_Mouse_Button ev;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if ((ec->maximized) || (ec->fullscreen)) return;

   /* get compositor data from seat */
   if (!(cdata = wl_resource_get_user_data(seat_resource)))
     {
        wl_resource_post_error(seat_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Comp_Data for Seat");
        return;
     }

   switch (cdata->ptr.button)
     {
      case BTN_LEFT:
        ev.button = 1;
        break;
      case BTN_MIDDLE:
        ev.button = 2;
        break;
      case BTN_RIGHT:
        ev.button = 3;
        break;
      default:
        ev.button = cdata->ptr.button;
        break;
     }

   e_comp_object_frame_xy_unadjust(ec->frame,
                                   wl_fixed_to_int(cdata->ptr.x),
                                   wl_fixed_to_int(cdata->ptr.y),
                                   &ev.canvas.x, &ev.canvas.y);

   _e_shell_surface_mouse_down_helper(ec, &ev, EINA_TRUE);
}

static void
_e_shell_surface_cb_resize(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial EINA_UNUSED, uint32_t edges)
{
   E_Client *ec;
   E_Comp_Data *cdata;
   E_Binding_Event_Mouse_Button ev;
   int cx, cy;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if ((edges == 0) || (edges > 15) ||
       ((edges & 3) == 3) || ((edges & 12) == 12)) return;

   if ((ec->maximized) || (ec->fullscreen)) return;

   /* get compositor data from seat */
   if (!(cdata = wl_resource_get_user_data(seat_resource)))
     {
        wl_resource_post_error(seat_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Comp_Data for Seat");
        return;
     }

   DBG("Comp Resize Edges Set: %d", edges);

   cdata->resize.resource = resource;
   cdata->resize.edges = edges;

   cx = wl_fixed_to_int(cdata->ptr.x) - ec->client.x;
   cy = wl_fixed_to_int(cdata->ptr.y) - ec->client.y;
   cdata->ptr.grab_x = wl_fixed_from_int(cx);
   cdata->ptr.grab_y = wl_fixed_from_int(cy);

   switch (cdata->ptr.button)
     {
      case BTN_LEFT:
        ev.button = 1;
        break;
      case BTN_MIDDLE:
        ev.button = 2;
        break;
      case BTN_RIGHT:
        ev.button = 3;
        break;
      default:
        ev.button = cdata->ptr.button;
        break;
     }

   e_comp_object_frame_xy_unadjust(ec->frame,
                                   wl_fixed_to_int(cdata->ptr.x),
                                   wl_fixed_to_int(cdata->ptr.y),
                                   &ev.canvas.x, &ev.canvas.y);

   _e_shell_surface_mouse_down_helper(ec, &ev, EINA_FALSE);
}

static void
_e_shell_surface_cb_toplevel_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* set toplevel client properties */
   ec->borderless = !ec->internal;

   ec->lock_border = EINA_TRUE;
   ec->border.changed = ec->changes.border = !ec->borderless;
   ec->netwm.type = E_WINDOW_TYPE_NORMAL;
   ec->comp_data->set_win_type = EINA_TRUE;
   if ((!ec->lock_user_maximize) && (ec->maximized))
     e_client_unmaximize(ec, E_MAXIMIZE_BOTH);
   if ((!ec->lock_user_fullscreen) && (ec->fullscreen))
     e_client_unfullscreen(ec);
   EC_CHANGED(ec);
}

static void
_e_shell_surface_cb_transient_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *parent_resource, int32_t x EINA_UNUSED, int32_t y EINA_UNUSED, uint32_t flags EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* set this client as a transient for parent */
   _e_shell_surface_parent_set(ec, parent_resource);

   EC_CHANGED(ec);
}

static void
_e_shell_surface_cb_fullscreen_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t method EINA_UNUSED, uint32_t framerate EINA_UNUSED, struct wl_resource *output_resource EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (!ec->lock_user_fullscreen)
     e_client_fullscreen(ec, e_config->fullscreen_policy);
}

static void
_e_shell_surface_cb_popup_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource EINA_UNUSED, uint32_t serial EINA_UNUSED, struct wl_resource *parent_resource, int32_t x, int32_t y, uint32_t flags EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (ec->comp_data)
     {
        ec->comp_data->popup.x = x;
        ec->comp_data->popup.y = y;
     }

   ec->borderless = !ec->internal_elm_win;
   ec->lock_border = EINA_TRUE;
   ec->border.changed = ec->changes.border = !ec->borderless;
   ec->changes.icon = !!ec->icccm.class;
   ec->netwm.type = E_WINDOW_TYPE_POPUP_MENU;
   ec->comp_data->set_win_type = EINA_TRUE;
   ec->layer = E_LAYER_CLIENT_POPUP;

   /* set this client as a transient for parent */
   _e_shell_surface_parent_set(ec, parent_resource);

   EC_CHANGED(ec);
}

static void
_e_shell_surface_cb_maximized_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *output_resource EINA_UNUSED)
{
   E_Client *ec;

   /* DBG("WL_SHELL: Surface Maximize: %d", wl_resource_get_id(resource)); */

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* tell E to maximize this client */
   if (!ec->lock_user_maximize)
     {
        unsigned int edges = 0;

        e_client_maximize(ec, ((e_config->maximize_policy & E_MAXIMIZE_TYPE) |
                               E_MAXIMIZE_BOTH));

        edges = (WL_SHELL_SURFACE_RESIZE_TOP | WL_SHELL_SURFACE_RESIZE_LEFT);
        wl_shell_surface_send_configure(resource, edges, ec->w, ec->h);
     }
}

static void
_e_shell_surface_cb_title_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, const char *title)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* set title */
   eina_stringshare_replace(&ec->icccm.title, title);
   if (ec->frame) e_comp_object_frame_title_set(ec->frame, title);
}

static void
_e_shell_surface_cb_class_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, const char *clas)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* use the wl_client to get the pid * and set it in the netwm props */
   wl_client_get_credentials(client, &ec->netwm.pid, NULL, NULL);

   /* set class */
   eina_stringshare_replace(&ec->icccm.class, clas);
   ec->changes.icon = !!ec->icccm.class;
   EC_CHANGED(ec);
}

static const struct wl_shell_surface_interface _e_shell_surface_interface =
{
   _e_shell_surface_cb_pong,
   _e_shell_surface_cb_move,
   _e_shell_surface_cb_resize,
   _e_shell_surface_cb_toplevel_set,
   _e_shell_surface_cb_transient_set,
   _e_shell_surface_cb_fullscreen_set,
   _e_shell_surface_cb_popup_set,
   _e_shell_surface_cb_maximized_set,
   _e_shell_surface_cb_title_set,
   _e_shell_surface_cb_class_set,
};

static void
_e_shell_surface_configure_send(struct wl_resource *resource, uint32_t edges, int32_t width, int32_t height)
{
   wl_shell_surface_send_configure(resource, edges, width, height);
}

static void
_e_shell_surface_configure(struct wl_resource *resource, Evas_Coord x, Evas_Coord y, Evas_Coord w, Evas_Coord h)
{
   E_Client *ec;

   /* DBG("WL_SHELL: Surface Configure: %d \t%d %d %d %d",  */
   /*     wl_resource_get_id(resource), x, y, w, h); */

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (ec->parent)
     {
        if ((ec->netwm.type == E_WINDOW_TYPE_MENU) ||
            (ec->netwm.type == E_WINDOW_TYPE_POPUP_MENU) ||
            (ec->netwm.type == E_WINDOW_TYPE_DROPDOWN_MENU))
          {
             x = E_CLAMP(ec->parent->client.x + ec->comp_data->popup.x,
               ec->parent->client.x,
               ec->parent->client.x + ec->parent->client.w - ec->client.w);
             y = E_CLAMP(ec->parent->client.y + ec->comp_data->popup.y,
             ec->parent->client.y,
             ec->parent->client.y + ec->parent->client.h - ec->client.h);
          }
     }

   e_client_util_move_resize_without_frame(ec, x, y, w, h);
}

static void
_e_shell_surface_ping(struct wl_resource *resource)
{
   E_Client *ec;
   uint32_t serial;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   serial = wl_display_next_serial(ec->comp->wl_comp_data->wl.disp);
   wl_shell_surface_send_ping(ec->comp_data->shell.surface, serial);
}

static void
_e_shell_surface_map(struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* map this surface if needed */
   if ((!ec->comp_data->mapped) && (e_pixmap_usable_get(ec->pixmap)))
     {
        ec->visible = EINA_TRUE;
        evas_object_geometry_set(ec->frame, ec->x, ec->y, ec->w, ec->h);
        evas_object_show(ec->frame);
        ec->comp_data->mapped = EINA_TRUE;
     }
}

static void
_e_shell_surface_unmap(struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (ec->comp_data->mapped)
     {
        ec->visible = EINA_FALSE;
        evas_object_hide(ec->frame);
        ec->comp_data->mapped = EINA_FALSE;
     }
}

static void
_e_shell_cb_shell_surface_get(struct wl_client *client, struct wl_resource *resource EINA_UNUSED, uint32_t id, struct wl_resource *surface_resource)
{
   E_Pixmap *ep;
   E_Client *ec;
   E_Comp_Client_Data *cdata;

   /* get the pixmap from this surface so we can find the client */
   if (!(ep = wl_resource_get_user_data(surface_resource)))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Pixmap Set On Surface");
        return;
     }

   /* make sure it's a wayland pixmap */
   if (e_pixmap_type_get(ep) != E_PIXMAP_TYPE_WL) return;

   /* find the client for this pixmap */
   if (!(ec = e_pixmap_client_get(ep)))
     ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, e_pixmap_window_get(ep));

   if (!ec)
     {
        /* no client found. not internal window. maybe external client app ? */
        if (!(ec = e_client_new(NULL, ep, 0, 0)))
          {
             wl_resource_post_error(surface_resource,
                                    WL_DISPLAY_ERROR_INVALID_OBJECT,
                                    "No Client For Pixmap");
             return;
          }

        ec->netwm.ping = EINA_TRUE;
     }

   /* get the client data */
   if (!(cdata = ec->comp_data))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Data For Client");
        return;
     }

   /* check for existing shell surface */
   if (cdata->shell.surface)
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Client already has shell surface");
        return;
     }

   /* try to create a shell surface */
   if (!(cdata->shell.surface =
         wl_resource_create(client, &wl_shell_surface_interface, 1, id)))
     {
        wl_resource_post_no_memory(surface_resource);
        return;
     }

   wl_resource_set_implementation(cdata->shell.surface,
                                  &_e_shell_surface_interface,
                                  ec, _e_shell_surface_cb_destroy);

   cdata->surface = surface_resource;
   cdata->shell.configure_send = _e_shell_surface_configure_send;
   cdata->shell.configure = _e_shell_surface_configure;
   cdata->shell.ping = _e_shell_surface_ping;
   cdata->shell.map = _e_shell_surface_map;
   cdata->shell.unmap = _e_shell_surface_unmap;
}

static void
_e_xdg_surface_state_add(struct wl_resource *resource, struct wl_array *states, uint32_t state)
{
  uint32_t *s;
  s = wl_array_add(states, sizeof(*s));
  if (s)
    *s = state;
  else
    wl_resource_post_no_memory(resource);

  return;
}

static void
_e_xdg_shell_surface_configure_send(struct wl_resource *resource, uint32_t edges, int32_t width, int32_t height)
{
   E_Client *ec;
   struct wl_array states;
   uint32_t serial;

   DBG("XDG_SHELL: Surface Configure Send: %d \t%d %d\tEdges: %d",
       wl_resource_get_id(resource), width, height, edges);

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   wl_array_init(&states);

   if (ec->fullscreen)
     _e_xdg_surface_state_add(resource, &states, XDG_SURFACE_STATE_FULLSCREEN);
   else if (ec->maximized)
     _e_xdg_surface_state_add(resource, &states, XDG_SURFACE_STATE_MAXIMIZED);
   if (edges != 0)
     _e_xdg_surface_state_add(resource, &states, XDG_SURFACE_STATE_RESIZING);
   if (ec->focused)
     _e_xdg_surface_state_add(resource, &states, XDG_SURFACE_STATE_ACTIVATED);

   serial = wl_display_next_serial(ec->comp->wl_comp_data->wl.disp);
   if (ec->netwm.type != E_WINDOW_TYPE_POPUP_MENU)
     xdg_surface_send_configure(resource, width, height, &states, serial);

   wl_array_release(&states);
}

static void
_e_xdg_shell_surface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   _e_shell_surface_destroy(resource);
}

static void
_e_xdg_shell_surface_cb_parent_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *parent_resource)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* set this client as a transient for parent */
   _e_shell_surface_parent_set(ec, parent_resource);
}

static void
_e_xdg_shell_surface_cb_title_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, const char *title)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* set title */
   eina_stringshare_replace(&ec->icccm.title, title);
   eina_stringshare_replace(&ec->icccm.name, title);
   if (ec->frame) e_comp_object_frame_title_set(ec->frame, title);
}

static void
_e_xdg_shell_surface_cb_app_id_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, const char *id)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   /* use the wl_client to get the pid * and set it in the netwm props */
   wl_client_get_credentials(client, &ec->netwm.pid, NULL, NULL);

   /* set class */
   eina_stringshare_replace(&ec->icccm.class, id);
   /* eina_stringshare_replace(&ec->netwm.name, id); */
   ec->changes.icon = !!ec->icccm.class;
   EC_CHANGED(ec);
}

static void
_e_xdg_shell_surface_cb_window_menu_show(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource EINA_UNUSED, uint32_t serial EINA_UNUSED, int32_t x, int32_t y)
{
   E_Client *ec;
   double timestamp;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

     timestamp = ecore_loop_time_get();
     e_int_client_menu_show(ec, x, y, 0, timestamp);
}

static void
_e_xdg_shell_surface_cb_move(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial EINA_UNUSED)
{
   E_Client *ec;
   E_Comp_Data *cdata;
   E_Binding_Event_Mouse_Button ev;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if ((ec->maximized) || (ec->fullscreen)) return;

   /* get compositor data from seat */
   if (!(cdata = wl_resource_get_user_data(seat_resource)))
     {
        wl_resource_post_error(seat_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Comp_Data for Seat");
        return;
     }

   switch (cdata->ptr.button)
     {
      case BTN_LEFT:
        ev.button = 1;
        break;
      case BTN_MIDDLE:
        ev.button = 2;
        break;
      case BTN_RIGHT:
        ev.button = 3;
        break;
      default:
        ev.button = cdata->ptr.button;
        break;
     }

   e_comp_object_frame_xy_unadjust(ec->frame,
                                   wl_fixed_to_int(cdata->ptr.x),
                                   wl_fixed_to_int(cdata->ptr.y),
                                   &ev.canvas.x, &ev.canvas.y);

   _e_shell_surface_mouse_down_helper(ec, &ev, EINA_TRUE);
}

static void
_e_xdg_shell_surface_cb_resize(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial EINA_UNUSED, uint32_t edges)
{
   E_Client *ec;
   E_Comp_Data *cdata;
   E_Binding_Event_Mouse_Button ev;
   int cx, cy;

   /* DBG("XDG_SHELL: Surface Resize: %d\tEdges: %d",  */
   /*     wl_resource_get_id(resource), edges); */

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if ((edges == 0) || (edges > 15) ||
       ((edges & 3) == 3) || ((edges & 12) == 12)) return;

   if ((ec->maximized) || (ec->fullscreen)) return;

   /* get compositor data from seat */
   if (!(cdata = wl_resource_get_user_data(seat_resource)))
     {
        wl_resource_post_error(seat_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Comp_Data for Seat");
        return;
     }

   cdata->resize.resource = resource;
   cdata->resize.edges = edges;

   cx = wl_fixed_to_int(cdata->ptr.x) - ec->client.x;
   cy = wl_fixed_to_int(cdata->ptr.y) - ec->client.y;
   cdata->ptr.grab_x = wl_fixed_from_int(cx);
   cdata->ptr.grab_y = wl_fixed_from_int(cy);

   switch (cdata->ptr.button)
     {
      case BTN_LEFT:
        ev.button = 1;
        break;
      case BTN_MIDDLE:
        ev.button = 2;
        break;
      case BTN_RIGHT:
        ev.button = 3;
        break;
      default:
        ev.button = cdata->ptr.button;
        break;
     }

   e_comp_object_frame_xy_unadjust(ec->frame,
                                   wl_fixed_to_int(cdata->ptr.x),
                                   wl_fixed_to_int(cdata->ptr.y),
                                   &ev.canvas.x, &ev.canvas.y);

   _e_shell_surface_mouse_down_helper(ec, &ev, EINA_FALSE);
}

static void
_e_xdg_shell_surface_cb_ack_configure(struct wl_client *client EINA_UNUSED, struct wl_resource *resource EINA_UNUSED, uint32_t serial EINA_UNUSED)
{
   /* No-Op */
}

static void
_e_xdg_shell_surface_cb_window_geometry_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   E_Client *ec;

   ec = wl_resource_get_user_data(resource);
   if (!ec)
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                "No Client For Shell Surface");
        return;
     }
   EINA_RECTANGLE_SET(&ec->comp_data->shell.window, x, y, w, h);
   //DBG("XDG_SHELL: Window Geom Set: %d \t%d %d, %d %d", wl_resource_get_id(resource), x, y, w, h);
}

static void
_e_xdg_shell_surface_cb_maximized_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (!ec->lock_user_maximize)
     {
        e_client_maximize(ec, ((e_config->maximize_policy & E_MAXIMIZE_TYPE) |
                               E_MAXIMIZE_BOTH));
        _e_xdg_shell_surface_configure_send(resource, 0, ec->w, ec->h);
     }
}

static void
_e_xdg_shell_surface_cb_maximized_unset(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   e_client_unmaximize(ec, E_MAXIMIZE_BOTH);
   _e_xdg_shell_surface_configure_send(resource, 0, ec->w, ec->h);
}

static void
_e_xdg_shell_surface_cb_fullscreen_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *output_resource EINA_UNUSED)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (!ec->lock_user_fullscreen)
     e_client_fullscreen(ec, e_config->fullscreen_policy);
}

static void
_e_xdg_shell_surface_cb_fullscreen_unset(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (!ec->lock_user_fullscreen)
     e_client_unfullscreen(ec);
}

static void
_e_xdg_shell_surface_cb_minimized_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (!ec->lock_client_iconify)
     e_client_iconify(ec);
}

static const struct xdg_surface_interface _e_xdg_surface_interface =
{
   _e_xdg_shell_surface_cb_destroy,
   _e_xdg_shell_surface_cb_parent_set,
   _e_xdg_shell_surface_cb_title_set,
   _e_xdg_shell_surface_cb_app_id_set,
   _e_xdg_shell_surface_cb_window_menu_show,
   _e_xdg_shell_surface_cb_move,
   _e_xdg_shell_surface_cb_resize,
   _e_xdg_shell_surface_cb_ack_configure,
   _e_xdg_shell_surface_cb_window_geometry_set,
   _e_xdg_shell_surface_cb_maximized_set,
   _e_xdg_shell_surface_cb_maximized_unset,
   _e_xdg_shell_surface_cb_fullscreen_set,
   _e_xdg_shell_surface_cb_fullscreen_unset,
   _e_xdg_shell_surface_cb_minimized_set,
};

static void
_e_xdg_shell_cb_unstable_version(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t version)
{
   if (version > 1)
     wl_resource_post_error(resource, 1, "XDG Version Not Implemented Yet");
}

static void
_e_xdg_shell_surface_configure(struct wl_resource *resource, Evas_Coord x, Evas_Coord y, Evas_Coord w, Evas_Coord h)
{
   E_Client *ec;
   Eina_Bool new_client;

   /* DBG("XDG_SHELL: Surface Configure: %d \t%d %d %d %d",  */
   /*     wl_resource_get_id(resource), x, y, w, h); */

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (ec->parent)
     {
        if ((ec->netwm.type == E_WINDOW_TYPE_MENU) ||
            (ec->netwm.type == E_WINDOW_TYPE_POPUP_MENU) ||
            (ec->netwm.type == E_WINDOW_TYPE_DROPDOWN_MENU))
          {
             x = ec->parent->client.x + ec->comp_data->popup.x;
             y = ec->parent->client.y + ec->comp_data->popup.y;
          }
     }

   /* ensure resize succeeds */
   new_client = ec->new_client;
   ec->new_client = 0;
   e_client_util_move_resize_without_frame(ec, x, y, w, h);
   ec->new_client = new_client;
   /* TODO: ack configure ?? */
}

static void
_e_xdg_shell_surface_ping(struct wl_resource *resource)
{
   E_Client *ec;
   uint32_t serial;

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   serial = wl_display_next_serial(ec->comp->wl_comp_data->wl.disp);
   if (ec->comp->wl_comp_data->shell_interface.xdg_shell)
     xdg_shell_send_ping(ec->comp->wl_comp_data->shell_interface.xdg_shell, serial);
}

static void
_e_xdg_shell_surface_map(struct wl_resource *resource)
{
   E_Client *ec;

   /* DBG("XDG_SHELL: Map Surface: %d", wl_resource_get_id(resource)); */

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if ((!ec->comp_data->mapped) && (e_pixmap_usable_get(ec->pixmap)))
     {
        /* map this surface if needed */
        ec->visible = EINA_TRUE;
        evas_object_show(ec->frame);
        ec->comp_data->mapped = EINA_TRUE;

        /* FIXME: sometimes popup surfaces Do Not raise above their
         * respective parents... */
        /* if (ec->netwm.type == E_WINDOW_TYPE_POPUP_MENU) */
        /*   e_client_raise_latest_set(ec); */
     }
}

static void
_e_xdg_shell_surface_unmap(struct wl_resource *resource)
{
   E_Client *ec;

   /* DBG("XDG_SHELL: Unmap Surface: %d", wl_resource_get_id(resource)); */

   /* get the client for this resource */
   if (!(ec = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Shell Surface");
        return;
     }

   if (ec->comp_data->mapped)
     {
        ec->visible = EINA_FALSE;
        evas_object_hide(ec->frame);
        ec->comp_data->mapped = EINA_FALSE;
     }
}

static void
_e_xdg_shell_cb_surface_get(struct wl_client *client, struct wl_resource *resource EINA_UNUSED, uint32_t id, struct wl_resource *surface_resource)
{
   E_Pixmap *ep;
   E_Client *ec;
   E_Comp_Client_Data *cdata;

   /* DBG("XDG_SHELL: Surface Get %d", wl_resource_get_id(surface_resource)); */

   /* get the pixmap from this surface so we can find the client */
   if (!(ep = wl_resource_get_user_data(surface_resource)))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Pixmap Set On Surface");
        return;
     }

   /* make sure it's a wayland pixmap */
   if (e_pixmap_type_get(ep) != E_PIXMAP_TYPE_WL) return;

   /* find the client for this pixmap */
   if (!(ec = e_pixmap_client_get(ep)))
     ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, e_pixmap_window_get(ep));

   if (!ec)
     {
        pid_t pid;
        int internal = 0;

        /* check if it's internal or external */
        wl_client_get_credentials(client, &pid, NULL, NULL);
        if (pid == getpid()) internal = 1;

        if (!(ec = e_client_new(NULL, ep, 0, internal)))
          {
             wl_resource_post_error(surface_resource,
                                    WL_DISPLAY_ERROR_INVALID_OBJECT,
                                    "No Client For Pixmap");
             return;
          }
     }

   ec->netwm.ping = EINA_TRUE;

   /* get the client data */
   if (!(cdata = ec->comp_data))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Data For Client");
        return;
     }

   /* check for existing shell surface */
   if (cdata->shell.surface)
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Client already has XDG shell surface");
        return;
     }

   /* try to create a shell surface */
   if (!(cdata->shell.surface =
         wl_resource_create(client, &xdg_surface_interface, 1, id)))
     {
        ERR("Could not create xdg shell surface");
        wl_resource_post_no_memory(surface_resource);
        return;
     }

   wl_resource_set_implementation(cdata->shell.surface,
                                  &_e_xdg_surface_interface, ec,
                                  _e_shell_surface_cb_destroy);

   cdata->surface = surface_resource;
   cdata->shell.configure_send = _e_xdg_shell_surface_configure_send;
   cdata->shell.configure = _e_xdg_shell_surface_configure;
   cdata->shell.ping = _e_xdg_shell_surface_ping;
   cdata->shell.map = _e_xdg_shell_surface_map;
   cdata->shell.unmap = _e_xdg_shell_surface_unmap;

   /* set toplevel client properties */
   ec->icccm.accepts_focus = 1;
   ec->borderless = !ec->internal_elm_win;
   ec->lock_border = EINA_TRUE;
   ec->border.changed = ec->changes.border = !ec->borderless;
   ec->netwm.type = E_WINDOW_TYPE_NORMAL;
   ec->comp_data->set_win_type = EINA_TRUE;

   E_Comp_Client_Data *p_cdata = e_pixmap_cdata_get(ep);
   EINA_SAFETY_ON_NULL_RETURN(p_cdata);
   ec->icccm.accepts_focus = ec->icccm.take_focus = p_cdata->accepts_focus;
}

static void
_e_xdg_shell_popup_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   _e_shell_surface_destroy(resource);
}

static const struct xdg_popup_interface _e_xdg_popup_interface =
{
   _e_xdg_shell_popup_cb_destroy,
};

static void
_e_xdg_shell_cb_popup_get(struct wl_client *client, struct wl_resource *resource EINA_UNUSED, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent_resource, struct wl_resource *seat_resource EINA_UNUSED, uint32_t serial EINA_UNUSED, int32_t x, int32_t y, uint32_t flags EINA_UNUSED)
{
   E_Pixmap *ep;
   E_Client *ec;
   E_Comp_Client_Data *cdata;

   /* DBG("XDG_SHELL: Popup Get"); */
   /* DBG("\tSurface: %d", wl_resource_get_id(surface_resource)); */
   /* DBG("\tParent Surface: %d", wl_resource_get_id(parent_resource)); */
   /* DBG("\tLocation: %d %d", x, y); */

   /* get the pixmap from this surface so we can find the client */
   if (!(ep = wl_resource_get_user_data(surface_resource)))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Pixmap Set On Surface");
        return;
     }

   /* make sure it's a wayland pixmap */
   if (e_pixmap_type_get(ep) != E_PIXMAP_TYPE_WL) return;

   /* find the client for this pixmap */
   if (!(ec = e_pixmap_client_get(ep)))
     ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, e_pixmap_window_get(ep));

   if (!ec)
     {
        /* no client found. create one */
        if (!(ec = e_client_new(NULL, ep, 0, 1)))
          {
             wl_resource_post_error(surface_resource,
                                    WL_DISPLAY_ERROR_INVALID_OBJECT,
                                    "No Client For Pixmap");
             return;
          }

        /* e_pixmap_ref(ep); */
     }

   /* get the client data */
   if (!(cdata = ec->comp_data))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Data For Client");
        return;
     }

   /* check for existing shell surface */
   if (cdata->shell.surface)
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Client already has shell popup surface");
        return;
     }

   /* check for the parent surface */
   if (!parent_resource)
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Popup requires a parent shell surface");
        return;
     }

   /* try to create a shell surface */
   if (!(cdata->shell.surface =
         wl_resource_create(client, &xdg_popup_interface, 1, id)))
     {
        ERR("Could not create xdg shell surface");
        wl_resource_post_no_memory(surface_resource);
        return;
     }

   wl_resource_set_implementation(cdata->shell.surface,
                                  &_e_xdg_popup_interface, ec, NULL);

   cdata->surface = surface_resource;
   cdata->shell.configure_send = _e_xdg_shell_surface_configure_send;
   cdata->shell.configure = _e_xdg_shell_surface_configure;
   cdata->shell.ping = _e_xdg_shell_surface_ping;
   cdata->shell.map = _e_xdg_shell_surface_map;
   cdata->shell.unmap = _e_xdg_shell_surface_unmap;

   ec->override = 1;
   ec->borderless = !ec->internal_elm_win;
   ec->lock_border = EINA_TRUE;
   ec->border.changed = ec->changes.border = !ec->borderless;
   ec->changes.icon = !!ec->icccm.class;
   ec->netwm.type = E_WINDOW_TYPE_POPUP_MENU;
   ec->comp_data->set_win_type = EINA_TRUE;
   evas_object_layer_set(ec->frame, E_LAYER_CLIENT_POPUP);
   e_client_focus_stack_set(eina_list_remove(e_client_focus_stack_get(), ec));

   /* set this client as a transient for parent */
   _e_shell_surface_parent_set(ec, parent_resource);

   if (ec->parent)
     {
        cdata->popup.x = E_CLAMP(x, 0, ec->parent->client.w);
        cdata->popup.y = E_CLAMP(y, 0, ec->parent->client.h);
     }
   else
     {
        cdata->popup.x = x;
        cdata->popup.y = y;
     }

}

static void
_e_xdg_shell_cb_pong(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t serial EINA_UNUSED)
{
   E_Client *ec;

   if ((ec = wl_resource_get_user_data(resource)))
     {
        ec->ping_ok = EINA_TRUE;
        ec->hung = EINA_FALSE;
     }
}

static void
_e_tz_res_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_resource_interface _e_tz_res_interface =
{
   _e_tz_res_cb_destroy
};

static void
_e_tz_surf_ext_cb_tz_res_get(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface)
{
   struct wl_resource *res;
   E_Pixmap *ep;
   uint32_t res_id;

   /* get the pixmap from this surface so we can find the client */
   if (!(ep = wl_resource_get_user_data(surface)))
     {
        wl_resource_post_error(surface,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Pixmap Set On Surface");
        return;
     }

   /* make sure it's a wayland pixmap */
   if (e_pixmap_type_get(ep) != E_PIXMAP_TYPE_WL) return;

   /* find the window id for this pixmap */
   res_id = e_pixmap_res_id_get(ep);

   DBG("tizen resource id %" PRIu32, res_id);

   /* try to create a tizen_gid */
   if (!(res = wl_resource_create(client,
                                  &tizen_resource_interface,
                                  wl_resource_get_version(resource),
                                  id)))
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   wl_resource_set_implementation(res,
                                  &_e_tz_res_interface,
                                  ep,
                                  NULL);

   tizen_resource_send_resource_id(res, res_id);
}

static void
_e_tz_transient_for_cb_set(struct wl_client *client, struct wl_resource *resource, uint32_t child_id, uint32_t parent_id)
{
   E_Client *ec, *pc;
   struct wl_resource *parent_res;

   DBG("chid_id: %" PRIu32 ", parent_id: %" PRIu32, child_id, parent_id);

   ec = e_pixmap_find_client_by_res_id(child_id);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   pc = e_pixmap_find_client_by_res_id(parent_id);
   EINA_SAFETY_ON_NULL_RETURN(pc);
   EINA_SAFETY_ON_NULL_RETURN(pc->comp_data);

   parent_res = pc->comp_data->surface;
   _e_shell_surface_parent_set(ec, parent_res);
   tizen_transient_for_send_done(resource, child_id);

   EC_CHANGED(ec);
}

static void
_e_tz_transient_for_cb_unset(struct wl_client *client, struct wl_resource *resource, uint32_t child_id)
{
   E_Client *ec;

   DBG("chid_id: %" PRIu32, child_id);

   ec = e_pixmap_find_client_by_res_id(child_id);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   _e_shell_surface_parent_set(ec, NULL);
   tizen_transient_for_send_done(resource, child_id);

   EC_CHANGED(ec);
}

static const struct tizen_surface_extension_interface  _e_tz_surf_ext_interface =
{
   _e_tz_surf_ext_cb_tz_res_get,
};

static const struct tizen_transient_for_interface _e_tz_transient_for_interface =
{
   _e_tz_transient_for_cb_set,
   _e_tz_transient_for_cb_unset,
};

static const struct wl_shell_interface _e_shell_interface =
{
   _e_shell_cb_shell_surface_get
};

static const struct xdg_shell_interface _e_xdg_shell_interface =
{
   _e_xdg_shell_cb_unstable_version,
   _e_xdg_shell_cb_surface_get,
   _e_xdg_shell_cb_popup_get,
   _e_xdg_shell_cb_pong
};

static void
_e_xdg_shell_cb_unbind(struct wl_resource *resource)
{
   E_Comp_Data *cdata;

   if (!(cdata = wl_resource_get_user_data(resource))) return;

   cdata->shell_interface.xdg_shell = NULL;
}

static int
_e_xdg_shell_cb_dispatch(const void *implementation EINA_UNUSED, void *target, uint32_t opcode, const struct wl_message *message EINA_UNUSED, union wl_argument *args)
{
   E_Comp_Data *cdata;
   struct wl_resource *res;

   if (!(res = target)) return 0;
   if (!(cdata = wl_resource_get_user_data(res))) return 0;

   if (opcode != 0)
     {
        wl_resource_post_error(res, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Must call use_unstable_version first");
        return 0;
     }

   if (args[0].i != XDG_SERVER_VERSION)
     {
        wl_resource_post_error(res, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Incompatible versions. "
                               "Server: %d, Client: %d",
                               XDG_SERVER_VERSION, args[0].i);
        return 0;
     }

   wl_resource_set_implementation(res, &_e_xdg_shell_interface, cdata,
                                  _e_xdg_shell_cb_unbind);

   return 1;
}

static void
_e_shell_cb_unbind(struct wl_resource *resource)
{
   E_Comp_Data *cdata;

   if (!(cdata = wl_resource_get_user_data(resource))) return;

   cdata->shell_interface.shell = NULL;
}

static void
_e_shell_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp_Data *cdata;
   struct wl_resource *res;

   if (!(cdata = data))
     {
        wl_client_post_no_memory(client);
        return;
     }

   if (!(res = wl_resource_create(client, &wl_shell_interface, MIN(version, 1), id)))
     {
        wl_client_post_no_memory(client);
        return;
     }

   cdata->shell_interface.shell = res;
   wl_resource_set_implementation(res, &_e_shell_interface, cdata,
                                  _e_shell_cb_unbind);
}

static void
_e_xdg_shell_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp_Data *cdata;
   struct wl_resource *res;

   if (!(cdata = data))
     {
        wl_client_post_no_memory(client);
        return;
     }

   if (!(res = wl_resource_create(client, &xdg_shell_interface, MIN(version, 1), id)))
     {
        wl_client_post_no_memory(client);
        return;
     }

   cdata->shell_interface.xdg_shell = res;
   wl_resource_set_dispatcher(res, _e_xdg_shell_cb_dispatch, NULL, cdata, NULL);
}

static void
_e_tz_surf_ext_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp_Data *cdata;
   struct wl_resource *res;

   if (!(cdata = data))
     {
        wl_client_post_no_memory(client);
        return;
     }

   if (!(res = wl_resource_create(client,
                                  &tizen_surface_extension_interface,
                                  MIN(version, 1),
                                  id)))
     {
        ERR("Could not create tizen_ext resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_tz_surf_ext_interface, cdata, NULL);
}

static void
_e_tz_transient_for_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp_Data *cdata;
   struct wl_resource *res;

   if (!(cdata = data))
     {
        wl_client_post_no_memory(client);
        return;
     }

   if (!(res = wl_resource_create(client,
                                  &tizen_transient_for_interface,
                                  MIN(version, 1),
                                  id)))
     {
        ERR("Could not create tizen_transient_for resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_tz_transient_for_interface, cdata, NULL);
}

static void
_e_tz_subsurface_cb_place_below_parent(struct wl_client *client, struct wl_resource *resource, struct wl_resource *subsurface)
{
   E_Client *ec;
   E_Client *epc;
   E_Comp_Wl_Subsurf_Data *sdata;

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(subsurface)))
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Invalid subsurface");
        return;
     }

   sdata = ec->comp_data->sub.data;
   if (!sdata)
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Not subsurface");
        return;
     }

   epc = sdata->parent;
   EINA_SAFETY_ON_NULL_RETURN(epc);

   /* check if a subsurface has already placed below a parent */
   if (eina_list_data_find(epc->comp_data->sub.below_list, ec)) return;

   epc->comp_data->sub.list = eina_list_remove(epc->comp_data->sub.list, ec);
   epc->comp_data->sub.list_pending = eina_list_remove(epc->comp_data->sub.list_pending, ec);
   epc->comp_data->sub.below_list_pending = eina_list_append(epc->comp_data->sub.below_list_pending, ec);
   epc->comp_data->sub.list_changed = EINA_TRUE;
}

static const struct tizen_subsurface_interface  _e_tz_subsurface_interface =
{
   _e_tz_subsurface_cb_place_below_parent,
};

static void
_e_tz_subsurface_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp_Data *cdata;
   struct wl_resource *res;

   if (!(cdata = data))
     {
        wl_client_post_no_memory(client);
        return;
     }

   if (!(res = wl_resource_create(client,
                                  &tizen_subsurface_interface,
                                  MIN(version, 1),
                                  id)))
     {
        ERR("Could not create tizen_subsurface resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_tz_subsurface_interface, cdata, NULL);
}

EAPI E_Module_Api e_modapi = { E_MODULE_API_VERSION, "Wl_Desktop_Shell" };

EAPI void *
e_modapi_init(E_Module *m)
{
   E_Comp *comp;
   E_Comp_Data *cdata;

   /* try to get the current compositor */
   if (!(comp = e_comp)) return NULL;

   /* make sure it's a wayland compositor */
   /* if (comp->comp_type != E_PIXMAP_TYPE_WL) return NULL; */

   /* try to get the compositor data */
   if (!(cdata = comp->wl_comp_data)) return NULL;

   /* try to create global shell interface */
   if (!wl_global_create(cdata->wl.disp, &wl_shell_interface, 1,
                         cdata, _e_shell_cb_bind))
     {
        ERR("Could not create shell global: %m");
        return NULL;
     }

   /* try to create global xdg_shell interface */
   if (!wl_global_create(cdata->wl.disp, &xdg_shell_interface, 1,
                         cdata, _e_xdg_shell_cb_bind))
     {
        ERR("Could not create xdg_shell global: %m");
        return NULL;
     }

   e_scaler_init();

   /* try to create global tizen surface extention interface */
   if (!wl_global_create(cdata->wl.disp,
                         &tizen_surface_extension_interface,
                         1,
                         cdata,
                         _e_tz_surf_ext_cb_bind))
     {
        ERR("Could not create tizen_surface_extension to wayland globals: %m");
        return NULL;
     }
   /* try to create global tizen transient_for interface */
   if (!wl_global_create(cdata->wl.disp,
                         &tizen_transient_for_interface,
                         1,
                         cdata,
                         _e_tz_transient_for_cb_bind))
     {
        ERR("Could not create tizen_transient_for to wayland globals: %m");
        return NULL;
     }

   /* try to create global tizen subsurface interface */
   if (!wl_global_create(cdata->wl.disp,
                         &tizen_subsurface_interface,
                         1,
                         cdata,
                         _e_tz_subsurface_cb_bind))
     {
        ERR("Could not create tizen_surface_extension to wayland globals: %m");
        return NULL;
     }

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   return 1;
}
