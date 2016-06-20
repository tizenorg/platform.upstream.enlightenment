#ifndef _E_INFO_SHARED_TYPES_
#define _E_INFO_SHARED_TYPES_

typedef enum
{
   E_INFO_ROTATION_MESSAGE_SET,
   E_INFO_ROTATION_MESSAGE_ENABLE,
   E_INFO_ROTATION_MESSAGE_DISABLE
} E_Info_Rotation_Message;

typedef enum
{
   E_INFO_SLOT_MESSAGE_LIST,
   E_INFO_SLOT_MESSAGE_CREATE,
   E_INFO_SLOT_MESSAGE_MODIFY,
   E_INFO_SLOT_MESSAGE_DEL,
   E_INFO_SLOT_MESSAGE_RAISE,
   E_INFO_SLOT_MESSAGE_LOWER,
   E_INFO_SLOT_MESSAGE_ADD_EC_TRANSFORM,
   E_INFO_SLOT_MESSAGE_ADD_EC_RESIZE,
   E_INFO_SLOT_MESSAGE_DEL_EC,
   E_INFO_SLOT_MESSAGE_FOCUS
} E_Info_Slot_Message;

#endif /* end of _E_INFO_SHARED_TYPES_ */
