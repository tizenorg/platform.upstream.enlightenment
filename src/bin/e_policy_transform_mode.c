#include "e.h"
#include "e_policy_transform_mode.h"

#define E_TRANSFORM_MODE_NAME         "wm.policy.win.transform.mode"
#define E_TRANSFORM_MODE_AUTOFIT      "autofit"
#define E_TRANSFORM_MODE_RATIOFIT     "ratiofit"

typedef struct _E_Mod_Transform_Client     E_Mod_Transform_Client;
typedef struct _E_Mod_Transform_Manager    E_Mod_Transform_Manager;

typedef enum
{
   E_Mod_Transform_Type_None = 0,
   E_Mod_Transform_Type_AutoFit,
   E_Mod_Transform_Type_RatioFit

}E_Mod_Transform_Type;

struct _E_Mod_Transform_Client
{
   E_Client             *ec;
   E_Util_Transform     *transform;
   Eina_Bool             user_geometry_by_me;
   E_Mod_Transform_Type  transform_type;
   int x, y, w, h;
};

struct _E_Mod_Transform_Manager
{
   Eina_List *transform_client_list;
   Eina_List *hook_list;
   Eina_List *event_list;
};

static E_Mod_Transform_Manager *_transform_mode = NULL;

static E_Mod_Transform_Client    *_e_policy_transform_client_new(E_Client *ec, E_Mod_Transform_Manager *mng);
static void                       _e_policy_transform_client_del(E_Mod_Transform_Client *transform_ec, E_Mod_Transform_Manager *mng);
static E_Mod_Transform_Client    *_e_policy_transform_client_find(E_Client *ec, E_Mod_Transform_Manager *mng);
static void                       _e_policy_transform_client_mode_change(E_Mod_Transform_Client *transform_ec, E_Mod_Transform_Type mode);
static void                       _e_policy_transform_client_mode_cancel(E_Mod_Transform_Client *transform_ec);
static void                       _e_policy_transform_client_mode_autofit_ratiofit_set(E_Mod_Transform_Client *transform_ec, E_Mod_Transform_Type mode);
static void                       _e_policy_transform_client_mode_autofit_ratiofit_cancel(E_Mod_Transform_Client *transform_ec);

static void                       _e_policy_transform_cb_aux_change(void *data, E_Client *ec);
static void                       _e_policy_transform_cb_client_del(void *data, E_Client *ec);
static void                       _e_policy_transform_cb_client_move_resize(void *data, Evas *e, Evas_Object *obj, void *event_info);


static E_Mod_Transform_Type       _e_policy_transform_mode_string_to_enum(const char *str_mode);
static void                       _e_policy_transform_screen_size_get(E_Mod_Transform_Client *transform_ec, int *w, int *h);
static void                       _e_policy_transform_client_size_get(E_Mod_Transform_Client *transform_ec, int *w, int *h);
static void                       _e_policy_transform_client_pos_get(E_Mod_Transform_Client *transform_ec, int *x, int *y);
static Eina_Bool                  _e_policy_transform_client_geometry_change_check(E_Mod_Transform_Client *transform_ec);

Eina_Bool
e_policy_transform_mode_init(void)
{
   if (_transform_mode) return EINA_FALSE;

   _transform_mode = (E_Mod_Transform_Manager*)malloc(sizeof(E_Mod_Transform_Manager));
   memset(_transform_mode, 0, sizeof(E_Mod_Transform_Manager));

   _transform_mode->hook_list = eina_list_append(_transform_mode->hook_list,
                                                 e_client_hook_add(E_CLIENT_HOOK_AUX_HINT_CHANGE ,
                                                                   _e_policy_transform_cb_aux_change,
                                                                   _transform_mode));

   _transform_mode->hook_list = eina_list_append(_transform_mode->hook_list,
                                                 e_client_hook_add(E_CLIENT_HOOK_DEL ,
                                                                   _e_policy_transform_cb_client_del,
                                                                   _transform_mode));

   e_hints_aux_hint_supported_add(E_TRANSFORM_MODE_NAME);
   return EINA_TRUE;
}

void
e_policy_transform_mode_shutdown(void)
{
   if (!_transform_mode) return;

   while(_transform_mode->transform_client_list)
     {
        _e_policy_transform_client_del(eina_list_nth(_transform_mode->transform_client_list, 0), _transform_mode);
     }

   if (_transform_mode->hook_list)
     {
        E_Client_Hook *hook;

        EINA_LIST_FREE(_transform_mode->hook_list, hook)
          {
             e_client_hook_del(hook);
          }
     }

   free(_transform_mode);
   _transform_mode = NULL;

   e_hints_aux_hint_supported_del(E_TRANSFORM_MODE_NAME);
}

static E_Mod_Transform_Client*
_e_policy_transform_client_new(E_Client *ec, E_Mod_Transform_Manager *mng)
{
   E_Mod_Transform_Client *result = NULL;

   if (!mng) return NULL;
   if (!ec) return NULL;
   if (_e_policy_transform_client_find(ec, mng)) return NULL;

   result = (E_Mod_Transform_Client*)malloc(sizeof(E_Mod_Transform_Client));
   memset(result, 0, sizeof(E_Mod_Transform_Client));
   result->ec = ec;
   result->transform = e_util_transform_new();

   mng->transform_client_list = eina_list_append(mng->transform_client_list, result);
   return result;
}

