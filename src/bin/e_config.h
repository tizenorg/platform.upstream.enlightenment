#ifdef E_TYPEDEFS

#define E_CONFIG_LIMIT(v, min, max) {if (v >= max) v = max; else if (v <= min) v = min; }

typedef struct _E_Config                    E_Config;
typedef struct _E_Config_Module             E_Config_Module;
typedef struct _E_Config_Binding_Mouse      E_Config_Binding_Mouse; // TODO: should be removed - yigl
typedef struct _E_Config_Binding_Key        E_Config_Binding_Key; // TODO: should be removed - yigl
typedef struct _E_Config_Binding_Edge       E_Config_Binding_Edge; // TODO: should be removed - yigl
typedef struct _E_Config_Binding_Signal     E_Config_Binding_Signal; // TODO: should be removed - yigl
typedef struct _E_Config_Binding_Wheel      E_Config_Binding_Wheel; // TODO: should be removed - yigl
typedef struct _E_Config_Binding_Acpi       E_Config_Binding_Acpi; // TODO: should be removed - yigl
typedef struct _E_Config_Desktop_Background E_Config_Desktop_Background; // TODO: should be removed - yigl
typedef struct _E_Config_Desklock_Background E_Config_Desklock_Background; // TODO: should be removed - yigl
typedef struct _E_Config_Desktop_Name       E_Config_Desktop_Name;
typedef struct _E_Config_Desktop_Window_Profile E_Config_Desktop_Window_Profile;
typedef struct _E_Config_Syscon_Action      E_Config_Syscon_Action; // TODO: should be removed - yigl
typedef struct _E_Config_Env_Var            E_Config_Env_Var;
typedef struct _E_Config_Client_Type        E_Config_Client_Type;

typedef struct _E_Event_Config_Icon_Theme   E_Event_Config_Icon_Theme; // TODO: should be removed - yigl

typedef struct E_Config_Bindings E_Config_Bindings; // TODO: should be removed - yigl

typedef enum
{
   E_CONFIG_PROFILE_TYPE_NONE,
   E_CONFIG_PROFILE_TYPE_MOBILE,
   E_CONFIG_PROFILE_TYPE_TABLET,
   E_CONFIG_PROFILE_TYPE_DESKTOP
} E_Config_Profile_Type;

#else
#ifndef E_CONFIG_H
#define E_CONFIG_H

/* increment this whenever we change config enough that you need new
 * defaults for e to work.
 */
#define E_CONFIG_FILE_EPOCH      1
/* increment this whenever a new set of config values are added but the users
 * config doesn't need to be wiped - simply new values need to be put in
 */
#define E_CONFIG_FILE_GENERATION 19
#define E_CONFIG_FILE_VERSION    ((E_CONFIG_FILE_EPOCH * 1000000) + E_CONFIG_FILE_GENERATION)

#define E_CONFIG_BINDINGS_VERSION 0 // DO NOT INCREMENT UNLESS YOU WANT TO WIPE ALL BINDINGS!!!!!

struct _E_Config
{
   int         config_version; // INTERNAL // done
   E_Config_Profile_Type config_type; // INTERNAL // done
   int         show_splash; // GUI // TODO: should be removed - yigl
   const char *desktop_default_background; // GUI // done
   Eina_List  *desktop_backgrounds; // GUI // done
   const char *desktop_default_name; // done
   const char *desktop_default_window_profile; // done
   Eina_List  *desktop_names; // GUI // done
   Eina_List  *desktop_window_profiles; // GUI // done
   double      menus_scroll_speed; // GUI // TODO: should be removed - yigl
   double      menus_fast_mouse_move_threshhold; // GUI // TODO: should be removed - yigl
   double      menus_click_drag_timeout; // GUI // TODO: should be removed - yigl
   int         border_shade_animate; // GUI // TODO: should be removed - yigl
   int         border_shade_transition; // GUI // TODO: should be removed - yigl
   double      border_shade_speed; // GUI // TODO: should be removed - yigl
   double      framerate; // GUI // done
   int         priority; // GUI // done
   int         zone_desks_x_count; // GUI // done
   int         zone_desks_y_count; // GUI // done
   int         show_desktop_icons; // GUI // TODO: should be removed - yigl
   int         edge_flip_dragging; // GUI // TODO: should be removed - yigl
   int         no_module_delay; // GUI // TODO: should be removed - yigl
   const char *language; // GUI // TODO: should be removed - yigl
   const char *desklock_language; // GUI // TODO: should be removed - yigl
   Eina_List  *modules; // GUI // done
   Eina_List  *bad_modules; // GUI // TODO: should be removed - yigl
   Eina_List  *font_fallbacks; // GUI
   Eina_List  *font_defaults; // GUI

