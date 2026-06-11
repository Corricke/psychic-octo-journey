#include "task.h"
#include "paging.h"
#include "frame.h"
#include "heap.h"
#include "gdt.h"
#include "string.h"
#include "console.h"

#define KSTACK_SIZE 4096

extern void ctx_switch(uint32_t *old_esp, uint32_t new_esp);
extern void user_entry(void);

static struct task tasks[MAX_TASKS];
static int current;
static int next_pid = 1;

void task_init(void)
{
    /* Task 0 is the boot flow (kernel shell), using the boot stack. */
    struct task *t = &tasks[0];
    t->state = TASK_READY;
    t->cr3 = kernel_pgdir();
    t->pid = next_pid++;
    memcpy(t->name, "shell", 6);
    current = 0;
}

struct task *task_current(void)
{
    return &tasks[current];
}

struct task *task_by_pid(int pid)
{
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state != TASK_FREE && tasks[i].pid == pid)
            return &tasks[i];
    return 0;
}

struct task *task_table(void)
{
    return tasks;
}

int task_spawn_user(const char *name, const void *image, uint32_t size)
{
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        console_puts("too many tasks\n");
        return -1;
    }

    uint32_t pgdir = paging_new_user(image, size);
    if (!pgdir) {
        console_puts("out of memory\n");
        return -1;
    }
    uint8_t *kstack = kmalloc(KSTACK_SIZE);
    if (!kstack) {
        paging_destroy_user(pgdir);
        console_puts("out of memory\n");
        return -1;
    }

    /*
     * Craft the initial kernel stack so that ctx_switch unwinds into
     * user_entry with an iret frame for ring 3 on top:
     *
     *   [eflags edi esi ebx ebp]  <- popped by ctx_switch
     *   [&user_entry]             <- ctx_switch's ret
     *   [eip cs eflags esp ss]    <- user_entry's iretd
     */
    uint32_t *sp = (uint32_t *)(kstack + KSTACK_SIZE);
    *--sp = SEL_UDATA;              /* ss */
    *--sp = USER_STACK_TOP;         /* esp */
    *--sp = 0x202;                  /* eflags: IF set */
    *--sp = SEL_UCODE;              /* cs */
    *--sp = USER_BASE;              /* eip */
    *--sp = (uint32_t)user_entry;
    *--sp = 0;                      /* ebp */
    *--sp = 0;                      /* ebx */
    *--sp = 0;                      /* esi */
    *--sp = 0;                      /* edi */
    *--sp = 0x002;                  /* eflags for popfd: IF off until iretd */

    struct task *t = &tasks[slot];
    t->esp = (uint32_t)sp;
    t->cr3 = pgdir;
    t->pgdir = pgdir;
    t->kstack = kstack;
    t->pid = next_pid++;
    t->exit_code = 0;
    int n = 0;
    while (name[n] && n < 15) {
        t->name[n] = name[n];
        n++;
    }
    t->name[n] = '\0';
    t->state = TASK_READY;
    return t->pid;
}

void task_reap(struct task *t)
{
    if (t->state != TASK_ZOMBIE)
        return;
    if (t->pgdir)
        paging_destroy_user(t->pgdir);
    if (t->kstack)
        kfree(t->kstack);
    memset(t, 0, sizeof(*t));
    t->state = TASK_FREE;
}

void schedule(void)
{
    int next = current;
    for (int i = 1; i <= MAX_TASKS; i++) {
        int cand = (current + i) % MAX_TASKS;
        if (tasks[cand].state == TASK_READY) {
            next = cand;
            break;
        }
    }
    if (next == current)
        return;

    struct task *prev = &tasks[current];
    struct task *to = &tasks[next];
    current = next;

    if (to->kstack)
        tss_set_esp0((uint32_t)to->kstack + KSTACK_SIZE);
    if (to->cr3 != prev->cr3)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(to->cr3) : "memory");
    ctx_switch(&prev->esp, to->esp);
}

void sched_preempt(void)
{
    schedule();
}

void task_exit(int code)
{
    struct task *t = &tasks[current];
    t->exit_code = code;
    __asm__ volatile ("cli");
    t->state = TASK_ZOMBIE;
    for (;;) {
        schedule();
        /* Unreachable unless scheduled by mistake; stay parked. */
        __asm__ volatile ("sti; hlt; cli");
    }
}
