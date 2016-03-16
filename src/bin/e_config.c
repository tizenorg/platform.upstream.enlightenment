#include "e.h"

E_API E_Config *e_config = NULL;

static int _e_config_revisions = 9;

/* local subsystem functions */
static void      _e_config_save_cb(void *data);
static void      _e_config_free(E_Config *cfg);
static Eina_Bool _e_config_cb_timer(void *data);
static int       _e_config_eet_close_handle(Eet_File *ef, char *file);

/* local subsystem globals */
static int _e_config_save_block = 0;
static const char *_e_config_profile = NULL;

static E_Config_DD *_e_config_edd = NULL;
static E_Config_DD *_e_config_module_edd = NULL;
static E_Config_DD *_e_config_theme_edd = NULL;
static E_Config_DD *_e_config_desktop_bg_edd = NULL;
static E_Config_DD *_e_config_desklock_bg_edd = NULL;
static E_Config_DD *_e_config_desktop_name_edd = NULL;
static E_Config_DD *_e_config_desktop_window_profile_edd = NULL;
static E_Config_DD *_e_config_env_var_edd = NULL;
static E_Config_DD *_e_config_client_type_edd = NULL;

E_API int E_EVENT_CONFIG_MODE_CHANGED = 0;

static E_Dialog *_e_config_error_dialog = NULL;

static void
_e_config_error_dialog_cb_delete(void *dia)
{
   if (dia == _e_config_error_dialog)
     _e_config_error_dialog = NULL;
}

static void
_e_config_edd_shutdown(void)
{
   E_CONFIG_DD_FREE(_e_config_edd);
   E_CONFIG_DD_FREE(_e_config_module_edd);
   E_CONFIG_DD_FREE(_e_config_theme_edd);
   E_CONFIG_DD_FREE(_e_config_desktop_bg_edd);
   E_CONFIG_DD_FREE(_e_config_desklock_bg_edd);
   E_CONFIG_DD_FREE(_e_config_desktop_name_edd);
   E_CONFIG_DD_FREE(_e_config_desktop_window_profile_edd);
   E_CONFIG_DD_FREE(_e_config_env_var_edd);
   E_CONFIG_DD_FREE(_e_config_client_type_edd);
}

