#include "e.h"
#include "e_tc_main.h"

#define ADD_TEST_CASE(test_, name_, type_, desc_) tc = _e_test_case_add(name_, type_, desc_, test_)
#define ADD_INNER_TEST_CASE(test_, name_, type_, desc_) _e_test_case_inner_add(tc, name_, type_, desc_, test_)

Eldbus_Connection *dbus_conn;
Eldbus_Proxy *dbus_proxy;
Eldbus_Object *dbus_obj;

static Eina_List *signal_hdlrs;
static Eina_List *tcs = NULL;

static void
_e_test_case_do(E_Test_Case *tc)
{
   Eina_Bool pass = EINA_FALSE;

   pass = tc->test(tc);
   tc->passed = pass;

   printf("TEST \"%s\" : %s\n", tc->name, pass?"PASS":"FAIL");
}

static void
_e_test_case_inner_add(E_Test_Case* gtc, const char *name, const char *type, const char* desc, Eina_Bool (*test)(E_Test_Case*))
{
   E_Test_Case *tc;

   tc = E_NEW(E_Test_Case, 1);
   tc->name = name;
   tc->type = type;
   tc->desc = desc;
   tc->test = test;

   gtc->inner_tcs = eina_list_append(gtc->inner_tcs, tc);
}

static E_Test_Case *
_e_test_case_add(const char *name, const char *type, const char* desc, Eina_Bool (*test)(E_Test_Case*))
{
   E_Test_Case *tc = E_NEW(E_Test_Case, 1);
   tc->name = name;
   tc->type = type;
   tc->desc = desc;
   tc->test = test;

   tcs = eina_list_append(tcs, tc);

   return tc;
}

EAPI_MAIN int
elm_main(int argc EINA_UNUSED, char **argv EINA_UNUSED)
{
   E_Test_Case *tc, *in_tc;
   Eina_List *l, *ll;
   Eldbus_Signal_Handler *_sh;
   E_TC_Client *client;

   if(!eldbus_init()) return -1;

   /* connect to dbus */
   dbus_conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   dbus_obj = eldbus_object_get(dbus_conn, BUS, PATH);
   dbus_proxy = eldbus_proxy_get(dbus_obj, INTERFACE);

   /* add basic signal handler */

   /* make test case list */
   ADD_TEST_CASE(test_case_basic, "test_case_basic", "Window", "Create basic window and do inner tcs after it shows completely");
   ADD_INNER_TEST_CASE(test_case_basic_stack, "stack", "Test Case", "Check stack of the window");
   ADD_INNER_TEST_CASE(test_case_easy_pass, "pass TC", "Test Case", "Always passed");

   ADD_TEST_CASE(test_case_easy_fail, "fail TC", "Test Case", "Always failed");
   ADD_TEST_CASE(test_case_basic, "test_case_basic2", "Window", "Create basic window");
   ADD_TEST_CASE(test_case_easy_pass, "pass TC", "Test Case", "Always passed");

   /* do test */
   EINA_LIST_FOREACH(tcs, l, tc)
      _e_test_case_do(tc);

   /* print test result */
   printf("\n\n===============================\n");
   printf("All Test done\n");
   EINA_LIST_FOREACH(tcs, l, tc)
     {
        printf("TEST \"%s\" : %s\n", tc->name, tc->passed?"PASS":"FAIL");
        EINA_LIST_FOREACH(tc->inner_tcs, ll, in_tc)
           printf(" TEST \"%s\" : %s\n", in_tc->name, in_tc->passed?"PASS":"FAIL");

     }

tc_shutdown:
   EINA_LIST_FREE(tcs, tc)
     {
        EINA_LIST_FREE(tc->inner_tcs, in_tc)
           E_FREE(in_tc);

        E_FREE(tc);
     }

   EINA_LIST_FREE(signal_hdlrs, _sh)
     {
        eldbus_signal_handler_del(_sh);
     }

   eldbus_proxy_unref(dbus_proxy);
   eldbus_object_unref(dbus_obj);
   eldbus_connection_unref(dbus_conn);

   eldbus_shutdown();
   elm_shutdown();

   return 0;
}
ELM_MAIN()