   /* NO LONGER SAVED WITH THIS STRUCT */
   Eina_List  *mouse_bindings; // GUI // TODO: should be removed - yigl
   Eina_List  *key_bindings; // GUI // TODO: should be removed - yigl
   Eina_List  *edge_bindings; // GUI // TODO: should be removed - yigl
   Eina_List  *signal_bindings; // GUI // TODO: should be removed - yigl
   Eina_List  *wheel_bindings; // GUI // TODO: should be removed - yigl
   Eina_List  *acpi_bindings; // GUI // TODO: should be removed - yigl

   Eina_List  *path_append_data; // GUI // TODO: should be removed - yigl
   Eina_List  *path_append_images; // GUI
   Eina_List  *path_append_fonts; // GUI
   Eina_List  *path_append_init; // GUI // TODO: should be removed - yigl
   Eina_List  *path_append_icons; // GUI // TODO: should be removed - yigl
   Eina_List  *path_append_modules; // GUI // TODO: should be removed - yigl
   Eina_List  *path_append_backgrounds; // GUI // TODO: should be removed - yigl
   Eina_List  *path_append_messages; // GUI // TODO: should be removed - yigl
   int         window_placement_policy; // GUI // done
   int         window_grouping; // GUI // TODO: should be removed - yigl
   int         focus_policy; // GUI // done
   int         focus_policy_ext; // GUI // done
   int         focus_setting; // GUI // done
   int         pass_click_on; // GUI // TODO: should be removed - yigl
   int         window_activehint_policy; // GUI // TODO: should be removed - yigl
   int         always_click_to_raise; // GUI // done
   int         always_click_to_focus; // GUI // done
   int         use_auto_raise; // GUI // done
   double      auto_raise_delay; // GUI // TODO: should be removed - yigl
   int         use_resist; // GUI // TODO: should be removed - yigl
   int         drag_resist; // TODO: should be removed - yigl
   int         desk_resist; // GUI // TODO: should be removed - yigl
   int         window_resist; // GUI // TODO: should be removed - yigl
   int         gadget_resist; // GUI // TODO: should be removed - yigl
   int         geometry_auto_move; // GUI // TODO: should be removed - yigl
   int         geometry_auto_resize_limit; // GUI // TODO: should be removed - yigl
   int         winlist_warp_while_selecting; // GUI // TODO: should be removed - yigl
   int         winlist_warp_at_end; // GUI // TODO: should be removed - yigl
   int         winlist_no_warp_on_direction; // GUI // TODO: should be removed - yigl
   double      winlist_warp_speed; // GUI **** NO LONGER USED!!!  // TODO: should be removed - yigl
   int         winlist_scroll_animate; // GUI // TODO: should be removed - yigl
   double      winlist_scroll_speed; // GUI // TODO: should be removed - yigl
   int         winlist_list_show_iconified; // GUI // TODO: should be removed - yigl
   int         winlist_list_show_other_desk_iconified; // GUI // TODO: should be removed - yigl
   int         winlist_list_show_other_screen_iconified; // GUI // TODO: should be removed - yigl
   int         winlist_list_show_other_desk_windows; // GUI // TODO: should be removed - yigl
   int         winlist_list_show_other_screen_windows; // GUI // TODO: should be removed - yigl
   int         winlist_list_uncover_while_selecting; // GUI // TODO: should be removed - yigl
   int         winlist_list_jump_desk_while_selecting; // GUI // TODO: should be removed - yigl
   int         winlist_list_focus_while_selecting; // GUI // TODO: should be removed - yigl
   int         winlist_list_raise_while_selecting; // GUI // TODO: should be removed - yigl
   int         winlist_list_move_after_select; // GUI // TODO: should be removed - yigl
   double      winlist_pos_align_x; // GUI // TODO: should be removed - yigl
   double      winlist_pos_align_y; // GUI // TODO: should be removed - yigl
   double      winlist_pos_size_w; // GUI // TODO: should be removed - yigl
   double      winlist_pos_size_h; // GUI // TODO: should be removed - yigl
   int         winlist_pos_min_w; // GUI // TODO: should be removed - yigl
   int         winlist_pos_min_h; // GUI // TODO: should be removed - yigl
   int         winlist_pos_max_w; // GUI // TODO: should be removed - yigl
   int         winlist_pos_max_h; // GUI // TODO: should be removed - yigl
   int         maximize_policy; // GUI // done
   int         allow_manip; // GUI // done
   int         border_fix_on_shelf_toggle; // GUI // TODO: should be removed - yigl
   int         allow_above_fullscreen; // GUI // TODO: should be removed - yigl
   int         kill_if_close_not_possible; // GUI // done
   int         kill_process; // GUI // done
   double      kill_timer_wait; // GUI // done
   int         ping_clients; // GUI // done
   const char *transition_start; // GUI // TODO: should be removed - yigl
   const char *transition_desk; // GUI // TODO: should be removed - yigl
   const char *transition_change; // GUI // TODO: should be removed - yigl
   Eina_List  *remembers; // GUI // TODO: should be removed - yigl
   int         remember_internal_windows; // GUI // TODO: should be removed - yigl
   Eina_Bool  remember_internal_fm_windows; // GUI // TODO: should be removed - yigl
   Eina_Bool  remember_internal_fm_windows_globally; // GUI // TODO: should be removed - yigl
   int         move_info_follows; // GUI // TODO: should be removed - yigl
   int         resize_info_follows; // GUI // TODO: should be removed - yigl
   int         move_info_visible; // GUI // TODO: should be removed - yigl
   int         resize_info_visible; // GUI // TODO: should be removed - yigl
   int         focus_last_focused_per_desktop; // GUI // TODO: should be removed - yigl
   int         focus_revert_on_hide_or_close; // GUI // TODO: should be removed - yigl
   int         disable_all_pointer_warps; // GUI // TODO: should be removed - yigl
   int         pointer_slide; // GUI // TODO: should be removed - yigl
   double      pointer_warp_speed; // GUI // TODO: should be removed - yigl
   int         use_e_cursor; // GUI // done
   int         cursor_size; // GUI // done
   int         menu_autoscroll_margin; // GUI // TODO: should be removed - yigl
   int         menu_autoscroll_cursor_margin; // GUI
   const char *input_method; // GUI // TODO: should be removed - yigl
   struct
   {
      int move;      // GUI // done
      int resize;      // GUI // done
      int raise;      // GUI // done
      int lower;      // GUI // done
      int layer;      // GUI // done
      int desktop;      // GUI // done
      int iconify;      // GUI // done
   } transient; // done
   int                       menu_eap_name_show; // GUI // TODO: should be removed - yigl
   int                       menu_eap_generic_show; // GUI // TODO: should be removed - yigl
   int                       menu_eap_comment_show; // GUI // TODO: should be removed - yigl
   int                       menu_favorites_show; // GUI // TODO: should be removed - yigl
   int                       menu_apps_show; // GUI // TODO: should be removed - yigl
   Eina_Bool                menu_icons_hide; // GUI // TODO: should be removed - yigl
   int                       menu_gadcon_client_toplevel; // GUI // TODO: should be removed - yigl
   int                       fullscreen_policy; // GUI // TODO: should be removed - yigl
   const char               *exebuf_term_cmd; // GUI // TODO: should be removed - yigl
   Eina_List                *color_classes; // dead
   int                       use_app_icon; // GUI // TODO: should be removed - yigl
   int                       cnfmdlg_disabled; // GUI // TODO: should be removed - yigl
   int                       cfgdlg_auto_apply; // GUI // TODO: should be removed - yigl
   int                       cfgdlg_default_mode; // GUI // TODO: should be removed - yigl
   Eina_List                *gadcons; // GUI // TODO: should be removed - yigl
   Eina_List                *shelves; // GUI // TODO: should be removed - yigl
   int                       font_hinting; // GUI // TODO: should be removed - yigl
   int                       desklock_passwd; // GUI // hashed // TODO: should be removed - yigl
   int                       desklock_pin; // GUI // hashed // TODO: should be removed - yigl
   Eina_List                *desklock_backgrounds; // GUI // TODO: should be removed - yigl
   int                       desklock_auth_method; // GUI // TODO: should be removed - yigl
   int                       desklock_login_box_zone; // GUI // TODO: should be removed - yigl
   int                       desklock_start_locked; // GUI // TODO: should be removed - yigl
   int                       desklock_on_suspend; // GUI // TODO: should be removed - yigl
   int                       desklock_autolock_screensaver; // GUI // TODO: should be removed - yigl
   double                    desklock_post_screensaver_time; // GUI // TODO: should be removed - yigl
   int                       desklock_autolock_idle; // GUI // TODO: should be removed - yigl
   double                    desklock_autolock_idle_timeout; // GUI // TODO: should be removed - yigl
   int                       desklock_use_custom_desklock; // GUI // TODO: should be removed - yigl
   const char               *desklock_custom_desklock_cmd; // GUI // TODO: should be removed - yigl
   unsigned char             desklock_ask_presentation; // GUI // TODO: should be removed - yigl
   double                    desklock_ask_presentation_timeout; // GUI // TODO: should be removed - yigl

