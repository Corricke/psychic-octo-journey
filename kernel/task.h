#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define MAX_TASKS 8

enum task_state {
    TASK_FREE = 0,
    TASK_READY,
    TASK_ZOMBIE,
};

struct task {
    uint32_t esp;               /* saved kernel stack pointer */
    uint32_t cr3;
    enum task_state state;
    uint8_t *kstack;            /* kmalloc'd; 0 for the boot task */
    uint32_t pgdir;             /* user page dir to free; 0 for kernel */
    int pid;
    int exit_code;
    char name[16];
};

void task_init(void);
struct task *task_current(void);
struct task *task_by_pid(int pid);
struct task *task_table(void);  /* for ps */

/* Spawn a ring-3 process from a flat binary image. Returns pid or -1. */
int task_spawn_user(const char *name, const void *image, uint32_t size);

void task_reap(struct task *t);
void task_exit(int code);       /* current task; does not return */

void schedule(void);            /* call with interrupts disabled */
void sched_preempt(void);       /* called from the timer interrupt */

#endif
