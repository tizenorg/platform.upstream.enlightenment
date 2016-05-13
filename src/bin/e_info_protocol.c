#include "e.h"

#include "e_info_protocol.h"
#include "bool_exp_rule_checker.h"

static RULE_CHECKER rc = NULL;

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
        rc = rulechecker_init();

   if (argc == 0)
     {
        rulechecker_print_rule(rc, reply, len);
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

        result = rulechecker_add_rule(rc, policy_type, rule);
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
        rulechecker_print_rule(rc, reply, len);
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
                  rulechecker_destroy(rc);
                  rc = rulechecker_init();
                  REPLY("Every rules were successfully removed.\n");
               }
             else
               {
                  int index = atoi(remove_idx);
                  if (isdigit(*remove_idx) && rulechecker_remove_rule(rc, index) == 0)
                    REPLY("The rule [%d] was successfully removed.\n", index);
                  else
                    REPLY("Rule remove fail : No such rule [%s].\n", remove_idx);
               }
          }
        rulechecker_print_rule(rc, reply, len);

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
        rulechecker_print_rule(rc, reply, len);

        return EINA_TRUE;
     }
   else if (!_strcasecmp(command, "print"))
     {
        rulechecker_print_rule (rc, reply, len);

        return EINA_TRUE;
     }
   else if (!_strcasecmp(command, "help"))
     {
        REPLY("%s", rulechecker_print_usage());

        return EINA_TRUE;
     }

   REPLY("%s\nUnknown command : [%s].\n\n", rulechecker_print_usage(), command);

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
     rc = rulechecker_init ();

   if (!rc)
     {
        ERR ("failed: create rulechecker\n");
        return EINA_FALSE;
     }

   cmd = _e_info_protocol_cmd_get(log->cmd);

   return rulechecker_validate_rule(rc,
                                    log->type,
                                    log->target_id,
                                    log->name,
                                    log->client_pid,
                                    cmd);
}

