#ifndef E_KBD_H
#define E_KBD_H

typedef struct _E_Kbd E_Kbd;

#define E_KBD_TYPE 0xE1b0988

typedef enum _E_Kbd_Layout
{
   E_KBD_LAYOUT_NONE,
   E_KBD_LAYOUT_DEFAULT,
   E_KBD_LAYOUT_ALPHA,
   E_KBD_LAYOUT_NUMERIC,
   E_KBD_LAYOUT_PIN,
   E_KBD_LAYOUT_PHONE_NUMBER,
   E_KBD_LAYOUT_HEX,
   E_KBD_LAYOUT_TERMINAL,
   E_KBD_LAYOUT_PASSWORD, 
   E_KBD_LAYOUT_IP, 
   E_KBD_LAYOUT_HOST, 
   E_KBD_LAYOUT_FILE, 
   E_KBD_LAYOUT_URL, 
   E_KBD_LAYOUT_KEYPAD, 
   E_KBD_LAYOUT_J2ME
} E_Kbd_Layout;

struct _E_Kbd
{
   E_Object e_obj_inherit;
   E_Win *win;
   E_Border *border;
   Ecore_Timer *delay_hide;
   Ecore_Animator *animator;
   Eina_List *waiting_borders;
   E_Kbd_Layout layout;
   double start, len;

   int h, adjust_start, adjust, adjust_end;

   unsigned char visible : 1;
   unsigned char actually_visible : 1;
   unsigned char disabled : 1; // if we have a real kbd plugged in
   unsigned char fullscreen : 1;
};

int e_kbd_init(E_Module *m);
int e_kbd_shutdown(void);

E_Kbd *e_kbd_new(void);
void e_kbd_enable(E_Kbd *kbd);
void e_kbd_disable(E_Kbd *kbd);
void e_kbd_show(E_Kbd *kbd);
void e_kbd_hide(E_Kbd *kbd);
void e_kbd_safe_app_region_get(E_Zone *zone, int *x, int *y, int *w, int *h);
void e_kbd_fullscreen_set(E_Zone *zone, int fullscreen);
void e_kbd_layout_set(E_Kbd *kbd, E_Kbd_Layout layout);
void e_kbd_all_enable(void);
void e_kbd_all_disable(void);

extern const char *mod_dir;

#endif
