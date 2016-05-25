#include <common.h>
#include "libkern.h"
#include "sleepq.h"
#include "thread.h"

#define SC_TABLESIZE    256 /* Must be power of 2. */
#define SC_MASK         (SC_TABLESIZE - 1)
#define SC_SHIFT        8
#define SC_HASH(wc)     ((((uintptr_t)(wc) >> SC_SHIFT) ^ (uintptr_t)(wc)) & \
                            SC_MASK)
#define SC_LOOKUP(wc)   &sleepq_chains[SC_HASH(wc)]

static sleepq_chain_t sleepq_chains[SC_TABLESIZE];

void sleepq_init() {
  memset(sleepq_chains, 0, sizeof sleepq_chains);
  for (int i = 0; i < SC_TABLESIZE; i++)
    LIST_INIT(&sleepq_chains[i].sc_queues);
}

sleepq_t *sleepq_lookup(void *wchan) {
  sleepq_chain_t *sc = SC_LOOKUP(wchan);

  sleepq_t *sq;

  LIST_FOREACH(sq, &sc->sc_queues, sq_entry) {
    if (sq->sq_wchan == wchan)
      return sq;
  }

  return NULL;
}

void sleepq_add(void *wchan, const char *wmesg, thread_t *td) {
  log("Adding a thread to the sleep queue. *td: %p, *wchan: %p", td, wchan);
  sleepq_chain_t *sc = SC_LOOKUP(wchan);
  sleepq_t *sq = sleepq_lookup(wchan);

  assert(td->td_wchan == NULL);
  assert(td->td_wmesg == NULL);
  assert(td->td_sleepqueue != NULL);
  assert(TAILQ_EMPTY(&td->td_sleepqueue->sq_blocked));
  assert(LIST_EMPTY(&td->td_sleepqueue->sq_free));
  assert(td->td_sleepqueue->sq_nblocked == 0);


  if (sq == NULL) {
    // We need to insert a new sleepq.
    sq = td->td_sleepqueue;
    LIST_INSERT_HEAD(&sc->sc_queues, sq, sq_entry);
    sq->sq_wchan = wchan;

    /* It can also be done when allocating memory for a thread's sleepqueue. */
    TAILQ_INIT(&sq->sq_blocked);
    LIST_INIT(&sq->sq_free);
  } else {
    /* A sleepqueue already exists. We add this thread's sleepqueue to the free list. */
    LIST_INSERT_HEAD(&sq->sq_free, td->td_sleepqueue, sq_entry);
  }

  TAILQ_INSERT_TAIL(&sq->sq_blocked, td, td_sleepq_entry);
  td->td_wchan = wchan;
  td->td_wmesg = wmesg;
  td->td_sleepqueue = NULL;
  sq->sq_nblocked++;
}

void sleepq_wait(void *wchan) {
  /* TODO SLEEP */
}

/* Remove a queue from the sleep queue and resume it */
static void sleepq_resume_thread(sleepq_t *sq, thread_t *td) {
  log("Resuming thread from sleepq. *td: %p, *td_wchan: %p", td, td->td_wchan);
  assert(td->td_wchan != NULL);
  assert(td->td_sleepqueue == NULL);
  assert(sq->sq_nblocked >= 1);

  TAILQ_REMOVE(&sq->sq_blocked, td, td_sleepq_entry);
  sq->sq_nblocked--;

  if (TAILQ_EMPTY(&sq->sq_blocked)) {
    td->td_sleepqueue = sq;
    sq->sq_wchan = NULL;
    assert(sq->sq_nblocked == 0);
  } else {
    td->td_sleepqueue = LIST_FIRST(&sq->sq_free);
    assert(sq->sq_nblocked > 0);
  }

  LIST_REMOVE(td->td_sleepqueue,
              sq_entry); /* Either removes from the sleepqueue chain or free list. */
  td->td_wchan = NULL;
  td->td_wmesg = NULL;

  /* TODO Wake this thread up */
}


void sleepq_signal(void *wchan) {
  sleepq_t *sq = sleepq_lookup(wchan);

  /* Should it panic?
    if (sq == NULL)
      panic("Trying to signal a wait channel that isn't in a sleep queue.");
  */
  thread_t *current_td;
  thread_t *best_td = NULL;

  TAILQ_FOREACH(current_td, &sq->sq_blocked, td_sleepq_entry) {
    if (best_td == NULL || current_td->td_priority > best_td->td_priority)
      best_td = current_td;
  }

  sleepq_resume_thread(sq, best_td);
}

void sleepq_broadcast(void *wchan) {
  sleepq_t *sq = sleepq_lookup(wchan);
  // if (sq == NULL) Should it panic?

  struct thread *current_td;
  TAILQ_FOREACH(current_td, &sq->sq_blocked, td_sleepq_entry) {
    sleepq_resume_thread(sq, current_td);
  }
}

void sleepq_remove(thread_t *td, void *wchan) {
  sleepq_t *sq = sleepq_lookup(wchan);

  if (td->td_wchan == NULL || td->td_wchan != wchan)
    return;

  sleepq_resume_thread(sq, td);
}

void sleepq_test() {
  sleepq_init();
  thread_t t1, t2;
  sleepq_t sq1, sq2;
  memset(&t1, 0, sizeof(t1));
  memset(&sq1, 0, sizeof(sq1));
  memset(&t2, 0, sizeof(t2));
  memset(&sq2, 0, sizeof(sq2));

  t1.td_sleepqueue = &sq1;
  t2.td_sleepqueue = &sq2;

  void *wchan = (void *)0x123;

  sleepq_add(wchan, NULL, &t1);
  sleepq_add(wchan, NULL, &t2);

  sleepq_signal(wchan);
  sleepq_signal(wchan);

  sleepq_add(wchan, NULL, &t2);
  sleepq_add(wchan, NULL, &t1);

  sleepq_broadcast(wchan);

  void *wchan2 = (void *)0x124;

  sleepq_add(wchan, NULL, &t1);
  sleepq_add(wchan2, NULL, &t2);

  sleepq_signal(wchan);
  sleepq_signal(wchan2);

  sleepq_add(wchan, NULL, &t1);
  sleepq_add(wchan2, NULL, &t2);

  sleepq_remove(&t1, wchan);
  sleepq_remove(&t2, wchan2);
}