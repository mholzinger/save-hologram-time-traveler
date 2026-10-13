#ifndef TT_CFG_H
#define TT_CFG_H
#include <stdbool.h>
#include <stdio.h>
/* The port's settings files on the SD card (settings.cfg, video.cfg): plain text, one "name=value" per
 * line, so they can be edited by hand. */
typedef void (*cfg_fn)(void *user, const char *name, int value);
bool cfg_read(const char *path, cfg_fn fn, void *user);   /* calls fn for each line; false if no file */
FILE *cfg_create(const char *path);                       /* opens for writing, making TT_SAVE_DIR; NULL on failure */
#endif
