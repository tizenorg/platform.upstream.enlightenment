#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bool_exp_parser.h"
#include "bool_exp_rule_checker.h"

#define MAX_RULE	64

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

typedef struct
{
   POLICY_TYPE policy;
   BINARY_TREE tree;
} RULE;

struct _RULE_CHECKER
{
   RULE rules[MAX_RULE];
   int count;
};

typedef struct
{
   int type;
   int reqID;
   const char * name;
   int pid;
   char * cmd;
} VAL_ARGUMENTS;

typedef struct
{
   char **reply;
   int  *len;
} REPLY_BUFFER;

static int print_func(BINARY_TREE tree, BINARY_TREE_NODE node, BINARY_TREE_NODE parent, void * arg)
{
   REPLY_BUFFER *buffer = (REPLY_BUFFER*)arg;
   char *reply = *buffer->reply;
   int *len = buffer->len;
   char * operators[] = { "==", "<", ">", "<=", ">=", "!=" };

   PARSE_DATA data = bintree_get_node_data(node);

   if (data->node_type == ALL)
     REPLY("ALL");
   else if (data->node_type == AND)
     REPLY(" and ");
   else if (data->node_type == OR)
     REPLY(" or ");
   else // data->node_type == DATA
     {
        if (node == bintree_get_left_child(parent))
          REPLY("(");

        REPLY("%s %s ", data->variable_name, operators[data->compare]);

        if (data->value_type == INTEGER)
          REPLY("%d", data->value.integer);
        else
          REPLY("%s", data->value.string);

        if (node == bintree_get_right_child(parent))
          REPLY(")");
     }

   *buffer->reply = reply;

   return 0;
}

static int compare_string(COMPARER compare, char * str2, char * str1)
{
   int result = strcasecmp(str2, str1);

   switch (compare)
     {
        case EQUAL:
          return result == 0;
        case LESS:
          return result < 0;
        case GREATER:
          return result > 0;
        case LESS_EQ:
          return result <= 0;
        case GREATER_EQ:
          return result >= 0;
        case NOT_EQ:
          return result != 0;
     }

   return 0;
}

static int compare_int(COMPARER compare, int int2, int int1)
{
   switch (compare)
     {
        case EQUAL:
          return int1 == int2;
        case LESS:
          return int1 < int2;
        case GREATER:
          return int1 > int2;
        case LESS_EQ:
          return int1 <= int2;
        case GREATER_EQ:
          return int1 >= int2;
        case NOT_EQ:
          return int1 != int2;
     }

   return 0;
}

static int validate_func(BINARY_TREE tree, BINARY_TREE_NODE node, BINARY_TREE_NODE parent, void * arg)
{
   VAL_ARGUMENTS * args = (VAL_ARGUMENTS*)arg;
   BINARY_TREE_NODE left, right;

   PARSE_DATA left_data = NULL, right_data = NULL;
   PARSE_DATA data = bintree_get_node_data(node);

   data->result = BEP_UNKNOWN;

   if (data->node_type == AND || data->node_type == OR)
     {
        left = bintree_get_left_child(node);
        right = bintree_get_right_child(node);
        if (left == NULL || right == NULL)
          {
             printf("Node error\n");
             return -1;
          }

        left_data = bintree_get_node_data(left);
        right_data = bintree_get_node_data(right);
     }

   if (data->node_type == ALL)
     {
        data->result = BEP_TRUE;
     }
   else if (data->node_type == DATA)
     {
        char iface[64];
        char * msg = NULL;

        if (args->name)
          msg = index(args->name, ':');
        if (msg)
          {
             int min = MIN(sizeof(iface)-1, msg-args->name);
             strncpy(iface, args->name, min);
             iface[min] = '\0';
             msg++;
          }
        if (!strcasecmp(data->variable_name, "TYPE"))
          {
             char * type_string;
             if (args->type == 0)
               type_string = "REQUEST";
             else if (args->type == 1)
               type_string = "EVENT";
             else
               {
                  fprintf (stderr, "Invalid type %d\n", args->type);
                  return -1;
               }

             if (compare_string(data->compare, data->value.string, type_string))
               data->result = BEP_TRUE;
             else
               data->result = BEP_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "IFACE"))
          {
             if (msg && compare_string(data->compare, data->value.string, iface))
               data->result = BEP_TRUE;
             else
               data->result = BEP_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "MSG"))
          {
             if (msg && compare_string(data->compare, data->value.string, msg))
               data->result = BEP_TRUE;
             else
               data->result = BEP_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "PID"))
          {
             if (compare_int(data->compare, data->value.integer, args->pid))
               data->result = BEP_TRUE;
             else
               data->result = BEP_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "CMD") || !strcasecmp(data->variable_name, "COMMAND"))
          {
             if (args->cmd && compare_string(data->compare, data->value.string, args->cmd))
               data->result = BEP_TRUE;
             else
               data->result = BEP_FALSE;
          }
     }
   else if (data->node_type == AND)
     {
        if (left_data->result == BEP_TRUE && right_data->result == BEP_TRUE)
          data->result = BEP_TRUE;
        else
          data->result = BEP_FALSE;
     }
   else if (data->node_type == OR)
     {
        if (left_data->result == BEP_TRUE || right_data->result == BEP_TRUE)
          data->result = BEP_TRUE;
        else
          data->result = BEP_FALSE;
     }
   else
     return -1;

   return 0;
}


