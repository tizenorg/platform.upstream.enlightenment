#ifndef _BOOL_EXP_PARSER_H_
#define _BOOL_EXP_PARSER_H_

#include "bintree.h"

#define STRING_MAX	64

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

typedef struct _PARSE_DATA * PARSE_DATA;

struct _PARSE_DATA
{
   NODE_TYPE node_type;

   char variable_name[STRING_MAX];
   COMPARER compare;
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

BINARY_TREE bool_exp_parse(const char * string);

#endif /* _BOOL_EXP_PARSER_H_ */