static void
_e_config_edd_init(Eina_Bool old)
{
   _e_config_desktop_bg_edd = E_CONFIG_DD_NEW("E_Config_Desktop_Background", E_Config_Desktop_Background);
#undef T
#undef D
#define T E_Config_Desktop_Background
#define D _e_config_desktop_bg_edd
   E_CONFIG_VAL(D, T, zone, INT);
   E_CONFIG_VAL(D, T, desk_x, INT);
   E_CONFIG_VAL(D, T, desk_y, INT);
   E_CONFIG_VAL(D, T, file, STR);

   _e_config_desktop_name_edd = E_CONFIG_DD_NEW("E_Config_Desktop_Name", E_Config_Desktop_Name);
#undef T
#undef D
#define T E_Config_Desktop_Name
#define D _e_config_desktop_name_edd
   E_CONFIG_VAL(D, T, zone, INT);
   E_CONFIG_VAL(D, T, desk_x, INT);
   E_CONFIG_VAL(D, T, desk_y, INT);
   E_CONFIG_VAL(D, T, name, STR);

   _e_config_desktop_window_profile_edd = E_CONFIG_DD_NEW("E_Config_Desktop_Window_Profile", E_Config_Desktop_Window_Profile);
#undef T
#undef D
#define T E_Config_Desktop_Window_Profile
#define D _e_config_desktop_window_profile_edd
   E_CONFIG_VAL(D, T, zone, INT);
   E_CONFIG_VAL(D, T, desk_x, INT);
   E_CONFIG_VAL(D, T, desk_y, INT);
   E_CONFIG_VAL(D, T, profile, STR);

   _e_config_module_edd = E_CONFIG_DD_NEW("E_Config_Module", E_Config_Module);
#undef T
#undef D
#define T E_Config_Module
#define D _e_config_module_edd
   E_CONFIG_VAL(D, T, name, STR);
   E_CONFIG_VAL(D, T, enabled, UCHAR);
   E_CONFIG_VAL(D, T, delayed, UCHAR);
   E_CONFIG_VAL(D, T, priority, INT);

   _e_config_env_var_edd = E_CONFIG_DD_NEW("E_Config_Env_Var", E_Config_Env_Var);
#undef T
#undef D
#define T E_Config_Env_Var
#define D _e_config_env_var_edd
   E_CONFIG_VAL(D, T, var, STR);
   E_CONFIG_VAL(D, T, val, STR);
   E_CONFIG_VAL(D, T, unset, UCHAR);

   _e_config_client_type_edd = E_CONFIG_DD_NEW("E_Config_Client_Type", E_Config_Client_Type);
#undef T
#undef D
#define T E_Config_Client_Type
#define D _e_config_client_type_edd
   E_CONFIG_VAL(D, T, name, STR);
   E_CONFIG_VAL(D, T, clas, STR);
   E_CONFIG_VAL(D, T, window_type, INT);
   E_CONFIG_VAL(D, T, client_type, INT);

   _e_config_edd = E_CONFIG_DD_NEW("E_Config", E_Config);
#undef T
#undef D
#define T E_Config
#define D _e_config_edd
   /**/ /* == already configurable via ipc */
   E_CONFIG_VAL(D, T, config_version, INT);
   E_CONFIG_VAL(D, T, desktop_default_background, STR);
   E_CONFIG_VAL(D, T, desktop_default_name, STR);
   E_CONFIG_VAL(D, T, desktop_default_window_profile, STR);
   E_CONFIG_LIST(D, T, desktop_backgrounds, _e_config_desktop_bg_edd);
   E_CONFIG_LIST(D, T, desktop_names, _e_config_desktop_name_edd);
   E_CONFIG_LIST(D, T, desktop_window_profiles, _e_config_desktop_window_profile_edd);
   E_CONFIG_VAL(D, T, framerate, DOUBLE);
   E_CONFIG_VAL(D, T, priority, INT);
   E_CONFIG_VAL(D, T, zone_desks_x_count, INT);
   E_CONFIG_VAL(D, T, zone_desks_y_count, INT);
   E_CONFIG_LIST(D, T, modules, _e_config_module_edd);
   E_CONFIG_VAL(D, T, window_placement_policy, INT);
   E_CONFIG_VAL(D, T, focus_policy, INT);
   E_CONFIG_VAL(D, T, focus_policy_ext, INT);
   E_CONFIG_VAL(D, T, focus_setting, INT);
   E_CONFIG_VAL(D, T, always_click_to_raise, INT);
   E_CONFIG_VAL(D, T, always_click_to_focus, INT);
   E_CONFIG_VAL(D, T, use_auto_raise, INT);
   E_CONFIG_VAL(D, T, maximize_policy, INT);
   E_CONFIG_VAL(D, T, allow_manip, INT);
   E_CONFIG_VAL(D, T, kill_if_close_not_possible, INT);
   E_CONFIG_VAL(D, T, kill_process, INT);
   E_CONFIG_VAL(D, T, kill_timer_wait, DOUBLE);
   E_CONFIG_VAL(D, T, ping_clients, INT);
   E_CONFIG_VAL(D, T, use_e_cursor, INT);
   E_CONFIG_VAL(D, T, cursor_size, INT);
   E_CONFIG_VAL(D, T, transient.move, INT);
   E_CONFIG_VAL(D, T, transient.resize, INT);
   E_CONFIG_VAL(D, T, transient.raise, INT);
   E_CONFIG_VAL(D, T, transient.lower, INT);
   E_CONFIG_VAL(D, T, transient.layer, INT);
   E_CONFIG_VAL(D, T, transient.desktop, INT);
   E_CONFIG_VAL(D, T, transient.iconify, INT);
   E_CONFIG_VAL(D, T, fullscreen_policy, INT);
   E_CONFIG_VAL(D, T, dpms_enable, INT);
   E_CONFIG_VAL(D, T, dpms_standby_enable, INT);
   E_CONFIG_VAL(D, T, dpms_suspend_enable, INT);
   E_CONFIG_VAL(D, T, dpms_off_enable, INT);
   E_CONFIG_VAL(D, T, dpms_standby_timeout, INT);
   E_CONFIG_VAL(D, T, dpms_suspend_timeout, INT);
   E_CONFIG_VAL(D, T, dpms_off_timeout, INT);
   E_CONFIG_VAL(D, T, no_dpms_on_fullscreen, UCHAR);
   E_CONFIG_VAL(D, T, mouse_hand, INT);
   E_CONFIG_VAL(D, T, border_raise_on_mouse_action, INT);
   E_CONFIG_VAL(D, T, border_raise_on_focus, INT);
   E_CONFIG_VAL(D, T, raise_on_revert_focus, INT);
   E_CONFIG_VAL(D, T, theme_default_border_style, STR);
   E_CONFIG_VAL(D, T, screen_limits, INT);
   E_CONFIG_VAL(D, T, ping_clients_interval, INT);
   E_CONFIG_VAL(D, T, border_keyboard.timeout, DOUBLE);
   E_CONFIG_VAL(D, T, border_keyboard.move.dx, UCHAR);
   E_CONFIG_VAL(D, T, border_keyboard.move.dy, UCHAR);
   E_CONFIG_VAL(D, T, border_keyboard.resize.dx, UCHAR);
   E_CONFIG_VAL(D, T, border_keyboard.resize.dy, UCHAR);
   E_CONFIG_VAL(D, T, scale.min, DOUBLE);
   E_CONFIG_VAL(D, T, scale.max, DOUBLE);
   E_CONFIG_VAL(D, T, scale.factor, DOUBLE);
   E_CONFIG_VAL(D, T, scale.base_dpi, INT);
   E_CONFIG_VAL(D, T, scale.use_dpi, UCHAR);
   E_CONFIG_VAL(D, T, scale.use_custom, UCHAR);
   E_CONFIG_VAL(D, T, show_cursor, UCHAR);
   E_CONFIG_VAL(D, T, idle_cursor, UCHAR);
   E_CONFIG_LIST(D, T, env_vars, _e_config_env_var_edd);
   E_CONFIG_VAL(D, T, xkb.only_label, INT);
   E_CONFIG_VAL(D, T, xkb.default_model, STR);
   E_CONFIG_VAL(D, T, xkb.dont_touch_my_damn_keyboard, UCHAR);
   E_CONFIG_VAL(D, T, xkb.use_cache, UCHAR);
   E_CONFIG_VAL(D, T, xkb.delay_held_key_input_to_focus, UINT);
   E_CONFIG_VAL(D, T, keyboard.repeat_delay, INT);
   E_CONFIG_VAL(D, T, keyboard.repeat_rate, INT);
   E_CONFIG_VAL(D, T, use_desktop_window_profile, INT);
#ifdef _F_ZONE_WINDOW_ROTATION_
   E_CONFIG_VAL(D, T, wm_win_rotation, UCHAR);
#endif
   E_CONFIG_VAL(D, T, use_cursor_timer, INT);
   E_CONFIG_VAL(D, T, cursor_timer_interval, INT);
   E_CONFIG_LIST(D, T, client_types, _e_config_client_type_edd);
   E_CONFIG_VAL(D, T, comp_shadow_file, STR);
}

