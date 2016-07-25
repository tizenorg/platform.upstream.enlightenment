#ifndef E_POLICY_PRIVATE_DATA_H
#define E_POLICY_PRIVATE_DATA_H

/* define layer values here */
typedef enum {
     E_POLICY_ANGLE_MAP_0 = 0,
     E_POLICY_ANGLE_MAP_90,
     E_POLICY_ANGLE_MAP_180,
     E_POLICY_ANGLE_MAP_270,
     E_POLICY_ANGLE_MAP_NUM,
} E_Policy_Angle_Map;

static inline Eina_Bool
e_policy_angle_valid_check(int angle)
{
   return ((angle >= 0) && (angle <= 270) && !(angle % 90));
}

static inline E_Policy_Angle_Map
e_policy_angle_map(int angle)
{
   if (!e_policy_angle_valid_check(angle))
     return -1;

   return angle / 90;
}

static inline int
e_policy_angle_get(E_Policy_Angle_Map map)
{
   if ((map < E_POLICY_ANGLE_MAP_0 ) || (map > E_POLICY_ANGLE_MAP_NUM))
     return -1;

   return map * 90;
}

/* layer level - 999 */
# define E_POLICY_QUICKPANEL_LAYER  E_LAYER_CLIENT_ALERT
# define E_POLICY_TOAST_POPUP_LAYER E_LAYER_CLIENT_ALERT

/* layer level - E_LAYER_CLIENT_NOTIFICATION_TOP (800) */
# define E_POLICY_VOLUME_LAYER      E_LAYER_CLIENT_NOTIFICATION_TOP

/* layer level - E_LAYER_CLIENT_NOTIFICATION_HIGH (750) */
/* layer level - E_LAYER_CLIENT_NOTIFICATION_NORMAL (700) */
/* layer level - E_LAYER_CLIENT_NOTIFICATION_LOW (650) */

/* layer level - E_LAYER_CLIENT_PRIO (600) */
# define E_POLICY_FLOATING_LAYER E_LAYER_CLIENT_PRIO

/* layer level - E_LAYER_CLIENT_FULLSCREEN (350) */
/* layer level - E_LAYER_CLIENT_ABOVE (250) */
/* layer level - E_LAYER_CLIENT_NORMAL (200) */
/* layer level - E_LAYER_CLIENT_BELOW (150) */

#endif