   int                       screensaver_enable; // GUI // TODO: should be removed - yigl
   int                       screensaver_timeout; // GUI // TODO: should be removed - yigl
   int                       screensaver_interval; // GUI // TODO: should be removed - yigl
   int                       screensaver_blanking; // GUI // TODO: should be removed - yigl
   int                       screensaver_expose; // GUI // TODO: should be removed - yigl
   unsigned char             screensaver_ask_presentation; // GUI // TODO: should be removed - yigl
   double                    screensaver_ask_presentation_timeout; // GUI // TODO: should be removed - yigl

   int                       screensaver_wake_on_notify; // GUI // TODO: should be removed - yigl
   int                       screensaver_wake_on_urgent; // GUI // TODO: should be removed - yigl

   unsigned char             screensaver_suspend; // GUI // TODO: should be removed - yigl
   unsigned char             screensaver_suspend_on_ac; // GUI // TODO: should be removed - yigl
   double                    screensaver_suspend_delay; // GUI // TODO: should be removed - yigl

   int                       dpms_enable; // GUI // done
   int                       dpms_standby_enable; // GUI // done
   int                       dpms_standby_timeout; // GUI // done
   int                       dpms_suspend_enable; // GUI // done
   int                       dpms_suspend_timeout; // GUI // done
   int                       dpms_off_enable; // GUI // done
   int                       dpms_off_timeout; // GUI // done
   unsigned char             no_dpms_on_fullscreen; // GUI // done

