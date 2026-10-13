/* Two-player alternation on the host, the way the 3DS front end does it: each player's game runs
 * in its own thread; one runs at a time; a lost life hands over (after_death -> D7C0:038F).
 * Prints "P<n> C<ch> E<ep>" per scene and "P<n> LIFE LOST" for a no-input game, to compare with
 * the ROM (docs/ROM_FINDINGS.md "Two players").   ./test_2p GAME.ttg [SEED] */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "../source/engine.h"

static tt_game games[2];
static int turn, done[2];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

static int who(tt_game *g) { return g == &games[1]; }
static void wait_turn(int p)
{
    pthread_mutex_lock(&mu);
    while (turn != p) pthread_cond_wait(&cv, &mu);
    pthread_mutex_unlock(&mu);
}
static void hand_over(int p)
{
    pthread_mutex_lock(&mu);
    if (!done[1 - p]) turn = 1 - p;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
}
static int none_input(tt_game *g, const tt_ep *ep, const tt_rule *const *r, int n)
{
    (void)g;
    (void)ep;
    (void)r;
    (void)n;
    return 0;
}
static int no(tt_game *g)
{
    (void)g;
    return 0;
}
static int no_cube(tt_game *g, const tt_ep *ep)
{
    (void)g;
    (void)ep;
    return 0;
}
static int first(tt_game *g, const int *rem, int n)
{
    (void)g;
    (void)rem;
    (void)n;
    return 0;
}
static void show(tt_game *g, const tt_ep *ep) { printf("P%d C%d E%d\n", who(g) + 1, ep->chapter, ep->episode); }
static void lost(tt_game *g)
{
    int p = who(g);
    printf("P%d LIFE LOST (lives %d)\n", p + 1, g->lives);
    if (g->lives > 0 && !done[1 - p]) {
        hand_over(p);
        wait_turn(p);
    }
}
static void *run(void *arg)
{
    int p = (int)(long)arg;
    wait_turn(p);
    tt_run(&games[p]);
    printf("P%d GAME OVER\n", p + 1);
    pthread_mutex_lock(&mu);
    done[p] = 1;
    pthread_mutex_unlock(&mu);
    hand_over(p);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s GAME.ttg [SEED]\n", argv[0]);
        return 2;
    }
    static unsigned char blob[1 << 20];
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    int len = (int)fread(blob, 1, sizeof blob, f);
    fclose(f);
    tt_frontend fe = {.choose_input = none_input,
                      .use_cube = no_cube,
                      .accept_hellgate = no,
                      .map_choice = first,
                      .on_episode = show,
                      .life_lost = lost};
    tt_settings s = {3, 6, 0, 3, 2, 0, 1};
    int seed = argc > 2 ? atoi(argv[2]) : 1;
    for (int p = 0; p < 2; p++) {
        if (tt_load(&games[p], blob, len)) {
            fprintf(stderr, "bad blob\n");
            return 1;
        }
        tt_new_game(&games[p], (uint32_t)(seed + p), &s, &fe);
        games[p].players = 2;
    }
    pthread_t t[2];
    for (int p = 0; p < 2; p++) pthread_create(&t[p], NULL, run, (void *)(long)p);
    for (int p = 0; p < 2; p++) pthread_join(t[p], NULL);
    return 0;
}
