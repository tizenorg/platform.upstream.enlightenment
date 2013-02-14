#ifndef E_MOD_MIXER_H
#define E_MOD_MIXER_H

#include "e.h"

typedef void E_Mixer_App;
typedef void E_Mixer_System;
typedef void E_Mixer_Channel;

typedef struct _E_Mixer_Channel_State
{
   int mute;
   int left;
   int right;
} E_Mixer_Channel_State;

#define E_MIXER_CHANNEL_CAN_MUTE       0x01
#define E_MIXER_CHANNEL_IS_MONO        0x02
#define E_MIXER_CHANNEL_HAS_CAPTURE    0x04
#define E_MIXER_CHANNEL_HAS_PLAYBACK   0x08
#define E_MIXER_CHANNEL_MASK           0xFC

typedef struct _E_Mixer_Channel_Info
{
   int                      capabilities;
   const char              *name;
   E_Mixer_Channel         *id;
   E_Mixer_App             *app;
} E_Mixer_Channel_Info;

typedef int (*E_Mixer_Volume_Set_Cb)(E_Mixer_System *, E_Mixer_Channel *, int, int);
typedef int (*E_Mixer_Volume_Get_Cb)(E_Mixer_System *, E_Mixer_Channel *, int *, int *);
typedef int (*E_Mixer_Mute_Get_Cb)(E_Mixer_System *, E_Mixer_Channel *, int *);
typedef int (*E_Mixer_Mute_Set_Cb)(E_Mixer_System *, E_Mixer_Channel *, int);
typedef int (*E_Mixer_State_Get_Cb)(E_Mixer_System *, E_Mixer_Channel *, E_Mixer_Channel_State *);
typedef int (*E_Mixer_Capture_Cb)(E_Mixer_System *, E_Mixer_Channel *);
typedef void *(*E_Mixer_Cb)();
typedef void *(*E_Mixer_Ready_Cb)(Eina_Bool);

extern Eina_Bool _mixer_using_default;
extern E_Mixer_Volume_Get_Cb e_mod_mixer_volume_get;
extern E_Mixer_Volume_Set_Cb e_mod_mixer_volume_set;
extern E_Mixer_Mute_Get_Cb e_mod_mixer_mute_get;
extern E_Mixer_Mute_Set_Cb e_mod_mixer_mute_set;
extern E_Mixer_State_Get_Cb e_mod_mixer_state_get;
extern E_Mixer_Cb e_mod_mixer_new;
extern E_Mixer_Cb e_mod_mixer_del;
extern E_Mixer_Cb e_mod_mixer_channel_default_name_get;
extern E_Mixer_Cb e_mod_mixer_channel_info_get_by_name;
extern E_Mixer_Cb e_mod_mixer_channel_name_get;
extern E_Mixer_Cb e_mod_mixer_channel_names_get;
extern E_Mixer_Cb e_mod_mixer_card_name_get;
extern E_Mixer_Cb e_mod_mixer_card_names_get;
extern E_Mixer_Cb e_mod_mixer_card_default_get;

void e_mod_mixer_channel_info_free(E_Mixer_Channel_Info*);
Eina_List *e_mod_mixer_channel_infos_get(E_Mixer_System *sys);
void e_mod_mixer_channel_infos_free(Eina_List*);
void e_mod_mixer_channel_names_free(Eina_List*);
void e_mod_mixer_card_names_free(Eina_List*);

int e_mod_mixer_channel_mutable(const E_Mixer_Channel_Info *channel);
int e_mod_mixer_channel_is_mono(const E_Mixer_Channel_Info *channel);
int e_mod_mixer_channel_has_capture(const E_Mixer_Channel_Info *channel);
int e_mod_mixer_channel_has_playback(const E_Mixer_Channel_Info *channel);
int e_mod_mixer_channel_is_boost(const E_Mixer_Channel_Info *channel);
int e_mod_mixer_channel_has_no_volume(const E_Mixer_Channel_Info *channel);

void e_mixer_default_setup(void);
void e_mixer_pulse_setup();

