#define EXECUTIVE_MODE_ENABLED
#define E_COMP_WL
#include "e.h"
#include <sys/mman.h>
#ifdef HAVE_WL_DRM
#include <Ecore_Drm.h>
#endif

static void
_e_comp_wl_input_update_seat_caps(E_Comp_Data *cdata)
{
   Eina_List *l;
   struct wl_resource *res;
   enum wl_seat_capability caps = 0;

   if (cdata->ptr.enabled)
     caps |= WL_SEAT_CAPABILITY_POINTER;
   if (cdata->kbd.enabled)
     caps |= WL_SEAT_CAPABILITY_KEYBOARD;
   if (cdata->touch.enabled)
     caps |= WL_SEAT_CAPABILITY_TOUCH;

   EINA_LIST_FOREACH(cdata->seat.resources, l, res)
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
   E_Comp_Data *cdata;
   E_Client *ec;
   Eina_Bool got_mouse = EINA_FALSE;
   int cursor_w = 0, cursor_h = 0;

   /* get compositor data */
   if (!(cdata = wl_resource_get_user_data(resource))) return;
   E_CLIENT_FOREACH(e_comp, ec)
     {
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
        ec->icccm.title = eina_stringshare_add("noshadow");
        ec->icccm.window_role = eina_stringshare_add("wl_pointer-cursor");
        evas_object_pass_events_set(ec->frame, 1);
        ec->client.w = ec->client.h = 1;
        l = e_client_focus_stack_get();
        e_client_focus_stack_set(eina_list_remove(l, ec));

        /* Set fuctions to prevent unwanted handling by shell */
        ec->comp_data->shell.surface = surface_resource;
        ec->comp_data->shell.configure = _e_comp_wl_input_pointer_configure;
        ec->comp_data->shell.map = _e_comp_wl_input_pointer_map;
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
   E_Comp_Data *cdata;

   /* get compositor data */
   if (!(cdata = wl_resource_get_user_data(resource))) return;

   cdata->ptr.resources = eina_list_remove(cdata->ptr.resources, resource);
}

static void
_e_comp_wl_input_cb_pointer_get(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   E_Comp_Data *cdata;
   struct wl_resource *res;

   /* get compositor data */
   if (!(cdata = wl_resource_get_user_data(resource))) return;

   /* try to create pointer resource */
   res = wl_resource_create(client, &wl_pointer_interface,
                            wl_resource_get_version(resource), id);
   if (!res)
     {
        ERR("Could not create pointer on seat %s: %m", cdata->seat.name);
        wl_client_post_no_memory(client);
        return;
     }

   cdata->ptr.resources = eina_list_append(cdata->ptr.resources, res);
   wl_resource_set_implementation(res, &_e_pointer_interface, cdata,
                                 _e_comp_wl_input_cb_pointer_unbind);
}

static void
_e_comp_wl_input_cb_keyboard_unbind(struct wl_resource *resource)
{
   E_Comp_Data *cdata;
   Eina_List *l, *ll;
   struct wl_resource *res;

   /* get compositor data */
   if (!(cdata = wl_resource_get_user_data(resource))) return;

   cdata->kbd.resources = eina_list_remove(cdata->kbd.resources, resource);
   EINA_LIST_FOREACH_SAFE(cdata->kbd.focused, l, ll, res)
     if (res == resource)
       cdata->kbd.focused = eina_list_remove_list(cdata->kbd.focused, l);
}

static void
_e_comp_wl_input_cb_keyboard_get(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   E_Comp_Data *cdata;
   struct wl_resource *res;
   E_Client *ec;
   struct wl_client *wc;
   uint32_t serial, *k;
   Eina_List *l;

   /* get compositor data */
   if (!(cdata = wl_resource_get_user_data(resource))) return;

   /* try to create keyboard resource */
   res = wl_resource_create(client, &wl_keyboard_interface,
                            wl_resource_get_version(resource), id);
   if (!res)
     {
        ERR("Could not create keyboard on seat %s: %m", cdata->seat.name);
        wl_client_post_no_memory(client);
        return;
     }

   cdata->kbd.resources = eina_list_append(cdata->kbd.resources, res);
   wl_resource_set_implementation(res, &_e_keyboard_interface, cdata,
                                  _e_comp_wl_input_cb_keyboard_unbind);

   /* send current repeat_info */
   if (wl_resource_get_version(res) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
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
}

static void
_e_comp_wl_input_cb_touch_get(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t id EINA_UNUSED)
{
    E_Comp_Data *cdata;
    struct wl_resource *res;

    /* get compositor data */
    if (!(cdata = wl_resource_get_user_data(resource))) return;

    /* try to create pointer resource */
    res = wl_resource_create(client, &wl_touch_interface,
                             wl_resource_get_version(resource), id);
    if (!res)
      {
         ERR("Could not create touch on seat %s: %m", cdata->seat.name);
         wl_client_post_no_memory(client);
         return;
      }

    cdata->touch.resources = eina_list_append(cdata->touch.resources, res);
    wl_resource_set_implementation(res, &_e_touch_interface, cdata,
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
   E_Comp_Data *cdata;

   if (!(cdata = wl_resource_get_user_data(resource))) return;

   cdata->seat.resources = eina_list_remove(cdata->seat.resources, resource);
}

static void
_e_comp_wl_input_cb_bind_seat(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp_Data *cdata;
   struct wl_resource *res;

   /* try to create the seat resource */
   cdata = data;
   res = wl_resource_create(client, &wl_seat_interface, MIN(version, 4), id);
   if (!res)
     {
        ERR("Could not create seat resource: %m");
        return;
     }

   /* store version of seat interface for reuse in updating capabilities */
   cdata->seat.version = version;
   cdata->seat.resources = eina_list_append(cdata->seat.resources, res);

   wl_resource_set_implementation(res, &_e_seat_interface, cdata,
                                  _e_comp_wl_input_cb_unbind_seat);

   _e_comp_wl_input_update_seat_caps(cdata);
   if (cdata->seat.version >= WL_SEAT_NAME_SINCE_VERSION)
     wl_seat_send_name(res, cdata->seat.name);
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
_e_comp_wl_input_keymap_update(E_Comp_Data *cdata, struct xkb_keymap *keymap, const char *keymap_path)
{
   char *tmp;
   xkb_mod_mask_t latched = 0, locked = 0, group = 0;
   struct wl_resource *res;
   Eina_List *l;
   uint32_t serial;

   /* unreference any existing keymap */
   if (cdata->xkb.keymap) xkb_map_unref(cdata->xkb.keymap);

   /* unmap any existing keyboard area */
   if (cdata->xkb.area) munmap(cdata->xkb.area, cdata->xkb.size);
   if (cdata->xkb.fd >= 0) close(cdata->xkb.fd);

   /* unreference any existing keyboard state */
   if (cdata->xkb.state)
     {
        latched =
          xkb_state_serialize_mods(cdata->xkb.state, XKB_STATE_MODS_LATCHED);
        locked =
          xkb_state_serialize_mods(cdata->xkb.state, XKB_STATE_MODS_LOCKED);
        group =
          xkb_state_serialize_layout(cdata->xkb.state,
                                     XKB_STATE_LAYOUT_EFFECTIVE);
        xkb_state_unref(cdata->xkb.state);
     }

   /* create a new xkb state */
   cdata->xkb.state = xkb_state_new(keymap);

   if ((latched) || (locked) || (group))
     xkb_state_update_mask(cdata->xkb.state, 0, latched, locked, 0, 0, group);

   /* increment keymap reference */
   cdata->xkb.keymap = xkb_map_ref(keymap);

   /* fetch updated modifiers */
   cdata->kbd.mod_shift = xkb_map_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
   cdata->kbd.mod_caps = xkb_map_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
   cdata->kbd.mod_ctrl = xkb_map_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
   cdata->kbd.mod_alt = xkb_map_mod_get_index(keymap, XKB_MOD_NAME_ALT);
   cdata->kbd.mod_super = xkb_map_mod_get_index(keymap, XKB_MOD_NAME_LOGO);

   if (!(tmp = xkb_map_get_as_string(keymap)))
     {
        ERR("Could not get keymap string");
        return;
     }

   cdata->xkb.size = strlen(tmp) + 1;
   cdata->xkb.fd = _e_comp_wl_input_keymap_fd_get(cdata->xkb.size);
   if (cdata->xkb.fd < 0)
     {
        ERR("Could not create keymap file");
        return;
     }

   _e_comp_wl_input_keymap_cache_create(keymap_path, tmp);

   cdata->xkb.area =
     mmap(NULL, cdata->xkb.size, (PROT_READ | PROT_WRITE),
          MAP_SHARED, cdata->xkb.fd, 0);
   if (cdata->xkb.area == MAP_FAILED)
     {
        ERR("Failed to mmap keymap area: %m");
        return;
     }

   strcpy(cdata->xkb.area, tmp);
   free(tmp);

   /* send updated keymap */
   EINA_LIST_FOREACH(cdata->kbd.resources, l, res)
     wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                             cdata->xkb.fd, cdata->xkb.size);

   /* update modifiers */
   e_comp_wl_input_keyboard_modifiers_update(cdata);

   if ((!latched) && (!locked)) return;

   /* send modifiers */
   serial = wl_display_get_serial(cdata->wl.disp);
   EINA_LIST_FOREACH(cdata->kbd.resources, l, res)
     wl_keyboard_send_modifiers(res, serial, cdata->kbd.mod_depressed,
                                cdata->kbd.mod_latched, cdata->kbd.mod_locked,
                                cdata->kbd.mod_group);
}

EINTERN Eina_Bool
e_comp_wl_input_init(E_Comp_Data *cdata)
{
   /* check for valid compositor data */
   if (!cdata)
     {
        ERR("No compositor data");
        return EINA_FALSE;
     }

   /* set default seat name */
   if (!cdata->seat.name) cdata->seat.name = "default";

   cdata->xkb.fd = -1;

   /* get default keyboard repeat rate/delay from configuration */
   cdata->kbd.repeat_delay = e_config->keyboard.repeat_delay;
   cdata->kbd.repeat_rate = e_config->keyboard.repeat_rate;

   /* check for valid repeat_delay and repeat_rate value */
   /* if invalid, set the default value of repeat delay and rate value */
   if (cdata->kbd.repeat_delay < 0) cdata->kbd.repeat_delay = 400;
   if (cdata->kbd.repeat_delay < 0) cdata->kbd.repeat_rate = 25;

   /* create the global resource for input seat */
   cdata->seat.global =
     wl_global_create(cdata->wl.disp, &wl_seat_interface, 4,
                      cdata, _e_comp_wl_input_cb_bind_seat);
   if (!cdata->seat.global)
     {
        ERR("Could not create global for seat: %m");
        return EINA_FALSE;
     }

   wl_array_init(&cdata->kbd.keys);

   return EINA_TRUE;
}

EINTERN void
e_comp_wl_input_shutdown(E_Comp_Data *cdata)
{
   struct wl_resource *res;
   E_Comp_Wl_Input_Device *dev;

   /* check for valid compositor data */
   if (!cdata)
     {
        ERR("No compositor data");
        return;
     }

   /* destroy pointer resources */
   EINA_LIST_FREE(cdata->ptr.resources, res)
     wl_resource_destroy(res);

   /* destroy keyboard resources */
   EINA_LIST_FREE(cdata->kbd.resources, res)
     wl_resource_destroy(res);

   /* destroy touch resources */
   EINA_LIST_FREE(cdata->touch.resources, res)
     wl_resource_destroy(res);

   /* destroy cdata->kbd.keys array */
   wl_array_release(&cdata->kbd.keys);

   /* unreference any existing keymap */
   if (cdata->xkb.keymap) xkb_map_unref(cdata->xkb.keymap);

   /* unmap any existing keyboard area */
   if (cdata->xkb.area) munmap(cdata->xkb.area, cdata->xkb.size);
   if (cdata->xkb.fd >= 0) close(cdata->xkb.fd);

   /* unreference any existing keyboard state */
   if (cdata->xkb.state) xkb_state_unref(cdata->xkb.state);

   /* unreference any existing context */
   if (cdata->xkb.context) xkb_context_unref(cdata->xkb.context);

   /* destroy the global seat resource */
   if (cdata->seat.global) wl_global_destroy(cdata->seat.global);
   cdata->seat.global = NULL;

   if (cdata->input_device_mgr.global) wl_global_destroy(cdata->input_device_mgr.global);
   cdata->input_device_mgr.global = NULL;

   if (cdata->input_device_mgr.curr_device_name) eina_stringshare_del(cdata->input_device_mgr.curr_device_name);
   EINA_LIST_FREE (cdata->input_device_mgr.device_list, dev)
     {
        if (dev->name) eina_stringshare_del(dev->name);
	 if (dev->identifier) eina_stringshare_del(dev->identifier);
        free(dev);
     }
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

EINTERN void
e_comp_wl_input_keyboard_modifiers_update(E_Comp_Data *cdata)
{
   uint32_t serial;
   struct wl_resource *res;
   Eina_List *l;

   cdata->kbd.mod_depressed =
     xkb_state_serialize_mods(cdata->xkb.state, XKB_STATE_DEPRESSED);
   cdata->kbd.mod_latched =
     xkb_state_serialize_mods(cdata->xkb.state, XKB_STATE_MODS_LATCHED);
   cdata->kbd.mod_locked =
     xkb_state_serialize_mods(cdata->xkb.state, XKB_STATE_MODS_LOCKED);
   cdata->kbd.mod_group =
     xkb_state_serialize_layout(cdata->xkb.state, XKB_STATE_LAYOUT_EFFECTIVE);

   serial = wl_display_next_serial(cdata->wl.disp);
   EINA_LIST_FOREACH(cdata->kbd.resources, l, res)
     wl_keyboard_send_modifiers(res, serial,
                                cdata->kbd.mod_depressed,
                                cdata->kbd.mod_latched,
                                cdata->kbd.mod_locked,
                                cdata->kbd.mod_group);
}

EINTERN void
e_comp_wl_input_keyboard_state_update(E_Comp_Data *cdata, uint32_t keycode, Eina_Bool pressed)
{
   enum xkb_key_direction dir;

   if (!cdata->xkb.state) return;

   if (pressed) dir = XKB_KEY_DOWN;
   else dir = XKB_KEY_UP;

   cdata->kbd.mod_changed =
     xkb_state_update_key(cdata->xkb.state, keycode + 8, dir);
}

EAPI void
e_comp_wl_input_pointer_enabled_set(Eina_Bool enabled)
{
   /* check for valid compositor data */
   if (!e_comp->wl_comp_data)
     {
        ERR("No compositor data");
        return;
     }

   e_comp->wl_comp_data->ptr.enabled = !!enabled;
   _e_comp_wl_input_update_seat_caps(e_comp->wl_comp_data);
}

EAPI void
e_comp_wl_input_keyboard_enabled_set(Eina_Bool enabled)
{
   /* check for valid compositor data */
   if (!e_comp->wl_comp_data)
     {
        ERR("No compositor data");
        return;
     }

   e_comp->wl_comp_data->kbd.enabled = !!enabled;
   _e_comp_wl_input_update_seat_caps(e_comp->wl_comp_data);
}

EAPI Eina_Stringshare *
e_comp_wl_input_keymap_path_get(struct xkb_rule_names names)
{
   return eina_stringshare_printf("/var/lib/xkb/%s-%s-%s-%s-%s.xkb",
            names.rules ? names.rules : "evdev",
            names.model ? names.model : "pc105",
            names.layout ? names.layout : "us",
            names.variant ? names.variant : "",
            names.options ? names.options : "");
}


EAPI struct xkb_keymap *
e_comp_wl_input_keymap_compile(struct xkb_context *ctx, struct xkb_rule_names names, char **keymap_path)
{
   struct xkb_keymap *keymap;
   char *cache_path = NULL;
   FILE *file = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ctx, NULL);

   if (e_config->xkb.use_cache)
     {
        cache_path = (char *)e_comp_wl_input_keymap_path_get(names);
        file = fopen(cache_path, "r");
     }

   if (!file)
     {
        INF("There is a no keymap file (%s). Generate keymap using rmlvo\n", cache_path);

        /* fetch new keymap based on names */
        keymap = xkb_map_new_from_names(ctx, &names, 0);
     }
   else
     {
        INF("Keymap file (%s) has been found. xkb_keymap is going to be generated with it.\n", cache_path);
        keymap = xkb_map_new_from_file(ctx, file, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
        eina_stringshare_del(cache_path);
        cache_path = NULL;
        fclose(file);
     }

   *keymap_path = cache_path;
   EINA_SAFETY_ON_NULL_RETURN_VAL(keymap, NULL);

   return keymap;
}

EAPI void
e_comp_wl_input_keymap_set(E_Comp_Data *cdata, const char *rules, const char *model, const char *layout,
                           struct xkb_context *dflt_ctx, struct xkb_keymap *dflt_map)
{
   struct xkb_keymap *keymap;
   struct xkb_rule_names names;
   char *keymap_path = NULL;
   Eina_Bool use_dflt_xkb = EINA_FALSE;

   /* check for valid compositor data */
   if (!cdata)
     {
        ERR("No compositor data");
        return;
     }

   /* DBG("COMP_WL: Keymap Set: %s %s %s", rules, model, layout); */

   if (dflt_ctx && dflt_map) use_dflt_xkb = EINA_TRUE;

   /* unreference any existing context */
   if (cdata->xkb.context) xkb_context_unref(cdata->xkb.context);

   /* create a new xkb context */
   if (use_dflt_xkb) cdata->xkb.context = dflt_ctx;
   else cdata->xkb.context = xkb_context_new(0);

   if (!cdata->xkb.context)
     return;

#ifdef HAVE_WL_DRM
   if (e_config->xkb.use_cache)
     ecore_drm_device_keyboard_cached_context_set(cdata->xkb.context);
#endif

   /* assemble xkb_rule_names so we can fetch keymap */
   memset(&names, 0, sizeof(names));
   if (rules) names.rules = strdup(rules);
   else names.rules = strdup("evdev");
   if (model) names.model = strdup(model);
   else names.model = strdup("pc105");
   if (layout) names.layout = strdup(layout);
   else names.layout = strdup("us");

   if (use_dflt_xkb)
     {
        keymap = dflt_map;
        keymap_path = (char *)e_comp_wl_input_keymap_path_get(names);
        if (access(keymap_path, R_OK) == 0)
          {
             eina_stringshare_del(keymap_path);
             keymap_path = NULL;
          }
     }
   else
     keymap = e_comp_wl_input_keymap_compile(cdata->xkb.context, names, &keymap_path);

   /* update compositor keymap */
   _e_comp_wl_input_keymap_update(cdata, keymap, keymap_path);

#ifdef HAVE_WL_DRM
   if (e_config->xkb.use_cache)
     ecore_drm_device_keyboard_cached_keymap_set(keymap);
#endif

   /* cleanup */
   if (keymap_path) eina_stringshare_del(keymap_path);
   free((char *)names.rules);
   free((char *)names.model);
   free((char *)names.layout);
}

EAPI void
e_comp_wl_input_touch_enabled_set(Eina_Bool enabled)
{
   /* check for valid compositor data */
   if (!e_comp->wl_comp_data)
     {
        ERR("No compositor data");
        return;
     }

   e_comp->wl_comp_data->touch.enabled = !!enabled;
   _e_comp_wl_input_update_seat_caps(e_comp->wl_comp_data);
}

EINTERN Eina_Bool
e_comp_wl_input_touch_check(struct wl_resource *res)
{
   return wl_resource_instance_of(res, &wl_touch_interface,
                                  &_e_touch_interface);
}
