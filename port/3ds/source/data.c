// See data.h.
#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "data.h"

const char *tt_data = "sdmc:/timetrv";

void tt_data_init(void)
{
    if (R_FAILED(romfsInit())) return;   // a plain .3dsx has no RomFS
    FILE *f = fopen("romfs:/timetrv/timetrv.ttg", "rb");
    if (f) {
        fclose(f);
        tt_data = "romfs:/timetrv";
    } else
        romfsExit();
}

void tt_data_exit(void)
{
    if (!strncmp(tt_data, "romfs:", 6)) romfsExit();
    tt_data = "sdmc:/timetrv";
}
