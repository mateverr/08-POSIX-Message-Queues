#include "msgq_lab.h"

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* One queue name per task — prevents cross-contamination between runs */
#define Q_EXCHANGE "/msgq_lab_exchange"
#define Q_PRIO     "/msgq_lab_prio"
#define Q_FULL     "/msgq_lab_full"
#define Q_SIZE     "/msgq_lab_size"

/*
 * mq_msgsize used when opening every queue.
 * Must be >= sizeof(struct SensorReading) so a struct fits in one message.
 */
#define MSG_BUF_SIZE 128

/*
 * Helper: open (and create) a queue with the given capacity and message size.
 * Returns (mqd_t)-1 on failure, like mq_open itself.
 */
static mqd_t open_queue(const char *name, int flags, long maxmsg, long msgsize) {
    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg  = maxmsg;
    attr.mq_msgsize = msgsize;
    return mq_open(name, flags | O_CREAT, 0660, &attr);
}

/* Helper: wait for a child and return 0 only if it exited with status 0. */
static int wait_child(pid_t pid) {
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : 1;
}

/*
 * Task A — task_struct_exchange()
 *
 * Demonstrates: passing a struct between two processes via a POSIX message
 * queue.  Unlike a pipe, the queue persists until explicitly unlinked.
 *
 * Steps:
 *   1. mq_unlink(Q_EXCHANGE) — remove any leftover queue (ignore errors).
 *   2. Open the queue with mq_open():
 *        flags  = O_CREAT | O_RDWR
 *        mode   = 0660
 *        attr   = { .mq_maxmsg = 4, .mq_msgsize = MSG_BUF_SIZE }
 *   3. Fork one child process.
 *   4. Parent: // Remove comments and modify them into real code
 *        a. Build SensorReading r = { .sensor_id = 7, .value = 42 }.
 *        b. mq_send(mqd, (char *)&r, sizeof(r), 0) — priority 0.
 *        c. mq_close(mqd).
 *        d. waitpid() for the child.
 *        e. Print exactly:  parent: sent reading
 *   5. Child:
 *        a. mq_receive(mqd, buf, MSG_BUF_SIZE, &prio) — blocks until message.
 *        b. Cast buf to SensorReading * and print exactly:
 *             child received: sensor id=7 value=42
 *        c. mq_close(mqd).
 *        d. mq_unlink(Q_EXCHANGE).
 *        e. exit(0).
 *
 * Note: mq_receive buffer size must be >= mq_msgsize (use MSG_BUF_SIZE).
 */
int task_struct_exchange(void) {
    mq_unlink(Q_EXCHANGE); /* ignore errors: queue may not exist */

    mqd_t mqd = open_queue(Q_EXCHANGE, O_RDWR, 4, MSG_BUF_SIZE);
    if (mqd == (mqd_t)-1) {
        perror("mq_open " Q_EXCHANGE);
        return 1;
    }

    fflush(stdout); /* don't duplicate buffered output into the child */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        mq_close(mqd);
        mq_unlink(Q_EXCHANGE);
        return 1;
    }

    if (pid == 0) {
        /* Child: block until the parent's reading arrives */
        char buf[MSG_BUF_SIZE];
        unsigned int prio;
        ssize_t n = mq_receive(mqd, buf, MSG_BUF_SIZE, &prio);
        if (n < (ssize_t)sizeof(struct SensorReading)) {
            perror("mq_receive " Q_EXCHANGE);
            mq_close(mqd);
            exit(1);
        }

        struct SensorReading r;
        memcpy(&r, buf, sizeof(r)); /* memcpy avoids alignment issues */
        printf("child received: sensor id=%d value=%d\n", r.sensor_id, r.value);
        fflush(stdout);

        mq_close(mqd);
        mq_unlink(Q_EXCHANGE);
        exit(0);
    }

    /* Parent: send the reading, then wait for the child to print */
    struct SensorReading r = { .sensor_id = 7, .value = 42 };
    int rc = 0;
    if (mq_send(mqd, (const char *)&r, sizeof(r), 0) < 0) {
        perror("mq_send " Q_EXCHANGE);
        rc = 1;
    }
    mq_close(mqd);

    if (wait_child(pid) != 0) rc = 1;
    if (rc != 0) {
        mq_unlink(Q_EXCHANGE);
        return 1;
    }

    printf("parent: sent reading\n");
    fflush(stdout);
    return 0;
}

