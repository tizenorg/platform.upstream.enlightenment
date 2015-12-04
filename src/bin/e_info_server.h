#ifdef E_TYPEDEFS
#else
#ifndef E_INFO_SERVER_H
#define E_INFO_SERVER_H

#include <e_info_shared_types.h>

typedef struct E_Event_Info_Rotation_Message E_Event_Info_Rotation_Message;

struct E_Event_Info_Rotation_Message
{
   E_Zone *zone;
   E_Info_Rotation_Message message;
   int rotation;
};

EAPI extern int E_EVENT_INFO_ROTATION_MESSAGE;

EINTERN int e_info_server_init(void);
EINTERN int e_info_server_shutdown(void);

#endif
#endif
