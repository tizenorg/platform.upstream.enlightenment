#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bintree.h"
#include "bool_exp_tokenizer.h"
#include "bool_exp_parser.h"

typedef struct _TOKEN_DATA * TOKEN_DATA;

struct _TOKEN_DATA
{
   const char ** string;
   TOKEN last_token;
   const char * last_symbol;
   int symbol_len;
};

static BINARY_TREE_NODE bool_exp_parse_line(BINARY_TREE tree, TOKEN_DATA token);

#define PARSE_DEBUG 0

#if PARSE_DEBUG
#define process_token(t) _process_token(t, __LINE__)
static void _process_token(TOKEN_DATA token, int line)
#else
static void process_token(TOKEN_DATA token)
#endif
{
   do
     {
        token->last_symbol = *(token->string);
        token->last_token = get_next_token(token->string);
        token->symbol_len = *(token->string) - token->last_symbol;
     }
   while (token->last_token == BET_SPACE);
#if PARSE_DEBUG
   printf ("token : %d remained string : [%s] (line:%d)\n", token->last_token, *token->string, line);
#endif
}

static BINARY_TREE_NODE bool_exp_parse_statement(BINARY_TREE tree, TOKEN_DATA token)
{
   BINARY_TREE_NODE node = NULL;
   PARSE_DATA data = NULL;

#if PARSE_DEBUG
   printf ("%s:%d (token->last_token %d)\n", __FILE__, __LINE__, token->last_token);
#endif

   if (token->last_token == BET_L_BR)
     {
        process_token(token);

        node = bool_exp_parse_line(tree, token);
        if (node == NULL)
          return NULL;

        if (token->last_token != BET_R_BR)
          goto fail;
        process_token(token);

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
        process_token(token);

        return node;
     }

   data->node_type = DATA;

   process_token(token);

   switch (token->last_token)
     {
      case BET_NOT_EQ:
        data->compare = NOT_EQ;
        break;
      case BET_EQUAL:
        data->compare = EQUAL;
        break;
      case BET_LSS_THAN:
        data->compare = LESS;
        break;
      case BET_LSS_EQ:
        data->compare = LESS_EQ;
        break;
      case BET_GRT_THAN:
        data->compare = GREATER;
        break;
      case BET_GRT_EQ:
        data->compare = GREATER_EQ;
        break;
      default:
        goto fail;
     }

   process_token(token);

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

   process_token(token);

   return node;

fail:
   if (node)
     bintree_remove_node_recursive(node);

   return NULL;
}

static BINARY_TREE_NODE bool_exp_parse_line(BINARY_TREE tree, TOKEN_DATA token)
{
   BINARY_TREE_NODE node = NULL;
   BINARY_TREE_NODE left = NULL;
   BINARY_TREE_NODE right = NULL;

   PARSE_DATA data;

#if PARSE_DEBUG
   printf("%s:%d\n", __FILE__, __LINE__);
#endif

   node = bool_exp_parse_statement(tree, token);
   if (node == NULL)
     {
        printf("PARSE statement error\n");
        goto fail;
     }

   while (token->last_token == BET_AND)
     {
        left = node;
        node = NULL;

        process_token(token);
        right = bool_exp_parse_statement(tree, token);
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

        process_token(token);
        right = bool_exp_parse_line(tree, token);
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

BINARY_TREE bool_exp_parse (const char * string)
{
   BINARY_TREE tree = bintree_create_tree(sizeof (struct _PARSE_DATA));
   BINARY_TREE_NODE node;

   struct _TOKEN_DATA token;

   token.string = &string;
   process_token(&token);

   node = bool_exp_parse_line(tree, &token);
   if (node == NULL)
     {
        bintree_destroy_tree(tree);
        return NULL;
     }

   bintree_set_head(tree, node);

   return tree;
}
