#ifdef E_TYPEDEFS

#define E_MODULE_API_VERSION 17

typedef struct _E_Module     E_Module;
typedef struct _E_Module_Api E_Module_Api;

typedef struct _E_Event_Module_Update E_Event_Module_Update;

/* e-mod-tizen-eom related */
typedef struct _E_Module_EOM_Data E_Module_EOM_Data;

#else
#ifndef E_MODULE_H
#define E_MODULE_H

#define E_MODULE_TYPE 0xE0b0100b

/* e-mod-tize-eom related */
#include "e_comp_wl.h"

extern E_API int E_EVENT_MODULE_UPDATE;
extern E_API int E_EVENT_MODULE_INIT_END;
extern E_API int E_EVENT_MODULE_DEFER_JOB;

struct _E_Event_Module_Update
{
   const char *name;
   Eina_Bool enabled : 1;
};

struct _E_Module
{
   E_Object             e_obj_inherit;

   E_Module_Api        *api;

   Eina_Stringshare    *name;
   Eina_Stringshare    *dir;
   void                *handle;

   struct {
      void * (*init)        (E_Module *m);
      int    (*shutdown)    (E_Module *m);
      int    (*save)        (E_Module *m);
   } func;

   Eina_Bool        enabled : 1;
   Eina_Bool        error : 1;

   /* the module is allowed to modify these */
   void                *data;
};

struct _E_Module_Api
{
   int         version;
   const char *name;
};

/* e-mod-tize-eom related */
struct _E_Module_EOM_Data
{
   Eina_Bool (*output_is_external)(struct wl_resource *output_resource);
};

EINTERN int          e_module_init(void);
EINTERN int          e_module_shutdown(void);

E_API void         e_module_deferred_job(void);
E_API void         e_module_all_load(void);
E_API E_Module    *e_module_new(const char *name);
E_API int          e_module_save(E_Module *m);
E_API const char  *e_module_dir_get(E_Module *m);
E_API int          e_module_enable(E_Module *m);
E_API int          e_module_disable(E_Module *m);
E_API int          e_module_enabled_get(E_Module *m);
E_API int          e_module_save_all(void);
E_API E_Module    *e_module_find(const char *name);
E_API Eina_List   *e_module_list(void);
E_API void         e_module_delayed_set(E_Module *m, int delayed);
E_API void         e_module_priority_set(E_Module *m, int priority);
E_API Eina_Bool   e_module_loading_get(void);
#endif
#endif
