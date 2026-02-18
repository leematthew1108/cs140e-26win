// Intentionally buggy "lock-free" publish.
// Sequentially correct, but fails under interleaving:
// A publishes head before initializing node->val.
#include "check-interleave.h"

typedef struct node {
    int val;
    struct node *next;
} node_t;

static volatile node_t *head;
static node_t n0;

static volatile int observed;

// A publishes node before initializing its value (BUG).
static void pub_A(checker_t *c) {
    n0.next = (node_t *)head;
    head = &n0;        // publish first (BUG)
    n0.val = 42;       // initialize after publish
}

// B pops once head is non-null.
static int pub_B(checker_t *c) {
    if (!head)
        return 0;

    node_t *n = (node_t *)head;
    observed = n->val;          // can read before A writes 42
    head = n->next;
    return 1;
}

static void pub_init(checker_t *c) {
    head = 0;
    n0.val = 0;
    n0.next = 0;
    observed = -1;
}

static int pub_check(checker_t *c) {
    // If B ran, it should have observed 42 and emptied the list.
    // Sequential run always satisfies this.
    return (observed == 42) && (head == 0);
}

static checker_t pub_mk_checker(void) {
    return (checker_t){
        .state = 0,
        .A = pub_A,
        .B = pub_B,
        .init = pub_init,
        .check = pub_check,
    };
}

void notmain(void) {
    enable_cache();

    // Turn on checker debug prints so you can see which interleave triggers.
    brk_verbose(1);

    checker_t c = pub_mk_checker();
    if (check(&c))
        panic("check should have failed!\n");
    else
        exit_success("publish-before-init failed as expected, ntrials=[%d], nerrors=[%d]\n",
                     c.ntrials, c.nerrors);
}
