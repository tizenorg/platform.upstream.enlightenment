#include "e.h"

struct _E_Obj
{
   int            magic;
   int            size;
   int            type;
   unsigned int   id;
   int            ref;
   E_Obj_Del_Func del_func;
};

/* externally globals */
EAPI Eina_Hash *_e_obj_hash = NULL;

/* local subsystem globals */
static const unsigned int _uint_max = 4294967295U;
static unsigned int e_obj_id = 1;

/* local subsystem functions */
static void
_e_obj_free(void *data)
{
   E_Obj *obj = (E_Obj *)data;

   obj->del_func(obj);
   memset(obj, 0x0, obj->size);
   free(obj);
}

/* externally accessible functions */
E_API void
e_obj_init(void)
{
   _e_obj_hash = eina_hash_int32_new(_e_obj_free);
}

E_API void
e_obj_shutdown(void)
{
   eina_hash_free(_e_obj_hash);
}

E_API E_Obj_H
e_obj_add(int size, int type, E_Obj_Del_Func cb)
{
   E_Obj *obj;
   unsigned int start_id;

   if (e_obj_id >= _uint_max) e_obj_id = 1;

   start_id = e_obj_id;
   while (eina_hash_find(_e_obj_hash, (void *)e_obj_id))
     {
        e_obj_id++;
        if (e_obj_id == start_id) return 0;
     }

   obj = calloc(1, size);
   EINA_SAFETY_ON_NULL_RETURN_VAL(obj, 0);

   obj->magic = E_OBJECT_MAGIC;
   obj->size = size;
   obj->type = type;
   obj->id = e_obj_id;
   obj->ref = 1;
   obj->del_func = cb;

   eina_hash_add(_e_obj_hash, (void *)e_obj_id, obj);

   e_obj_id++;
   return obj->id;
}

E_API int
e_obj_ref(E_Obj_H o)
{
   E_Obj *obj = e_obj_get(o);
   EINA_SAFETY_ON_NULL_RETURN_VAL(obj, 0);

   return obj->ref++;
}

E_API int
e_obj_unref(E_Obj_H o)
{
   E_Obj *obj = e_obj_get(o);
   EINA_SAFETY_ON_NULL_RETURN_VAL(obj, 0);

   obj->ref--;

   if (obj->ref < 0)
     {
        /* insane case */
        ERR("invalid object");
     }

   if (obj->ref <= 0)
     {
        eina_hash_del_by_key(_e_obj_hash, (void *)(obj->id));
        return 0;
     }

   return obj->ref;
}

E_API Eina_Bool
e_obj_is_del(E_Obj_H o)
{
   E_Obj *obj = e_obj_get(o);
   if (!obj) return EINA_TRUE;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(obj->ref <= 0, EINA_TRUE);

   return EINA_FALSE;
}