   int                       use_hw_underlay;

   int                       clientlist_group_by; // GUI // TODO: should be removed - yigl
   int                       clientlist_include_all_zones; // GUI // TODO: should be removed - yigl
   int                       clientlist_separate_with; // GUI // TODO: should be removed - yigl
   int                       clientlist_sort_by; // GUI // TODO: should be removed - yigl
   int                       clientlist_separate_iconified_apps; // GUI // TODO: should be removed - yigl
   int                       clientlist_warp_to_iconified_desktop; // GUI // TODO: should be removed - yigl
   int                       clientlist_limit_caption_len; // GUI // TODO: should be removed - yigl
   int                       clientlist_max_caption_len; // GUI // TODO: should be removed - yigl

   int                       mouse_hand; //GUI // done
   int                       mouse_accel_numerator; // GUI // TODO: should be removed - yigl
   int                       mouse_accel_denominator; // GUI // TODO: should be removed - yigl
   int                       mouse_accel_threshold; // GUI // TODO: should be removed - yigl

   int                       border_raise_on_mouse_action; // GUI // done
   int                       border_raise_on_focus; // GUI // done
   int                       raise_on_revert_focus; // GUI // done
   int                       desk_flip_wrap; // GUI // TODO: should be removed - yigl
   int                       fullscreen_flip; // GUI // TODO: should be removed - yigl
   int                       multiscreen_flip; // GUI // TODO: should be removed - yigl

   const char               *icon_theme; // GUI // TODO: should be removed - yigl
   unsigned char             icon_theme_overrides; // GUI // TODO: should be removed - yigl
   const char               *desktop_environment; // GUI // TODO: should be removed - yigl