/* externally accessible functions */
EINTERN int
e_config_init(void)
{
   E_EVENT_CONFIG_MODE_CHANGED = ecore_event_type_new();

   /* if environment var set - use this profile name */
   _e_config_profile = eina_stringshare_add(getenv("E_CONF_PROFILE"));

   _e_config_edd_init(EINA_FALSE);

   e_config_load();

   e_config_save_queue();
   return 1;
}

EINTERN int
e_config_shutdown(void)
{
   eina_stringshare_del(_e_config_profile);

   _e_config_edd_shutdown();

   return 1;
}

E_API void
e_config_load(void)
{
   int reload = 0;

   e_config = e_config_domain_load("e", _e_config_edd);
   if (e_config)
     {
        /* major version change - that means wipe and restart */
        if ((e_config->config_version) < E_CONFIG_FILE_EPOCH * 1000000)
          {
             /* your config is too old - need new defaults */
             _e_config_free(e_config);
             e_config = NULL;
             reload = 1;
             ecore_timer_add(1.0, _e_config_cb_timer,
                             _("Settings data needed upgrading. Your old settings have<br>"
                               "been wiped and a new set of defaults initialized. This<br>"
                               "will happen regularly during development, so don't report a<br>"
                               "bug. This simply means Enlightenment needs new settings<br>"
                               "data by default for usable functionality that your old<br>"
                               "settings simply lack. This new set of defaults will fix<br>"
                               "that by adding it in. You can re-configure things now to your<br>"
                               "liking. Sorry for the hiccup in your settings.<br>"));
          }
        /* config is too new? odd! suspect corruption? */
        else if (e_config->config_version > E_CONFIG_FILE_VERSION)
          {
             /* your config is too new - what the fuck??? */
             _e_config_free(e_config);
             e_config = NULL;
             reload = 1;
             ecore_timer_add(1.0, _e_config_cb_timer,
                             _("Your settings are NEWER than Enlightenment. This is very<br>"
                               "strange. This should not happen unless you downgraded<br>"
                               "Enlightenment or copied the settings from a place where<br>"
                               "a newer version of Enlightenment was running. This is bad and<br>"
                               "as a precaution your settings have been now restored to<br>"
                               "defaults. Sorry for the inconvenience.<br>"));
          }
        if (reload)
          {
             e_config_profile_del(e_config_profile_get());
             e_config_profile_set("default");
             e_config = e_config_domain_load("e", _e_config_edd);
          }
     }
   while (!e_config)
     {
        _e_config_edd_shutdown();
        _e_config_edd_init(EINA_TRUE);
        e_config = e_config_domain_load("e", _e_config_edd);
        /* I made a c&p error here and fucked the world, so this ugliness
         * will be my public mark of shame until E :/
         * -zmike, 2013
         */
        if (e_config)
          {
             /* this is essentially CONFIG_VERSION_CHECK(7) */
             INF("Performing config upgrade to %d.%d", 1, 7);
             _e_config_edd_shutdown();
             _e_config_edd_init(EINA_FALSE);
             break;
          }
#undef T
#undef D
        e_config_profile_set("default");
        if (!reload) e_config_profile_del(e_config_profile_get());
        e_config_save_block_set(1);
        ecore_app_restart();
        //e_sys_action_do(E_SYS_RESTART, NULL);
        return;
     }

#define CONFIG_VERSION_CHECK(VERSION) \
  if (e_config->config_version - (E_CONFIG_FILE_EPOCH * 1000000) < (VERSION))

#define CONFIG_VERSION_UPDATE_INFO(VERSION) \
  INF("Performing config upgrade for %d.%d", E_CONFIG_FILE_EPOCH, VERSION);

   if (e_config->config_version < E_CONFIG_FILE_VERSION)
     {
        CONFIG_VERSION_CHECK(18)
          {
             ;
          }
        CONFIG_VERSION_CHECK(19)
          {
             CONFIG_VERSION_UPDATE_INFO(19);

             /* set (400, 25) as the default values of repeat delay, rate */
             e_config->keyboard.repeat_delay = 400;
             e_config->keyboard.repeat_rate = 25;
          }
     }

   e_config->config_version = E_CONFIG_FILE_VERSION;

   /* limit values so they are sane */
   E_CONFIG_LIMIT(e_config->framerate, 1.0, 200.0);
   E_CONFIG_LIMIT(e_config->priority, 0, 19);
   E_CONFIG_LIMIT(e_config->zone_desks_x_count, 1, 64);
   E_CONFIG_LIMIT(e_config->zone_desks_y_count, 1, 64);
   E_CONFIG_LIMIT(e_config->window_placement_policy, E_WINDOW_PLACEMENT_SMART, E_WINDOW_PLACEMENT_MANUAL);
   E_CONFIG_LIMIT(e_config->focus_policy, 0, 2);
   E_CONFIG_LIMIT(e_config->focus_policy_ext, 0, 1);
   E_CONFIG_LIMIT(e_config->focus_setting, 0, 3);
   E_CONFIG_LIMIT(e_config->always_click_to_raise, 0, 1);
   E_CONFIG_LIMIT(e_config->always_click_to_focus, 0, 1);
   E_CONFIG_LIMIT(e_config->use_auto_raise, 0, 1);
   E_CONFIG_LIMIT(e_config->maximize_policy, E_MAXIMIZE_FULLSCREEN, E_MAXIMIZE_DIRECTION);
   E_CONFIG_LIMIT(e_config->allow_manip, 0, 1);
   E_CONFIG_LIMIT(e_config->kill_if_close_not_possible, 0, 1);
   E_CONFIG_LIMIT(e_config->kill_process, 0, 1);
   E_CONFIG_LIMIT(e_config->kill_timer_wait, 0.0, 120.0);
   E_CONFIG_LIMIT(e_config->ping_clients, 0, 1);
   E_CONFIG_LIMIT(e_config->use_e_cursor, 0, 1);
   E_CONFIG_LIMIT(e_config->cursor_size, 0, 1024);
   E_CONFIG_LIMIT(e_config->dpms_enable, 0, 1);
   E_CONFIG_LIMIT(e_config->dpms_standby_enable, 0, 1);
   E_CONFIG_LIMIT(e_config->dpms_suspend_enable, 0, 1);
   E_CONFIG_LIMIT(e_config->dpms_off_enable, 0, 1);
   E_CONFIG_LIMIT(e_config->dpms_standby_timeout, 30, 5400);
   E_CONFIG_LIMIT(e_config->dpms_suspend_timeout, 30, 5400);
   E_CONFIG_LIMIT(e_config->dpms_off_timeout, 30, 5400);
   E_CONFIG_LIMIT(e_config->mouse_hand, 0, 1);
   E_CONFIG_LIMIT(e_config->border_raise_on_mouse_action, 0, 1);
   E_CONFIG_LIMIT(e_config->border_raise_on_focus, 0, 1);
   E_CONFIG_LIMIT(e_config->raise_on_revert_focus, 0, 1);
   E_CONFIG_LIMIT(e_config->screen_limits, 0, 2);
   E_CONFIG_LIMIT(e_config->ping_clients_interval, 16, 1024);
   E_CONFIG_LIMIT(e_config->border_keyboard.move.dx, 1, 255);
   E_CONFIG_LIMIT(e_config->border_keyboard.move.dy, 1, 255);
   E_CONFIG_LIMIT(e_config->border_keyboard.resize.dx, 1, 255);
   E_CONFIG_LIMIT(e_config->border_keyboard.resize.dy, 1, 255);
   E_CONFIG_LIMIT(e_config->show_cursor, 0, 1);
   E_CONFIG_LIMIT(e_config->xkb.delay_held_key_input_to_focus, 0,5000); // 5000(ms) == 5(s)
   E_CONFIG_LIMIT(e_config->keyboard.repeat_delay, -1, 1000); // 1 second
   E_CONFIG_LIMIT(e_config->keyboard.repeat_rate, -1, 1000); // 1 second
   E_CONFIG_LIMIT(e_config->use_cursor_timer, 0, 1);
}

