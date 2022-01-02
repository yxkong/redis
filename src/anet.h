/* anet.c -- Basic TCP socket stuff made a bit less boring
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

#ifndef ANET_H
#define ANET_H

#include <sys/types.h>

#define ANET_OK 0
#define ANET_ERR -1
#define ANET_ERR_LEN 256

/* Flags used with certain functions. */
#define ANET_NONE 0
//只解析ip
#define ANET_IP_ONLY (1<<0)

#if defined(__sun) || defined(_AIX)
#define AF_LOCAL AF_UNIX
#endif

#ifdef _AIX
#undef ip_len
#endif
/**
 * @brief redis将网络的操作都封装到了anet.h里，然后在anet.c中实现了
 */

/**
 * @brief TCP的默认连接
 * @param err 异常缓冲数组，256字节
 * @param addr 地址
 * @param port 端口
 * @return int 
 */
int anetTcpConnect(char *err, char *addr, int port);
/**
 * @brief TCP的非阻塞连接
 * @param err 
 * @param addr 
 * @param port 
 * @return int 
 */
int anetTcpNonBlockConnect(char *err, char *addr, int port);

int anetTcpNonBlockBindConnect(char *err, char *addr, int port, char *source_addr);
int anetTcpNonBlockBestEffortBindConnect(char *err, char *addr, int port, char *source_addr);
/**
 * @brief Unix默认连接方式
 * 
 * @param err 
 * @param path 
 * @return int 
 */
int anetUnixConnect(char *err, char *path);
/**
 * @brief Unix非阻塞连接方式
 * 
 * @param err 
 * @param path 
 * @return int 
 */
int anetUnixNonBlockConnect(char *err, char *path);
/**
 * @brief 从网络读取内容到buffer缓冲区
 * @param fd 网络链接对应的fd
 * @param buf 
 * @param count 
 * @return int 
 */
int anetRead(int fd, char *buf, int count);
/**
 * @brief 解析通用方法
 * 
 * @param err 
 * @param host 
 * @param ipbuf 
 * @param ipbuf_len 
 * @return int 
 */
int anetResolve(char *err, char *host, char *ipbuf, size_t ipbuf_len);
/**
 * @brief 解析IP地址
 * 
 * @param err 
 * @param host 
 * @param ipbuf 
 * @param ipbuf_len 
 * @return int 
 */
int anetResolveIP(char *err, char *host, char *ipbuf, size_t ipbuf_len);
/**
 * @brief 对ipv4创建tcp监听
 * @param err 
 * @param port 
 * @param bindaddr 
 * @param backlog  背压
 * @return int 
 */
int anetTcpServer(char *err, int port, char *bindaddr, int backlog);
/**
 * @brief 对ipv6创建tcp监听
 * 
 * @param err 
 * @param port 
 * @param bindaddr 
 * @param backlog 
 * @return int 
 */
int anetTcp6Server(char *err, int port, char *bindaddr, int backlog);
int anetUnixServer(char *err, char *path, mode_t perm, int backlog);
/**
 * @brief 
 * 
 * @param err 
 * @param serversock 
 * @param ip 
 * @param ip_len 
 * @param port 
 * @return int 
 */
int anetTcpAccept(char *err, int serversock, char *ip, size_t ip_len, int *port);
int anetUnixAccept(char *err, int serversock);
/**
 * @brief 将缓冲区的内容写到回复里
 * 
 * @param fd 
 * @param buf 
 * @param count 
 * @return int 
 */
int anetWrite(int fd, char *buf, int count);
/**
 * @brief 设置非阻塞
 * 
 * @param err 
 * @param fd 
 * @return int 
 */
int anetNonBlock(char *err, int fd);
int anetBlock(char *err, int fd);
int anetEnableTcpNoDelay(char *err, int fd);
int anetDisableTcpNoDelay(char *err, int fd);
int anetTcpKeepAlive(char *err, int fd);
int anetSendTimeout(char *err, int fd, long long ms);
int anetPeerToString(int fd, char *ip, size_t ip_len, int *port);
int anetKeepAlive(char *err, int fd, int interval);
int anetSockName(int fd, char *ip, size_t ip_len, int *port);
int anetFormatAddr(char *fmt, size_t fmt_len, char *ip, int port);
int anetFormatPeer(int fd, char *fmt, size_t fmt_len);
int anetFormatSock(int fd, char *fmt, size_t fmt_len);

#endif
