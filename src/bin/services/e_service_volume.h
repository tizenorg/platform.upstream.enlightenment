#ifndef E_SERVICE_VOLUME_H
#define E_SERVICE_VOLUME_H

#include <e.h>
#include "e_policy_private_data.h"

EINTERN Eina_Bool     e_service_volume_client_set(E_Client *ec);
EINTERN E_Client     *e_service_volume_client_get(void);
EINTERN Eina_Bool     e_service_volume_region_set(int region_type, int angle, Eina_Tiler *tiler);

#endif /* E_SERVICE_VOLUME_H */
