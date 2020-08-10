#include <ctime>
#include <cstdint>
#include <atomic>
#include <cassert>
#include <iostream>
#include <stdio.h>

bool UNLOCKED = true;
bool LOCKED = false;
bool S_WAITING = true; // Waiting on the node status
bool S_READY = false; // Waiter at the head of the queue
bool S_SHUFFLER = true;
bool S_WAITER = false;
int MAX_SHUFFLES = 1024;
time_t WAIT_TIME = 1; // seconds

struct lock {
    std::atomic<bool> glock =  ATOMIC_VAR_INIT(UNLOCKED);
    std::atomic<bool> no_stealing = ATOMIC_VAR_INIT(false);
    std::atomic<struct qnode*> tail = ATOMIC_VAR_INIT(nullptr);
};

struct qnode {
    bool status = S_WAITING;
    int socket_ID;
    int shuffler_status = S_WAITER;
    int batch_count = 0; // Limit batching too many waiters from the same socket
    const time_t timestamp = std::time(0); // Deadlock prevention with wait time limit
    struct qnode* next = nullptr;
};

void shuffler_waiters(struct lock* lock, struct qnode* qnode, bool vnext_waiter) {    
    // Keeps track of shuffled nodes
    struct qnode* qlast = qnode;
    // Used for queue traversal
    struct qnode* qprev = qnode;
    struct qnode* qcurr = nullptr;
    struct qnode* qnext = nullptr;
    
    int batch = qnode -> batch_count;
    if (batch == 0)
        qnode -> batch_count = ++batch;
    
    qnode -> shuffler_status = false;
    if (batch >= MAX_SHUFFLES)
        return;

    // Walk the queue in sequence
    while (true) {
        qcurr = qprev -> next;
        if (!qcurr)
            break;
        // Deadlock prevention: Limit wait time in queue
        if (std::time(0) - qcurr -> timestamp > WAIT_TIME) {
            printf("------ Deadlock prevented ------\n");
            qprev -> next = qcurr -> next;
            delete qcurr;
            continue;
        };
        // No shuffle if at the end of queue
        if (lock -> tail == qcurr)
            break;
        
        // NUMA-awareness policy: Group by SOCKET_ID
        //// Found thread waiting on the same socket
        if (qcurr -> socket_ID == qnode -> socket_ID) {
            // No shuffling required
            if (qprev -> socket_ID == qnode -> socket_ID) {
                qcurr -> batch_count = ++batch;
                qlast = qprev = qcurr;
            }
            // Other socket waiters exist between qcurr and qlast
            else {
                qnext = qcurr -> next;
                if (!qnext)
                    break;
                // Move qcurr after qlast and point qprev.next to qnext
                qcurr -> batch_count = ++batch;
                qprev -> next = qnext;
                qcurr -> next = qlast -> next;
                qlast -> next = qcurr;
                // Update qlast to point to qcurr now
                qlast = qcurr;
            };
        }
        // Move on to the next qnode
        else {
            qprev = qcurr;
        };

        // Exit: Next waiter can acquire lock or waiter at the head of the waiting queue
        if ((vnext_waiter == true && lock -> glock == UNLOCKED)
            || (vnext_waiter == false && qnode -> status == S_READY))
            break;
    };

    qlast -> shuffler_status = S_SHUFFLER;
};

void spin_until_very_next_waiter(struct lock* lock, 
                                 struct qnode* qprev, 
                                 struct qnode* qcurr) {
    qprev -> next = qcurr;
    while (true) {
        // Ready to hold the lock
        if (qcurr -> status == S_READY) {
            return;
        };
        // Previous shuffler assigned qcurr as shuffler
        if (qcurr -> shuffler_status == S_SHUFFLER) {
            shuffler_waiters(lock, qcurr, false);
        };
    }
};

void spin_lock(struct lock* lock) {
    // Try to steal/acquire the lock if there is no lock holder
    if (lock -> glock == UNLOCKED && (lock -> glock).compare_exchange_strong(UNLOCKED, LOCKED))
        return;

    // Otherwise, join queue
    // 1. Initialize node states
    struct qnode* qnode = new struct qnode;
    qnode -> socket_ID = 999;

    // 2. Add node to queue tail
    struct qnode* qprev = std::atomic_exchange(&(lock -> tail), qnode);
    if (qprev)
        // Waiters ahead
        spin_until_very_next_waiter(lock, qprev, qnode);
    else 
        // Disable stealing to maintain FIFO property
        std::atomic_exchange(&(lock -> no_stealing), true);

    // 3. qnode at the head of queue; get TAS lock
    while (true) {
        // 1st qnode or qnode with socket_ID different from pred becomes shuffler
        if (qnode -> batch_count == 0 || qnode -> shuffler_status == S_SHUFFLER)
            shuffler_waiters(lock, qnode, true);

        // Deadlock prevention
        if (std::time(0) - qnode -> timestamp > WAIT_TIME) {
            printf("------ Deadlock prevented ------\n");
            break;
        }

        // Wait until lock holder exits critical section
        while (lock -> glock == LOCKED)
            continue;

        // Try to automatically get the lock
        if ((lock -> glock).compare_exchange_strong(UNLOCKED, LOCKED))
            break;
    };

    // TODO: Adapt it to deadlock prevention?
    // MCS unlock phase
    struct qnode* qnext = qnode -> next;
    // Deadlock prevention
    if (std::time(0) - qnode -> timestamp > WAIT_TIME) {
        printf("------ Deadlock prevented ------\n");
        delete qnode;
    }
    // qnode is the last one / qnext is being updated
    if (!qnext) {
        if ((lock -> tail).compare_exchange_strong(qnode, nullptr)) {
            bool expected = true;
            (lock -> no_stealing).compare_exchange_strong(expected, false);
            return;
        }
        // Failed CAS: Wait for next waiter
        while (!(qnode -> next)) {
            continue;
        }
        qnext = qnode -> next;
    }
    // Notify next waiter
    qnext -> status = S_READY;
};

void spin_unlock(struct lock* lock) {
    lock -> glock = UNLOCKED;
};