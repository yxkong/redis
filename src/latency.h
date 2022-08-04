/* latency.h -- latency monitor API header file
 * See latency.c for more information.
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2014, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef __LATENCY_H
#define __LATENCY_H

#define LATENCY_TS_LEN 160 /* History length for every monitored event. */

/* Representation of a latency sample: the sampling time and the latency
 * observed in milliseconds. */
/**
 * 延迟样本结构体
 */
struct latencySample {
    //采样时间
    int32_t time; /* We don't use time_t to force 4 bytes usage everywhere. */
    //毫秒级的执行时长
    uint32_t latency; /* Latency in milliseconds. */
};

/* The latency time series for a given event. */

struct latencyTimeSeries {
    //下一个延迟示例的存储位置
    int idx; /* Index of the next sample to store. */
    //当前事件的最大延迟时间
    uint32_t max; /* Max latency observed for this event. */
    //同类型采样事件的数组
    struct latencySample samples[LATENCY_TS_LEN]; /* Latest history. */
};

/* Latency statistics structure. */
struct latencyStats {
    uint32_t all_time_high; /* Absolute max observed since latest reset. */
    uint32_t avg;           /* Average of current samples. */
    uint32_t min;           /* Min of current samples. */
    uint32_t max;           /* Max of current samples. */
    uint32_t mad;           /* Mean absolute deviation. */
    uint32_t samples;       /* Number of non-zero samples. */
    time_t period;          /* Number of seconds since first event and now. */
};

void latencyMonitorInit(void);
void latencyAddSample(char *event, mstime_t latency);
int THPIsEnabled(void);

/* Latency monitoring macros. */

/* Start monitoring an event. We just set the current time. */
/**
 * 延迟耗时开始标记
 */
#define latencyStartMonitor(var) if (server.latency_monitor_threshold) { \
    var = mstime(); \
} else { \
    var = 0; \
}

/* End monitoring an event, compute the difference with the current time
 * to check the amount of time elapsed. */
/**
 * 延迟耗时结束监听，
 *    计算耗时，当前时间-开始时间
 */
#define latencyEndMonitor(var) if (server.latency_monitor_threshold) { \
    var = mstime() - var; \
}

/* Add the sample only if the elapsed time is >= to the configured threshold. */
/**
 * 定义的宏
 *   latency_monitor_threshold 为采样阈值，
 *    只有配置了，并且超过这个阈值的才会采样
 * 
 */
#define latencyAddSampleIfNeeded(event,var) \
    if (server.latency_monitor_threshold && \
        (var) >= server.latency_monitor_threshold) \
          latencyAddSample((event),(var));

/* Remove time from a nested event. */
#define latencyRemoveNestedEvent(event_var,nested_var) \
    event_var += nested_var;

#endif /* __LATENCY_H */
