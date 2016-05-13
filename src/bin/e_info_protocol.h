#ifndef _E_INFO_PROTOCOL_H_
#define _E_INFO_PROTOCOL_H_

typedef enum
{
   REQUEST,
   EVENT,
} E_Protocol_Type;

typedef struct _E_Protocol_Log
{
   E_Protocol_Type type;
   int client_pid;
   int target_id;
   char name[PATH_MAX + 1];
   char cmd[PATH_MAX + 1];
} E_Protocol_Log;

Eina_Bool   e_info_protocol_rule_set(const int argc, const char **argv, char *reply, int *len);
Eina_Bool   e_info_protocol_rule_file_set(const char *filename, char *reply, int *len);

Eina_Bool   e_info_protocol_rule_validate(E_Protocol_Log *log);
#endif
