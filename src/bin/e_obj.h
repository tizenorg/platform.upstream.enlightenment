#ifdef E_TYPEDEFS

typedef unsigned int E_Obj_H;
typedef struct _E_Obj E_Obj;
typedef void (*E_Obj_Del_Func) (void *obj);

#else
# ifndef E_OBJ_H
# define E_OBJ_H

extern E_API Eina_Hash *_e_obj_hash;

E_API void      e_obj_init(void);
E_API void      e_obj_shutdown(void);
E_API E_Obj_H   e_obj_add(int size, int type, E_Obj_Del_Func cb);
E_API int       e_obj_ref(E_Obj_H o);
E_API int       e_obj_unref(E_Obj_H o);
E_API Eina_Bool e_obj_is_del(E_Obj_H o);

static inline E_Obj *
e_obj_get(E_Obj_H o)
{
   return (E_Obj *)eina_hash_find(_e_obj_hash, (void *)o);
}

# endif
#endif
