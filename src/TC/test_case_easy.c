#include "e.h"
#include "e_tc_main.h"

Eina_Bool
test_case_easy_fail(E_Test_Case *tc EINA_UNUSED)
{
   return EINA_FALSE;
}

Eina_Bool
test_case_easy_pass(E_Test_Case *tc EINA_UNUSED)
{
   return EINA_TRUE;
}
