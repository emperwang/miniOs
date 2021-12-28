#include "thread.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "debug.h"
#include "process.h"
#include "sync.h"
#include "print.h"
#include "file.h"
#include "stdio.h"

struct task_struct* idle_thread;   //idle线程
struct task_struct* main_thread;        //  主线程PCB
struct list thread_ready_list;          // 就绪队列
struct list thread_all_list;            // 所有任务队列
static struct list_elem* thread_tag;     // 用于保存队列中的线程节点
struct lock pid_lock;

extern void switch_to(struct task_struct* cur, struct task_struct* next);

static void idle(void* arg UNUSED){
    while(1){
        thread_block(TASK_BLOCKED);
        // 执行halt
        asm volatile("sti; hlt": : :"memory");
    }
}

// 获取当前线程PCB 指针
struct task_struct* running_thread(void){
    uint32_t esp;
    asm volatile ("mov %%esp, %0":"=g"(esp));
    return (struct task_struct*)(esp & 0xfffff000);
}

// 由kernel_thread 去执行function(func_arg)
static void kernel_thread(thread_func *func, void *func_arg){
    //打开中断
    intr_enable();
    func(func_arg);
}

// 给线程分配pid
static pid_t allocate_pid(void){
    static pid_t next_pid = 0;
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
}

// 线程创建pid
pid_t fork_pid(void){
    return allocate_pid();
}

// 初始化线程栈 thread_stack, 将待执行的函数和参数放入 thread_stack中相应位置
void thread_create(struct task_struct *pthread, thread_func function, void *func_arg){
    // 先预留中断栈的空间
    pthread->self_kstack -= sizeof(struct intr_stack);
    //再预留出 线程栈空间
    pthread->self_kstack -= sizeof(struct thread_stack);

    struct thread_stack *kthread_stack = (struct thread_stack*)pthread->self_kstack;

    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = 0;
    kthread_stack->ebx = 0;
    kthread_stack->esi = 0;
    kthread_stack->edi = 0;
}


// 初始化线程基本信息
void init_thread(struct task_struct *pthread, char*name, int prio){
    memset(pthread, 0, sizeof(*pthread));
    pthread->pid = allocate_pid();
    strcpy(pthread->name, name);

    if(pthread == main_thread){
        pthread->status = TASK_RUNNING;
    }else{
        pthread->status = TASK_READY;
    }

    // self_kstack 是线程自己在内核态下使用的栈顶地址
    pthread->self_kstack = (uint32_t*)((uint32_t)pthread+PG_SIZE);
    pthread->priority = prio;
    pthread->ticks = prio;
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;

    // 初始化线程对应的文件描述符表
    //预留标准输入输出
    pthread->fd_table[0] = 0;
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;
    // 其余设置为-1
    uint8_t fd_idx = 3;
    while(fd_idx < MAX_FILES_OPEN_PER_PROC){
        pthread->fd_table[fd_idx] = -1;
        fd_idx++;
    }
    pthread->cwd_inode_nr = 0;  // 默认以根目录为默认工作路径
    pthread->parent_pid = -1;       // 表示没有父进程
    pthread->stack_magic = 0x19870916;      // 魔数
}


// 创建优先级为prio的线程,名称为name,线程所执行的函数为 function(func_arg)
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg){
    //PCB 都位于内核空间, 包括用户进程的PCB也位域内核空间
    struct task_struct *thread = get_kernel_pages(1);
    init_thread(thread,name, prio);
    thread_create(thread, function, func_arg);

    // 确保之前不在队列中
    ASSERT(!(elem_find(&thread_ready_list, &thread->general_tag)));
    // 加入就绪队列中
    list_append(&thread_ready_list, &thread->general_tag);

    ASSERT(!(elem_find(&thread_all_list, &thread->all_list_tag)));
    //加入全部队列中
    list_append(&thread_all_list, &thread->all_list_tag);

    return thread;
}
//asm volatile ("movl %0, %%esp; pop %%ebp; pop %%ebx; pop %%edi; pop %%esi;
//ret"::"g" (thread->self_kstack):"memory") ;

// 将kernel中的main函数完善为主线程
static void make_main_thread(void){
    /******
     * 因为main线程早已运行,咱们在loader.S中进入内核时 mov esp,0xc009f000,就是为其预留PCB的,
     * 因此PCB 地址为0xc009e000,无需在申请一页
     */
    main_thread = running_thread();
    init_thread(main_thread, "main",31);

    // main函数是当前线程,当前线程不在thread_ready_list中农,所以只将其添加到 thread_all_list中
    ASSERT(!(elem_find(&thread_all_list, &main_thread->all_list_tag)));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}

