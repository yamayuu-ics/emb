#include "defines.h"
#include "kozos.h"
#include "intr.h"
#include "interrupt.h"
#include "syscall.h"
#include "lib.h"

#define THREAD_NUM 6
#define PRIORITY_NUM 16
#define THREAD_NAME_SIZE 15

/* スレッド・コンテキスト */
typedef struct _kz_context {
  uint32 sp; /* スタック・ポインタ */
} kz_context;

/* タスク・コントロール・ブロック(TCB) */
typedef struct _kz_thread
{
    struct _kz_thread *next;
    char name[THREAD_NAME_SIZE + 1]; /* スレッド名 */
    int priority;
    char *stack; /* スタック */
    uint32 flags;
#define KZ_THREAD_FLAG_READY (1 << 0)
    struct
    {                   /* スレッドのスタート・アップ(thread_init())に渡すパラメータ */
        kz_func_t func; /* スレッドのメイン関数 */
        int argc;       /* スレッドのメイン関数に渡す argc */
        char **argv;    /* スレッドのメイン関数に渡す argv */
    } init;

    struct
    { /* システム・コール用バッファ */
        kz_syscall_type_t type;
        kz_syscall_param_t *param;
    } syscall;

    kz_context context; /* コンテキスト情報 */
} kz_thread;

/* スレッドのレディー・キュー */
static struct
{
    kz_thread *head;
    kz_thread *tail;
} readyque[PRIORITY_NUM];

static kz_thread *current; /* カレント・スレッド */
static kz_thread threads[THREAD_NUM]; /* タスク・コントロール・ブロック */
static kz_handler_t handlers[SOFTVEC_TYPE_NUM]; /* 割込みハンドラ */

void dispatch(kz_context *context);


static int getcurrent(void){
    if(current == NULL){
        return -1;
    }

    if(!(current->flags & KZ_THREAD_FLAG_READY)){
        return -1;
    }

    readyque[current->priority].head = current->next;
    if(readyque[current->priority].head == NULL){
        readyque[current->priority].tail = NULL;
    }
    current->flags &= ~KZ_THREAD_FLAG_READY;
    current->next = NULL;

    return 0;
}

static int putcurrent(void){
    if(current == NULL){
        return -1;
    }

    if(current->flags & KZ_THREAD_FLAG_READY){
        return -1;
    }

    if(readyque[current->priority].tail){
        readyque[current->priority].tail->next = current;
    }
    else{
        readyque[current->priority].head = current;
    }
    readyque[current->priority].tail = current;
    current->flags |= KZ_THREAD_FLAG_READY;

    return 0;
}

static void thread_end(void){
    kz_exit();
}

static void thread_init(kz_thread *thp){
    thp->init.func(thp->init.argc,thp->init.argv);
    thread_end();
}

static kz_thread_id_t thread_run(kz_func_t func,char *name,int priority,int stacksize,int argc,char *argv[]){
    int i;
    kz_thread *thp;
    uint32 *sp;
    extern char userstack;
    static char *thread_stack = &userstack;

    for(i=0;i<THREAD_NUM;i++){
        thp = &threads[i];
        if(!thp->init.func){
            /* 空 */
            break;
        }
    }
    if(i == THREAD_NUM){
        return -1;
    }

    memset(thp,0,sizeof(*thp));

    /* set TCB infomation */
    strcpy(thp->name,name);

    thp->next = NULL;
    thp->priority = priority;
    thp->flags = 0;
    thp->init.func = func;
    thp->init.argc = argc;
    thp->init.argv = argv;

    memset(thread_stack,0,stacksize);
    thread_stack+= stacksize;

    thp->stack = thread_stack;

    sp = (uint32 *)thp->stack;
    *(--sp) = (uint32)thread_end;

    *(--sp) = (uint32)thread_init | ((uint32)(priority ? 0 : 0xc0) << 24);

    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;

    *(--sp) = (uint32)thp;

    thp->context.sp = (uint32)sp;

    putcurrent();

    current = thp;
    putcurrent();

    return (kz_thread_id_t)current;
}

static int thread_exit(void){
    puts(current->name);
    puts("EXIT.\n");
    memset(current,0,sizeof(*current));
    return 0;
}

static int thread_wait(void){
    putcurrent();
    return 0;
}

static int thread_sleep(void){
    return 0;
}

