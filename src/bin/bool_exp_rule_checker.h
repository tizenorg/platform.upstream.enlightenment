#ifndef _BOOL_EXP_RULE_CHECKER_H_
#define _BOOL_EXP_RULE_CHECKER_H_

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

typedef enum
{
   UNDEFINED,
   ALLOW,
   DENY
} POLICY_TYPE;

typedef enum
{
   RC_OK,
   RC_ERR_TOO_MANY_RULES,
   RC_ERR_PARSE_ERROR,
   RC_ERR_NO_RULE
} RC_RESULT_TYPE;

typedef struct _RULE_CHECKER * RULE_CHECKER;

RULE_CHECKER   rulechecker_init();
void           rulechecker_destroy(RULE_CHECKER rc);

RC_RESULT_TYPE rulechecker_add_rule(RULE_CHECKER rc, POLICY_TYPE policy, const char * rule_string);
RC_RESULT_TYPE rulechecker_remove_rule(RULE_CHECKER rc, int index);

void           rulechecker_print_rule(RULE_CHECKER rc, char *reply, int *len);
const char *   rulechecker_print_usage(void);

int            rulechecker_validate_rule(RULE_CHECKER rc, int direct, int reqID, const char * name, int pid, char * cmd);

#endif /* _BOOL_EXP_RULE_CHECKER_H_ */
