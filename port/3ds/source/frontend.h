#ifndef TT_FRONTEND_H
#define TT_FRONTEND_H
/* The front end's public entry points, called from main.c's menu. The modules behind them are fe_*.c (fe.h). */
int frontend_play(int players);   /* 1 or 2 players; returns when the game-over screen is left */
void frontend_attract(void);  /* loops the system (chapter 10000) clips until a key is pressed */
void frontend_settings(void); /* operator settings mirroring the arcade DIPs */
void frontend_extras(void);   /* port extras: the death reel, scene select */
void frontend_scene_select(void);   /* one game in level-select mode (pick any scene) */
void frontend_video(void);    /* video settings (vset.h) with a live preview on the cabinet stage */
#endif
