#ifndef E_TEST_CASE_MAIN_H
#define E_TEST_CASE_MAIN_H

typedef struct _E_TC_Client E_TC_Client;
typedef struct _E_Test_Case E_Test_Case;

extern Eldbus_Connection *dbus_conn;
extern Eldbus_Proxy *dbus_proxy;
extern Eldbus_Object *dbus_obj;

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define INTERFACE "org.enlightenment.wm.Test"

struct _E_Test_Case
{
   const char *name;
   const char *type;
   const char *desc;

   Eina_Bool (*test) (E_Test_Case *tc);
   Eina_Bool passed;

   Eina_List *inner_tcs;
};

struct _E_TC_Client
{
   const char *name;
   int x, y, w, h;
   int layer;
   int visible;
   int argb;
   Ecore_Window win;
};

/* test cases */
Eina_Bool test_case_basic(E_Test_Case *tc);
Eina_Bool test_case_basic_stack(E_Test_Case *tc);

Eina_Bool test_case_easy_pass(E_Test_Case *tc);
Eina_Bool test_case_easy_fail(E_Test_Case *tc);
/* end of test cases */

#endif
