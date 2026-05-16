#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: put a new process to queue [q] */
        if (q == NULL || proc == NULL)
                return;
        // the queue_t is a struct with 2 atributes: proc is an array and its size
        // thus, enqueue is add new elements after its tail(the last non-NULL element)
        if (q->size == MAX_QUEUE_SIZE)
        {
                // somehow the queue.h cannot be inclueded directly
                printf("QUEUE IS FULL, CANNOT ADD PID %d\n", proc->pid);
                return;
        }
        q->proc[(q->size)++] = proc;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * so the highest proprity is the smallest numberically
         * 
        */
        if (q == NULL || empty(q))
                return NULL;

        int best_idx = 0;

        for (int i = 1; i < q->size; i++) {
        #ifdef MLQ_SCHED //find the reason in common.h struct pcb_t
                if (q->proc[i]->prio < q->proc[best_idx]->prio)
        #else
                if (q->proc[i]->priority < q->proc[best_idx]->priority)
        #endif
                {
                        best_idx = i;
                }
        }

        struct pcb_t *chosen = q->proc[best_idx];
        //shift left
        for (int i = best_idx; i < q->size - 1; i++) {
                q->proc[i] = q->proc[i + 1];
        }

        q->proc[--q->size] = NULL;

        return chosen;
}
struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: remove a specific item from queue
         * */
        if (q == NULL || empty(q) || proc == NULL)
                return NULL;

        for (int i = 0; i < q->size; i++)
        {
                if (q->proc[i] == proc)
                { // shift left
                        for (int j = i; j < q->size - 1; j++)
                        {
                                q->proc[j] = q->proc[j + 1];
                        }
                        q->proc[--(q->size)] = NULL;
                        return proc;
                }
        }
        return NULL; // not found
}
