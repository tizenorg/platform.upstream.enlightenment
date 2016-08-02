#ifndef E_MOD_INDICATOR_H
#define E_MOD_INDICATOR_H

EINTERN Eina_Bool     e_mod_indicator_client_set(E_Client *ec);

EINTERN void          e_mod_indicator_owner_set(E_Client *ec);
EINTERN E_Client     *e_mod_indicator_owner_get(void);


#endif