static void
_e_policy_transform_client_del(E_Mod_Transform_Client *transform_ec, E_Mod_Transform_Manager *mng)
{
   if (!mng) return;
   if (!transform_ec) return;

   if (transform_ec->transform)
     {
        _e_policy_transform_client_mode_cancel(transform_ec);
        e_util_transform_del(transform_ec->transform);
        transform_ec->transform = NULL;
     }

   mng->transform_client_list = eina_list_remove(mng->transform_client_list, transform_ec);
   free(transform_ec);
}

static E_Mod_Transform_Client*
_e_policy_transform_client_find(E_Client *ec, E_Mod_Transform_Manager *mng)
{
   E_Mod_Transform_Client *temp = NULL;
   E_Mod_Transform_Client *result = NULL;
   Eina_List *l = NULL;

   if (!mng) return NULL;
   if (!ec) return NULL;

   EINA_LIST_FOREACH(mng->transform_client_list, l, temp)
     {
        if (temp->ec == ec)
          {
             result = temp;
             break;
          }
     }
   return result;
}

static void
_e_policy_transform_client_mode_change(E_Mod_Transform_Client *transform_ec, E_Mod_Transform_Type mode)
{
   if (!transform_ec) return;
   if (mode == E_Mod_Transform_Type_None) return;

   if (transform_ec->transform_type != mode)
     {
        _e_policy_transform_client_mode_cancel(transform_ec);

        if (mode == E_Mod_Transform_Type_AutoFit ||
            mode == E_Mod_Transform_Type_RatioFit)
          {
             _e_policy_transform_client_mode_autofit_ratiofit_set(transform_ec, mode);
          }
     }
}

static void
_e_policy_transform_client_mode_cancel(E_Mod_Transform_Client *transform_ec)
{
   if (!transform_ec) return;
   if (transform_ec->transform_type == E_Mod_Transform_Type_None) return;

   if (transform_ec->transform_type == E_Mod_Transform_Type_AutoFit ||
       transform_ec->transform_type == E_Mod_Transform_Type_RatioFit)
     {
        _e_policy_transform_client_mode_autofit_ratiofit_cancel(transform_ec);
     }

   transform_ec->transform_type = E_Mod_Transform_Type_None;
}

static void
_e_policy_transform_client_mode_autofit_ratiofit_set(E_Mod_Transform_Client *transform_ec,  E_Mod_Transform_Type mode)
{
   if (!transform_ec) return;
   if (transform_ec->transform_type != E_Mod_Transform_Type_None) return;

   if (transform_ec->transform)
     {
        if (mode == E_Mod_Transform_Type_RatioFit)
           e_util_transform_keep_ratio_set(transform_ec->transform, EINA_TRUE);
        e_client_transform_core_add(transform_ec->ec, transform_ec->transform);
     }
   _e_policy_transform_cb_client_move_resize(transform_ec, 0, 0, 0);

   if (transform_ec->ec->frame)
     {
        evas_object_event_callback_add(transform_ec->ec->frame, EVAS_CALLBACK_RESIZE, _e_policy_transform_cb_client_move_resize, transform_ec);
        evas_object_event_callback_add(transform_ec->ec->frame, EVAS_CALLBACK_MOVE, _e_policy_transform_cb_client_move_resize, transform_ec);
     }

   e_policy_allow_user_geometry_set(transform_ec->ec, EINA_TRUE);
   transform_ec->transform_type = mode;
}

static void
_e_policy_transform_client_mode_autofit_ratiofit_cancel(E_Mod_Transform_Client *transform_ec)
{
   if (!transform_ec) return;
   if (transform_ec->transform_type != E_Mod_Transform_Type_AutoFit &&
       transform_ec->transform_type != E_Mod_Transform_Type_RatioFit) return;

   if (transform_ec->transform)
     {
        e_util_transform_keep_ratio_set(transform_ec->transform, EINA_FALSE);
        e_client_transform_core_remove(transform_ec->ec, transform_ec->transform);
     }

   if (transform_ec->ec->frame)
     {
        evas_object_event_callback_del(transform_ec->ec->frame, EVAS_CALLBACK_RESIZE, _e_policy_transform_cb_client_move_resize);
        evas_object_event_callback_del(transform_ec->ec->frame, EVAS_CALLBACK_MOVE, _e_policy_transform_cb_client_move_resize);
     }

   e_policy_allow_user_geometry_set(transform_ec->ec, EINA_FALSE);
   transform_ec->transform_type = E_Mod_Transform_Type_None;
}