/*
 * Task B — task_priority_order()
 *
 * Demonstrates: POSIX message queues deliver messages highest-priority-first,
 * regardless of send order (key difference from pipes and System V queues with
 * type=0).
 *
 * Steps:
 *   1. mq_unlink(Q_PRIO) — clean slate.
 *   2. Open the queue O_CREAT | O_RDWR with mq_maxmsg=4, mq_msgsize=MSG_BUF_SIZE.
 *   3. Before forking, send TWO messages:
 *        a. SensorReading { .sensor_id = 1, .value = 10 } at priority 1  (first)
 *        b. SensorReading { .sensor_id = 2, .value = 99 } at priority 5  (second)
 *   4. mq_close(mqd), then fork().
 *   5. Child:
 *        a. Re-open the queue O_RDONLY (no O_CREAT).
 *        b. First mq_receive → highest priority wins → value=99.
 *           Print exactly:  child received hi-pri: value=99
 *        c. Second mq_receive → remaining message → value=10.
 *           Print exactly:  child received lo-pri: value=10
 *        d. mq_close, mq_unlink(Q_PRIO), exit(0).
 *   6. Parent: waitpid() for the child.
 *
 * Key insight: value=10 was enqueued first but is dequeued LAST because its
 * priority (1) is lower than priority (5).
 */
int task_priority_order(void) {
    mq_unlink(Q_PRIO);

    mqd_t mqd = open_queue(Q_PRIO, O_RDWR, 4, MSG_BUF_SIZE);
    if (mqd == (mqd_t)-1) {
        perror("mq_open " Q_PRIO);
        return 1;
    }

    /* Send low priority FIRST, high priority SECOND */
    struct SensorReading lo = { .sensor_id = 1, .value = 10 };
    struct SensorReading hi = { .sensor_id = 2, .value = 99 };
    if (mq_send(mqd, (const char *)&lo, sizeof(lo), 1) < 0 ||
        mq_send(mqd, (const char *)&hi, sizeof(hi), 5) < 0) {
        perror("mq_send " Q_PRIO);
        mq_close(mqd);
        mq_unlink(Q_PRIO);
        return 1;
    }
    mq_close(mqd);

    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        mq_unlink(Q_PRIO);
        return 1;
    }

    if (pid == 0) {
        /* Child: re-open read-only; the queue persists after the close above */
        mqd_t rq = mq_open(Q_PRIO, O_RDONLY);
        if (rq == (mqd_t)-1) {
            perror("mq_open (child) " Q_PRIO);
            exit(1);
        }

        char buf[MSG_BUF_SIZE];
        unsigned int prio;
        struct SensorReading r;

        /* First receive: highest priority (5) wins */
        if (mq_receive(rq, buf, MSG_BUF_SIZE, &prio) < (ssize_t)sizeof(r)) {
            perror("mq_receive " Q_PRIO);
            mq_close(rq);
            exit(1);
        }
        memcpy(&r, buf, sizeof(r));
        printf("child received hi-pri: value=%d\n", r.value);

        /* Second receive: the remaining priority-1 message */
        if (mq_receive(rq, buf, MSG_BUF_SIZE, &prio) < (ssize_t)sizeof(r)) {
            perror("mq_receive " Q_PRIO);
            mq_close(rq);
            exit(1);
        }
        memcpy(&r, buf, sizeof(r));
        printf("child received lo-pri: value=%d\n", r.value);
        fflush(stdout);

        mq_close(rq);
        mq_unlink(Q_PRIO);
        exit(0);
    }

    if (wait_child(pid) != 0) {
        mq_unlink(Q_PRIO);
        return 1;
    }
    return 0;
}

