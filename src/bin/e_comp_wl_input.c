#define EXECUTIVE_MODE_ENABLED
#define E_COMP_WL
#include "e.h"
#include <sys/mman.h>

E_API int E_EVENT_TEXT_INPUT_PANEL_VISIBILITY_CHANGE = -1;

static void
_e_comp_wl_input_update_seat_caps(void)
{
   Eina_List *l;
   struct wl_resource *res;
   enum wl_seat_capability caps = 0;

   if (e_comp_wl->ptr.enabled)
     caps |= WL_SEAT_CAPABILITY_POINTER;
   if (e_comp_wl->kbd.enabled)
     caps |= WL_SEAT_CAPABILITY_KEYBOARD;
   if (e_comp_wl->touch.enabled)
     caps |= WL_SEAT_CAPABILITY_TOUCH;

   EINA_LIST_FOREACH(e_comp_wl->seat.resources, l, res)
        wl_seat_send_capabilities(res, caps);
}

static void
_e_comp_wl_input_pointer_map(struct wl_resource *resource)
{
   E_Pixmap *ep;
   E_Client *ec;

   if (!(ep = wl_resource_get_user_data(resource))) return;
   if (!(ec = e_pixmap_client_get(ep))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   e_pointer_object_set(e_comp->pointer, ec->frame, ec->x, ec->y);
   ec->comp_data->mapped = EINA_TRUE;
}

static void
_e_comp_wl_input_pointer_configure(struct wl_resource *resource,
                                   Evas_Coord x, Evas_Coord y,
                                   Evas_Coord w, Evas_Coord h)
{
   E_Pixmap *ep;
   E_Client *ec;

   if (!(ep = wl_resource_get_user_data(resource))) return;
   if (!(ec = e_pixmap_client_get(ep))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   e_client_util_resize_without_frame(ec, w, h);
}

static void
_e_comp_wl_input_cb_resource_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_input_pointer_cb_cursor_set(struct wl_client *client, struct wl_resource *resource EINA_UNUSED, uint32_t serial EINA_UNUSED, struct wl_resource *surface_resource, int32_t x, int32_t y)
{
   E_Client *ec;
   Eina_Bool got_mouse = EINA_FALSE;
   int cursor_w = 0, cursor_h = 0;

   E_CLIENT_FOREACH(ec)
     {
       if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) continue;
       if (!ec->comp_data->surface) continue;
       if (client != wl_resource_get_client(ec->comp_data->surface)) continue;
       got_mouse = EINA_TRUE;
       break;
     }
   if (!got_mouse) return;
   if (!surface_resource)
     {
        e_pointer_object_set(e_comp->pointer, NULL, x, y);
        return;
     }
<<<<<<< HEAD
   if (!(ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, (uintptr_t)surface_resource)))
     {
        Eina_List *l;
        E_Pixmap *ep = NULL;

        ep = e_pixmap_find(E_PIXMAP_TYPE_WL, surface_resource);
        if (!ep) ep = e_pixmap_new(E_PIXMAP_TYPE_WL, surface_resource);
        EINA_SAFETY_ON_NULL_RETURN(ep);

        ec = e_client_new(NULL, ep, 1, 0);
        if (!ec) return;
        ec->lock_focus_out = ec->layer_block = ec->visible = ec->override = 1;
        ec->new_client = 0;
        e_comp->new_clients--;
=======
   ec = wl_resource_get_user_data(surface_resource);
   if (!ec->re_manage)
     {
        ec->re_manage = 1;
        ec->ignored = 0;

        ec->lock_focus_out = ec->layer_block = ec->visible = ec->override = 1;
>>>>>>> upstream
        ec->icccm.title = eina_stringshare_add("noshadow");
        ec->icccm.window_role = eina_stringshare_add("wl_pointer-cursor");
        evas_object_pass_events_set(ec->frame, 1);
<<<<<<< HEAD
        ec->client.w = ec->client.h = 1;
        l = e_client_focus_stack_get();
        e_client_focus_stack_set(eina_list_remove(l, ec));

        /* Set fuctions to prevent unwanted handling by shell */
        ec->comp_data->shell.surface = surface_resource;
        ec->comp_data->shell.configure = _e_comp_wl_input_pointer_configure;
        ec->comp_data->shell.map = _e_comp_wl_input_pointer_map;
=======
        e_client_focus_stack_set(eina_list_remove(e_client_focus_stack_get(), ec));
        EC_CHANGED(ec);
>>>>>>> upstream
     }

   /* ignore cursor changes during resize/move I guess */
   if (e_client_action_get()) return;

   evas_object_geometry_get(ec->frame, NULL, NULL, &cursor_w, &cursor_h);
   if ((cursor_w == 0) || (cursor_h == 0))
     return;

   e_pointer_object_set(e_comp->pointer, ec->frame, x, y);
}

static const struct wl_pointer_interface _e_pointer_interface =
{
   _e_comp_wl_input_pointer_cb_cursor_set,
   _e_comp_wl_input_cb_resource_destroy
};

static const struct wl_keyboard_interface _e_keyboard_interface =
{
   _e_comp_wl_input_cb_resource_destroy
};

static const struct wl_touch_interface _e_touch_interface =
{
   _e_comp_wl_input_cb_resource_destroy
};

static void
_e_comp_wl_input_cb_pointer_unbind(struct wl_resource *resource)
{
   e_comp_wl->ptr.resources =
     eina_list_remove(e_comp_wl->ptr.resources, resource);
}

static void
_e_comp_wl_input_cb_pointer_get(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   struct wl_resource *res;
   E_Client *ec;
   uint32_t serial;
   int cx, cy;

   /* try to create pointer resource */
   res = wl_resource_create(client, &wl_pointer_interface,
                            wl_resource_get_version(resource), id);
   if (!res)
     {
        ERR("Could not create pointer on seat %s: %m",
            e_comp_wl->seat.name);
        wl_client_post_no_memory(client);
        return;
     }

   e_comp_wl->ptr.resources =
     eina_list_append(e_comp_wl->ptr.resources, res);
   wl_resource_set_implementation(res, &_e_pointer_interface,
                                  e_comp->wl_comp_data,
                                 _e_comp_wl_input_cb_pointer_unbind);
}

static void
_e_comp_wl_input_cb_keyboard_unbind(struct wl_resource *resource)
{
   Eina_List *l, *ll;
   struct wl_resource *res;

   e_comp_wl->kbd.resources =
     eina_list_remove(e_comp_wl->kbd.resources, resource);
   EINA_LIST_FOREACH_SAFE(e_comp_wl->kbd.focused, l, ll, res)
     if (res == resource)
       e_comp_wl->kbd.focused =
         eina_list_remove_list(e_comp_wl->kbd.focused, l);
}

void
e_comp_wl_input_keyboard_enter_send(E_Client *ec)
{
   struct wl_resource *res;
   Eina_List *l;
   uint32_t serial;

   if (!ec->comp_data->surface) return;

   if (!e_comp_wl->kbd.focused) return;

   e_comp_wl_input_keyboard_modifiers_serialize();

   serial = wl_display_next_serial(e_comp_wl->wl.disp);

   EINA_LIST_FOREACH(e_comp_wl->kbd.focused, l, res)
     {
        wl_keyboard_send_enter(res, serial, ec->comp_data->surface,
                               &e_comp_wl->kbd.keys);
        wl_keyboard_send_modifiers(res, serial,
                                   e_comp_wl->kbd.mod_depressed,
                                   e_comp_wl->kbd.mod_latched,
                                   e_comp_wl->kbd.mod_locked,
                                   e_comp_wl->kbd.mod_group);
     }
}

static void
_e_comp_wl_input_cb_keyboard_get(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   E_Client *focused;
   struct wl_resource *res;
   E_Client *ec;
   struct wl_client *wc;
   uint32_t serial, *k;
   Eina_List *l;

   /* try to create keyboard resource */
   res = wl_resource_create(client, &wl_keyboard_interface,
                            wl_resource_get_version(resource), id);
   if (!res)
     {
        ERR("Could not create keyboard on seat %s: %m",
            e_comp_wl->seat.name);
        wl_client_post_no_memory(client);
        return;
     }

   e_comp_wl->kbd.resources =
     eina_list_append(e_comp_wl->kbd.resources, res);
   wl_resource_set_implementation(res, &_e_keyboard_interface,
                                  e_comp->wl_comp_data,
                                  _e_comp_wl_input_cb_keyboard_unbind);

   /* send current repeat_info */
   if (wl_resource_get_version(res) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
<<<<<<< HEAD
     wl_keyboard_send_repeat_info(res, cdata->kbd.repeat_rate, cdata->kbd.repeat_delay);

   /* send current keymap */
   wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                           cdata->xkb.fd, cdata->xkb.size);

   /* if client has focus, send keyboard enter */
   E_CLIENT_FOREACH(e_comp, ec)
     {
        if (!ec->comp_data->surface) continue;
        if (client != wl_resource_get_client(ec->comp_data->surface)) continue;

          {
             /* update keyboard modifier state */
             wl_array_for_each(k, &e_comp->wl_comp_data->kbd.keys)
                e_comp_wl_input_keyboard_state_update(e_comp->wl_comp_data, *k, EINA_TRUE);
             ec->comp_data->focus_update = 1;
             if (!ec->comp_data->surface) return;

             /* send keyboard_enter to all keyboard resources */
             wc = wl_resource_get_client(ec->comp_data->surface);
             serial = wl_display_next_serial(e_comp->wl_comp_data->wl.disp);
             EINA_LIST_FOREACH(e_comp->wl_comp_data->kbd.resources, l, res)
               {
                  if (wl_resource_get_client(res) != wc) continue;
                  wl_keyboard_send_enter(res, serial, ec->comp_data->surface,
                                         &e_comp->wl_comp_data->kbd.keys);
                  ec->comp_data->focus_update = 0;
               }
          }
     }
}

static void
_e_comp_wl_input_cb_touch_unbind(struct wl_resource *resource)
{
   E_Comp_Data *cdata;

   /* get compositor data */
   if (!(cdata = wl_resource_get_user_data(resource))) return;

   cdata->touch.resources = eina_list_remove(cdata->touch.resources, resource);
=======
     wl_keyboard_send_repeat_info(res, e_config->keyboard.repeat_rate, e_config->keyboard.repeat_delay);

   /* send current keymap */
   wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                           e_comp_wl->xkb.fd,
                           e_comp_wl->xkb.size);

   /* if the client owns the focused surface, we need to send an enter */
   focused = e_client_focused_get();
   if (!focused) return;

   if (client != wl_resource_get_client(focused->comp_data->surface)) return;
   e_comp_wl->kbd.focused = eina_list_append(e_comp_wl->kbd.focused, res);

   e_comp_wl_input_keyboard_enter_send(focused);
}

