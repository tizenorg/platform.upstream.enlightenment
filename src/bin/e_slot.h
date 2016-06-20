#ifdef E_TYPEDEFS

typedef struct _E_Slot E_Slot;
typedef struct _E_Slot_Client E_Slot_Client;

#else

#ifndef E_SLOT_H_
#define E_SLOT_H_

typedef enum _E_Slot_Type
{
   E_SLOT_TYPE_NORMAL = 0,
   E_SLOT_TYPE_TRANSFORM = 1
} E_Slot_Type;

struct _E_Slot
{
   int id;
   Eina_Rectangle rect;
   Eina_List *client_list;
   Eina_Bool changed;
};

struct _E_Slot_Client
{
   E_Client *ec;
   Eina_Rectangle backup_client;
   E_Slot_Type type;
   E_Util_Transform *transform;
};

EAPI void        e_slot_init (void);
EAPI int         e_slot_shutdown(void);

EAPI int         e_slot_new (void);
EAPI void        e_slot_del(int slot_id);
EAPI void        e_slot_move  (int slot_id, int x, int y);
EAPI void        e_slot_resize  (int slot_id, int w, int h);
EAPI void        e_slot_update (int slot_id);

EAPI Eina_Bool   e_slot_client_add(int slot_id, E_Client *ec, E_Slot_Type type);
EAPI Eina_Bool   e_slot_client_remove (int slot_id, E_Client *ec);
EAPI Eina_List*  e_slot_client_list_get(int slot_id);
EAPI int         e_slot_client_count_get(int slot_id);
EAPI int         e_slot_client_slot_id_get(E_Client* ec);
EAPI E_Slot_Type e_slot_client_type_get(E_Client* ec);

EAPI void        e_slot_raise (int slot_id);
EAPI void        e_slot_lower (int slot_id);

EAPI void        e_slot_focus_set(int slot_id);

EAPI Eina_List*  e_slot_list_get(void);
#endif
#endif