/*
 * Task C — task_full_queue()
 *
 * Demonstrates: a bounded POSIX queue has a hard capacity of mq_maxmsg.
 * When the queue is full and O_NONBLOCK is set, mq_send() fails immediately
 * with errno == EAGAIN instead of blocking.
 *
 * Steps (no fork required):
 *   1. mq_unlink(Q_FULL) — clean slate.
 *   2. Open the queue with mq_maxmsg=2, mq_msgsize=MSG_BUF_SIZE.
 *      Include O_NONBLOCK in the flags.
 *   3. Send two messages (both succeed — queue is now full).
 *   4. Attempt a third mq_send().
 *      It must fail: errno == EAGAIN.
 *   5. If errno == EAGAIN, print exactly:  queue full: EAGAIN detected
 *   6. mq_close, mq_unlink(Q_FULL).
 */
int task_full_queue(void) {
    mq_unlink(Q_FULL);

    mqd_t mqd = open_queue(Q_FULL, O_RDWR | O_NONBLOCK, 2, MSG_BUF_SIZE);
    if (mqd == (mqd_t)-1) {
        perror("mq_open " Q_FULL);
        return 1;
    }

    struct SensorReading r = { .sensor_id = 3, .value = 0 };
    int rc = 1;

    /* Fill the queue to its capacity of 2 */
    for (int i = 0; i < 2; i++) {
        r.value = i;
        if (mq_send(mqd, (const char *)&r, sizeof(r), 0) < 0) {
            perror("mq_send " Q_FULL);
            goto out;
        }
    }

    /* Third send must fail immediately because of O_NONBLOCK */
    errno = 0;
    if (mq_send(mqd, (const char *)&r, sizeof(r), 0) < 0 && errno == EAGAIN) {
        printf("queue full: EAGAIN detected\n");
        fflush(stdout);
        rc = 0;
    } else {
        fprintf(stderr, "expected EAGAIN on full queue\n");
    }

out:
    mq_close(mqd);
    mq_unlink(Q_FULL);
    return rc;
}

/*
 * Task D — task_size_limit()
 *
 * Demonstrates: mq_send() enforces mq_msgsize; any payload whose byte length
 * exceeds mq_msgsize causes the call to return -1 with errno == EMSGSIZE.
 *
 * Steps (no fork required):
 *   1. mq_unlink(Q_SIZE) — clean slate.
 *   2. Open the queue with mq_msgsize = sizeof(struct SensorReading).
 *      Use O_CREAT | O_WRONLY.
 *   3. Prepare a char buffer of size sizeof(struct SensorReading) + 1. MAKE THE MESSAGE BIGGER
 *   4. Call mq_send() with that buffer — it must fail: errno == EMSGSIZE. TRY TO SEND THE MESSAGE, AND IT MUST FAIL
 *   5. If errno == EMSGSIZE, print exactly:  oversized send: EMSGSIZE detected
 *   6. mq_close, mq_unlink(Q_SIZE).
 */
int task_size_limit(void) {
    mq_unlink(Q_SIZE);

    mqd_t mqd = open_queue(Q_SIZE, O_WRONLY, 4,
                           (long)sizeof(struct SensorReading));
    if (mqd == (mqd_t)-1) {
        perror("mq_open " Q_SIZE);
        return 1;
    }

    /* One byte larger than mq_msgsize */
    char buf[sizeof(struct SensorReading) + 1];
    memset(buf, 'X', sizeof(buf));

    int rc = 1;
    errno = 0;
    if (mq_send(mqd, buf, sizeof(buf), 0) < 0 && errno == EMSGSIZE) {
        printf("oversized send: EMSGSIZE detected\n");
        fflush(stdout);
        rc = 0;
    } else {
        fprintf(stderr, "expected EMSGSIZE on oversized send\n");
    }

    mq_close(mqd);
    mq_unlink(Q_SIZE);
    return rc;
}

/* ------------------------------------------------------------------ */
/* main — do not modify                                                */
/* ------------------------------------------------------------------ */
int main(void) 
{
    if (task_struct_exchange() != 0) return EXIT_FAILURE;
    if (task_priority_order()  != 0) return EXIT_FAILURE;
    if (task_full_queue()      != 0) return EXIT_FAILURE;
    if (task_size_limit()      != 0) return EXIT_FAILURE;
    printf("all tasks done\n");
    return EXIT_SUCCESS;
}