E_API int
e_config_save(void)
{
   _e_config_save_cb(NULL);
   return e_config_domain_save("e", _e_config_edd, e_config);
}

E_API void
e_config_save_queue(void)
{
   // TODO: add ecore_timer_add and call _e_config_save_cb
   _e_config_save_cb(NULL);
}

E_API const char *
e_config_profile_get(void)
{
   return _e_config_profile;
}

E_API void
e_config_profile_set(const char *prof)
{
   eina_stringshare_replace(&_e_config_profile, prof);
   e_util_env_set("E_CONF_PROFILE", _e_config_profile);
}

E_API char *
e_config_profile_dir_get(const char *prof)
{
   char buf[PATH_MAX];

   e_user_dir_snprintf(buf, sizeof(buf), "config/%s", prof);
   if (ecore_file_is_dir(buf)) return strdup(buf);
   e_prefix_data_snprintf(buf, sizeof(buf), "data/config/%s", prof);
   if (ecore_file_is_dir(buf)) return strdup(buf);
   return NULL;
}

static int
_cb_sort_files(char *f1, char *f2)
{
   return strcmp(f1, f2);
}

E_API Eina_List *
e_config_profile_list(void)
{
   Eina_List *files;
   char buf[PATH_MAX], *p;
   Eina_List *flist = NULL;
   size_t len;

   len = e_user_dir_concat_static(buf, "config");
   if (len + 1 >= (int)sizeof(buf))
     return NULL;

   files = ecore_file_ls(buf);

   buf[len] = '/';
   len++;

   p = buf + len;
   len = sizeof(buf) - len;
   if (files)
     {
        char *file;

        files = eina_list_sort(files, 0, (Eina_Compare_Cb)_cb_sort_files);
        EINA_LIST_FREE(files, file)
          {
             if (eina_strlcpy(p, file, len) >= len)
               {
                  free(file);
                  continue;
               }
             if (ecore_file_is_dir(buf))
               flist = eina_list_append(flist, file);
             else
               free(file);
          }
     }
   len = e_prefix_data_concat_static(buf, "data/config");
   if (len + 1 >= sizeof(buf))
     return NULL;

   files = ecore_file_ls(buf);

   buf[len] = '/';
   len++;

   p = buf + len;
   len = sizeof(buf) - len;
   if (files)
     {
        char *file;
        files = eina_list_sort(files, 0, (Eina_Compare_Cb)_cb_sort_files);
        EINA_LIST_FREE(files, file)
          {
             if (eina_strlcpy(p, file, len) >= len)
               {
                  free(file);
                  continue;
               }
             if (ecore_file_is_dir(buf))
               {
                  const Eina_List *l;
                  const char *tmp;
                  EINA_LIST_FOREACH(flist, l, tmp)
                    if (!strcmp(file, tmp)) break;

                  if (!l) flist = eina_list_append(flist, file);
                  else free(file);
               }
             else
               free(file);
          }
     }
   return flist;
}