static void
_e_comp_wl_input_cb_touch_unbind(struct wl_resource *resource)
{
   e_comp_wl->touch.resources =
     eina_list_remove(e_comp_wl->touch.resources, resource);
>>>>>>> upstream
}

static void
_e_comp_wl_input_cb_touch_get(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t id EINA_UNUSED)
{
    struct wl_resource *res;

    /* try to create pointer resource */
    res = wl_resource_create(client, &wl_touch_interface,
                             wl_resource_get_version(resource), id);
    if (!res)
      {
         ERR("Could not create touch on seat %s: %m",
             e_comp_wl->seat.name);
         wl_client_post_no_memory(client);
         return;
      }

    e_comp_wl->touch.resources =
     eina_list_append(e_comp_wl->touch.resources, res);
    wl_resource_set_implementation(res, &_e_touch_interface,
                                   e_comp->wl_comp_data,
                                  _e_comp_wl_input_cb_touch_unbind);
}

static const struct wl_seat_interface _e_seat_interface =
{
   _e_comp_wl_input_cb_pointer_get,
   _e_comp_wl_input_cb_keyboard_get,
   _e_comp_wl_input_cb_touch_get,
};

static void
_e_comp_wl_input_cb_unbind_seat(struct wl_resource *resource)
{
   e_comp_wl->seat.resources =
     eina_list_remove(e_comp_wl->seat.resources, resource);
}