   /* modes:
    * 1-"pane") horizontal or vertical movement to/from next/previous "screen"
    * 2-"zoom") 45degree diagonal movement based on border position
    */
   int                       desk_flip_animate_mode; // GUI // TODO: should be removed - yigl
   /* types based on theme */
   Eina_Stringshare         *desk_flip_animate_type; // GUI // TODO: should be removed - yigl
   int                       desk_flip_animate_interpolation; // GUI // TODO: should be removed - yigl

   const char               *wallpaper_import_last_dev; // INTERNAL // TODO: should be removed - yigl
   const char               *wallpaper_import_last_path; // INTERNAL // TODO: should be removed - yigl

   const char               *theme_default_border_style; // GUI // done

   Eina_List                *mime_icons; // GUI
   int                       desk_auto_switch; // GUI; // TODO: should be removed - yigl

   int                       screen_limits; // done

   int                       thumb_nice; // TODO: should be removed - yigl

   int                       ping_clients_interval; // GUI // done

   int                       thumbscroll_enable; // GUI // TODO: should be removed - yigl
   int                       thumbscroll_threshhold; // GUI // TODO: should be removed - yigl
   double                    thumbscroll_momentum_threshhold; // GUI // TODO: should be removed - yigl
   double                    thumbscroll_friction; // GUI // TODO: should be removed - yigl

   unsigned char             filemanager_single_click; // GUI // TODO: should be removed - yigl
   int                       device_desktop; // GUI // TODO: should be removed - yigl
   int                       device_auto_mount; // GUI // TODO: should be removed - yigl
   int                       device_auto_open; // GUI // TODO: should be removed - yigl
   //Efm_Mode                  device_detect_mode; /* not saved, display-only */
   unsigned char             filemanager_copy; // GUI // TODO: should be removed - yigl
   unsigned char             filemanager_secure_rm; // GUI // TODO: should be removed - yigl

   struct
   {
      double timeout; // GUI // done
      struct
      {
         unsigned char dx; // GUI // done
         unsigned char dy; // GUI // done
      } move;
      struct
      {
         unsigned char dx; // GUI // done
         unsigned char dy; // GUI // done
      } resize;
   } border_keyboard; // done

   struct
   {
      double        min; // GUI // done
      double        max; // GUI // done
      double        factor; // GUI // done
      int           base_dpi; // GUI // done
      unsigned char use_dpi; // GUI // done
      unsigned char use_custom; // GUI // done
   } scale; // done

   unsigned char show_cursor; // GUI // done
   unsigned char idle_cursor; // GUI // done

   const char   *default_system_menu; // GUI // TODO: should be removed - yigl

   unsigned char cfgdlg_normal_wins; // GUI // TODO: should be removed - yigl

   struct
   {
      struct
      {
         int icon_size;         // GUI // TODO: should be removed - yigl
      } main, secondary, extra; // TODO: should be removed - yigl
      double        timeout;  // GUI // TODO: should be removed - yigl
      unsigned char do_input;  // GUI // TODO: should be removed - yigl
      Eina_List    *actions; // TODO: should be removed - yigl
   } syscon; // TODO: should be removed - yigl

   struct
   {
      unsigned char presentation; // INTERNAL // TODO: should be removed - yigl
      unsigned char offline; // INTERNAL // TODO: should be removed - yigl
   } mode; // TODO: should be removed - yigl

   struct
   {
      double        expire_timeout; // TODO: should be removed - yigl
      unsigned char show_run_dialog; // TODO: should be removed - yigl
      unsigned char show_exit_dialog; // TODO: should be removed - yigl
   } exec; // TODO: should be removed - yigl

   unsigned char null_container_win; // HYPER-ADVANCED-ONLY - TURNING ON KILLS DESKTOP BG // TODO: should be removed - yigl

   Eina_List    *env_vars; // GUI // done

   struct
   {
      double        normal; // GUI // TODO: should be removed - yigl
      double        dim; // GUI // TODO: should be removed - yigl
      double        transition; // GUI // TODO: should be removed - yigl
      double        timer; // GUI // TODO: should be removed - yigl
      const char   *sysdev; // GUI // TODO: should be removed - yigl
      unsigned char idle_dim; // GUI // TODO: should be removed - yigl
      //E_Backlight_Mode mode; /* not saved, display-only */
   } backlight; // TODO: should be removed - yigl