RULE_CHECKER rulechecker_init()
{
   RULE_CHECKER rc = calloc(1, sizeof (struct _RULE_CHECKER));
   if (rc == NULL)
     return NULL;

   rc->count = 0;

   return rc;
}

void rulechecker_destroy(RULE_CHECKER rc)
{
   for (int i=rc->count - 1; i>=0; i--)
     rulechecker_remove_rule(rc, i);

   free(rc);
}

RC_RESULT_TYPE rulechecker_add_rule(RULE_CHECKER rc, POLICY_TYPE policy, const char * rule_string)
{
   if (rc->count == MAX_RULE)
     return RC_ERR_TOO_MANY_RULES;

   rc->rules[rc->count].tree = bool_exp_parse(rule_string);
   if (rc->rules[rc->count].tree == NULL)
     return RC_ERR_PARSE_ERROR;

   rc->rules[rc->count].policy = policy;

   rc->count++;

   return RC_OK;
}

RC_RESULT_TYPE rulechecker_remove_rule(RULE_CHECKER rc, int index)
{
   if (index < 0 || index >= rc->count)
     return RC_ERR_NO_RULE;

   bintree_destroy_tree(rc->rules[index].tree);

   rc->count--;
   if (index != rc->count)
      memmove(&rc->rules[index], &rc->rules[index + 1], sizeof (RULE) * (rc->count - index));

   return RC_OK;
}

void rulechecker_print_rule(RULE_CHECKER rc, char *reply, int *len)
{
   REPLY_BUFFER buffer = {&reply, len};
   int i;

   REPLY(" ---------------- Protocol Filter Rules ----------------\n");

   for (i=0; i<rc->count; i++)
     {
        REPLY(" [Rule %d] [%s] \"", i, rc->rules[i].policy == ALLOW ? "ALLOW" : "DENY");

        bintree_inorder_traverse(rc->rules[i].tree, print_func, (void*)&buffer);
        REPLY("\"\n");
     }
}

const char * rulechecker_print_usage()
{
   return
        "######################################################################\n"
        "###     RuleChecker 1.0 for Enlightenment Protocol Log filtering.                  ###\n"
        "###                 Designed and developed by                      ###\n"
        "###                 Duna Oh <duna.oh@samsung.com>, Boram Park <boram1288.park@samsung.com>        ###\n"
        "######################################################################\n"
        "\n"
        "-----------------------------------------------------------------\n"
        "How to read e_info protocol messages :\n"
        "[timestamp] Server --> Client [PID: [pid]] interface@id.message(arguments..) cmd: CMD\n"
        "   ie)\n"
        "[1476930.145] Server --> Client [PID: 103] wl_touch@10.down(758, 6769315, wl_surface@23, 0, 408.000, 831.000) cmd: /usr/bin/launchpad-loader\n"
        "             ==> type = event && pid = 103 && cmd = launchpad-loader && iface = wl_touch && msg = up\n"
        "[4234234.123] Server <-- Client [PID: 123] wl_seat@32.get_touch(new id wl_touch@22) cmd: /usr/bin/launchpad-loader\n"
        "             ==> type = request && pid = 123 && cmd = launchpad-loader && iface = wl_seat && msg = get_touch\n"
        "\n"
        "-----------------------------------------------------------------\n"
        "Usage : enlightenment_info -protocol_rule add [POLICY] [RULE]\n"
        "            enlightenment_info -protocol_rule remove [INDEX]\n"
        "            enlightenment_info -protocol_rule file [RULE_FILE]\n"
        "            enlightenment_info -protocol_rule help / print\n"
        "            [POLICY] : allow / deny \n"
        "            [RULE] : C Language-style boolean expression syntax. [VARIABLE] [COMPAROTOR] [VALUE]\n"
        "            [VARIABLE] : type / iface / msg / cmd(command) / pid\n"
        "            [COMPARATOR] : & / && / and / | / || / or / = / == / != / > / >= / < / <=\n"
        "            [VALUE] : string / number  \n"
        "\n"
        "   ie)\n"
        "            enlightenment_info -protocol_rule add allow \"(type=request) && (iface == wl_pointer and (msg = down or msg = up))\"\n"
        "            enlightenment_info -protocol_rule add deny cmd!= launch-loader\n"
        "            enlightenment_info -protocol_rule remove all\n"
        "            enlightenment_info -protocol_rule remove 3\n"
        "\n";
}

int rulechecker_validate_rule(RULE_CHECKER rc, int type, int reqID, const char * name, int pid, char * cmd)
{
   VAL_ARGUMENTS args = { type, reqID, name, pid, cmd };
   BINARY_TREE_NODE node;
   PARSE_DATA data;

   // set default value here
   POLICY_TYPE default_policy = DENY;
   for (int i=rc->count - 1; i >= 0; i--)
     {
        bintree_postorder_traverse (rc->rules[i].tree, validate_func, &args);
        node = bintree_get_head (rc->rules[i].tree);
        data = bintree_get_node_data(node);

        if (data->result == BEP_TRUE)
          return rc->rules[i].policy == ALLOW;
     }

   return default_policy == ALLOW;
}
