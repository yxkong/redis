/* Background I/O service for Redis.
 *
 * This file implements operations that we need to perform in the background.
 * Currently there is only a single operation, that is a background close(2)
 * system call. This is needed as when the process is the last owner of a
 * reference to a file closing it means unlinking it, and the deletion of the
 * file is slow, blocking the server.
 *
 * In the future we'll either continue implementing new things we need or
 * we'll switch to libeio. However there are probably long term uses for this
 * file as we may want to put here Redis specific background tasks (for instance
 * it is not impossible that we'll need a non blocking FLUSHDB/FLUSHALL
 * implementation).
 *
 * DESIGN
 * ------
 *
 * The design is trivial, we have a structure representing a job to perform
 * and a different thread and job queue for every job type.
 * Every thread waits for new jobs in its queue, and process every job
 * sequentially.
 *
 * Jobs of the same type are guaranteed to be processed from the least
 * recently inserted to the most recently inserted (older jobs processed
 * first).
 *
 * Currently there is no way for the creator of the job to be notified about
 * the completion of the operation, this will only be added when/if needed.
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include "server.h"
#include "bio.h"

/**
 * @brief 后端线程组
 * 
 */
static pthread_t bio_threads[BIO_NUM_OPS];
/**
 * @brief 互斥锁组
 * 
 */
static pthread_mutex_t bio_mutex[BIO_NUM_OPS];
/**
 * @brief 新任务条件变量
 * 
 */
static pthread_cond_t bio_newjob_cond[BIO_NUM_OPS];
static pthread_cond_t bio_step_cond[BIO_NUM_OPS];
/**
 * @brief 后台线程组处理的对应任务列表
 * 
 */
static list *bio_jobs[BIO_NUM_OPS];
/* The following array is used to hold the number of pending jobs for every
 * OP type. This allows us to export the bioPendingJobsOfType() API that is
 * useful when the main thread wants to perform some operation that may involve
 * objects shared with the background thread. The main thread will just wait
 * that there are no longer jobs of this type to be executed before performing
 * the sensible operation. This data is also useful for reporting. */

/**
 * @brief 对应线程待处理的任务数
 * 
 */
static unsigned long long bio_pending[BIO_NUM_OPS];

/* This structure represents a background Job. It is only used locally to this
 * file as the API does not expose the internals at all. */
struct bio_job {
    time_t time; /* Time at which the job was created. */
    /* Job specific arguments pointers. If we need to pass more than three
     * arguments we can just pass a pointer to a structure or alike. */
    void *arg1, *arg2, *arg3;
};

void *bioProcessBackgroundJobs(void *arg);
void lazyfreeFreeObjectFromBioThread(robj *o);
void lazyfreeFreeDatabaseFromBioThread(dict *ht1, dict *ht2);
void lazyfreeFreeSlotsMapFromBioThread(zskiplist *sl);

/* Make sure we have enough stack to perform all the things we do in the
 * main thread. */
#define REDIS_THREAD_STACK_SIZE (1024*1024*4)

/* Initialize the background system, spawning the thread. */

/**
 * @brief 初始化后台线程
 * 
 */
void bioInit(void) {
    //创建线程的属性
    pthread_attr_t attr;
    pthread_t thread;
    size_t stacksize;
    int j;

    /* Initialization of state vars and objects */

    /**
     * @brief 根据后台线程的数量创建线程
     * 
     */
    for (j = 0; j < BIO_NUM_OPS; j++) {
        //初始化一个NUll的&bio_mutex[j]互斥锁
        pthread_mutex_init(&bio_mutex[j],NULL);
        //初始化新任务&bio_newjob_cond[j]的条件变量
        pthread_cond_init(&bio_newjob_cond[j],NULL);
        //初始化下一个执行任务bio_step_cond[j]的条件变量
        pthread_cond_init(&bio_step_cond[j],NULL);
        //为对应的线程创建创建一个list
        bio_jobs[j] = listCreate();
        //待处理任务个数
        bio_pending[j] = 0;
    }

    /* Set the stack size as by default it may be small in some system */
    //初始化线程属性
    pthread_attr_init(&attr);
    //获取线程栈大小
    pthread_attr_getstacksize(&attr,&stacksize);
    if (!stacksize) stacksize = 1; /* The world is full of Solaris Fixes */
    //如果栈大小小于4mb，一直翻倍，知道大于4mb
    while (stacksize < REDIS_THREAD_STACK_SIZE) stacksize *= 2;
    //设置线程栈大小（默认是当前用户的操作系统限制）
    pthread_attr_setstacksize(&attr, stacksize);

    /* Ready to spawn our threads. We use the single argument the thread
     * function accepts in order to pass the job ID the thread is
     * responsible of. */
    
    /**
     * @brief 创建后端线程，并放入线程组
     */
    for (j = 0; j < BIO_NUM_OPS; j++) {
        //线程号
        void *arg = (void*)(unsigned long) j;
        //创建线程，并启动bioProcessBackgroundJobs
        if (pthread_create(&thread,&attr,bioProcessBackgroundJobs,arg) != 0) {
            serverLog(LL_WARNING,"Fatal: Can't initialize Background Jobs.");
            exit(1);
        }
        bio_threads[j] = thread;
    }
}

