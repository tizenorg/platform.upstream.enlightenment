# ifdef E_TYPEDEFS
typedef struct _E_Policy_Desk     E_Policy_Desk;
typedef struct _E_Policy_Client   E_Policy_Client;
typedef struct _E_Policy_Softkey  E_Policy_Softkey;
typedef struct _E_Policy_Config_Match E_Policy_Config_Match;
typedef struct _E_Policy_Config_Desk  E_Policy_Config_Desk;
typedef struct _E_Policy_Config_Rot   E_Policy_Config_Rot;
typedef struct _E_Policy_Config       E_Policy_Config;
typedef struct _E_Policy          E_Policy;
typedef struct _E_Policy_System_Info E_Policy_System_Info;
typedef struct _E_Policy_Interceptor E_Policy_Interceptor;

typedef enum _E_Policy_Intercept_Point
{
   E_POLICY_INTERCEPT_LAST,
} E_Policy_Intercept_Point;

typedef Eina_Bool (*E_Policy_Intercept_Cb)(void *data, E_Client *ec, va_list list);

# else
# ifndef E_POLICY_H
# define E_POLICY_H
#  ifndef _
#    ifdef HAVE_GETTEXT
#  define _(str) gettext(str)
#    else
#  define _(str) (str)
#    endif
#  endif

struct _E_Policy_Desk
{
   E_Desk          *desk;
   E_Zone          *zone;
};

struct _E_Policy_Client
{
   E_Client        *ec;
   struct
   {
      E_Maximize    maximized;
      unsigned int  fullscreen : 1;
      unsigned char borderless : 1;
      unsigned int  lock_user_location : 1;
      unsigned int  lock_client_location : 1;
      unsigned int  lock_user_size : 1;
      unsigned int  lock_client_size : 1;
      unsigned int  lock_client_stacking : 1;
      unsigned int  lock_user_shade : 1;
      unsigned int  lock_client_shade : 1;
      unsigned int  lock_user_maximize : 1;
      unsigned int  lock_client_maximize : 1;
      unsigned int  lock_user_fullscreen : 1;
      unsigned int  lock_client_fullscreen : 1;
   } orig;

   struct
   {
      unsigned int vkbd_state;
      unsigned int already_hide;
   } changes;

   Eina_Bool max_policy_state;
   Eina_Bool flt_policy_state;
   Eina_Bool allow_user_geom;
   int       user_geom_ref;
};

struct _E_Policy_Softkey
{
   EINA_INLIST;

   E_Zone          *zone;
   Evas_Object     *home;
   Evas_Object     *back;
};

struct _E_Policy
{
   E_Module        *module;
   Eina_List       *launchers; /* launcher window per zone */
   Eina_Inlist     *softkeys; /* softkey ui object per zone */
};

struct _E_Policy_System_Info
{
   struct
   {
      E_Client  *ec;
      Eina_Bool  show;
   } lockscreen;

   struct
   {
      int system;
      int client;
      Eina_Bool use_client;
   } brightness;
};

struct _E_Policy_Interceptor
{
   E_Policy_Intercept_Point ipoint;
   E_Policy_Intercept_Cb    func;
   void                    *data;
   unsigned int             delete_me : 1;
};

extern E_Policy *e_policy;
extern E_Policy_System_Info e_policy_system_info;

EINTERN E_Policy_Config_Desk *e_policy_conf_desk_get_by_nums(E_Policy_Config *conf, unsigned int zone_num, int x, int y);
EINTERN E_Policy_Client      *e_policy_client_get(E_Client *ec);
EINTERN void                  e_policy_allow_user_geometry_set(E_Client *ec, Eina_Bool set);
EINTERN void                  e_policy_desk_add(E_Desk *desk);
EINTERN void                  e_policy_desk_del(E_Policy_Desk *pd);
EINTERN E_Policy_Client      *e_policy_client_launcher_get(E_Zone *zone);

EINTERN Eina_Bool        e_policy_client_is_lockscreen(E_Client *ec);
EINTERN Eina_Bool        e_policy_client_is_home_screen(E_Client *ec);
EINTERN Eina_Bool        e_policy_client_is_quickpanel(E_Client *ec);
EINTERN Eina_Bool        e_policy_client_is_conformant(E_Client *ec);
EINTERN Eina_Bool        e_policy_client_is_volume(E_Client *ec);
EINTERN Eina_Bool        e_policy_client_is_volume_tv(E_Client *ec);
EINTERN Eina_Bool        e_policy_client_is_noti(E_Client *ec);
EINTERN Eina_Bool        e_policy_client_is_floating(E_Client *ec);
EINTERN Eina_Bool        e_policy_client_is_cursor(E_Client *ec);
EINTERN Eina_Bool        e_policy_client_is_subsurface(E_Client *ec);

EINTERN E_Policy_Softkey *e_policy_softkey_add(E_Zone *zone);
EINTERN void              e_policy_softkey_del(E_Policy_Softkey *softkey);
EINTERN void              e_policy_softkey_show(E_Policy_Softkey *softkey);
EINTERN void              e_policy_softkey_hide(E_Policy_Softkey *softkey);
EINTERN void              e_policy_softkey_update(E_Policy_Softkey *softkey);
EINTERN E_Policy_Softkey *e_policy_softkey_get(E_Zone *zone);

EINTERN void             e_policy_client_visibility_send(E_Client *ec);
EINTERN void             e_policy_client_iconify_by_visibility(E_Client *ec);
EINTERN void             e_policy_client_uniconify_by_visibility(E_Client *ec);

EINTERN Eina_Bool        e_policy_client_maximize(E_Client *ec);

EINTERN void             e_policy_client_window_opaque_set(E_Client *ec);

EINTERN void             e_policy_stack_init(void);
EINTERN void             e_policy_stack_shutdonw(void);
EINTERN void             e_policy_stack_transient_for_set(E_Client *child, E_Client *parent);
EINTERN void             e_policy_stack_cb_client_remove(E_Client *ec);
EINTERN void             e_policy_stack_hook_pre_fetch(E_Client *ec);
EINTERN void             e_policy_stack_hook_pre_post_fetch(E_Client *ec);

EINTERN void             e_policy_stack_below(E_Client *ec, E_Client *below_ec);

EINTERN void             e_policy_stack_clients_restack_above_lockscreen(E_Client *ec_lock, Eina_Bool show);
EINTERN Eina_Bool        e_policy_stack_check_above_lockscreen(E_Client *ec, E_Layer layer, E_Layer *new_layer, Eina_Bool set_layer);

EINTERN Eina_Bool        e_policy_conf_rot_enable_get(int angle);

EINTERN void             e_policy_interceptors_clean(void);
EINTERN Eina_Bool        e_policy_interceptor_call(E_Policy_Intercept_Point ipoint, E_Client *ec, ...);

E_API Eina_Bool e_policy_aux_message_use_get(E_Client *ec);
E_API void      e_policy_aux_message_send(E_Client *ec, const char *key, const char *val, Eina_List *options);

E_API E_Policy_Interceptor *e_policy_interceptor_add(E_Policy_Intercept_Point ipoint, E_Policy_Intercept_Cb func, const void *data);
E_API void                  e_policy_interceptor_del(E_Policy_Interceptor *pi);

E_API void e_policy_deferred_job(void);
E_API int  e_policy_init(void);
E_API int  e_policy_shutdown(void);
#endif
#endif