   struct
   {
      double           none; // TODO: should be removed - yigl
      double           low; // TODO: should be removed - yigl
      double           medium; // TODO: should be removed - yigl
      double           high; // TODO: should be removed - yigl
      double           extreme; // TODO: should be removed - yigl
      E_Powersave_Mode min; // TODO: should be removed - yigl
      E_Powersave_Mode max; // TODO: should be removed - yigl
   } powersave; // TODO: should be removed - yigl

   struct
   {
      unsigned char load_xrdb; // GUI // TODO: should be removed - yigl
      unsigned char load_xmodmap; // GUI // TODO: should be removed - yigl
      unsigned char load_gnome; // GUI // TODO: should be removed - yigl
      unsigned char load_kde; // GUI // TODO: should be removed - yigl
   } deskenv; // TODO: should be removed - yigl

   struct
   {
      unsigned char enabled;  // GUI // TODO: should be removed - yigl
      unsigned char match_e17_theme;  // GUI // TODO: should be removed - yigl
      unsigned char match_e17_icon_theme;  // GUI // TODO: should be removed - yigl
      int           xft_antialias; // TODO: should be removed - yigl
      int           xft_hinting; // TODO: should be removed - yigl
      const char   *xft_hint_style; // TODO: should be removed - yigl
      const char   *xft_rgba; // TODO: should be removed - yigl
      const char   *net_theme_name;  // GUI // TODO: should be removed - yigl
      const char   *net_theme_name_detected; // not saved // TODO: should be removed - yigl
      const char   *net_icon_theme_name; // TODO: should be removed - yigl
      const char   *gtk_font_name; // TODO: should be removed - yigl
   } xsettings; // TODO: should be removed - yigl

   struct
   {
      unsigned char check; // INTERNAL // TODO: should be removed - yigl
      unsigned char later; // INTERNAL // TODO: should be removed - yigl
   } update; // TODO: should be removed - yigl

   struct
   {
      Eina_List  *used_layouts; // done
      Eina_List  *used_options; // done
      int         only_label; // done
      const char *default_model; // done
      int         cur_group; // TODO: should be removed - yigl
      Eina_Bool dont_touch_my_damn_keyboard; // done
      Eina_Bool use_cache; // done
      unsigned int delay_held_key_input_to_focus; // done

      /* NO LONGER USED BECAUSE I SUCK
       * -zmike, 31 January 2013
       */
      const char *cur_layout; // whatever the current layout is // TODO: should be removed - yigl
      const char *selected_layout; // whatever teh current layout that the user has selected is // TODO: should be removed - yigl
      const char *desklock_layout; // TODO: should be removed - yigl
   } xkb; // done

   struct
   {
      int repeat_delay;//delay in milliseconds since key down until repeating starts // done
      int repeat_rate;//the rate of repeating keys in characters per second // done
   } keyboard; // done

   unsigned char exe_always_single_instance; // GUI // TODO: should be removed - yigl
   int           use_desktop_window_profile; // GUI // done
#ifdef _F_ZONE_WINDOW_ROTATION_
   unsigned char wm_win_rotation; // done
#endif
   int use_cursor_timer; // done
   int cursor_timer_interval; // done

   Eina_List *client_types; // done
};

struct E_Config_Bindings
{
   unsigned int config_version;
   Eina_List  *mouse_bindings; // GUI
   Eina_List  *key_bindings; // GUI
   Eina_List  *edge_bindings; // GUI
   Eina_List  *signal_bindings; // GUI
   Eina_List  *wheel_bindings; // GUI
   Eina_List  *acpi_bindings; // GUI
};

struct _E_Config_Desklock_Background
{
   const char *file;
   Eina_Bool hide_logo;
};

struct _E_Config_Env_Var
{
   const char   *var;
   const char   *val;
   unsigned char unset;
};

struct _E_Config_Syscon_Action
{
   const char *action;
   const char *params;
   const char *button;
   const char *icon;
   int         is_main;
};

struct _E_Config_Module
{
   const char   *name;
   unsigned char enabled;
   unsigned char delayed;
   int           priority;
};

struct _E_Config_Binding_Mouse
{
   int           context;
   int           modifiers;
   const char   *action;
   const char   *params;
   unsigned char button;
   unsigned char any_mod;
};

struct _E_Config_Binding_Key
{
   int           context;
   unsigned int  modifiers;
   const char   *key;
   const char   *action;
   const char   *params;
   unsigned char any_mod;
};

