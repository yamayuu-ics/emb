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

/* メッセージ・バッファ */
typedef struct _kz_msgbuf {
  struct _kz_msgbuf *next;
  kz_thread *sender; /* メッセージを送信したスレッド */
  struct { /* メッセージのパラメータ保存領域 */
    int size;
    char *p;
  } param;
} kz_msgbuf;

/* メッセージ・ボックス */
typedef struct _kz_msgbox {
  kz_thread *receiver; /* 受信待ち状態のスレッド */
  kz_msgbuf *head;
  kz_msgbuf *tail;

  /*
   * H8は16ビットCPUなので，32ビット整数に対しての乗算命令が無い．よって
   * 構造体のサイズが２の累乗になっていないと，構造体の配列のインデックス
   * 計算で乗算が使われて「___mulsi3が無い」などのリンク・エラーになる場合が
   * ある．(２の累乗ならばシフト演算が利用されるので問題は出ない)
   * 対策として，サイズが２の累乗になるようにダミー・メンバで調整する．
   * 他構造体で同様のエラーが出た場合には，同様の対処をすること．
   */
  long dummy[1];
} kz_msgbox;


/* スレッドのレディー・キュー */
static struct
{
    kz_thread *head;
    kz_thread *tail;
} readyque[PRIORITY_NUM];

static kz_thread *current; /* カレント・スレッド */
static kz_thread threads[THREAD_NUM]; /* タスク・コントロール・ブロック */
static kz_handler_t handlers[SOFTVEC_TYPE_NUM]; /* 割込みハンドラ */
static kz_msgbox msgboxes[MSGBOX_ID_NUM]; /* メッセージ・ボックス */

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

/* メッセージの送信処理 */
static void sendmsg(kz_msgbox *mboxp, kz_thread *thp, int size, char *p)
{
  kz_msgbuf *mp;

  /* メッセージ・バッファの作成 */
  mp = (kz_msgbuf *)kzmem_alloc(sizeof(*mp));
  if (mp == NULL)
    kz_sysdown();
  mp->next       = NULL;
  mp->sender     = thp;
  mp->param.size = size;
  mp->param.p    = p;

  /* メッセージ・ボックスの末尾にメッセージを接続する */
  if (mboxp->tail) {
    mboxp->tail->next = mp;
  } else {
    mboxp->head = mp;
  }
  mboxp->tail = mp;
}

/* メッセージの受信処理 */
static void recvmsg(kz_msgbox *mboxp)
{
  kz_msgbuf *mp;
  kz_syscall_param_t *p;

  /* メッセージ・ボックスの先頭にあるメッセージを抜き出す */
  mp = mboxp->head;
  mboxp->head = mp->next;
  if (mboxp->head == NULL)
    mboxp->tail = NULL;
  mp->next = NULL;

  /* メッセージを受信するスレッドに返す値を設定する */
  p = mboxp->receiver->syscall.param;
  p->un.recv.ret = (kz_thread_id_t)mp->sender;
  if (p->un.recv.sizep)
    *(p->un.recv.sizep) = mp->param.size;
  if (p->un.recv.pp)
    *(p->un.recv.pp) = mp->param.p;

  /* 受信待ちスレッドはいなくなったので，NULLに戻す */
  mboxp->receiver = NULL;

  /* メッセージ・バッファの解放 */
  kzmem_free(mp);
}

/* システム・コールの処理(kz_send():メッセージ送信) */
static int thread_send(kz_msgbox_id_t id, int size, char *p)
{
  kz_msgbox *mboxp = &msgboxes[id];

  putcurrent();
  sendmsg(mboxp, current, size, p); /* メッセージの送信処理 */

  /* 受信待ちスレッドが存在している場合には受信処理を行う */
  if (mboxp->receiver) {
    current = mboxp->receiver; /* 受信待ちスレッド */
    recvmsg(mboxp); /* メッセージの受信処理 */
    putcurrent(); /* 受信により動作可能になったので，ブロック解除する */
  }

  return size;
}

/* システム・コールの処理(kz_recv():メッセージ受信) */
static kz_thread_id_t thread_recv(kz_msgbox_id_t id, int *sizep, char **pp)
{
  kz_msgbox *mboxp = &msgboxes[id];

  if (mboxp->receiver) /* 他のスレッドがすでに受信待ちしている */
    kz_sysdown();

  mboxp->receiver = current; /* 受信待ちスレッドに設定 */

  if (mboxp->head == NULL) {
    /*
     * メッセージ・ボックスにメッセージが無いので，スレッドを
     * スリープさせる．(システム・コールがブロックする)
     */
    return -1;
  }

  recvmsg(mboxp); /* メッセージの受信処理 */
  putcurrent(); /* メッセージを受信できたので，レディー状態にする */

  return current->syscall.param->un.recv.ret;
}

static int thread_setintr(softvec_type_t type, kz_handler_t handler){
    static void thread_intr(softvec_type_t type,unsigned long sp);

    softvec_setintr(type,thread_intr);
    handlers[type] = handler;
    putcurrent();
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
        case KZ_SYSCALL_TYPE_SEND: /* kz_send() */
            p->un.send.ret = thread_send(p->un.send.id,
                                         p->un.send.size, p->un.send.p);
            break;
        case KZ_SYSCALL_TYPE_RECV: /* kz_recv() */
            p->un.recv.ret = thread_recv(p->un.recv.id,
                                         p->un.recv.sizep, p->un.recv.pp);
            break;
        case KZ_SYSCALL_TYPE_SETINTR: /* kz_setintr() */
            p->un.setintr.ret = thread_setintr(p->un.setintr.type,
                                               p->un.setintr.handler);
            break;
        default:
            break;
        }
}

static void syscall_proc(kz_syscall_type_t type,kz_syscall_param_t *p){
    getcurrent();
    call_functions(type,p);
}

/* サービス・コールの処理 */
static void srvcall_proc(kz_syscall_type_t type, kz_syscall_param_t *p)
{
  /*
   * システム・コールとサービス・コールの処理関数の内部で，
   * システム・コールの実行したスレッドIDを得るために current を
   * 参照している部分があり(たとえば thread_send() など)，
   * current が残っていると誤動作するため NULL に設定する．
   * サービス・コールは thread_intrvec() 内部の割込みハンドラ呼び出しの
   * 延長で呼ばれているはずでなので，呼び出し後に thread_intrvec() で
   * スケジューリング処理が行われ，current は再設定される．
   */
  current = NULL;
  call_functions(type, p);
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
    memset(msgboxes, 0, sizeof(msgboxes));

    thread_setintr(SOFTVEC_TYPE_SYSCALL, syscall_intr); /* システム・コール */
    thread_setintr(SOFTVEC_TYPE_SOFTERR, softerr_intr); /* ダウン要因発生 */

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

/* サービス・コール呼び出し用ライブラリ関数 */
void kz_srvcall(kz_syscall_type_t type, kz_syscall_param_t *param)
{
  srvcall_proc(type, param);
}
