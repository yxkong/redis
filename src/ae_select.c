/* Select()-based ae.c module.
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


#include <sys/select.h>
#include <string.h>
typedef struct aeApiState {
    /**
     * @brief 
     * rfds 读集合
     * wfds 写集合
     */
    fd_set rfds, wfds;
    /* We need to have a copy of the fd sets as it's not safe to reuse
     * FD sets after select(). */
    
    /**
     * @brief 从rfds, wfds里 拷贝的fd集合，确保在处理的时候，是安全的
     * 增删改都是操作的rfds和wfds
     */
    fd_set _rfds, _wfds;
} aeApiState;

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zmalloc(sizeof(aeApiState));

    if (!state) return -1;
    //FD_ZERO 清空集合
    FD_ZERO(&state->rfds);
    FD_ZERO(&state->wfds);
    eventLoop->apidata = state;
    return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    /* Just ensure we have enough room in the fd_set type. */
    //FD_SETSIZE 获取setsize
    if (setsize >= FD_SETSIZE) return -1;
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    zfree(eventLoop->apidata);
}
/**
 * @brief 根据事件的掩码将fd放到对应的set里
 * @param eventLoop 主程序
 * @param fd 
 * @param mask 事件类型掩码
 * @return int 
 */
static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    //FD_SET将一个给定的文件描述符加入集合之中
    if (mask & AE_READABLE) FD_SET(fd,&state->rfds);
    if (mask & AE_WRITABLE) FD_SET(fd,&state->wfds);
    return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    //FD_CLR 将一个给定的文件描述符从集合中删除
    if (mask & AE_READABLE) FD_CLR(fd,&state->rfds);
    if (mask & AE_WRITABLE) FD_CLR(fd,&state->wfds);
}

/**
 * @brief 单位时间内获取事件数量
 * 
 * @param eventLoop 
 * @param tvp 在单位时间内
 * @return int 返回待处理的事件数量
 */
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, j, numevents = 0;
    /**
     *  将rfds 和wfds 的fd集合拷贝到_rfds和_wfds
     */
    memcpy(&state->_rfds,&state->rfds,sizeof(fd_set));
    memcpy(&state->_wfds,&state->wfds,sizeof(fd_set));

    /**
     * 调用select函数，
     *     检查&state->_rfds 数组中是否有读事件就绪
     *     检查&state->_wfds 数组是否有写事件就绪
     *  select(int, fd_set * __restrict, fd_set * __restrict,fd_set * __restrict, struct timeval * __restrict)
     * eventLoop->maxfd+1  监听文件描述符数量
     * &state->_rfds  AE_READABLE 事件fd集合
     * &state->_wfds AE_WRITABLE事件fd集合
     * tvp 阻塞等待时长
     * return retval 就绪描述符的数目，超时返回0，出错返回-1
     */
    retval = select(eventLoop->maxfd+1,
                &state->_rfds,&state->_wfds,NULL,tvp);
    if (retval > 0) {
        //遍历所有的事件，需要再次遍历
        for (j = 0; j <= eventLoop->maxfd; j++) {
            int mask = 0;
            aeFileEvent *fe = &eventLoop->events[j];

            if (fe->mask == AE_NONE) continue;
            //FD_ISSET 检查集合中指定的文件描述符是否可以读写
            if (fe->mask & AE_READABLE && FD_ISSET(j,&state->_rfds))
                mask |= AE_READABLE;
            if (fe->mask & AE_WRITABLE && FD_ISSET(j,&state->_wfds))
                mask |= AE_WRITABLE;
            //将监听到的事件放入到触发事件数组中
            eventLoop->fired[numevents].fd = j;
            //标记事件类型
            eventLoop->fired[numevents].mask = mask;
            numevents++;
        }
    }
    return numevents;
}

static char *aeApiName(void) {
    return "select";
}