/**
 * @brief linux 线程操作
 * pthread_create 创建线程
 * 
 * 
 * ## 互斥锁操作
 * pthread_mutex_init  创建互斥锁
 * pthread_cond_init  只有等待这个条件发生线程才继续执行
 * pthread_mutex_lock  获取互斥锁
 * phtread_mutex_unlock 解除互斥锁
 * pthread_cond_wait  阻塞在条件变量上
 * pthread_cond_signal 解除条件变量上的阻塞
 * pthread_cond_broadcast 唤醒所有被阻塞的线程
 */

/**
 * @brief 创建后台任务，在流程中生成的任务都会扔到这里，然后放入对应的队列中
 * 
 * @param type 任务类型
 * @param arg1 
 * @param arg2 
 * @param arg3 
 */
void bioCreateBackgroundJob(int type, void *arg1, void *arg2, void *arg3) {
    struct bio_job *job = zmalloc(sizeof(*job));

    job->time = time(NULL);
    job->arg1 = arg1;
    job->arg2 = arg2;
    job->arg3 = arg3;
    //获取互斥锁，确保一次只能执行一个
    pthread_mutex_lock(&bio_mutex[type]);
    //添加到对应类型的后台队列里
    listAddNodeTail(bio_jobs[type],job);
    //待处理任务数+1
    bio_pending[type]++;
    //解除bio_newjob_cond[type]位置的阻塞状态（添加完任务就解除，这样bioProcessBackgroundJobs才能执行）
    pthread_cond_signal(&bio_newjob_cond[type]);
    // 释放锁
    pthread_mutex_unlock(&bio_mutex[type]);
}

/**
 * @brief 后台任务处理
 * 
 * @param arg 线程号索引
 * @return void* 
 */