static void
_e_comp_wl_input_cb_bind_seat(struct wl_client *client, void *data EINA_UNUSED, uint32_t version, uint32_t id)
{
   struct wl_resource *res;

   res = wl_resource_create(client, &wl_seat_interface, version, id);
   if (!res)
     {
        ERR("Could not create seat resource: %m");
        return;
     }

   /* store version of seat interface for reuse in updating capabilities */
   e_comp_wl->seat.version = version;
   e_comp_wl->seat.resources =
     eina_list_append(e_comp_wl->seat.resources, res);

<<<<<<< HEAD
   wl_resource_set_implementation(res, &_e_seat_interface,
                                  e_comp->wl_comp_data,
                                  _e_comp_wl_input_cb_unbind_seat);

   _e_comp_wl_input_update_seat_caps();
   if (e_comp_wl->seat.version >= WL_SEAT_NAME_SINCE_VERSION)
     wl_seat_send_name(res, e_comp_wl->seat.name);
}

static void
_e_comp_wl_input_keymap_cache_create(const char *keymap_path, char *keymap_data)
{
   FILE *file = NULL;

   if (EINA_FALSE == e_config->xkb.use_cache) return;

   if (keymap_path)
     {
        file = fopen(keymap_path, "w");
        EINA_SAFETY_ON_NULL_RETURN(file);

        if (fputs(keymap_data, file) < 0)
          {
             WRN("Failed  to write keymap file: %s\n", keymap_path);
             fclose(file);
             unlink(keymap_path);
          }
        else
          {
             INF("Success to make keymap file: %s\n", keymap_path);
             fclose(file);
          }
     }
}

