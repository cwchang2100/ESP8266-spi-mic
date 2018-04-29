#ifndef _SOUND_H_
#define _SOUND_H_
#ifdef __cplusplus
extern "C" {
#endif

int  sound_init();
void sound_play();
void sound_stop();
void sound_suspend();
void sound_resume();

#ifdef __cplusplus
}
#endif
#endif
