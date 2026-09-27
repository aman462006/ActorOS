#pragma once
#include "types.h"
#include "actor.h"

void scheduler_init(void);
void scheduler_enqueue(Actor* a);
void scheduler_dequeue(Actor* a);
Actor* scheduler_next(void);

/* Called from timer interrupt — may trigger actor switch */
void scheduler_tick(void);

/* Save current actor context and switch to next ready actor.
   If no actor is ready, spin in idle loop. */
void NORETURN scheduler_enter(void);

/* Switch from 'from' to 'to'. Defined in context_switch.asm */
extern void actor_context_switch(CpuContext* from, CpuContext* to);

/* Currently running actor on this CPU */
extern Actor* current_actor;

/* Idle loop actor — runs when nothing else is ready */
extern Actor* idle_actor;

/* Ticks since boot */
extern volatile uint64_t tick_count;
