#ifndef _E_INFO_PROTOCOL_H_
#define _E_INFO_PROTOCOL_H_

#define STRING_MAX 64
#define MAX_RULE   64

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

typedef struct _E_Info_Tree_Node    E_Info_Tree_Node;
typedef struct _E_Info_Tree         E_Info_Tree;
typedef struct _E_Info_Token_Data   E_Info_Token_Data;
typedef struct _E_Info_Rule_Node    E_Info_Rule_Node;
typedef struct _E_Info_Rule         E_Info_Rule;
typedef struct _E_Info_Rule_Checker E_Info_Rule_Checker;
typedef struct _E_Info_Protocol_Log E_Info_Protocol_Log;

typedef int (*E_Info_Tree_Traverse_Cb) (E_Info_Tree *tree, E_Info_Tree_Node *node, E_Info_Tree_Node *parent, void * arg);

struct _E_Info_Tree_Node
{
   E_Info_Tree_Node *left;
   E_Info_Tree_Node *right;
};

struct _E_Info_Tree
{
   int               size;
   E_Info_Tree_Node *head;
};

typedef enum
{
   E_INFO_TOKEN_UNKNOWN = 0,
   E_INFO_TOKEN_L_BR = 1,
   E_INFO_TOKEN_R_BR = 2,
   E_INFO_TOKEN_NOT_EQ = 3,
   E_INFO_TOKEN_EQUAL = 4,
   E_INFO_TOKEN_LSS_THAN = 5,
   E_INFO_TOKEN_LSS_EQ = 6,
   E_INFO_TOKEN_GRT_THAN = 7,
   E_INFO_TOKEN_GRT_EQ = 8,
   E_INFO_TOKEN_AND = 9,
   E_INFO_TOKEN_OR = 10,
   E_INFO_TOKEN_SPACE = 11,
   E_INFO_TOKEN_SYMBOL = 12,
   E_INFO_TOKEN_NUMBER = 13,
   E_INFO_TOKEN_EOS = 14,
} E_Info_Token;

struct _E_Info_Token_Data
{
   const char **string;
   E_Info_Token last_token;
   const char  *last_symbol;
   int          symbol_len;
};

typedef enum
{
   E_INFO_NODE_TYPE_NONE,
   E_INFO_NODE_TYPE_AND,
   E_INFO_NODE_TYPE_OR,
   E_INFO_NODE_TYPE_DATA,
   E_INFO_NODE_TYPE_ALL
} E_Info_Node_Type;

typedef enum
{
   E_INFO_COMPARER_EQUAL,
   E_INFO_COMPARER_LESS,
   E_INFO_COMPARER_GREATER,
   E_INFO_COMPARER_LESS_EQ,
   E_INFO_COMPARER_GREATER_EQ,
   E_INFO_COMPARER_NOT_EQ
} E_Info_Comparer;

typedef enum
{
   E_INFO_DATA_TYPE_INTEGER,
   E_INFO_DATA_TYPE_STRING
} E_Info_Data_Type;

typedef enum
{
   E_INFO_RESULT_UNKNOWN,
   E_INFO_RESULT_TRUE,
   E_INFO_RESULT_FALSE
} E_Info_Result_Type;

struct _E_Info_Rule_Node
{
   E_Info_Node_Type node_type;

   char             variable_name[STRING_MAX];
   E_Info_Comparer  comparer;
   E_Info_Data_Type value_type;

   union
   {
      char string[STRING_MAX];
      int  integer;
   } value;

   E_Info_Result_Type result;
};

typedef enum
{
   E_INFO_POLICY_TYPE_UNDEFINED,
   E_INFO_POLICY_TYPE_ALLOW,
   E_INFO_POLICY_TYPE_DENY
} E_Info_Policy_Type;

struct _E_Info_Rule
{
   E_Info_Policy_Type policy;
   E_Info_Tree       *tree;
};

struct _E_Info_Rule_Checker
{
   E_Info_Rule rules[MAX_RULE];
   int         count;
};

typedef enum
{
   E_INFO_PROTOCOL_TYPE_REQUEST,
   E_INFO_PROTOCOL_TYPE_EVENT,
} E_Info_Protocol_Type;

struct _E_Info_Protocol_Log
{
   E_Info_Protocol_Type type;
   int                  client_pid;
   int                  target_id;
   char                 name[PATH_MAX + 1];
   char                 cmd[PATH_MAX + 1];
};

void      e_info_protocol_init();
void      e_info_protocol_shutdown();

Eina_Bool e_info_protocol_rule_set(const int argc, const char **argv, char *reply, int *len);
Eina_Bool e_info_protocol_rule_validate(E_Info_Protocol_Log *log);
#endif