E_API void
e_config_profile_add(const char *prof)
{
   char buf[4096];
   if (e_user_dir_snprintf(buf, sizeof(buf), "config/%s", prof) >= sizeof(buf))
     return;
   ecore_file_mkdir(buf);
}

E_API void
e_config_profile_del(const char *prof)
{
   char buf[4096];
   if (e_user_dir_snprintf(buf, sizeof(buf), "config/%s", prof) >= sizeof(buf))
     return;
   ecore_file_recursive_rm(buf);
}

E_API void
e_config_save_block_set(int block)
{
   _e_config_save_block = block;
}

E_API int
e_config_save_block_get(void)
{
   return _e_config_save_block;
}

/**
 * Loads configurations from file located in the working profile
 * The configurations are stored in a struct declated by the
 * macros E_CONFIG_DD_NEW and E_CONFIG_<b>TYPE</b>
 *
 * @param domain of the configuration file.
 * @param edd to struct definition
 * @return returns allocated struct on success, if unable to find config returns null
 */
E_API void *
e_config_domain_load(const char *domain, E_Config_DD *edd)
{
   Eet_File *ef;
   char buf[4096];
   void *data = NULL;
   int i;

   e_user_dir_snprintf(buf, sizeof(buf), "config/%s/%s.cfg",
                       _e_config_profile, domain);
   ef = eet_open(buf, EET_FILE_MODE_READ);
   if (ef)
     {
        data = eet_data_read(ef, edd, "config");
        eet_close(ef);
        if (data) return data;
     }

   for (i = 1; i <= _e_config_revisions; i++)
     {
        e_user_dir_snprintf(buf, sizeof(buf), "config/%s/%s.%i.cfg",
                            _e_config_profile, domain, i);
        ef = eet_open(buf, EET_FILE_MODE_READ);
        if (ef)
          {
             data = eet_data_read(ef, edd, "config");
             eet_close(ef);
             if (data) return data;
          }
     }
   return e_config_domain_system_load(domain, edd);
}

