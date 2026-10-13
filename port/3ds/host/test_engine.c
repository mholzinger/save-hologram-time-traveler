/* Host test for engine.c: runs N games with a policy and prints one trace line per game,
 * in the same format as `tools/sim.py --auto N --rng xorshift --trace`.
 *   ./test_engine GAME.ttg POLICY(perfect|none) N [SEED] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/engine.h"

static int perfect_input(tt_game *g, const tt_ep *ep, const tt_rule *const *r, int n)
{
    (void)g;
    (void)ep;
    for (int i = 0; i < n; i++)
        if (tt_rule_good(r[i]) && r[i]->inputs)
            return r[i]->inputs & -r[i]->inputs;   /* lowest bit, like sim.py BIT_ORDER */
    return 0;
}
static int none_input(tt_game *g, const tt_ep *ep, const tt_rule *const *r, int n)
{
    (void)g;
    (void)ep;
    (void)r;
    (void)n;
    return 0;
}
/* sim.py randomly(): uniform over none, up, down, left, right, attack, cube */
static int random_input(tt_game *g, const tt_ep *ep, const tt_rule *const *r, int n)
{
    (void)ep;
    (void)r;
    (void)n;
    static const int pick[7] = {0, TT_IN_UP, TT_IN_DOWN, TT_IN_LEFT, TT_IN_RIGHT, TT_IN_ATTACK, TT_IN_CUBE};
    return pick[tt_randrange(g, 7)];
}
/* sim.py: rng.random() < 0.5 uses the cube / declines Hellgate (perfect declines without a draw) */
static int coin_cube(tt_game *g, const tt_ep *ep)
{
    (void)ep;
    return tt_rand(g) < 0x80000000u;
}
static int perfect_hellgate(tt_game *g)
{
    (void)g;
    return 0;
}
static int coin_hellgate(tt_game *g) { return tt_rand(g) >= 0x80000000u; }
static int verbose;
static void show(tt_game *g, const tt_ep *ep)
{
    if (verbose) fprintf(stderr, "C%d E%d score %u hard %d\n", ep->chapter, ep->episode, g->score, g->hard);
}
static void show_death(tt_game *g, const tt_ep *d)
{
    if (verbose) fprintf(stderr, "  death %d at t=%d score %u\n", d->episode, g->death_t, g->score);
}
static int random_map(tt_game *g, const int *rem, int n)
{
    (void)rem;
    return tt_randrange(g, n);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s GAME.ttg perfect|none|random N [SEED]\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    static unsigned char blob[1 << 20];
    int len = (int)fread(blob, 1, sizeof blob, f);
    fclose(f);
    static tt_game g;
    if (tt_load(&g, blob, len)) {
        fprintf(stderr, "bad blob\n");
        return 1;
    }
    int perfect = !strcmp(argv[2], "perfect"), rnd = !strcmp(argv[2], "random"), n = atoi(argv[3]),
        seed = argc > 4 ? atoi(argv[4]) : 1;
    tt_frontend fe = {.choose_input = perfect ? perfect_input
                          : rnd               ? random_input
                                              : none_input,
                      .use_cube = coin_cube,
                      .accept_hellgate = perfect ? perfect_hellgate : coin_hellgate,
                      .map_choice = random_map,
                      .on_episode = show,
                      .on_death = show_death};
    verbose = getenv("TT_VERBOSE") != NULL;
    tt_settings s = {3, 6, 0, 3, 2, 0, 1};
    for (int i = 0; i < n; i++) {
        tt_new_game(&g, (uint32_t)(seed + i), &s, &fe);
        if (getenv("TT_CHOSEN")) {
            for (int k = 0; k < g.n_eps; k++)
                if (g.chosen[k] && g.eps[k].kind != TT_KIND_DEATH)
                    fprintf(stderr, "C%d E%d\n", g.eps[k].chapter, g.eps[k].episode);
            fprintf(stderr, "rng %u bonus %d hg %d\n", g.rng, g.bonus, g.hellgate_slot);
        }
        tt_run(&g);
        printf("%d %u %d %d %d %u\n", seed + i, g.score, g.lives, g.cubes, g.played, (unsigned)g.played_hash);
    }
    return 0;
}