static int
_e_comp_wl_input_keymap_fd_get(off_t size)
{
   int fd = 0, blen = 0, len = 0;
   const char *path;
   char tmp[PATH_MAX];
   long flags;

   blen = sizeof(tmp) - 1;

   if (!(path = getenv("XDG_RUNTIME_DIR")))
     return -1;

   len = strlen(path);
   if (len < blen)
     {
        strcpy(tmp, path);
        strcat(tmp, "/e-wl-keymap-XXXXXX");
     }
   else
     return -1;

   if ((fd = mkstemp(tmp)) < 0) return -1;

   flags = fcntl(fd, F_GETFD);
   if (flags < 0)
     {
        close(fd);
        return -1;
     }

   if (fcntl(fd, F_SETFD, (flags | FD_CLOEXEC)) == -1)
     {
        close(fd);
        return -1;
     }

   if (ftruncate(fd, size) < 0)
     {
        close(fd);
        return -1;
     }

   unlink(tmp);
   return fd;
}

static void
_e_comp_wl_input_keymap_update(struct xkb_keymap *keymap)
{
   char *tmp;
   xkb_mod_mask_t latched = 0, locked = 0, group = 0;
   struct wl_resource *res;
   Eina_List *l;

   /* unreference any existing keymap */
   if (e_comp_wl->xkb.keymap)
     xkb_map_unref(e_comp_wl->xkb.keymap);

   /* unmap any existing keyboard area */
   if (e_comp_wl->xkb.area)
     munmap(e_comp_wl->xkb.area, e_comp_wl->xkb.size);
   if (e_comp_wl->xkb.fd >= 0) close(e_comp_wl->xkb.fd);

   /* unreference any existing keyboard state */
   if (e_comp_wl->xkb.state)
     {
        latched =
          xkb_state_serialize_mods(e_comp_wl->xkb.state,
                                   XKB_STATE_MODS_LATCHED);
        locked =
          xkb_state_serialize_mods(e_comp_wl->xkb.state,
                                   XKB_STATE_MODS_LOCKED);
        group =
          xkb_state_serialize_layout(e_comp_wl->xkb.state,
                                     XKB_STATE_LAYOUT_EFFECTIVE);
        xkb_state_unref(e_comp_wl->xkb.state);
     }

   /* create a new xkb state */
   e_comp_wl->xkb.state = xkb_state_new(keymap);

   if ((latched) || (locked) || (group))
     xkb_state_update_mask(e_comp_wl->xkb.state, 0,
                           latched, locked, 0, 0, group);

   /* increment keymap reference */
   e_comp_wl->xkb.keymap = keymap;

   /* fetch updated modifiers */
   e_comp_wl->kbd.mod_shift =
     xkb_map_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
   e_comp_wl->kbd.mod_caps =
     xkb_map_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
   e_comp_wl->kbd.mod_ctrl =
     xkb_map_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
   e_comp_wl->kbd.mod_alt =
     xkb_map_mod_get_index(keymap, XKB_MOD_NAME_ALT);
   e_comp_wl->kbd.mod_super =
     xkb_map_mod_get_index(keymap, XKB_MOD_NAME_LOGO);

   if (!(tmp = xkb_map_get_as_string(keymap)))
     {
        ERR("Could not get keymap string");
        return;
     }

   e_comp_wl->xkb.size = strlen(tmp) + 1;
   e_comp_wl->xkb.fd =
     _e_comp_wl_input_keymap_fd_get(e_comp_wl->xkb.size);
   if (e_comp_wl->xkb.fd < 0)
     {
        ERR("Could not create keymap file");
        free(tmp);
        return;
     }

   _e_comp_wl_input_keymap_cache_create(keymap_path, tmp);

   e_comp_wl->xkb.area =
     mmap(NULL, e_comp_wl->xkb.size, (PROT_READ | PROT_WRITE),
          MAP_SHARED, e_comp_wl->xkb.fd, 0);
   if (e_comp_wl->xkb.area == MAP_FAILED)
     {
        ERR("Failed to mmap keymap area: %m");
        free(tmp);
        return;
     }

   strcpy(e_comp_wl->xkb.area, tmp);
   free(tmp);

   /* send updated keymap */
   EINA_LIST_FOREACH(e_comp_wl->kbd.resources, l, res)
     wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                             e_comp_wl->xkb.fd,
                             e_comp_wl->xkb.size);

   /* update modifiers */
   e_comp_wl_input_keyboard_modifiers_update();
}

