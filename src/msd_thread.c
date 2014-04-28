/**
 *  __  __  ___  ____ ____    _    ____  
 * |  \/  |/ _ \/ ___/ ___|  / \  |  _ \ 
 * | |\/| | | | \___ \___ \ / _ \ | | | |
 * | |  | | |_| |___) |__) / ___ \| |_| |
 * |_|  |_|\___/|____/____/_/   \_\____/ 
 *
 *    Filename :  Msd_thread.c
 * 
 * Description :  Msd_thread, 工作线程相关实现.
 * 
 *     Created :  Apr 20, 2012 
 *     Version :  0.0.1 
 * 
 *      Author :  HQ 
 *     Company :  Qh 
 *
 **/
#include "msd_core.h"

/**
 * 功能:创建线程池
 * 参数:@num: 工作线程个数 
 *      @statc_size: 工作线程栈大小 
 * 描述:
 *      1. 
 * 返回:成功:线程池地址;失败:NULL
 **/
msd_thread_pool_t *msd_thread_pool_create(int worker_num, int stack_size , void* (*worker_task)(void*)) 
{
    msd_thread_pool_t *pool;
    int i;
    assert(worker_num > 0 && stack_size >= 0);

    /* Allocate memory and zero all them. */
    pool = (msd_thread_pool_t *)calloc(1, sizeof(*pool));
    if (!pool) {
        return NULL;
    }

    /* 初始化线程锁 */
    if (MSD_LOCK_INIT(pool->thread_lock) != 0) 
    {
        free(pool);
        return NULL;
    }

    /* 初始化线程worker列表 */
    if(!(pool->thread_worker_array = 
        (msd_thread_worker_t **)calloc(worker_num, sizeof(msd_thread_worker_t))))
    {
        MSD_LOCK_DESTROY(pool->thread_lock);
        free(pool);
        return NULL;
    }
    
    pool->thread_worker_num = worker_num;
    pool->thread_stack_size = stack_size;    
    
    /* 初始化启动woker线程 */
    for (i = 0; i < worker_num; ++i) 
    {
        if(msd_thread_worker_create(pool, worker_task, i) != MSD_OK)
        {
            MSD_ERROR_LOG("Msd_thread_worker_create failed, idx:%d", i);
            return NULL;
        }
    }
    
    /* 初始化启动信号处理线程 */
    //TODO
    return pool;
}

/**
 * 功能:创建工作线程
 * 参数:@pool: 线程池句柄 
 *      @worker: 工作函数
 *      @idx: 线程位置索引号 
 * 描述:
 *      1. 首先创建woker线程的资源，并初始化
 *      2. 将线程句柄放入主线程的容器中的对应位置
 *      3. 启动woker线程，开始干活，函数返回
 * 返回:成功:0; 失败:-x
 **/
int msd_thread_worker_create(msd_thread_pool_t *pool, void* (*worker_task)(void *), int idx)
{
    msd_thread_worker_t *worker  = NULL;
    if(!(worker = calloc(1, sizeof(msd_thread_worker_t))))
    {
        MSD_ERROR_LOG("Thread_worker_creat failed, idx:%d", idx);
        return MSD_ERR;
    }
    
    worker->pool                 = pool;
    worker->idx                  = idx;
    pool->thread_worker_array[idx] = worker; /* 放入对应的位置 */

    /* 创建通信管道
     * master线程将需要处理的fd写入管道，woker线程从管道取出
     **/
    int fds[2];
    if (pipe(fds) != 0)
    {
        free(worker);
        MSD_ERROR_LOG("Thread_worker_creat pipe failed, idx:%d", idx);
        return MSD_ERR;
    }
    worker->notify_read_fd = fds[0];
    worker->notify_write_fd = fds[1];
    
    /*
    worker->ev_base = (struct event_base*) event_init();
    if (!worker->ev_base)
    {
        GKOLOG(FATAL, "Worker event base initialize error");
        return -1;
    }
    */
    
    /* 创建线程 */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, pool->thread_stack_size);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if(pthread_create(&worker->tid, &attr, worker_task, worker) != 0)
    {
        close(worker->notify_read_fd);
        close(worker->notify_write_fd);
        free(worker);
        MSD_ERROR_LOG("Thread_worker_creat failed, idx:%d", idx);
        return MSD_ERR;        
    }
    pthread_attr_destroy(&attr);
    return MSD_OK;    
}

