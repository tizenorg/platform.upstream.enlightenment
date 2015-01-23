#include "e.h"
#include "e_tc_main.h"
#include "test_case_basic.h"

Eina_Bool
test_case_basic_stack(E_Test_Case *tc EINA_UNUSED)
{
   E_TC_Client *client;
   Eina_Bool passed = EINA_FALSE;
   Eina_List *l;

   if (!_basic_info) return EINA_FALSE;
   if (!_basic_info->clients) return EINA_FALSE;

   EINA_LIST_FOREACH(_basic_info->clients, l, client)
     {
        if (client->layer > _basic_info->client->layer)
          continue;
        if (client->layer < _basic_info->client->layer)
          break;

        if (!strncmp(client->name, _basic_info->client->name, strlen(client->name)))
          passed = EINA_TRUE;

        break;
     }

   return passed;
}
