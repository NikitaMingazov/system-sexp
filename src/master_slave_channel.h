#ifndef MASTER_SLAVE_CHANNEL_H
#define MASTER_SLAVE_CHANNEL_H

#include <stdatomic.h>
#include <pthread.h>

typedef struct message {
	void *data; // owned by receiver
	struct message *prev;
	struct message *next;
} Message;

// doubly-linkedlist as queue
typedef struct channel {
	Message *tail;
	Message *head;
} Channel;

Channel *channel_new(void);
void channel_free(Channel *c);

int channel_queue(Channel *c, void *msg);
void *channel_dequeue(Channel *c);

typedef struct master_signal {
	atomic_bool slave_to_master_lock;
	// set when a slave sends into an empty channel
	atomic_bool has_msg;
	pthread_cond_t signal;
	pthread_mutex_t wait_mutex;
} MasterSignal;

MasterSignal master_signal_new();
// blocks until a message has been queued (for multi-slave topologies)
void master_signal_block_while_empty(MasterSignal *ms);

typedef struct channel_pair {
	Channel *slave_to_master;
	Channel *master_to_slave;
	atomic_bool master_to_slave_lock;
	// for master to respond and unblock
	MasterSignal *master_signal;
	pthread_cond_t slave_signal;
	pthread_mutex_t slave_wait_mutex;
} ChannelPair;

// multiple slaves can share the one channel to master
ChannelPair *channelpair_new_one_to_many(Channel *master_in,
                                         MasterSignal *master_signal);
void channelpair_free_one_to_many(ChannelPair *cp);
// master has only one slave, his channel and signal are owned by the ChannelPair
ChannelPair *channelpair_new_one_to_one();
void channelpair_free_one_to_one(ChannelPair *cp);

// note: this moves the data
void master_send_msg(ChannelPair *cp, void *msg);
// blocks until an inbound message has been queued if none queued
void *master_await_msg(ChannelPair *cp);
// returns null if no msg (for a one->many topology)
void *master_receive_msg_if_exists(ChannelPair *cp);

void slave_send_msg(ChannelPair *cp, void *msg);
void *slave_await_msg(ChannelPair *cp);
void *slave_receive_msg_if_exists(ChannelPair *cp);

#endif

