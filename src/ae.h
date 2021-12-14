/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef __AE_H__
#define __AE_H__

#include <time.h>

#define AE_OK 0
#define AE_ERR -1

#define AE_NONE 0       /* No events registered. */
#define AE_READABLE 1   /* Fire when descriptor is readable. */
#define AE_WRITABLE 2   /* Fire when descriptor is writable. */
#define AE_BARRIER 4    /* With WRITABLE, never fire the event if the
                           READABLE event already fired in the same event
                           loop iteration. Useful when you want to persist
                           things to disk before sending replies, and want
                           to do that in a group fashion. */

#define AE_FILE_EVENTS 1
#define AE_TIME_EVENTS 2
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)
#define AE_DONT_WAIT 4
#define AE_CALL_AFTER_SLEEP 8

#define AE_NOMORE -1
#define AE_DELETED_EVENT_ID -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

struct aeEventLoop;

/* Types and data structures */
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

/* File event structure */
typedef struct aeFileEvent {
    int mask; /* one of AE_(READABLE|WRITABLE|BARRIER) */
    aeFileProc *rfileProc;
    aeFileProc *wfileProc;
    void *clientData;
} aeFileEvent;

/* Time event structure */
typedef struct aeTimeEvent {
    long long id; /* time event identifier. */
    long when_sec; /* seconds */
    long when_ms; /* milliseconds */
    //会把执行任扔这里
    aeTimeProc *timeProc;
    aeEventFinalizerProc *finalizerProc;
    void *clientData;
    struct aeTimeEvent *prev;
    struct aeTimeEvent *next;
} aeTimeEvent;

/* A fired event */
/**
 * @brief 触发的事件
 * 
 */
typedef struct aeFiredEvent {
    /* 发生事件的套接字，目前只有inet和unix */
    int fd; 
    /* fd上发生的事件类型 */
    int mask;
} aeFiredEvent;

/* State of an event based program */
/**
 * @brief 事件管理器，整个进程只有一个
 */
typedef struct aeEventLoop {
    //最大的tcp socket的fd
    int maxfd;   /* highest file descriptor currently registered */
    //最多持有这么多连接（最大链接+128），events和fired 数组的大小
    int setsize; /* max number of file descriptors tracked */
     //记录最大的定时事件id（放几个为几），存放定时事件会自增
    long long timeEventNextId;
    time_t lastTime;     /* Used to detect system clock skew */
    //已注册的文件事件处理器，在initServer里，一个fd绑定一个
    aeFileEvent *events; /* Registered events */
    //触发的的事件（在ae中会把所有从epoll里拉取到的事件丢到这里）
    aeFiredEvent *fired; /* Fired events */
    //定时事件链表的头节点
    aeTimeEvent *timeEventHead;
    //事件循环结束标识
    int stop;
    //epoll的数据，
    void *apidata; /* This is used for polling API specific data */
    //aeProcessEvents处理前执行（每循环一次执行一次）
    aeBeforeSleepProc *beforesleep;
    //aeApiPoll 后执行
    aeBeforeSleepProc *aftersleep;
} aeEventLoop;

/* Prototypes */
/**
 * @brief 创建事件监听器
 * 
 * @param setsize 比配置的最大链接数要多128，为了安全处理（比如有的处理完了，还没有释放，多创建的就相当于缓冲队列了）
 * @return aeEventLoop* 
 */
aeEventLoop *aeCreateEventLoop(int setsize);
/**
 * @brief 删除aeEventLoop
 * 
 * @param eventLoop 
 */
void aeDeleteEventLoop(aeEventLoop *eventLoop);
void aeStop(aeEventLoop *eventLoop);
/**
 * @brief 创建文件事件监听器并放入到eventLoop->events中
 *   注册acceptTcpHandler处理 AE_READABLE
 *   注册readQueryFromClient 处理AE_READABLE
 * @param eventLoop 
 * @param fd 
 *    当是acceptTcpHandler时，对应的监听的tcp socket的fd值
 *    当时readQueryFromClient时
 * @param mask 
 * @param proc 处理器acceptTcpHandler
 * @param clientData 
 * @return int 
 */
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData);
/**
 * @brief 删除aeEventLoop中指定的fd
 * 
 * @param eventLoop 
 * @param fd 
 * @param mask 
 */
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask);
int aeGetFileEvents(aeEventLoop *eventLoop, int fd);
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc);
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);
int aeProcessEvents(aeEventLoop *eventLoop, int flags);
int aeWait(int fd, int mask, long long milliseconds);
void aeMain(aeEventLoop *eventLoop);
char *aeGetApiName(void);
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep);
void aeSetAfterSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *aftersleep);
int aeGetSetSize(aeEventLoop *eventLoop);
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);

#endif
