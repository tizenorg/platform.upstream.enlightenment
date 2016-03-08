#include "e.h"

typedef struct _E_Resist_Rect E_Resist_Rect;

struct _E_Resist_Rect
{
   int x, y, w, h;
   int v1;
   int resist_out;
};

E_API int
e_resist_client_position(Eina_List *skiplist,
                                   int px, int py, int pw, int ph,
                                   int x, int y, int w, int h,
                                   int *rx, int *ry, int *rw, int *rh)
{
   if (rx) *rx = x;
   if (ry) *ry = y;
   if (rw) *rw = w;
   if (rh) *rh = h;

   return 0;
}

E_API int
e_resist_gadman_position(Eina_List *skiplist EINA_UNUSED,
                                   int px, int py, int pw, int ph,
                                   int x, int y, int w, int h,
                                   int *rx, int *ry)
{
   if (rx) *rx = x;
   if (ry) *ry = y;

   return 0;
}
