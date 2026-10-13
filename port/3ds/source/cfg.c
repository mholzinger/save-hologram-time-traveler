// See cfg.h.
#include <string.h>
#include <sys/stat.h>

#include "cfg.h"
#include "data.h"

bool cfg_read(const char *path, cfg_fn fn, void *user)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[64], name[32];
    int value;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%31[^=]=%d", name, &value) == 2) fn(user, name, value);
    fclose(f);
    return true;
}

FILE *cfg_create(const char *path)
{
    mkdir(TT_SAVE_DIR, 0777);   // a one-file build may find no folder on the card
    return fopen(path, "w");
}