static int thread_wakeup(kz_thread_id_t id){
    putcurrent();

    current = (kz_thread *)id;

    putcurrent();
    return 0;
}

static kz_thread_id_t thread_getid(void){
    putcurrent();
    return (kz_thread_id_t)current;
}

static int thread_chpri(int priority){
    int old = current->priority;
    if(priority >= 0){
        current->priority = priority;
    }
    putcurrent();
    return old;
}

/* システム・コールの処理(kz_kmalloc():動的メモリ獲得) */
static void *thread_kmalloc(int size)
{
  putcurrent();
  return kzmem_alloc(size);
}

/* システム・コールの処理(kz_kfree():メモリ解放) */
static int thread_kmfree(char *p)
{
  kzmem_free(p);
  putcurrent();
  return 0;
}

static int setintr(softvec_type_t type,kz_handler_t handler){
    static void thread_intr(softvec_type_t type,unsigned long sp);

    softvec_setintr(type,thread_intr);
    handlers[type] = handler;
}


static void call_functions(kz_syscall_type_t type,kz_syscall_param_t *p){
    switch(type){
        case KZ_SYSCALL_TYPE_RUN:
            p->un.run.ret = thread_run(p->un.run.func,p->un.run.name,p->un.run.priority,p->un.run.stacksize,p->un.run.argc,p->un.run.argv);
            break;
        case KZ_SYSCALL_TYPE_EXIT:
            thread_exit();
            break;
        case KZ_SYSCALL_TYPE_WAIT:
            p->un.wait.ret = thread_wait();
            break;
        case KZ_SYSCALL_TYPE_SLEEP: /* kz_sleep() */
            p->un.sleep.ret = thread_sleep();
            break;
        case KZ_SYSCALL_TYPE_WAKEUP: /* kz_wakeup() */
            p->un.wakeup.ret = thread_wakeup(p->un.wakeup.id);
            break;
        case KZ_SYSCALL_TYPE_GETID: /* kz_getid() */
            p->un.getid.ret = thread_getid();
            break;
        case KZ_SYSCALL_TYPE_CHPRI: /* kz_chpri() */
            p->un.chpri.ret = thread_chpri(p->un.chpri.priority);
            break;
        case KZ_SYSCALL_TYPE_KMALLOC: /* kz_kmalloc() */
            p->un.kmalloc.ret = thread_kmalloc(p->un.kmalloc.size);
            break;
        case KZ_SYSCALL_TYPE_KMFREE: /* kz_kmfree() */
            p->un.kmfree.ret = thread_kmfree(p->un.kmfree.p);
            break;
        default:
            break;
        }
}

static void syscall_proc(kz_syscall_type_t type,kz_syscall_param_t *p){
    getcurrent();
    call_functions(type,p);
}

static void schedule(void){
    int i;
    for (i = 0; i < PRIORITY_NUM; i++)
    {
        if (readyque[i].head) /* 見つかった */
            break;
    }
    if (i == PRIORITY_NUM) /* 見つからなかった */
        kz_sysdown();

    current = readyque[i].head; /* カレント・スレッドに設定する */
}

static void syscall_intr(void){
    syscall_proc(current->syscall.type,current->syscall.param);
}

static void softerr_intr(void){
    puts(current->name);
    puts(" DOWN.\n");
    getcurrent();
    thread_exit();
}

static void thread_intr(softvec_type_t type,unsigned long sp){
    current->context.sp = sp;

    if(handlers[type]){
        handlers[type]();
    }
    schedule();

    dispatch(&current->context);
}

void kz_start(kz_func_t func ,char *name,int priority,int stacksize,int argc,char *argv[]){
    kzmem_init(); /* 動的メモリの初期化 */
    
    current = NULL;
    
    memset(readyque,0,sizeof(readyque));
    memset(threads,0,sizeof(threads));
    memset(handlers,0,sizeof(handlers));

    setintr(SOFTVEC_TYPE_SYSCALL,syscall_intr);
    setintr(SOFTVEC_TYPE_SOFTERR,softerr_intr);

    current = (kz_thread *)thread_run(func,name,priority,stacksize,argc,argv);

    dispatch(&current->context);

}

void kz_sysdown(void){
    puts("ststem error!\n");
    while(1)
        ;
}

void kz_syscall(kz_syscall_type_t type,kz_syscall_param_t *param){
    current->syscall.type = type;
    current->syscall.param = param;
    asm volatile ("trapa #0");
}


