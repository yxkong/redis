/*
 * Copyright (c) 2016, Salvatore Sanfilippo <antirez at gmail dot com>
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
#include <unistd.h>

/* Open a child-parent channel used in order to move information about the
 * RDB / AOF saving process from the child to the parent (for instance
 * the amount of copy on write memory used) */
/**
 * 打开一个子-父通道，用于将RDB / AOF保存进程的信息从子进程移动到父进程(例如，所使用的写内存的拷贝量)
 */
void openChildInfoPipe(void) {
    //创建pipe失败
    if (pipe(server.child_info_pipe) == -1) {
        /* On error our two file descriptors should be still set to -1,
         * but we call anyway cloesChildInfoPipe() since can't hurt. */
        closeChildInfoPipe();
    //设置管道成功，将管道设置为非阻塞的
    } else if (anetNonBlock(NULL,server.child_info_pipe[0]) != ANET_OK) {
        closeChildInfoPipe();
    } else {
        memset(&server.child_info_data,0,sizeof(server.child_info_data));
    }
}

/* Close the pipes opened with openChildInfoPipe(). */
/**
 * 关闭子进程管道，并将管道的fd值置为-1
 */
void closeChildInfoPipe(void) {
    if (server.child_info_pipe[0] != -1 ||
        server.child_info_pipe[1] != -1)
    {
        //关闭管道
        close(server.child_info_pipe[0]);
        close(server.child_info_pipe[1]);
        //并将值置为-1
        server.child_info_pipe[0] = -1;
        server.child_info_pipe[1] = -1;
    }
}

/* Send COW data to parent. The child should call this function after populating
 * the corresponding fields it want to sent (according to the process type). */
/**
 * 通过管道和主进程交互，告诉主进程完成了备份了，
 * @param ptype
 */
void sendChildInfo(int ptype) {
    if (server.child_info_pipe[1] == -1) return;
    server.child_info_data.magic = CHILD_INFO_MAGIC;
    server.child_info_data.process_type = ptype;
    ssize_t wlen = sizeof(server.child_info_data);
    if (write(server.child_info_pipe[1],&server.child_info_data,wlen) != wlen) {
        /* Nothing to do on error, this will be detected by the other side. */
    }
}

/* Receive COW data from parent. */
/**
 * 通过管道读取子进程的返回的信息
 */
void receiveChildInfo(void) {
    if (server.child_info_pipe[0] == -1) return;
    ssize_t wlen = sizeof(server.child_info_data);
    if (read(server.child_info_pipe[0],&server.child_info_data,wlen) == wlen &&
        server.child_info_data.magic == CHILD_INFO_MAGIC)
    {
        //rdb的处理
        if (server.child_info_data.process_type == CHILD_INFO_TYPE_RDB) {
            //获取子进程读写数据的大小
            server.stat_rdb_cow_bytes = server.child_info_data.cow_size;
            //aof的处理
        } else if (server.child_info_data.process_type == CHILD_INFO_TYPE_AOF) {
            server.stat_aof_cow_bytes = server.child_info_data.cow_size;
        }
    }
}
