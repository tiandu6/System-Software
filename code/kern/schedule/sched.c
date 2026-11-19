#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) 
{
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 2511107
void schedule(void)
{
    bool intr_flag;
    // 选进程和准备切换时先关中断，记住原来的开关状态。
    local_intr_save(intr_flag);
    // 这次调度正在处理，清掉当前进程的请求标记。
    current->need_resched = 0;

    // idleproc 从表头开始找，其他进程从自己后面开始找。
    list_entry_t *start = (current == idleproc) ? &proc_list : &current->list_link;
    list_entry_t *entry = start;
    // 没找到可运行的普通进程时，让 idleproc 运行。
    struct proc_struct *next = idleproc;
    // 链表首尾相连，绕回起点时，一轮查找结束。
    do
    {
        entry = list_next(entry);
        // proc_list 是链表头。跳过它，再由节点找出对应的 PCB。
        if (entry != &proc_list)
        {
            struct proc_struct *proc = le2proc(entry, list_link);
            if (proc->state == PROC_RUNNABLE)
            {
                next = proc;
                break;
            }
        }
    } while (entry != start);

    // 统计被选中的次数，即使选中的是当前进程。
    next->runs++;
    if (next != current)
    {
        // schedule 只决定谁运行，实际切换由 proc_run 完成。
        proc_run(next);
    }
    // 如果发生了切换，等本次调用所在的进程再次运行时才执行到这里。
    local_intr_restore(intr_flag);
}