struct _E_Config_Binding_Edge
{
   int           context;
   int           modifiers;
   float         delay;
   const char   *action;
   const char   *params;
   unsigned char edge;
   unsigned char any_mod;
   Eina_Bool    drag_only;
};

struct _E_Config_Binding_Signal
{
   int           context;
   const char   *signal;
   const char   *source;
   int           modifiers;
   unsigned char any_mod;
   const char   *action;
   const char   *params;
};

struct _E_Config_Binding_Wheel
{
   int           context;
   int           direction;
   int           z;
   int           modifiers;
   unsigned char any_mod;
   const char   *action;
   const char   *params;
};

struct _E_Config_Binding_Acpi
{
   int         context, type, status;
   const char *action, *params;
};

struct _E_Config_Desktop_Background
{
   int         zone;
   int         desk_x;
   int         desk_y;
   const char *file;
};

struct _E_Config_Desktop_Name
{
   int         zone;
   int         desk_x;
   int         desk_y;
   const char *name;
};

struct _E_Config_Desktop_Window_Profile
{
   int         zone;
   int         desk_x;
   int         desk_y;
   const char *profile;
};

struct _E_Event_Config_Icon_Theme
{
   const char *icon_theme;
};

struct _E_Config_Client_Type
{
   const char     *name; /* icccm.class_name */
   const char     *clas; /* icccm.class */
   E_Window_Type   window_type; /* Ecore_X_Window_Type / E_Window_Type */
   int             client_type; /* E_Client_Type */
};

EINTERN int                   e_config_init(void);
EINTERN int                   e_config_shutdown(void);

E_API void                     e_config_load(void);

E_API int                      e_config_save(void);
E_API void                     e_config_save_flush(void);
E_API void                     e_config_save_queue(void);

E_API const char              *e_config_profile_get(void);
E_API char                    *e_config_profile_dir_get(const char *prof);
E_API void                     e_config_profile_set(const char *prof);
E_API Eina_List               *e_config_profile_list(void);
E_API void                     e_config_profile_add(const char *prof);
E_API void                     e_config_profile_del(const char *prof);

E_API void                     e_config_save_block_set(int block);
E_API int                      e_config_save_block_get(void);

E_API void                    *e_config_domain_load(const char *domain, E_Config_DD *edd);
E_API void                    *e_config_domain_system_load(const char *domain, E_Config_DD *edd);
E_API int                      e_config_profile_save(void);
E_API int                      e_config_domain_save(const char *domain, E_Config_DD *edd, const void *data);

E_API E_Config_Binding_Mouse  *e_config_binding_mouse_match(E_Config_Binding_Mouse *eb_in);
E_API E_Config_Binding_Key    *e_config_binding_key_match(E_Config_Binding_Key *eb_in);
E_API E_Config_Binding_Edge   *e_config_binding_edge_match(E_Config_Binding_Edge *eb_in);
E_API E_Config_Binding_Signal *e_config_binding_signal_match(E_Config_Binding_Signal *eb_in);
E_API E_Config_Binding_Wheel  *e_config_binding_wheel_match(E_Config_Binding_Wheel *eb_in);
E_API E_Config_Binding_Acpi   *e_config_binding_acpi_match(E_Config_Binding_Acpi *eb_in);
E_API void                     e_config_mode_changed(void);


E_API void e_config_bindings_free(E_Config_Bindings *ecb);
E_API void e_config_binding_signal_free(E_Config_Binding_Signal *ebs);
E_API void e_config_binding_wheel_free(E_Config_Binding_Wheel *ebw);
E_API void e_config_binding_mouse_free(E_Config_Binding_Mouse *ebm);
E_API void e_config_binding_edge_free(E_Config_Binding_Edge *ebe);
E_API void e_config_binding_key_free(E_Config_Binding_Key *ebk);
E_API void e_config_binding_acpi_free(E_Config_Binding_Acpi *eba);

extern E_API E_Config *e_config;
extern E_API E_Config_Bindings *e_bindings;

extern E_API int E_EVENT_CONFIG_ICON_THEME; // TODO: should be removed - yigl
extern E_API int E_EVENT_CONFIG_MODE_CHANGED;
extern E_API int E_EVENT_CONFIG_LOADED;

#endif
#endif
