#ifndef _E_MOD_MAIN_H
#define _E_MOD_MAIN_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_WL_TEXT_INPUT
Eina_Bool   e_input_panel_init(void);
void        e_input_panel_shutdown(void);
#endif /* HAVE_WL_TEXT_INPUT */

#endif
