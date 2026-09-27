#include "../include/scheduler.h"
#include "../include/actor.h"
#include "../include/mm.h"
#include "../include/io.h"

Actor* current_actor = NULL;
Actor* idle_actor    = NULL;
volatile uint64_t tick_count = 0;

/* Per-priority run queues — simple circular linked list */
typedef struct RunQueue {
    Actor* head;
    Actor* tail;
    uint64_t count;
} RunQueue;

static RunQueue run_queues[ACTOR_PRIO_LEVELS];

#define QUEUE_NEXT(a)        ((a)->sched_next)
#define QUEUE_PREV(a)        ((a)->sched_prev)
#define SET_QUEUE_NEXT(a, n) ((a)->sched_next = (n))
#define SET_QUEUE_PREV(a, p) ((a)->sched_prev = (p))

static void rq_push(RunQueue* rq, Actor* a) {
    SET_QUEUE_NEXT(a, NULL);
    SET_QUEUE_PREV(a, rq->tail);
    if (rq->tail) SET_QUEUE_NEXT(rq->tail, a);
    rq->tail = a;
    if (!rq->head) rq->head = a;
    rq->count++;
}

static Actor* rq_pop(RunQueue* rq) {
    if (!rq->head) return NULL;
    Actor* a = rq->head;
    rq->head = QUEUE_NEXT(a);
    if (rq->head) SET_QUEUE_PREV(rq->head, NULL);
    else rq->tail = NULL;
    SET_QUEUE_NEXT(a, NULL);
    SET_QUEUE_PREV(a, NULL);
    rq->count--;
    return a;
}

static void idle_entry(Actor* self) {
    (void)self;
    while (true) {
        sti();
        hlt();    /* halt until next interrupt */
        cli();
    }
}

void scheduler_init(void) {
    for (int i = 0; i < ACTOR_PRIO_LEVELS; i++) {
        run_queues[i].head  = NULL;
        run_queues[i].tail  = NULL;
        run_queues[i].count = 0;
    }
    idle_actor = actor_create("idle", idle_entry, ACTOR_PRIO_LOW);
    /* Don't enqueue idle — scheduler_next() falls back to it */
}

void scheduler_enqueue(Actor* a) {
    if (!a || a == idle_actor) return;
    a->state = ACTOR_READY;
    uint8_t p = a->priority < ACTOR_PRIO_LEVELS ? a->priority : ACTOR_PRIO_NORMAL;
    rq_push(&run_queues[p], a);
}

void scheduler_dequeue(Actor* a) {
    if (!a) return;
    uint8_t p = a->priority;
    RunQueue* rq = &run_queues[p];

    /* Unlink from queue */
    Actor* prev = QUEUE_PREV(a);
    Actor* next = QUEUE_NEXT(a);
    if (prev) SET_QUEUE_NEXT(prev, next); else rq->head = next;
    if (next) SET_QUEUE_PREV(next, prev); else rq->tail = prev;
    SET_QUEUE_NEXT(a, NULL);
    SET_QUEUE_PREV(a, NULL);
    if (rq->count > 0) rq->count--;
}

Actor* scheduler_next(void) {
    for (int p = 0; p < ACTOR_PRIO_LEVELS; p++) {
        if (run_queues[p].head) {
            return rq_pop(&run_queues[p]);
        }
    }
    return idle_actor;
}

void scheduler_tick(void) {
    /* Called from timer IRQ. If current actor has used its timeslice,
       re-enqueue it at the back and let someone else run. */
    if (!current_actor || current_actor == idle_actor) return;

    /* Simple: always preempt on every tick */
    Actor* next = scheduler_next();
    if (next && next != current_actor) {
        Actor* prev = current_actor;
        current_actor = next;
        next->state = ACTOR_RUNNING;
        scheduler_enqueue(prev);  /* put old actor back */
        actor_context_switch(&prev->context, &next->context);
    }
}

/* Dummy context for the initial scheduler_enter — we never return to it */
static CpuContext bootstrap_ctx;

void NORETURN scheduler_enter(void) {
    while (true) {
        Actor* next = scheduler_next();
        if (next) {
            current_actor = next;
            next->state = ACTOR_RUNNING;
            actor_context_switch(&bootstrap_ctx, &next->context);
        }
        sti(); hlt(); cli();
    }
}