void *bioProcessBackgroundJobs(void *arg) {
    struct bio_job *job;
    unsigned long type = (unsigned long) arg;
    sigset_t sigset;

    /* Check that the type is within the right interval. */
    if (type >= BIO_NUM_OPS) {
        serverLog(LL_WARNING,
            "Warning: bio thread started with wrong type %lu",type);
        return NULL;
    }

    /* Make the thread killable at any time, so that bioKillThreads()
     * can work reliably. */

    /**
     * @brief  以下
     * PTHREAD_CANCEL_ENABLE PTHREAD_CANCEL_ASYNCHRONOUS 表示接收到取消信号后，立即取消（退出）
     * PTHREAD_CANCEL_ENABLE PTHREAD_CANCEL_DEFFERED  表示接收到取消信号后继续运行，直到下一个取消点再退出
     *
     * 设置本线程对cancel信号的反应
     * 
     */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    //设置本线程取消动作的执行时机 PTHREAD_CANCEL_ASYNCHRONOUS 和PTHREAD_CANCEL_DEFFERED
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    //对应的线程以进来就上锁
    pthread_mutex_lock(&bio_mutex[type]);
    /* Block SIGALRM so we are sure that only the main thread will
     * receive the watchdog signal. */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    if (pthread_sigmask(SIG_BLOCK, &sigset, NULL))
        serverLog(LL_WARNING,
            "Warning: can't mask SIGALRM in bio.c thread: %s", strerror(errno));

    //循环处理对应的任务
    while(1) {
        listNode *ln;

        /* The loop always starts with the lock hold. */
        
        /**
         * @brief 如果任务列表已经空了，那么就通过pthread_cond_wait 等待bio_newjob_cond的信号，以及互斥锁的释放
         */
        if (listLength(bio_jobs[type]) == 0) {
            //阻塞等待，等到新任务了，继续往下走
            pthread_cond_wait(&bio_newjob_cond[type],&bio_mutex[type]);
            continue;
        }
        /* Pop the job from the queue. */
        ln = listFirst(bio_jobs[type]);
        job = ln->value;
        /* It is now possible to unlock the background system as we know have
         * a stand alone job structure to process.*/
        //有新任务了，解锁执行
        pthread_mutex_unlock(&bio_mutex[type]);

        /* Process the job accordingly to its type. */
        //根据线程索引，处理不同的任务
        if (type == BIO_CLOSE_FILE) {
            //关闭任务
            close((long)job->arg1);
        } else if (type == BIO_AOF_FSYNC) {
            // 刷盘
            redis_fsync((long)job->arg1);
        } else if (type == BIO_LAZY_FREE) {
            /* What we free changes depending on what arguments are set:
             * arg1 -> free the object at pointer.
             * arg2 & arg3 -> free two dictionaries (a Redis DB).
             * only arg3 -> free the skiplist. */
            if (job->arg1)
                //将val和robj解绑，并释放
                lazyfreeFreeObjectFromBioThread(job->arg1);
            else if (job->arg2 && job->arg3)
                //回收db
                lazyfreeFreeDatabaseFromBioThread(job->arg2,job->arg3);
            else if (job->arg3)
                //同步从库删除
                lazyfreeFreeSlotsMapFromBioThread(job->arg3);
        } else {
            serverPanic("Wrong job type in bioProcessBackgroundJobs().");
        }
        zfree(job);

        /* Lock again before reiterating the loop, if there are no longer
         * jobs to process we'll block again in pthread_cond_wait(). */
        //处理完一个就加锁，如果没有任务处理就阻塞在pthread_cond_wait
        pthread_mutex_lock(&bio_mutex[type]);
        listDelNode(bio_jobs[type],ln);
        bio_pending[type]--;

        /* Unblock threads blocked on bioWaitStepOfType() if any. */
        //唤醒&bio_step_cond[type]的线程（这块代码没什么意义，循环里只处理了bio_newjob_cond）
        pthread_cond_broadcast(&bio_step_cond[type]);
    }
}

/* Return the number of pending jobs of the specified type. */
/**
 * @brief aof和内存不够时触发
 * @param type 任务类型
 * @return unsigned long long 
 */
unsigned long long bioPendingJobsOfType(int type) {
    unsigned long long val;
    pthread_mutex_lock(&bio_mutex[type]);
    val = bio_pending[type];
    pthread_mutex_unlock(&bio_mutex[type]);
    return val;
}

/* If there are pending jobs for the specified type, the function blocks
 * and waits that the next job was processed. Otherwise the function
 * does not block and returns ASAP.
 *
 * The function returns the number of jobs still to process of the
 * requested type.
 *
 * This function is useful when from another thread, we want to wait
 * a bio.c thread to do more work in a blocking way.
 */
//未找到调用
unsigned long long bioWaitStepOfType(int type) {
    unsigned long long val;
    pthread_mutex_lock(&bio_mutex[type]);
    val = bio_pending[type];
    if (val != 0) {
        pthread_cond_wait(&bio_step_cond[type],&bio_mutex[type]);
        val = bio_pending[type];
    }
    pthread_mutex_unlock(&bio_mutex[type]);
    return val;
}

/* Kill the running bio threads in an unclean way. This function should be
 * used only when it's critical to stop the threads for some reason.
 * Currently Redis does this only on crash (for instance on SIGSEGV) in order
 * to perform a fast memory check without other threads messing with memory. */
void bioKillThreads(void) {
    int err, j;

    for (j = 0; j < BIO_NUM_OPS; j++) {
        if (pthread_cancel(bio_threads[j]) == 0) {
            if ((err = pthread_join(bio_threads[j],NULL)) != 0) {
                serverLog(LL_WARNING,
                    "Bio thread for job type #%d can be joined: %s",
                        j, strerror(err));
            } else {
                serverLog(LL_WARNING,
                    "Bio thread for job type #%d terminated",j);
            }
        }
    }
}