// 任务调度
void schedule(void){
    ASSERT(intr_get_status() == INTR_OFF);

    struct task_struct *cur = running_thread();

    if(cur->status == TASK_RUNNING){
        ASSERT(!(elem_find(&thread_ready_list, &cur->general_tag)));
        list_append(&thread_ready_list, &cur->general_tag);
        cur->ticks = cur->priority;
        // 重新更新 ticks 和priority
        cur->status = TASK_READY;

    }else {
        //  如果此线程需要某件事发生后 才能继续 上cpu运行
        // 不需要将其加入队列,因为当前线程不在就绪队列中
    }
    //如果就绪队列中没有可运行的任务,就唤醒idle
    if(list_empty(&thread_ready_list)){
        thread_unblock(idle_thread);
    }

    thread_tag = NULL;
    thread_tag = list_pop(&thread_ready_list);

    struct task_struct *next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;

    process_activate(next);
    switch_to(cur, next);
}

// 当前线程阻塞自己
void thread_block(enum task_status stat){

    ASSERT((stat == TASK_BLOCKED) || (stat==TASK_WAITTING) || (stat == TASK_HANGING));

    enum intr_status old_status = intr_disable();
    struct task_struct *cur_thread = running_thread();

    cur_thread->status = stat;      // 更新状态
    schedule();                     // 重新调度
    intr_set_status(old_status);
}

// 将线程 pthread 接触阻塞
void thread_unblock(struct task_struct *pthread){
    enum intr_status old_status  = intr_disable();
    ASSERT((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITTING) || (pthread->status == TASK_HANGING));

    if(pthread->status != TASK_READY){
        ASSERT(!(elem_find(&thread_ready_list, &pthread->general_tag)));
        if(elem_find(&thread_ready_list, &pthread->general_tag)){
            PANIC("thread_unblock: blocked thread in ready_list\n");
        }
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->status = TASK_READY;
    }

    intr_set_status(old_status);
}

// 主动让出cpu, 换其他线程运行
void thread_yield(void){
    struct task_struct* cur = running_thread();
    enum intr_status old_status  = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag))
    list_append(&thread_ready_list, &cur->general_tag);
    cur->status = TASK_READY;
    schedule();
    intr_set_status(old_status);
}

// 以空格的方式输出buf
static void pad_print(char* buf, int32_t buf_len, void* ptr, char format){
    memset(buf, 0, buf_len);
    uint8_t out_pad_0idx = 0;

    switch(format){
        case 's':
            out_pad_0idx = sprintf(buf, "%s", ptr);
            break;
        case 'd':
            out_pad_0idx = sprintf(buf, "%d", *((uint16_t*)ptr));
            break;

        case 'x':
            out_pad_0idx = sprintf(buf, "%x", *((uint32_t*)ptr));
            break;
    }
    while(out_pad_0idx < buf_len){  // 空格填充
        buf[out_pad_0idx] = ' ';
        out_pad_0idx++;
    }
    sys_write(stdout_no, buf, buf_len-1);
}

// 作用在list_traversal函数中的回调函数,用于针对线程队列的处理
static bool ele2thread_info(struct list_elem* pelem, int arg UNUSED){
    struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);

    char out_pad[16] = {0};
    pad_print(out_pad, 16, &pthread->pid, 'd');
    if(pthread->parent_pid == -1){
        pad_print(out_pad, 16, "NULL", "16");
    }else {
        pad_print(out_pad, 16, &pthread->parent_pid, 'd');
    }

    switch(pthread->status){
        case 0:
            pad_print(out_pad, 16,"RUNNING",'s');
            break;

        case 1:
            pad_print(out_pad, 16,"READY",'s');
            break;

        case 2:
            pad_print(out_pad, 16,"BLOCKED",'s');
            break;

        case 3:
            pad_print(out_pad, 16,"WAITTING",'s');
            break;

        case 4:
            pad_print(out_pad, 16,"HANGING",'s');
            break;

        case 5:
            pad_print(out_pad, 16,"DIED",'s');
            break;
    }
    pad_print(out_pad, 16, &pthread->elapsed_ticks, 'x');

    memset(out_pad, 0, 16);
    ASSERT(strlen(pthread->name) < 17);
    memcpy(out_pad, pthread->name, strlen(pthread->name));
    strcat(out_pad, "\n");
    sys_write(stdout_no, out_pad, strlen(out_pad));
    return false;
}

// 打印任务列表
void sys_ps(void){
    char* ps_title ="PID        PPID        STAT        TICKS       COMMAND\n";
    sys_write(stdout_no, ps_title, strlen(ps_title));
    list_traversal(&thread_all_list, ele2thread_info, 0);
}

extern void init(void);
// 初始化 线程环境
void thread_init(void){
    put_str(" thread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    lock_init(&pid_lock);
    // 创建第一个用户进程
    process_execute(init, "init");
    make_main_thread();
    idle_thread = thread_start("idle", 10, idle,NULL);
    put_str(" thread init done\n");
}

