#include "master_slave_channel.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ---------- spinlock helpers (only for channel spinlocks) ---------- */
static inline void spin_lock(atomic_bool *lock) {
    while (atomic_exchange(lock, true)) {}
}
static inline void spin_unlock(atomic_bool *lock) {
    atomic_store(lock, false);
}

/* ---------- Channel (no internal locking) ------------------------- */
Channel *channel_new(void) {
    Channel *c = malloc(sizeof(*c));
    if (c) c->tail = c->head = NULL;
    return c;
}

void channel_free(Channel *c) {
    if (!c) return;
    Message *cur = c->head;
    while (cur) {
        Message *next = cur->next;
        if (cur->data) {
			free(cur->data);
		}
        free(cur);
        cur = next;
    }
    free(c);
}

int channel_queue(Channel *c, void *msg) {
    Message *m = malloc(sizeof(*m));
    if (!m) return -1;
    m->data = msg;
    m->next = NULL;
    m->prev = c->tail;
    if (c->tail) c->tail->next = m;
    else         c->head = m;
    c->tail = m;
    return 0;
}

void *channel_dequeue(Channel *c) {
    Message *m = c->head;
    if (!m) return NULL;
    c->head = m->next;
    if (c->head) c->head->prev = NULL;
    else         c->tail = NULL;
    void *data = m->data;
    free(m);
    return data;
}

/* ---------- MasterSignal ------------------------------------------ */
MasterSignal master_signal_new(void) {
    MasterSignal ms = {0};

    atomic_init(&ms.slave_to_master_lock, false);
    atomic_init(&ms.has_msg, false);

    pthread_mutex_init(&ms.wait_mutex, NULL);
    pthread_cond_init(&ms.signal, NULL);

    return ms;
}

void master_signal_block_while_empty(MasterSignal *ms) {
    pthread_mutex_lock(&ms->wait_mutex);
    while (!atomic_load(&ms->has_msg))
        pthread_cond_wait(&ms->signal, &ms->wait_mutex);
    pthread_mutex_unlock(&ms->wait_mutex);
}

/* ---------- ChannelPair life‑cycle -------------------------------- */
static void cond_mutex_init(pthread_cond_t *c, pthread_mutex_t *m) {
    pthread_mutex_init(m, NULL);
    pthread_cond_init(c, NULL);
}

ChannelPair *channelpair_new_one_to_one(void) {
    ChannelPair *cp = malloc(sizeof(*cp));
    if (!cp) return NULL;

    cp->slave_to_master = channel_new();
    cp->master_to_slave = channel_new();
    atomic_init(&cp->master_to_slave_lock, false);

    cp->master_signal = malloc(sizeof(MasterSignal));
	*cp->master_signal = master_signal_new();

    cond_mutex_init(&cp->slave_signal, &cp->slave_wait_mutex);

    if (!cp->slave_to_master || !cp->master_to_slave || !cp->master_signal) {
        channel_free(cp->slave_to_master);
        channel_free(cp->master_to_slave);
        free(cp->master_signal);
        free(cp);
        return NULL;
    }
    return cp;
}

ChannelPair *channelpair_new_one_to_many(Channel *master_in,
                                         MasterSignal *master_signal) {
    ChannelPair *cp = malloc(sizeof(*cp));
    if (!cp) return NULL;

    cp->slave_to_master = master_in;      /* shared – no ownership */
    cp->master_signal   = master_signal;  /* shared */
    cp->master_to_slave = channel_new();

    atomic_init(&cp->master_to_slave_lock, false);
    cond_mutex_init(&cp->slave_signal, &cp->slave_wait_mutex);

    if (!cp->master_to_slave) {
        free(cp);
        return NULL;
    }
    return cp;
}

void channelpair_free_one_to_one(ChannelPair *cp) {
    if (!cp) return;
    channel_free(cp->slave_to_master);
    channel_free(cp->master_to_slave);
    pthread_cond_destroy(&cp->slave_signal);
    pthread_mutex_destroy(&cp->slave_wait_mutex);
    pthread_cond_destroy(&cp->master_signal->signal);
    pthread_mutex_destroy(&cp->master_signal->wait_mutex);
    free(cp->master_signal);
    free(cp);
}

void channelpair_free_one_to_many(ChannelPair *cp) {
    if (!cp) return;
    channel_free(cp->master_to_slave);
    pthread_cond_destroy(&cp->slave_signal);
    pthread_mutex_destroy(&cp->slave_wait_mutex);
    free(cp);
}

/* ---------- Master‑side operations -------------------------------- */
void master_send_msg(ChannelPair *cp, void *msg) {
    spin_lock(&cp->master_to_slave_lock);
    channel_queue(cp->master_to_slave, msg);
    spin_unlock(&cp->master_to_slave_lock);

    pthread_mutex_lock(&cp->slave_wait_mutex);
    pthread_cond_signal(&cp->slave_signal);
    pthread_mutex_unlock(&cp->slave_wait_mutex);
}

void *master_await_msg(ChannelPair *cp) {
    pthread_mutex_lock(&cp->master_signal->wait_mutex);
    for (;;) {
        spin_lock(&cp->master_signal->slave_to_master_lock);
        void *msg = channel_dequeue(cp->slave_to_master);
        spin_unlock(&cp->master_signal->slave_to_master_lock);

        if (msg) {
            /* If queue is now empty, clear the flag. */
            if (cp->slave_to_master->head == NULL)
                atomic_store(&cp->master_signal->has_msg, false);
            pthread_mutex_unlock(&cp->master_signal->wait_mutex);
            return msg;
        }
        pthread_cond_wait(&cp->master_signal->signal, &cp->master_signal->wait_mutex);
    }
}

void *master_receive_msg_if_exists(ChannelPair *cp) {
    spin_lock(&cp->master_signal->slave_to_master_lock);
    void *msg = channel_dequeue(cp->slave_to_master);
    spin_unlock(&cp->master_signal->slave_to_master_lock);

    if (msg && cp->slave_to_master->head == NULL)
        atomic_store(&cp->master_signal->has_msg, false);
    return msg;
}

/* ---------- Slave‑side operations --------------------------------- */
void slave_send_msg(ChannelPair *cp, void *msg) {
    spin_lock(&cp->master_signal->slave_to_master_lock);
    int was_empty = (cp->slave_to_master->head == NULL);
    channel_queue(cp->slave_to_master, msg);
    spin_unlock(&cp->master_signal->slave_to_master_lock);

    if (was_empty) {
        atomic_store(&cp->master_signal->has_msg, true);
        pthread_mutex_lock(&cp->master_signal->wait_mutex);
        pthread_cond_signal(&cp->master_signal->signal);
        pthread_mutex_unlock(&cp->master_signal->wait_mutex);
    }
}

void *slave_await_msg(ChannelPair *cp) {
    pthread_mutex_lock(&cp->slave_wait_mutex);
    for (;;) {
        spin_lock(&cp->master_to_slave_lock);
        void *msg = channel_dequeue(cp->master_to_slave);
        spin_unlock(&cp->master_to_slave_lock);

        if (msg) {
            pthread_mutex_unlock(&cp->slave_wait_mutex);
            return msg;
        }
        pthread_cond_wait(&cp->slave_signal, &cp->slave_wait_mutex);
    }
}

void *slave_receive_msg_if_exists(ChannelPair *cp) {
    spin_lock(&cp->master_to_slave_lock);
    void *msg = channel_dequeue(cp->master_to_slave);
    spin_unlock(&cp->master_to_slave_lock);
    return msg;
}