E_API void *
e_config_domain_system_load(const char *domain, E_Config_DD *edd)
{
   Eet_File *ef;
   char buf[4096];
   void *data = NULL;

   e_prefix_data_snprintf(buf, sizeof(buf), "data/config/%s/%s.cfg",
                          _e_config_profile, domain);
   ef = eet_open(buf, EET_FILE_MODE_READ);
   if (ef)
     {
        data = eet_data_read(ef, edd, "config");
        eet_close(ef);
        return data;
     }

   return data;
}

static void
_e_config_mv_error(const char *from, const char *to)
{
   E_Dialog *dia;
   char buf[8192];

   if (_e_config_error_dialog) return;

   dia = e_dialog_new(NULL, "E", "_sys_error_logout_slow");
   EINA_SAFETY_ON_NULL_RETURN(dia);

   e_dialog_title_set(dia, _("Enlightenment Settings Write Problems"));
   e_dialog_icon_set(dia, "dialog-error", 64);
   snprintf(buf, sizeof(buf),
            _("Enlightenment has had an error while moving config files<br>"
              "from:<br>"
              "%s<br>"
              "<br>"
              "to:<br>"
              "%s<br>"
              "<br>"
              "The rest of the write has been aborted for safety.<br>"),
            from, to);
   e_dialog_text_set(dia, buf);
   e_dialog_button_add(dia, _("OK"), NULL, NULL, NULL);
   e_dialog_button_focus_num(dia, 0);
   //elm_win_center(dia->win, 1, 1);
   e_object_del_attach_func_set(E_OBJECT(dia),
                                _e_config_error_dialog_cb_delete);
   e_dialog_show(dia);
   _e_config_error_dialog = dia;
}

E_API int
e_config_profile_save(void)
{
   Eet_File *ef;
   char buf[4096], buf2[4096];
   int ok = 0;

   /* FIXME: check for other sessions fo E running */
   e_user_dir_concat_static(buf, "config/profile.cfg");
   e_user_dir_concat_static(buf2, "config/profile.cfg.tmp");

   ef = eet_open(buf2, EET_FILE_MODE_WRITE);
   if (ef)
     {
        ok = eet_write(ef, "config", _e_config_profile,
                       strlen(_e_config_profile), 0);
        if (_e_config_eet_close_handle(ef, buf2))
          {
             Eina_Bool ret = EINA_TRUE;

             if (_e_config_revisions > 0)
               {
                  int i;
                  char bsrc[4096], bdst[4096];

                  for (i = _e_config_revisions; i > 1; i--)
                    {
                       e_user_dir_snprintf(bsrc, sizeof(bsrc), "config/profile.%i.cfg", i - 1);
                       e_user_dir_snprintf(bdst, sizeof(bdst), "config/profile.%i.cfg", i);
                       if ((ecore_file_exists(bsrc)) &&
                           (ecore_file_size(bsrc)))
                         {
                            ret = ecore_file_mv(bsrc, bdst);
                            if (!ret)
                              {
                                 _e_config_mv_error(bsrc, bdst);
                                 break;
                              }
                         }
                    }
                  if (ret)
                    {
                       e_user_dir_snprintf(bsrc, sizeof(bsrc), "config/profile.cfg");
                       e_user_dir_snprintf(bdst, sizeof(bdst), "config/profile.1.cfg");
                       ret = ecore_file_mv(bsrc, bdst);
//                       if (!ret)
//                          _e_config_mv_error(bsrc, bdst);
                    }
               }
             ret = ecore_file_mv(buf2, buf);
             if (!ret) _e_config_mv_error(buf2, buf);
          }
        ecore_file_unlink(buf2);
     }
   return ok;
}