/**
 * 功能: 销毁线程池
 * 参数: @pool: 线程池句柄 
 * 返回:成功:0; 失败:-x
 **/
int msd_thread_pool_destroy(msd_thread_pool_t *pool) 
{
    assert(pool);
    int i;
    MSD_LOCK_LOCK(pool->thread_lock);
    for(i=0; i<pool->thread_worker_num; i++)
    {
        msd_thread_worker_destroy(pool->thread_worker_array[i]);
    }
    MSD_LOCK_UNLOCK(pool->thread_lock);
    MSD_LOCK_DESTROY(pool->thread_lock);
    free(pool->thread_worker_array);
    free(pool);
    return MSD_OK;
}

/**
 * 功能: 销毁worker线程池
 * 参数: @pool: 线程池句柄 
 * 返回:成功:0; 失败:-x
 **/
int msd_thread_worker_destroy(msd_thread_worker_t *worker) 
{
    assert(worker);
    //TODO
    return MSD_OK;
}

/**
 * 功能: 线程睡眠函数
 * 参数: @sec, 睡眠时长，秒数
 * 说明: 
 *       1.利用select变相实现睡眠效果
 **/
void msd_thread_sleep(int s) 
{
    struct timeval timeout;
    timeout.tv_sec  = s;
    timeout.tv_usec = 0;
    select( 0, NULL, NULL, NULL, &timeout);
}

/**
 * 功能: worker线程工作函数
 * 参数: @arg: 线程函数，通常来说，都是线程自身句柄的指针
 * 说明: 
 *       1.  
 * 返回:成功:0; 失败:-x
 **/
void* msd_thread_worker_task(void* arg) 
{
    msd_thread_worker_t *worker = (msd_thread_worker_t *)arg;
    MSD_INFO_LOG("Thread no.%d begin to work", worker->idx);
    
    while(1)
    {
        msd_thread_sleep(1);
        //printf("I am thread %d, My tid:%lu\n", worker->idx, (long unsigned int)worker->tid);
    }
    return NULL;
}

 
#ifdef __MSD_THREAD_TEST_MAIN__

int g_int = 0;
int fd;

//测试线程锁
//如果不加锁，则最终的文件可能行数不够，或者顺序不对
//如果加锁，则最终的文件可能行数或者顺序都完全正确
void* task(void* arg) 
{
    msd_thread_worker_t *worker = (msd_thread_worker_t *)arg;
    
    MSD_LOCK_LOCK(worker->pool->thread_lock);
    msd_thread_sleep(worker->idx); // 睡眠
    g_int++;
    printf("I am thread %d, I get the g_int:%d. My tid:%lu, pool:%lu\n", worker->idx, g_int, (long unsigned int)worker->tid, (long unsigned int)worker->pool);
    write(fd, "hello\n", 6);
    write(fd, "abcde\n", 6);
    MSD_LOCK_UNLOCK(worker->pool->thread_lock);
    
    return NULL;
}

int main(int argc, char *argv[]) 
{
    int i=0;
    fd = open("test.txt", O_WRONLY|O_CREAT, 0777);
    msd_log_init("./","thread.log", 4, 10*1024*1024, 1, 0); 
    //msd_thread_pool_create(1000, 1024*1024, task);
    msd_thread_pool_t *pool = msd_thread_pool_create(10, 1024*1024, task);
    msd_thread_sleep(1);//睡眠一秒，让线程充分启动起来！
    
    /* 注意，如果线程是detache状态，下面的join将失效
     * 但是，detached的线程，不会因为主线程退出而退出！！！
     */
    for(i=0; i<10; i++)
    {
        pthread_join(pool->thread_worker_array[i]->tid, NULL);
        printf("Thread %d(%lu) exit\n", i, pool->thread_worker_array[i]->tid);
    }
    printf("The final val:%d\n", g_int);
    msd_thread_pool_destroy(pool);
    return 0;
}

#endif /* __MSD_THREAD_TEST_MAIN__ */