EINTERN Eina_Bool
e_comp_wl_input_init(void)
{
   /* set default seat name */
   if (!e_comp_wl->seat.name)
     e_comp_wl->seat.name = "default";

   e_comp_wl->xkb.fd = -1;

   /* get default keyboard repeat rate/delay from configuration */
   e_comp_wl->kbd.repeat_delay = e_config->keyboard.repeat_delay;
   e_comp_wl->kbd.repeat_rate = e_config->keyboard.repeat_rate;

   /* check for valid repeat_delay and repeat_rate value */
   /* if invalid, set the default value of repeat delay and rate value */
   if (e_comp_wl->kbd.repeat_delay < 0) e_comp_wl->kbd.repeat_delay = 400;
   if (e_comp_wl->kbd.repeat_delay < 0) e_comp_wl->kbd.repeat_rate = 25;

   /* create the global resource for input seat */
   e_comp_wl->seat.global =
     wl_global_create(e_comp_wl->wl.disp, &wl_seat_interface, 4,
                      e_comp->wl_comp_data, _e_comp_wl_input_cb_bind_seat);
   if (!e_comp_wl->seat.global)
     {
        ERR("Could not create global for seat: %m");
        return EINA_FALSE;
     }

   wl_array_init(&e_comp_wl->kbd.keys);

   E_EVENT_TEXT_INPUT_PANEL_VISIBILITY_CHANGE = ecore_event_type_new();

   return EINA_TRUE;
}

