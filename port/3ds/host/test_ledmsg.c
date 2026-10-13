/* LED message module on the host: prints "<60 Hz tick> |<16 chars>|" at every change after show(id) at
 * tick 0, for comparison with the ROM (tools/romled.py).  ./test_ledmsg GAME.ttg ID SECONDS "INITIAL LINE"
 * Values are those of the ROM run: free play DIPs 06,04,7E (3 lives, coinage 3), default hall of fame,
 * player 1, score 0, cubes 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/ledmsg.h"

static tt_game g;
static int value(void *u, int what, int arg)
{
    (void)u;
    static const long hof[3] = {122373, 530602, 592410};   /* entry 2 = 1st */
    switch (what) {
    case LEDV_LIVES: return 3;
    case LEDV_LIVES_DIP: return 3;
    case LEDV_COINAGE: return 3;
    case LEDV_HOF_SCORE: return (int)hof[arg];
    default: return 0;
    }
}
static const char *initials(void *u, int entry)
{
    (void)u;
    static const char *in[3] = {"TAK", "STP", "DJS"};
    return in[entry];
}

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    static unsigned char blob[1 << 20];
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    int len = (int)fread(blob, 1, sizeof blob, f);
    fclose(f);
    if (tt_load(&g, blob, len)) {
        fprintf(stderr, "bad blob\n");
        return 1;
    }
    ledm_env env = {NULL, value, initials, NULL};
    ledm_init(&g, &env);
    if (argc > 4) {
        char l[17];
        snprintf(l, sizeof l, "%-16s", argv[4]);
        ledm_set_line(l);
    }
    ledm_show((int)strtol(argv[2], NULL, 0));
    char last[17] = "";
    int ticks = atoi(argv[3]) * 60;
    for (int t = 0; t < ticks; t++) {
        if (t & 1) ledm_tick30();   /* the ISR's odd ticks */
        if (strcmp(last, ledm_line())) {
            strcpy(last, ledm_line());
            printf("%d |%s|\n", t, last);
        }
    }
    return 0;
}
