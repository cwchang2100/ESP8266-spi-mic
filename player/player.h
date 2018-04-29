#ifndef _PLAYER_H_
#define _PLAYER_H_
#ifdef __cplusplus
extern "C" {
#endif

typedef struct track_t {
    struct track_t *next;
    struct track_t *prev;
    char           *filename;
} track_t;

typedef void (*OUT_CB)(void *data, int len);

int  player_init(OUT_CB cb);
void player_play(const char *file);
void player_stop();
void player_suspend();
void player_resume();

#ifdef __cplusplus
}
#endif
#endif