EINTERN void
e_comp_wl_input_shutdown(void)
{
   struct wl_resource *res;

   /* destroy pointer resources */
   EINA_LIST_FREE(e_comp_wl->ptr.resources, res)
     wl_resource_destroy(res);

   /* destroy keyboard resources */
   EINA_LIST_FREE(e_comp_wl->kbd.resources, res)
     wl_resource_destroy(res);
   e_comp_wl->kbd.resources = eina_list_free(e_comp_wl->kbd.resources);

   /* destroy touch resources */
   EINA_LIST_FREE(e_comp_wl->touch.resources, res)
     wl_resource_destroy(res);

   /* destroy e_comp_wl->kbd.keys array */
   wl_array_release(&e_comp_wl->kbd.keys);

   /* unmap any existing keyboard area */
   if (e_comp_wl->xkb.area)
     munmap(e_comp_wl->xkb.area, e_comp_wl->xkb.size);
   if (e_comp_wl->xkb.fd >= 0) close(e_comp_wl->xkb.fd);

   /* unreference any existing keyboard state */
   if (e_comp_wl->xkb.state)
     xkb_state_unref(e_comp_wl->xkb.state);

   /* unreference any existing keymap */
   if (e_comp_wl->xkb.keymap)
     xkb_map_unref(e_comp_wl->xkb.keymap);

   /* unreference any existing context */
   if (e_comp_wl->xkb.context)
     xkb_context_unref(e_comp_wl->xkb.context);

   /* destroy the global seat resource */
   if (e_comp_wl->seat.global)
     wl_global_destroy(e_comp_wl->seat.global);
   e_comp_wl->seat.global = NULL;
}

EINTERN Eina_Bool
e_comp_wl_input_pointer_check(struct wl_resource *res)
{
   return wl_resource_instance_of(res, &wl_pointer_interface,
                                  &_e_pointer_interface);
}

EINTERN Eina_Bool
e_comp_wl_input_keyboard_check(struct wl_resource *res)
{
   return wl_resource_instance_of(res, &wl_keyboard_interface,
                                  &_e_keyboard_interface);
}

EINTERN Eina_Bool
e_comp_wl_input_keyboard_modifiers_serialize(void)
{
   Eina_Bool changed = EINA_FALSE;
   xkb_mod_mask_t mod;
   xkb_layout_index_t grp;

   mod = xkb_state_serialize_mods(e_comp_wl->xkb.state,
                              XKB_STATE_DEPRESSED);
   changed |= mod != e_comp_wl->kbd.mod_depressed;
   e_comp_wl->kbd.mod_depressed = mod;

   mod = xkb_state_serialize_mods(e_comp_wl->xkb.state,
                              XKB_STATE_MODS_LATCHED);
   changed |= mod != e_comp_wl->kbd.mod_latched;
   e_comp_wl->kbd.mod_latched = mod;

   mod = xkb_state_serialize_mods(e_comp_wl->xkb.state,
                              XKB_STATE_MODS_LOCKED);
   changed |= mod != e_comp_wl->kbd.mod_locked;
   e_comp_wl->kbd.mod_locked = mod;

   grp = xkb_state_serialize_layout(e_comp_wl->xkb.state,
                                XKB_STATE_LAYOUT_EFFECTIVE);
   changed |= grp != e_comp_wl->kbd.mod_group;
   e_comp_wl->kbd.mod_group = grp;
   return changed;
}

EINTERN void
e_comp_wl_input_keyboard_modifiers_update(void)
{
   uint32_t serial;
   struct wl_resource *res;
   Eina_List *l;

   if (!e_comp_wl_input_keyboard_modifiers_serialize()) return;

   if (!e_comp_wl->kbd.focused) return;

   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   EINA_LIST_FOREACH(e_comp_wl->kbd.focused, l, res)
     wl_keyboard_send_modifiers(res, serial,
                                e_comp_wl->kbd.mod_depressed,
                                e_comp_wl->kbd.mod_latched,
                                e_comp_wl->kbd.mod_locked,
                                e_comp_wl->kbd.mod_group);
}

