#ifndef E_TEST_CASE_BASIC_H
#define E_TEST_CASE_BASIC_H

typedef struct _E_TC_Basic_Info E_TC_Basic_Info;

extern E_TC_Basic_Info *_basic_info;

struct _E_TC_Basic_Info
{
   E_TC_Client *client;
   Evas_Object *elm_win;
   Eina_List *clients;

   Eina_List *sig_hdlrs;

   Eina_Bool wait_close;
};

#endif