/* ALSA */
int e_mixer_alsa_callback_set(E_Mixer_System *self, int (*func)(void *data, E_Mixer_System *self), void *data);

E_Mixer_System *e_mixer_alsa_new(const char *card);
void e_mixer_alsa_del(E_Mixer_System *self);

Eina_List *e_mixer_alsa_get_cards(void);
const char *e_mixer_alsa_get_default_card(void);
const char *e_mixer_alsa_get_card_name(const char *card);
const char *e_mixer_alsa_get_channel_name(E_Mixer_System *self, E_Mixer_Channel *channel);

Eina_List *e_mixer_alsa_get_channels(E_Mixer_System *self);
Eina_List *e_mixer_alsa_get_channel_names(E_Mixer_System *self);

const char *e_mixer_alsa_get_default_channel_name(E_Mixer_System *self);
E_Mixer_Channel_Info *e_mixer_alsa_get_channel_by_name(E_Mixer_System *self, const char *name);

int e_mixer_alsa_get_volume(E_Mixer_System *self, E_Mixer_Channel *channel, int *left, int *right);
int e_mixer_alsa_set_volume(E_Mixer_System *self, E_Mixer_Channel *channel, int left, int right);
int e_mixer_alsa_get_mute(E_Mixer_System *self, E_Mixer_Channel *channel, int *mute);
int e_mixer_alsa_set_mute(E_Mixer_System *self, E_Mixer_Channel *channel, int mute);
int e_mixer_alsa_get_state(E_Mixer_System *self, E_Mixer_Channel *channel, E_Mixer_Channel_State *state);
int e_mixer_alsa_set_state(E_Mixer_System *self, E_Mixer_Channel *channel, const E_Mixer_Channel_State *state);

/* PULSE */
Eina_Bool e_mixer_pulse_ready(void);
Eina_Bool e_mixer_pulse_init(E_Mixer_Ready_Cb e_sys_pulse_ready_cb, E_Mixer_Cb e_sys_pulse_update_cb);
void e_mixer_pulse_shutdown(void);

E_Mixer_System *e_mixer_pulse_new(const char *name);
void e_mixer_pulse_del(E_Mixer_System *self);

Eina_List *e_mixer_pulse_get_cards(void);
const char *e_mixer_pulse_get_default_card(void);
const char *e_mixer_pulse_get_card_name(const char *card);
const char *e_mixer_pulse_get_channel_name(E_Mixer_System *self, E_Mixer_Channel *channel);

Eina_List *e_mixer_pulse_get_channels(E_Mixer_System *self);
Eina_List *e_mixer_pulse_get_channel_names(E_Mixer_System *self);

const char *e_mixer_pulse_get_default_channel_name(E_Mixer_System *self);
E_Mixer_Channel_Info *e_mixer_pulse_get_channel_by_name(E_Mixer_System *self, const char *name);

int e_mixer_pulse_get_volume(E_Mixer_System *self, E_Mixer_Channel *channel, int *left, int *right);
int e_mixer_pulse_set_volume(E_Mixer_System *self, E_Mixer_Channel *channel, int left, int right);
int e_mixer_pulse_get_mute(E_Mixer_System *self, E_Mixer_Channel *channel, int *mute);
int e_mixer_pulse_set_mute(E_Mixer_System *self, E_Mixer_Channel *channel, int mute);
int e_mixer_pulse_get_state(E_Mixer_System *self, E_Mixer_Channel *channel, E_Mixer_Channel_State *state);
int e_mixer_pulse_set_state(E_Mixer_System *self, E_Mixer_Channel *channel, const E_Mixer_Channel_State *state);

/**
 * @addtogroup Optional_Devices
 * @{
 *
 * @defgroup Module_Mixer Audio Mixer (Volume Control)
 *
 * Controls the audio volume and mute status for both playback
 * (output) and record (input) devices.
 *
 * Can work with ALSA (http://www.alsa-project.org/) or PulseAudio
 * (http://www.pulseaudio.org/).
 *
 * @}
 */
#endif /* E_MOD_SYSTEM_H */
