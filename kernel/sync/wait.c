#include <kernel/sched.h>
#include <kernel/wait.h>

void wait_queue_init(struct wait_queue *queue) {
    spin_init(&queue->lock);
    queue->head = queue->tail = 0;
}

static void enqueue(struct wait_queue *queue, struct waiter *waiter) {
    waiter->next = 0;
    waiter->queued = 1;
    if (queue->tail) queue->tail->next = waiter;
    else queue->head = waiter;
    queue->tail = waiter;
}

static void dequeue(struct wait_queue *queue, struct waiter *waiter) {
    struct waiter *prev = 0;
    for (struct waiter *cursor = queue->head; cursor; prev = cursor, cursor = cursor->next) {
        if (cursor != waiter) continue;
        if (prev) prev->next = cursor->next;
        else queue->head = cursor->next;
        if (queue->tail == cursor) queue->tail = prev;
        break;
    }
    waiter->next = 0;
    waiter->queued = 0;
}

static struct waiter *pop(struct wait_queue *queue) {
    struct waiter *waiter = queue->head;
    if (waiter) dequeue(queue, waiter);
    return waiter;
}

void wait_prepare(struct wait_queue *queue, struct waiter *waiter) {
    unsigned long flags = spin_lock_irqsave(&queue->lock);
    waiter->task = task_current();
    if (!waiter->queued) enqueue(queue, waiter);
    sched_sleep_prepare();
    spin_unlock_irqrestore(&queue->lock, flags);
}

void wait_finish(struct wait_queue *queue, struct waiter *waiter) {
    sched_sleep_cancel();
    unsigned long flags = spin_lock_irqsave(&queue->lock);
    if (waiter->queued) dequeue(queue, waiter);
    spin_unlock_irqrestore(&queue->lock, flags);
}

void wake_up_one(struct wait_queue *queue) {
    unsigned long flags = spin_lock_irqsave(&queue->lock);
    struct waiter *waiter = pop(queue);
    if (waiter) sched_wake(waiter->task);
    spin_unlock_irqrestore(&queue->lock, flags);
}

void wake_up_all(struct wait_queue *queue) {
    unsigned long flags = spin_lock_irqsave(&queue->lock);
    for (struct waiter *waiter; (waiter = pop(queue));) sched_wake(waiter->task);
    spin_unlock_irqrestore(&queue->lock, flags);
}