/**
 * Saves configurations to file located in the working profile
 * The configurations are read from a struct declated by the
 * macros E_CONFIG_DD_NEW and E_CONFIG_<b>TYPE</b>
 *
 * @param domain  name of the configuration file.
 * @param edd pointer to struct definition
 * @param data struct to save as configuration file
 * @return 1 if save success, 0 on failure
 */
E_API int
e_config_domain_save(const char *domain, E_Config_DD *edd, const void *data)
{
   Eet_File *ef;
   char buf[4096], buf2[4096];
   int ok = 0, ret;
   size_t len, len2;

   if (_e_config_save_block) return 0;
   /* FIXME: check for other sessions fo E running */
   len = e_user_dir_snprintf(buf, sizeof(buf), "config/%s", _e_config_profile);
   if (len + 1 >= sizeof(buf)) return 0;

   ecore_file_mkdir(buf);

   buf[len] = '/';
   len++;

   len2 = eina_strlcpy(buf + len, domain, sizeof(buf) - len);
   if (len2 + sizeof(".cfg") >= sizeof(buf) - len) return 0;

   len += len2;

   memcpy(buf + len, ".cfg", sizeof(".cfg"));
   len += sizeof(".cfg") - 1;

   if (len + sizeof(".tmp") >= sizeof(buf)) return 0;
   memcpy(buf2, buf, len);
   memcpy(buf2 + len, ".tmp", sizeof(".tmp"));

   ef = eet_open(buf2, EET_FILE_MODE_WRITE);
   if (ef)
     {
        ok = eet_data_write(ef, edd, "config", data, 1);
        if (_e_config_eet_close_handle(ef, buf2))
          {
             if (_e_config_revisions > 0)
               {
                  int i;
                  char bsrc[4096], bdst[4096];

                  for (i = _e_config_revisions; i > 1; i--)
                    {
                       e_user_dir_snprintf(bsrc, sizeof(bsrc), "config/%s/%s.%i.cfg", _e_config_profile, domain, i - 1);
                       e_user_dir_snprintf(bdst, sizeof(bdst), "config/%s/%s.%i.cfg", _e_config_profile, domain, i);
                       if ((ecore_file_exists(bsrc)) &&
                           (ecore_file_size(bsrc)))
                         {
                            ecore_file_mv(bsrc, bdst);
                         }
                    }
                  e_user_dir_snprintf(bsrc, sizeof(bsrc), "config/%s/%s.cfg", _e_config_profile, domain);
                  e_user_dir_snprintf(bdst, sizeof(bdst), "config/%s/%s.1.cfg", _e_config_profile, domain);
                  ecore_file_mv(bsrc, bdst);
               }
             ret = ecore_file_mv(buf2, buf);
             if (!ret)
               ERR("*** Error saving config. ***");
          }
        ecore_file_unlink(buf2);
     }
   return ok;
}

E_API void
e_config_mode_changed(void)
{
   ecore_event_add(E_EVENT_CONFIG_MODE_CHANGED, NULL, NULL, NULL);
}

/* local subsystem functions */
static void
_e_config_save_cb(void *data EINA_UNUSED)
{
   e_config_profile_save();
   e_module_save_all();
   e_config_domain_save("e", _e_config_edd, e_config);
}

static void
_e_config_free(E_Config *ecf)
{
   E_Config_Module *em;
   E_Config_Env_Var *evr;
   E_Config_Desktop_Window_Profile *wp;

   if (!ecf) return;

   EINA_LIST_FREE(ecf->desktop_window_profiles, wp)
     {
        eina_stringshare_del(wp->profile);
        E_FREE(wp);
     }

   eina_stringshare_del(ecf->xkb.default_model);

   EINA_LIST_FREE(ecf->modules, em)
     {
        if (em->name) eina_stringshare_del(em->name);
        E_FREE(em);
     }
   if (ecf->comp_shadow_file) eina_stringshare_del(ecf->comp_shadow_file);
   if (ecf->desktop_default_background) eina_stringshare_del(ecf->desktop_default_background);
   if (ecf->desktop_default_name) eina_stringshare_del(ecf->desktop_default_name);
   if (ecf->desktop_default_window_profile) eina_stringshare_del(ecf->desktop_default_window_profile);
   if (ecf->theme_default_border_style) eina_stringshare_del(ecf->theme_default_border_style);
   EINA_LIST_FREE(ecf->env_vars, evr)
     {
        if (evr->var) eina_stringshare_del(evr->var);
        if (evr->val) eina_stringshare_del(evr->val);
        E_FREE(evr);
     }

   E_FREE(ecf);
}

