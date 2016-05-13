#ifndef _BOOL_EXP_TOKENIZER_
#define _BOOL_EXP_TOKENIZER_

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

TOKEN get_next_token(const char ** string);

#endif /* _BOOL_EXP_TOKENIZER_ */