EINTERN void
e_comp_wl_input_keyboard_state_update(uint32_t keycode, Eina_Bool pressed)
{
   enum xkb_key_direction dir;

   if (!e_comp_wl->xkb.state) return;

   if (pressed) dir = XKB_KEY_DOWN;
   else dir = XKB_KEY_UP;

   e_comp_wl->kbd.mod_changed =
     xkb_state_update_key(e_comp_wl->xkb.state, keycode + 8, dir);

   e_comp_wl_input_keyboard_modifiers_update();
}

E_API void
e_comp_wl_input_pointer_enabled_set(Eina_Bool enabled)
{
   /* check for valid compositor data */
   if (!e_comp->wl_comp_data)
     {
        ERR("No compositor data");
        return;
     }

   e_comp_wl->ptr.enabled = !!enabled;
   _e_comp_wl_input_update_seat_caps();
}

E_API void
e_comp_wl_input_keyboard_enabled_set(Eina_Bool enabled)
{
   /* check for valid compositor data */
   if (!e_comp->wl_comp_data)
     {
        ERR("No compositor data");
        return;
     }

   e_comp_wl->kbd.enabled = !!enabled;
   _e_comp_wl_input_update_seat_caps();
}

E_API void
e_comp_wl_input_keymap_set(const char *rules, const char *model, const char *layout)
{
   struct xkb_keymap *keymap;
   struct xkb_rule_names names;
   FILE *file = NULL;
   const char *keymap_path = NULL;

   /* DBG("COMP_WL: Keymap Set: %s %s %s", rules, model, layout); */

   /* assemble xkb_rule_names so we can fetch keymap */
   memset(&names, 0, sizeof(names));
   if (rules) names.rules = strdup(rules);
   else names.rules = strdup("evdev");
   if (model) names.model = strdup(model);
   else names.model = strdup("pc105");
   if (layout) names.layout = strdup(layout);
   else names.layout = strdup("us");

   /* unreference any existing context */
   if (e_comp_wl->xkb.context)
     xkb_context_unref(e_comp_wl->xkb.context);

   /* create a new xkb context */
   e_comp_wl->xkb.context = xkb_context_new(0);

   if (!e_comp_wl->xkb.context)
     return;

   if (e_config->xkb.use_cache)
     {
        keymap_path = eina_stringshare_printf("/var/lib/xkb/%s-%s-%s-%s-%s.xkb",
              names.rules ? names.rules : "",
              names.model ? names.model : "",
              names.layout ? names.layout : "",
              names.variant ? names.variant : "",
              names.options ? names.options : "");

        file = fopen(keymap_path, "r");
     }

   if (!file)
     {
        INF("There is a no keymap file (%s). Generate keymap using rmlvo\n", keymap_path);
        /* fetch new keymap based on names */
        keymap = xkb_map_new_from_names(e_comp_wl->xkb.context, &names, 0);
     }
   else
     {
        INF("Keymap file (%s) has been found. xkb_keymap is going to be generated with it.\n", keymap_path);
        keymap = xkb_map_new_from_file(e_comp_wl->xkb.context, file, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
        eina_stringshare_del(keymap_path);
        keymap_path = NULL;
     }

   /* update compositor keymap */
   _e_comp_wl_input_keymap_update(keymap, keymap_path);

   /* cleanup */
   if (keymap_path) eina_stringshare_del(keymap_path);
   free((char *)names.rules);
   free((char *)names.model);
   free((char *)names.layout);
}

E_API void
e_comp_wl_input_touch_enabled_set(Eina_Bool enabled)
{
   /* check for valid compositor data */
   if (!e_comp->wl_comp_data)
     {
        ERR("No compositor data");
        return;
     }

   e_comp_wl->touch.enabled = !!enabled;
   _e_comp_wl_input_update_seat_caps();
}

EINTERN Eina_Bool
e_comp_wl_input_touch_check(struct wl_resource *res)
{
   return wl_resource_instance_of(res, &wl_touch_interface,
                                  &_e_touch_interface);
}