static Eina_Bool
_e_config_cb_timer(void *data)
{
   e_util_dialog_show(_("Settings Upgraded"), "%s", (char *)data);
   return 0;
}

static int
_e_config_eet_close_handle(Eet_File *ef, char *file)
{
   Eet_Error err;
   char *erstr = NULL;

   err = eet_close(ef);
   switch (err)
     {
      case EET_ERROR_NONE:
        /* all good - no error */
        break;

      case EET_ERROR_BAD_OBJECT:
        erstr = _("The EET file handle is bad.");
        break;

      case EET_ERROR_EMPTY:
        erstr = _("The file data is empty.");
        break;

      case EET_ERROR_NOT_WRITABLE:
        erstr = _("The file is not writable. Perhaps the disk is read-only<br>or you lost permissions to your files.");
        break;

      case EET_ERROR_OUT_OF_MEMORY:
        erstr = _("Memory ran out while preparing the write.<br>Please free up memory.");
        break;

      case EET_ERROR_WRITE_ERROR:
        erstr = _("This is a generic error.");
        break;

      case EET_ERROR_WRITE_ERROR_FILE_TOO_BIG:
        erstr = _("The settings file is too large.<br>It should be very small (a few hundred KB at most).");
        break;

      case EET_ERROR_WRITE_ERROR_IO_ERROR:
        erstr = _("You have I/O errors on the disk.<br>Maybe it needs replacing?");
        break;

      case EET_ERROR_WRITE_ERROR_OUT_OF_SPACE:
        erstr = _("You ran out of space while writing the file.");
        break;

      case EET_ERROR_WRITE_ERROR_FILE_CLOSED:
        erstr = _("The file was closed while writing.");
        break;

      case EET_ERROR_MMAP_FAILED:
        erstr = _("Memory-mapping (mmap) of the file failed.");
        break;

      case EET_ERROR_X509_ENCODING_FAILED:
        erstr = _("X509 Encoding failed.");
        break;

      case EET_ERROR_SIGNATURE_FAILED:
        erstr = _("Signature failed.");
        break;

      case EET_ERROR_INVALID_SIGNATURE:
        erstr = _("The signature was invalid.");
        break;

      case EET_ERROR_NOT_SIGNED:
        erstr = _("Not signed.");
        break;

      case EET_ERROR_NOT_IMPLEMENTED:
        erstr = _("Feature not implemented.");
        break;

      case EET_ERROR_PRNG_NOT_SEEDED:
        erstr = _("PRNG was not seeded.");
        break;

      case EET_ERROR_ENCRYPT_FAILED:
        erstr = _("Encryption failed.");
        break;

      case EET_ERROR_DECRYPT_FAILED:
        erstr = _("Decryption failed.");
        break;

      default: /* if we get here eet added errors we don't know */
        erstr = _("The error is unknown to Enlightenment.");
        break;
     }
   if (erstr)
     {
        /* delete any partially-written file */
        ecore_file_unlink(file);
        /* only show dialog for first error - further ones are likely */
        /* more of the same error */
        if (!_e_config_error_dialog)
          {
             E_Dialog *dia;

             dia = e_dialog_new(NULL, "E", "_sys_error_logout_slow");
             if (dia)
               {
                  char buf[8192];

                  e_dialog_title_set(dia, _("Enlightenment Settings Write Problems"));
                  e_dialog_icon_set(dia, "dialog-error", 64);
                  snprintf(buf, sizeof(buf),
                           _("Enlightenment has had an error while writing<br>"
                             "its config file.<br>"
                             "%s<br>"
                             "<br>"
                             "The file where the error occurred was:<br>"
                             "%s<br>"
                             "<br>"
                             "This file has been deleted to avoid corrupt data.<br>"),
                           erstr, file);
                  e_dialog_text_set(dia, buf);
                  e_dialog_button_add(dia, _("OK"), NULL, NULL, NULL);
                  e_dialog_button_focus_num(dia, 0);
                  //elm_win_center(dia->win, 1, 1);
                  e_object_del_attach_func_set(E_OBJECT(dia),
                                               _e_config_error_dialog_cb_delete);
                  e_dialog_show(dia);
                  _e_config_error_dialog = dia;
               }
          }
        return 0;
     }
   return 1;
}