static void
_e_policy_transform_cb_aux_change(void *data, E_Client *ec)
{
   E_Mod_Transform_Manager *mng = (E_Mod_Transform_Manager*)data;
   E_Mod_Transform_Client  *t_ec = NULL;
   E_Mod_Transform_Type     t_mode = E_Mod_Transform_Type_None;
   const char              *t_str = NULL;

   if (!ec) return;
   if (!mng) return;

   t_ec = _e_policy_transform_client_find(ec, mng);
   t_str = e_hints_aux_hint_value_get(ec, E_TRANSFORM_MODE_NAME);
   t_mode = _e_policy_transform_mode_string_to_enum(t_str);

   if (t_ec)
     {
        if (t_mode == E_Mod_Transform_Type_None)
           _e_policy_transform_client_del(t_ec, mng);
        else
           _e_policy_transform_client_mode_change(t_ec, t_mode);
     }
   else
     {
        if (t_mode != E_Mod_Transform_Type_None)
          {
             t_ec = _e_policy_transform_client_new(ec, mng);
             _e_policy_transform_client_mode_change(t_ec, t_mode);
          }
     }
}

static void
_e_policy_transform_cb_client_del(void *data, E_Client *ec)
{
   E_Mod_Transform_Manager *mng = (E_Mod_Transform_Manager*)data;
   E_Mod_Transform_Client *transform_ec = NULL;

   if (!mng) return;
   if (!ec) return;

   transform_ec = _e_policy_transform_client_find(ec, mng);

   if (transform_ec)
      _e_policy_transform_client_del(transform_ec, mng);
}

static void
_e_policy_transform_cb_client_move_resize(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
   E_Mod_Transform_Client *transform_ec = (E_Mod_Transform_Client*)data;
   int zone_w = 1, zone_h = 1;
   double sx = 1.0, sy = 1.0;
   int w = 1, h = 1;
   int x, y;

   if (!transform_ec) return;
   if (!_e_policy_transform_client_geometry_change_check(transform_ec)) return;

   _e_policy_transform_screen_size_get(transform_ec, &zone_w, &zone_h);
   _e_policy_transform_client_size_get(transform_ec, &w, &h);
   _e_policy_transform_client_pos_get(transform_ec, &x, &y);

   sx = (double) zone_w / (double) w;
   sy = (double) zone_h / (double) h;

   e_util_transform_init(transform_ec->transform);
   if (transform_ec->transform_type == E_Mod_Transform_Type_RatioFit)
      e_util_transform_keep_ratio_set(transform_ec->transform, EINA_TRUE);
   e_util_transform_move(transform_ec->transform, -x, -y, 0.0);
   e_util_transform_scale(transform_ec->transform, sx, sy, 1.0);
}

static E_Mod_Transform_Type
_e_policy_transform_mode_string_to_enum(const char *str_mode)
{
   if (!str_mode)                                                   return E_Mod_Transform_Type_None;
   else if (!e_util_strcmp(str_mode, E_TRANSFORM_MODE_AUTOFIT))     return E_Mod_Transform_Type_AutoFit;
   else if (!e_util_strcmp(str_mode, E_TRANSFORM_MODE_RATIOFIT))    return E_Mod_Transform_Type_RatioFit;

   return E_Mod_Transform_Type_None;
}

static void
_e_policy_transform_screen_size_get(E_Mod_Transform_Client *transform_ec, int *w, int *h)
{
   int zone_w = 1;
   int zone_h = 1;

   if (!transform_ec) return;

   if (transform_ec->ec->zone)
     {
        zone_w = transform_ec->ec->zone->w;
        zone_h = transform_ec->ec->zone->h;
     }
   else
     {
        zone_w = 1920;
        zone_h = 1080;
     }

   if (zone_w < 1) zone_w = 1;
   if (zone_h < 1) zone_h = 1;

   if (w) *w = zone_w;
   if (h) *h = zone_h;
}

static void
_e_policy_transform_client_size_get(E_Mod_Transform_Client *transform_ec, int *w, int *h)
{
   int ec_w = 1;
   int ec_h = 1;

   if (!transform_ec) return;

   ec_w = transform_ec->ec->w;
   ec_h = transform_ec->ec->h;

   if (ec_w < 1) ec_w = 1;
   if (ec_h < 1) ec_h = 1;

   if (w) *w = ec_w;
   if (h) *h = ec_h;
}

static void
_e_policy_transform_client_pos_get(E_Mod_Transform_Client *transform_ec, int *x, int *y)
{
   if (!transform_ec) return;

   if (x) *x = transform_ec->ec->x;
   if (y) *y = transform_ec->ec->y;

}

static Eina_Bool
_e_policy_transform_client_geometry_change_check(E_Mod_Transform_Client *transform_ec)
{
   if (!transform_ec) return EINA_FALSE;
   if (transform_ec->ec->x != transform_ec->x ||
       transform_ec->ec->y != transform_ec->y ||
       transform_ec->ec->w != transform_ec->w ||
       transform_ec->ec->h != transform_ec->h)
     {
        transform_ec->x = transform_ec->ec->x;
        transform_ec->y = transform_ec->ec->y;
        transform_ec->w = transform_ec->ec->w;
        transform_ec->h = transform_ec->ec->h;

        return EINA_TRUE;
     }

   return EINA_FALSE;
}
