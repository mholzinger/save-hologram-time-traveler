// Play log: one line per scene, death, cube offer, life and game over in sdmc:/timetrv/log.txt (rewritten each
// game) so a problem seen on hardware can be traced afterwards over FTP (make log).

#include "fe.h"

// Play log: one line per scene, death, cube offer, life and game over, in sdmc:/timetrv/log.txt
// (rewritten each game), so a problem seen on hardware can be traced afterwards over FTP. Lines are
// rare (a few per scene), so each is flushed at once and survives a crash.
static FILE *plog;

static u64 plog_t0;

void plog_open(void)
{
#ifndef TT_HOST   // the host test would write into build/sdmc, the staged card
    if (plog) fclose(plog);
    mkdir(TT_SAVE_DIR, 0777);
    plog = fopen(TT_SAVE_DIR "/log.txt", "w");
    plog_t0 = osGetTime();
#endif
}

void plog_line(const char *fmt, ...)
{
    if (!plog) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(plog, "%8.1f ", (osGetTime() - plog_t0) / 1000.0);
    vfprintf(plog, fmt, ap);
    fputc('\n', plog);
    fflush(plog);
    va_end(ap);
}
