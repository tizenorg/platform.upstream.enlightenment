#include "e.h"

EINTERN Eina_Bool
e_output_init(void)
{
   // TODO: initialization e_outputs

   return EINA_TRUE;
}

EINTERN void
e_output_shutdown(void)
{
   // TODO: deinitialization e_outputs
   E_Output *eout;
   E_Output_Mode *m;
   E_Plane *ep;
   Eina_List *outputs;

   if (!e_comp) return;
   if (!e_comp->e_comp_screen) return;

   outputs = e_comp->e_comp_screen->outputs;

   // free up our output screen data
   EINA_LIST_FREE(outputs, eout)
     {
        free(eout->id);
        free(eout->info.screen);
        free(eout->info.name);
        free(eout->info.edid);
        EINA_LIST_FREE(eout->info.modes, m) free(m);
        EINA_LIST_FREE(eout->planes, ep) e_plane_free(ep);
        free(eout);
     }
}

EINTERN E_Output *
e_output_find(const char *id)
{
   E_Output *eout;
   E_Comp_Screen *e_comp_screen;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp->e_comp_screen, NULL);

   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH(e_comp_screen->outputs, l, eout)
     {
        if (!strcmp(eout->id, id)) return eout;
     }
   return NULL;
}

E_API const Eina_List *
e_output_planes_get(E_Output *eout)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout->planes, NULL);

   return eout->planes;
}

E_API void
e_output_util_planes_print(void)
{
   Eina_List *l, *ll, *p_l;
   E_Output * eout = NULL;
   E_Comp_Screen *e_comp_screen = NULL;

   EINA_SAFETY_ON_NULL_RETURN(e_comp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->e_comp_screen);

   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH_SAFE(e_comp_screen->outputs, l, ll, eout)
     {
        E_Plane *ep;
        E_Client *ec;

        if (!eout && !eout->planes) continue;

        fprintf(stderr, "HWC in %s .. \n", eout->id);
        fprintf(stderr, "HWC \tzPos \t on_plane \t\t\t\t on_prepare \t \n");

        EINA_LIST_REVERSE_FOREACH(eout->planes, p_l, ep)
          {
             ec = ep->ec;
             if (ec) fprintf(stderr, "HWC \t[%d]%s\t %s (0x%08x)",
                             ep->zpos,
                             ep->is_primary ? "--" : "  ",
                             ec->icccm.title, (unsigned int)ec->frame);

             ec = ep->prepare_ec;
             if (ec) fprintf(stderr, "\t\t\t %s (0x%08x)",
                             ec->icccm.title, (unsigned int)ec->frame);
             fputc('\n', stderr);
          }
        fputc('\n', stderr);
     }
}
