#include <string.h>
#include <ctype.h>

#include "bool_exp_tokenizer.h"

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

TOKEN get_next_token(const char ** string)
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
