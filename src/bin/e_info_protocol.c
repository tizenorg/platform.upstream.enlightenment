#include "e.h"
#include "e_info_protocol.h"

#include "bintree.h"

#define STRING_MAX	64
#define MAX_RULE	64

#ifndef REPLY
#define REPLY(fmt, ARG...)  \
   do { \
        if (reply && len && *len > 0) \
          { \
             int s = snprintf(reply, *len, fmt, ##ARG); \
             reply += s; \
             *len -= s; \
          } \
     } while (0)
#endif

typedef struct _PARSE_DATA * PARSE_DATA;
typedef struct _TOKEN_DATA * TOKEN_DATA;
typedef struct _RULE_CHECKER * RULE_CHECKER;

typedef enum
{
   NONE,
   AND,
   OR,
   DATA,
   ALL
} NODE_TYPE;

typedef enum
{
   EQUAL,
   LESS,
   GREATER,
   LESS_EQ,
   GREATER_EQ,
   NOT_EQ
} COMPARER;

typedef enum
{
   INTEGER,
   STRING
} DATA_TYPE;

struct _PARSE_DATA
{
   NODE_TYPE node_type;

   char variable_name[STRING_MAX];
   COMPARER comparer;
   DATA_TYPE value_type;

   union
   {
      char string[STRING_MAX];
      int integer;
   } value;

   enum
   {
      BEP_UNKNOWN,
      BEP_TRUE,
      BEP_FALSE
   } result;
};

typedef enum
{
   BET_UNKNOWN = 0,
   BET_L_BR = 1,
   BET_R_BR = 2,
   BET_NOT_EQ = 3,
   BET_EQUAL = 4,
   BET_LSS_THAN = 5,
   BET_LSS_EQ = 6,
   BET_GRT_THAN = 7,
   BET_GRT_EQ = 8,
   BET_AND = 9,
   BET_OR = 10,
   BET_SPACE = 11,
   BET_SYMBOL = 12,
   BET_NUMBER = 13,
   BET_EOS = 14,
} TOKEN;

struct _TOKEN_DATA
{
   const char ** string;
   TOKEN last_token;
   const char * last_symbol;
   int symbol_len;
};

typedef enum
{
   UNDEFINED,
   ALLOW,
   DENY
} POLICY_TYPE;

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

typedef enum
{
   RC_OK,
   RC_ERR_TOO_MANY_RULES,
   RC_ERR_PARSE_ERROR,
   RC_ERR_NO_RULE
} RC_RESULT_TYPE;

static BINARY_TREE_NODE _create_tree_node_from_token(BINARY_TREE tree, TOKEN_DATA token);

static RULE_CHECKER rc = NULL;

// should be increasing order for binary search
static struct
{
   const char * token_char;
   const int token_length;
   TOKEN token_name;
} token_table[] =
{
   { "\0",	1, BET_EOS }, // 0
   { "\t",	1, BET_SPACE }, // 9
   { " ",	1, BET_SPACE }, // 32
   { "!=",	2, BET_NOT_EQ }, // 33 61
   { "&",	1, BET_AND }, // 38
   { "&&",	2, BET_AND }, // 38 38
   { "(",	1, BET_L_BR }, // 40
   { ")",	1, BET_R_BR }, // 41
   { "<",	1, BET_LSS_THAN }, // 60
   { "<=",	2, BET_LSS_EQ }, // 60 61
   { "<>",	2, BET_NOT_EQ }, // 60 62
   { "=",	1, BET_EQUAL }, // 61
   { "==",	2, BET_EQUAL }, // 61 61
   { ">",	1, BET_GRT_THAN }, // 62
   { ">=",	2, BET_GRT_EQ }, // 62 61
   { "and",3, BET_AND }, // 97 110
   { "or",	2, BET_OR }, // 111 114
   { "|",	1, BET_OR }, // 124
   { "||",	2, BET_OR }, // 124 124
};

TOKEN _get_next_token(const char ** string)
{
   static int token_cnt = sizeof (token_table) / sizeof (token_table[0]);
   int i, compare_res, found = 0, first, last;

   first = 0;
   last = token_cnt - 1;

   i = (first + last) / 2;
   while (1)
     {
        compare_res = strncmp(*string, token_table[i].token_char, token_table[i].token_length);
        while (compare_res == 0)
          {
             found = 1;
             i++;
             if (i == token_cnt)
               break;
             compare_res = strncmp(*string, token_table[i].token_char, token_table[i].token_length);
          }

        if (found)
          {
             i--;
             *string += token_table[i].token_length;

             return token_table[i].token_name;
          }

        if (first >= last)
          break;

        if (compare_res > 0)
          first = i + 1;
        else
          last = i - 1;

        i = (first + last) / 2;
     }
   if (isalpha(**string))
     {
        (*string)++;
        while (isalpha(**string) || isdigit(**string) || **string == '_' || **string == '-')
          (*string)++;

        return BET_SYMBOL;
     }
   if (isdigit(**string))
     {
        (*string)++;
        while (isdigit(**string))
          (*string)++;

        return BET_NUMBER;
    }

   return BET_UNKNOWN;
}

static void _process_token(TOKEN_DATA token)
{
   do
     {
        token->last_symbol = *(token->string);
        token->last_token = _get_next_token(token->string);
        token->symbol_len = *(token->string) - token->last_symbol;
     }
   while (token->last_token == BET_SPACE);
}

static BINARY_TREE_NODE _create_tree_node_from_statement(BINARY_TREE tree, TOKEN_DATA token)
{
   BINARY_TREE_NODE node = NULL;
   PARSE_DATA data = NULL;

   if (token->last_token == BET_L_BR)
     {
        _process_token(token);

        node = _create_tree_node_from_token(tree, token);
        if (node == NULL)
          return NULL;

        if (token->last_token != BET_R_BR)
          goto fail;
        _process_token(token);

        return node;
     }

   if (token->last_token != BET_SYMBOL)
     goto fail;

   node = bintree_create_node(tree);

   data = (PARSE_DATA) bintree_get_node_data(node);

   strncpy(data->variable_name, token->last_symbol, token->symbol_len);
   data->variable_name[token->symbol_len] = '\0';

   if (!strcasecmp(data->variable_name, "all"))
     {
        data->node_type = ALL;
        _process_token(token);

        return node;
     }

   data->node_type = DATA;

   _process_token(token);

   switch (token->last_token)
     {
      case BET_NOT_EQ:
        data->comparer = NOT_EQ;
        break;
      case BET_EQUAL:
        data->comparer = EQUAL;
        break;
      case BET_LSS_THAN:
        data->comparer = LESS;
        break;
      case BET_LSS_EQ:
        data->comparer = LESS_EQ;
        break;
      case BET_GRT_THAN:
        data->comparer = GREATER;
        break;
      case BET_GRT_EQ:
        data->comparer = GREATER_EQ;
        break;
      default:
        goto fail;
     }

   _process_token(token);

   if (token->last_token == BET_NUMBER)
     {
        data->value_type = INTEGER;
        data->value.integer = atoi(token->last_symbol);
     }
   else if (token->last_token == BET_SYMBOL)
     {
        data->value_type = STRING;
        strncpy(data->value.string, token->last_symbol, token->symbol_len);
        data->value.string[token->symbol_len] = '\0';
     }
   else
     {
        goto fail;
     }

   _process_token(token);

   return node;

fail:
   if (node)
     bintree_remove_node_recursive(node);

   return NULL;
}

static BINARY_TREE_NODE _create_tree_node_from_token(BINARY_TREE tree, TOKEN_DATA token)
{
   BINARY_TREE_NODE node = NULL;
   BINARY_TREE_NODE left = NULL;
   BINARY_TREE_NODE right = NULL;

   PARSE_DATA data;

   node = _create_tree_node_from_statement(tree, token);
   if (node == NULL)
     {
        printf("PARSE statement error\n");
        goto fail;
     }

   while (token->last_token == BET_AND)
     {
        left = node;
        node = NULL;

        _process_token(token);
        right = _create_tree_node_from_statement(tree, token);
        if (right == NULL)
          goto fail;

        node = bintree_create_node(tree);

        data = (PARSE_DATA) bintree_get_node_data(node);
        data->node_type = AND;
        bintree_set_left_child(node, left);
        bintree_set_right_child(node, right);
     }

   if (token->last_token == BET_OR)
     {
        left = node;
        node = NULL;

        _process_token(token);
        right = _create_tree_node_from_token(tree, token);
        if (right == NULL)
          goto fail;

        node = bintree_create_node(tree);

        data = (PARSE_DATA) bintree_get_node_data(node);
        data->node_type = OR;
        bintree_set_left_child(node, left);
        bintree_set_right_child(node, right);
     }

   return node;

fail:
   if (left)
     bintree_remove_node_recursive(left);

   return NULL;
}

BINARY_TREE _create_tree_from_rule_string(const char * string)
{
   BINARY_TREE tree = bintree_create_tree(sizeof (struct _PARSE_DATA));
   BINARY_TREE_NODE node;

   struct _TOKEN_DATA token;

   token.string = &string;
   _process_token(&token);

   node = _create_tree_node_from_token(tree, &token);
   if (node == NULL)
     {
        bintree_destroy_tree(tree);
        return NULL;
     }

   bintree_set_head(tree, node);

   return tree;
}

static int _print_rule_func(BINARY_TREE tree, BINARY_TREE_NODE node, BINARY_TREE_NODE parent, void * arg)
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

        REPLY("%s %s ", data->variable_name, operators[data->comparer]);

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

static int _compare_string_by_comparer(COMPARER comparer, char * str2, char * str1)
{
   int result = strcasecmp(str2, str1);

   switch (comparer)
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

static int _compare_int_by_comparer(COMPARER comparer, int int2, int int1)
{
   switch (comparer)
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

static int _validate_rule_func(BINARY_TREE tree, BINARY_TREE_NODE node, BINARY_TREE_NODE parent, void * arg)
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

             if (_compare_string_by_comparer(data->comparer, data->value.string, type_string))
               data->result = BEP_TRUE;
             else
               data->result = BEP_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "IFACE"))
          {
             if (msg && _compare_string_by_comparer(data->comparer, data->value.string, iface))
               data->result = BEP_TRUE;
             else
               data->result = BEP_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "MSG"))
          {
             if (msg && _compare_string_by_comparer(data->comparer, data->value.string, msg))
               data->result = BEP_TRUE;
             else
               data->result = BEP_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "PID"))
          {
             if (_compare_int_by_comparer(data->comparer, data->value.integer, args->pid))
               data->result = BEP_TRUE;
             else
               data->result = BEP_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "CMD") || !strcasecmp(data->variable_name, "COMMAND"))
          {
             if (args->cmd && _compare_string_by_comparer(data->comparer, data->value.string, args->cmd))
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


RULE_CHECKER _rulechecker_init()
{
   RULE_CHECKER rc = calloc(1, sizeof (struct _RULE_CHECKER));
   if (rc == NULL)
     return NULL;

   rc->count = 0;

   return rc;
}

RC_RESULT_TYPE _rulechecker_rule_add(RULE_CHECKER rc, POLICY_TYPE policy, const char * rule_string)
{
   if (rc->count == MAX_RULE)
     return RC_ERR_TOO_MANY_RULES;

   rc->rules[rc->count].tree = _create_tree_from_rule_string(rule_string);
   if (rc->rules[rc->count].tree == NULL)
     return RC_ERR_PARSE_ERROR;

   rc->rules[rc->count].policy = policy;

   rc->count++;

   return RC_OK;
}

RC_RESULT_TYPE _rulechecker_rule_remove(RULE_CHECKER rc, int index)
{
   if (index < 0 || index >= rc->count)
     return RC_ERR_NO_RULE;

   bintree_destroy_tree(rc->rules[index].tree);

   rc->count--;
   if (index != rc->count)
      memmove(&rc->rules[index], &rc->rules[index + 1], sizeof (RULE) * (rc->count - index));

   return RC_OK;
}

void _rulechecker_rule_print(RULE_CHECKER rc, char *reply, int *len)
{
   REPLY_BUFFER buffer = {&reply, len};
   int i;

   REPLY(" ---------------- Protocol Filter Rules ----------------\n");

   for (i=0; i<rc->count; i++)
     {
        REPLY(" [Rule %d] [%s] \"", i, rc->rules[i].policy == ALLOW ? "ALLOW" : "DENY");

        bintree_inorder_traverse(rc->rules[i].tree, _print_rule_func, (void*)&buffer);
        REPLY("\"\n");
     }
}

const char * _rulechecker_usage_print()
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

void _rulechecker_destroy(RULE_CHECKER rc)
{
   for (int i=rc->count - 1; i>=0; i--)
     _rulechecker_rule_remove(rc, i);

   free(rc);
}

int _rulechecker_rule_validate(RULE_CHECKER rc, int type, int reqID, const char * name, int pid, char * cmd)
{
   VAL_ARGUMENTS args = { type, reqID, name, pid, cmd };
   BINARY_TREE_NODE node;
   PARSE_DATA data;

   // set default value here
   POLICY_TYPE default_policy = DENY;
   for (int i=rc->count - 1; i >= 0; i--)
     {
        bintree_postorder_traverse (rc->rules[i].tree, _validate_rule_func, &args);
        node = bintree_get_head (rc->rules[i].tree);
        data = bintree_get_node_data(node);

        if (data->result == BEP_TRUE)
          return rc->rules[i].policy == ALLOW;
     }

   return default_policy == ALLOW;
}

static void
_mergeArgs (char *target, int target_size, int argc, const char ** argv)
{
   int i, len;

   for (i = 0; i < argc; i++)
     {
        len = snprintf(target, target_size, "%s", argv[i]);
        target += len;
        target_size -= len;

        if (i != argc - 1)
          {
             *(target++) = ' ';
             target_size--;
          }
     }
}

static int
_strcasecmp(const char *str1, const char *str2)
{
   const u_char *us1 = (const u_char *) str1, *us2 = (const u_char *) str2;

   while (tolower(*us1) == tolower(*us2))
     {
        if (*us1++ == '\0')
          return 0;

        us2++;
     }

   return (tolower(*us1) - tolower(*us2));
}

char*
_e_info_protocol_cmd_get(char *path)
{
   char *p;

   if (!path) return NULL;

   p = strrchr(path, '/');

   return (p) ? p+1 : path;
}

Eina_Bool
e_info_protocol_rule_set(const int argc, const char **argv, char *reply, int *len)
{
   const char * command;

   if (rc == NULL)
        rc = _rulechecker_init();

   if (argc == 0)
     {
        _rulechecker_rule_print(rc, reply, len);
        return EINA_TRUE;
     }

   command = argv[0];

   if (!_strcasecmp(command, "add"))
     {
        POLICY_TYPE policy_type;
        RC_RESULT_TYPE result;
        const char * policy = argv[1];
        char merge[8192] = {0,}, rule[8192] = {0,};
        int i, index = 0, size_rule;
        int apply = 0;

        if (argc < 3)
          {
             REPLY("Error : Too few arguments.\n");
             return EINA_FALSE;
          }

        if (!_strcasecmp(policy, "ALLOW"))
          policy_type = ALLOW;
        else if (!_strcasecmp(policy, "DENY"))
          policy_type = DENY;
        else
          {
             REPLY("Error : Unknown policy : [%s].\n          Policy should be ALLOW or DENY.\n", policy);
             return EINA_FALSE;
          }

        _mergeArgs(merge, sizeof(merge), argc - 2, &(argv[2]));

        size_rule = sizeof(rule) - 1;

        for (i = 0 ; i < strlen(merge) ; i++)
          {
             if (merge[i] == '\"' || merge[i] == '\'')
               {
                  rule[index++] = ' ';
                  if (index > size_rule)
                    return EINA_FALSE;

                  continue;
               }

             if (merge[i] == '+')
               {
                  rule[index++] = ' ';
                  if (index > size_rule)
                    return EINA_FALSE;

                  if (apply == 0)
                    {
                       const char* plus = "|| type=reply || type=error";
                       int len = MIN (size_rule - index, strlen(plus));
                       strncat(rule, plus, len);
                       index += len;
                       if (index > size_rule)
                         return EINA_FALSE;

                       apply = 1;
                    }
                  continue;
               }
             rule[index++] = merge[i];
             if (index > size_rule)
               return EINA_FALSE;
          }

        result = _rulechecker_rule_add(rc, policy_type, rule);
        if (result == RC_ERR_TOO_MANY_RULES)
          {
             REPLY("Error : Too many rules were added.\n");
             return EINA_FALSE;
          }
        else if (result == RC_ERR_PARSE_ERROR)
          {
             REPLY("Error : An error occured during parsing the rule [%s]\n", rule);
             return EINA_FALSE;
          }

        REPLY("The rule was successfully added.\n\n");
        _rulechecker_rule_print(rc, reply, len);
        return EINA_TRUE;
     }
   else if (!_strcasecmp(command, "remove"))
     {
        const char * remove_idx;
        int i;

        if (argc < 2)
          {
             REPLY("Error : Too few arguments.\n");
             return EINA_FALSE;
          }

        for (i = 0; i < argc - 1; i++)
          {
             remove_idx = argv[i + 1];

             if (!_strcasecmp(remove_idx, "all"))
               {
                  _rulechecker_destroy(rc);
                  rc = _rulechecker_init();
                  REPLY("Every rules were successfully removed.\n");
               }
             else
               {
                  int index = atoi(remove_idx);
                  if (isdigit(*remove_idx) && _rulechecker_rule_remove(rc, index) == 0)
                    REPLY("The rule [%d] was successfully removed.\n", index);
                  else
                    REPLY("Rule remove fail : No such rule [%s].\n", remove_idx);
               }
          }
        _rulechecker_rule_print(rc, reply, len);

        return EINA_TRUE;
     }
   else if (!_strcasecmp(command, "file"))
     {
        if (argc < 2)
          {
             REPLY("Error : Too few arguments.\n");
             return EINA_FALSE;
          }

        if (!e_info_protocol_rule_file_set(argv[1], reply, len))
          return EINA_FALSE;
        _rulechecker_rule_print(rc, reply, len);

        return EINA_TRUE;
     }
   else if (!_strcasecmp(command, "print"))
     {
        _rulechecker_rule_print (rc, reply, len);

        return EINA_TRUE;
     }
   else if (!_strcasecmp(command, "help"))
     {
        REPLY("%s", _rulechecker_usage_print());

        return EINA_TRUE;
     }

   REPLY("%s\nUnknown command : [%s].\n\n", _rulechecker_usage_print(), command);

   return EINA_TRUE;
}

Eina_Bool
e_info_protocol_rule_file_set(const char *filename, char *reply, int *len)
{
   int   fd = -1, rule_len;
   char  fs[8096], *pfs;

   fd = open (filename, O_RDONLY);
   if (fd < 0)
     {
        REPLY("failed: open '%s'\n", filename);
        return EINA_FALSE;
     }

   rule_len = read(fd, fs, sizeof(fs));
   pfs = fs;

   while (pfs - fs < rule_len)
     {
        int   new_argc = 3;
        char *new_argv[3] = {"add", };
        char  policy[64] = {0, };
        char  rule[1024] = {0, };
        int   i;

        if (pfs[0] == ' ' || pfs[0] == '\n')
          {
             pfs++;
             continue;
          }
        for (i = 0 ; pfs[i] != ' ' ; i++)
          policy[i] = pfs[i];

        new_argv[1] = policy;
        pfs += (strlen(new_argv[1]) + 1);

        memset(rule, 0, sizeof(rule));
        for (i = 0 ; pfs[i] != '\n' ; i++)
          rule[i] = pfs[i];

        new_argv[2] = rule;

        pfs += (strlen(new_argv[2]) + 1);
        REPLY("xDbgReadRuleFile new_argc:%d, [0]:%s, [1]:%s, [2]:%s\n", new_argc, new_argv[0]? :"NULL", new_argv[1]? :"NULL", new_argv[2]? :"NULL");

        if (!e_info_protocol_rule_set((const int) new_argc, (const char**) new_argv, reply, len))
          return EINA_FALSE;

     }

   if (fd >= 0)
     close (fd);

   return EINA_TRUE;
}

Eina_Bool
e_info_protocol_rule_validate(E_Protocol_Log *log)
{
   char *cmd = "";

   if (rc == NULL)
     rc = _rulechecker_init ();

   if (!rc)
     {
        ERR ("failed: create rulechecker\n");
        return EINA_FALSE;
     }

   cmd = _e_info_protocol_cmd_get(log->cmd);

   return _rulechecker_rule_validate(rc,
                                    log->type,
                                    log->target_id,
                                    log->name,
                                    log->client_pid,
                                    cmd);
}

