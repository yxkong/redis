/* Maxmemory directive handling (LRU eviction and other policies).
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2016, Salvatore Sanfilippo <antirez at gmail dot com>
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
#include "atomicvar.h"

/**
 * @brief 
 * LFU算法的思路是： 我们记录下来每个（被缓存的/出现过的）数据的请求频次信息，如果一个请求的请求次数多，那么它就很可能接着被请求。

在数据请求模式比较稳定（没有对于某个数据突发的高频访问这样的不稳定模式）的情况下，LFU的表现还是很不错的。在数据的请求模式大多不稳定的情况下，LFU一般会有这样一些问题：

1）微博热点数据一般只是几天内有较高的访问频次，过了这段时间就没那么大意义去缓存了。但是因为在热点期间他的频次被刷上去了，之后很长一段时间内很难被淘汰；
2）如果采用只记录缓存中的数据的访问信息，新加入的高频访问数据在刚加入的时候由于没有累积优势，很容易被淘汰掉；
3）如果记录全部出现过的数据的访问信息，会占用更多的内存空间。

对于上面这些问题，其实也都有一些对应的解决方式，相应的出现了很多LFU的变种。如：Window-LFU、LFU*、LFU-Aging。在Redis的LFU算法实现中，也有其解决方案。

近似计数算法 – Morris算法
Redis记录访问次数使用了一种近似计数算法——Morris算法。Morris算法利用随机算法来增加计数，在Morris算法中，计数不是真实的计数，它代表的是实际计数的量级。

算法的思想是这样的：算法在需要增加计数的时候通过随机数的方式计算一个值来判断是否要增加，算法控制 在计数越大的时候，得到结果“是”的几率越小。
 * 
 */

/* ----------------------------------------------------------------------------
 * Data structures
 * --------------------------------------------------------------------------*/

/* To improve the quality of the LRU approximation we take a set of keys
 * that are good candidate for eviction across freeMemoryIfNeeded() calls.
 *
 * Entries inside the eviciton pool are taken ordered by idle time, putting
 * greater idle times to the right (ascending order).
 *
 * When an LFU policy is used instead, a reverse frequency indication is used
 * instead of the idle time, so that we still evict by larger value (larger
 * inverse frequency means to evict keys with the least frequent accesses).
 *
 * Empty entries have the key pointer set to NULL. */
#define EVPOOL_SIZE 16
#define EVPOOL_CACHED_SDS_SIZE 255
/**
 * @brief 待淘汰数据候选集合
 */
struct evictionPoolEntry {
    //待淘汰数据的空闲时间
    unsigned long long idle;    /* Object idle time (inverse frequency for LFU) */
    //待淘汰的key
    sds key;                    /* Key name. */
    sds cached;                 /* Cached SDS object for key name. */
    //所在db的索引
    int dbid;                   /* Key DB number. */
};
/**
 * @brief 在本文件内共享，待淘汰数据候选集合
 * 
 */
static struct evictionPoolEntry *EvictionPoolLRU;

/* ----------------------------------------------------------------------------
 * Implementation of eviction, aging and LRU
 * --------------------------------------------------------------------------*/

/* Return the LRU clock, based on the clock resolution. This is a time
 * in a reduced-bits format that can be used to set and check the
 * object->lru field of redisObject structures. */

/**
 * @brief 服务器会把这个值刷入到server.lruclock
 * 
 * @return unsigned int 
 */
unsigned int getLRUClock(void) {
    /**
     * 获取秒  mstime()/LRU_CLOCK_RESOLUTION
     * LRU 存储的最大值 LRU_CLOCK_MAX 
     * & 计算获取时间戳的低24位
     * 194天才会溢出
     */
    return (mstime()/LRU_CLOCK_RESOLUTION) & LRU_CLOCK_MAX;
}

/* This function is used to obtain the current LRU clock.
 * If the current resolution is lower than the frequency we refresh the
 * LRU clock (as it should be in production servers) we return the
 * precomputed value, otherwise we need to resort to a system call. */
unsigned int LRU_CLOCK(void) {
    unsigned int lruclock;
    /**
     * 
     */
    if (1000/server.hz <= LRU_CLOCK_RESOLUTION) {
        atomicGet(server.lruclock,lruclock);
    } else {
        lruclock = getLRUClock();
    }
    return lruclock;
}

/* Given an object returns the min number of milliseconds the object was never
 * requested, using an approximated LRU algorithm. */

/**
 * @brief 返回最后一次请求到现在的时间差
 * 
 * @param o 
 * @return unsigned long long 
 */
unsigned long long estimateObjectIdleTime(robj *o) {
    //获取当前的lru时钟
    unsigned long long lruclock = LRU_CLOCK();
    //计算差值
    if (lruclock >= o->lru) {
        return (lruclock - o->lru) * LRU_CLOCK_RESOLUTION;
    } else {
        //这个是又重新进入了一个新的周期(如果跨越了多个周期呢？没什么意义，一个周期200天了)
        return (lruclock + (LRU_CLOCK_MAX - o->lru)) *
                    LRU_CLOCK_RESOLUTION;
    }
}

/* freeMemoryIfNeeded() gets called when 'maxmemory' is set on the config
 * file to limit the max memory used by the server, before processing a
 * command.
 *
 * The goal of the function is to free enough memory to keep Redis under the
 * configured memory limit.
 *
 * The function starts calculating how many bytes should be freed to keep
 * Redis under the limit, and enters a loop selecting the best keys to
 * evict accordingly to the configured policy.
 *
 * If all the bytes needed to return back under the limit were freed the
 * function returns C_OK, otherwise C_ERR is returned, and the caller
 * should block the execution of commands that will result in more memory
 * used by the server.
 *
 * ------------------------------------------------------------------------
 *
 * LRU approximation algorithm
 *
 * Redis uses an approximation of the LRU algorithm that runs in constant
 * memory. Every time there is a key to expire, we sample N keys (with
 * N very small, usually in around 5) to populate a pool of best keys to
 * evict of M keys (the pool size is defined by EVPOOL_SIZE).
 *
 * The N keys sampled are added in the pool of good keys to expire (the one
 * with an old access time) if they are better than one of the current keys
 * in the pool.
 *
 * After the pool is populated, the best key we have in the pool is expired.
 * However note that we don't remove keys from the pool when they are deleted
 * so the pool may contain keys that no longer exist.
 *
 * When we try to evict a key, and all the entries in the pool don't exist
 * we populate it again. This time we'll be sure that the pool has at least
 * one key that can be evicted, if there is at least one key that can be
 * evicted in the whole database. */

/* Create a new eviction pool. */
/**
 * @brief 创建待淘汰候选集合
 * 
 */
void evictionPoolAlloc(void) {
    struct evictionPoolEntry *ep;
    int j;
    //创建一个16个坑位的数组
    ep = zmalloc(sizeof(*ep)*EVPOOL_SIZE);
    for (j = 0; j < EVPOOL_SIZE; j++) {
        ep[j].idle = 0;
        ep[j].key = NULL;
        ep[j].cached = sdsnewlen(NULL,EVPOOL_CACHED_SDS_SIZE);
        ep[j].dbid = 0;
    }
    //数组给EvictionPoolLRU
    EvictionPoolLRU = ep;
}

/* This is an helper function for freeMemoryIfNeeded(), it is used in order
 * to populate the evictionPool with a few entries every time we want to
 * expire a key. Keys with idle time smaller than one of the current
 * keys are added. Keys are always added if there are free entries.
 *
 * We insert keys on place in ascending order, so keys with the smaller
 * idle time are on the left, and keys with the higher idle time on the
 * right. */

/**
 * @brief 将待淘汰的数据填充到pool里，最终从小到大排序
 * @param dbid 数据的id
 * @param sampledict 采样来源，可能是db->dict也可能是 db->expires
 * @param keydict 全局hash表  db->dict
 * @param pool 待淘汰的候选键集合
 */
void evictionPoolPopulate(int dbid, dict *sampledict, dict *keydict, struct evictionPoolEntry *pool) {
    int j, k, count;
    //定义dictEntry的指针数组，默认5
    dictEntry *samples[server.maxmemory_samples];
    //随机采样,
    count = dictGetSomeKeys(sampledict,samples,server.maxmemory_samples);
    for (j = 0; j < count; j++) {
        unsigned long long idle;
        sds key;
        robj *o;
        dictEntry *de;
        de = samples[j];
        key = dictGetKey(de);

        /* If the dictionary we are sampling from is not the main
         * dictionary (but the expires one) we need to lookup the key
         * again in the key dictionary to obtain the value object. */
        if (server.maxmemory_policy != MAXMEMORY_VOLATILE_TTL) {
            //这里是从全局hash表里取具体的entry，需要计算LRU或LFU
            if (sampledict != keydict) de = dictFind(keydict, key);
            o = dictGetVal(de);
        }

        /* Calculate the idle time according to the policy. This is called
         * idle just because the code initially handled LRU, but is in fact
         * just a score where an higher score means better candidate. */
        //LRU是值越大，越容易被清理
        if (server.maxmemory_policy & MAXMEMORY_FLAG_LRU) {
            //返回最后一次请求到现在的时间差
            idle = estimateObjectIdleTime(o);
        } else if (server.maxmemory_policy & MAXMEMORY_FLAG_LFU) {
            /* When we use an LRU policy, we sort the keys by idle time
             * so that we expire keys starting from greater idle time.
             * However when the policy is an LFU one, we have a frequency
             * estimation, and we want to evict keys with lower frequency
             * first. So inside the pool we put objects using the inverted
             * frequency subtracting the actual frequency to the maximum
             * frequency of 255. */
            /**
             * @brief LFUDecrAndReturn 返回的是LFU的counter，时间越长不访问，counter越小,最小为0
             * LFU访问频次越高counter越大（虽然会衰减），用最大值255-counter越大，说明访问的频率越低
             */
            idle = 255-LFUDecrAndReturn(o);
        } else if (server.maxmemory_policy == MAXMEMORY_VOLATILE_TTL) {
            /* In this case the sooner the expire the better. */
            //如果是TTL，一定是从db->expires采样的，de里面存储的是过期时间
            idle = ULLONG_MAX - (long)dictGetVal(de);
        } else {
            serverPanic("Unknown eviction policy in evictionPoolPopulate()");
        }

        /* Insert the element inside the pool.
         * First, find the first empty bucket or the first populated
         * bucket that has an idle time smaller than our idle time. */
        k = 0;
        /**
         * idle越大越容易被清理，当pool中的idle小于采样的数据时，会进行替换
         *  第一个进来6 0的位置没值，触发逻辑2，直接0的位置存放 idle=6对应的key
         *  然后idle 5，2 和8，7，3 过来
         *  过来5 话 k=0 这个时候0的位置上有key了，触发逻辑3，整体会把0位置向右移动，然后插入5  最后是：5，6
         *  2 的话k=0，同5的步骤，触发逻辑3  最后是：2，5，6
         *  8 的话 k=3,且k=3的位置没有值，触发逻辑2，直接放入3的位置， 最后是：2，5，6，8
         *  7 的话 k=3,k=3的位置已有值，触发逻辑3，整体把3以后的右移，然后插入7，最后是 2，5，6，7，8
         * 假设只有5个
         *  3 过来，k=1,最后一位已满，触发逻辑4，k--最后为0，取出原来0位置的cached，把1位置的拷贝到0位置，然后，2对应的key给cached和key
         *  2 过来，k=0,触发逻辑1然后continue
         */
        while (k < EVPOOL_SIZE &&
               pool[k].key &&
               pool[k].idle < idle) k++;
        //逻辑1，k=0,且pool满了就不再处理,
        if (k == 0 && pool[EVPOOL_SIZE-1].key != NULL) {
            /* Can't insert if the element is < the worst element we have
             * and there are no empty buckets. */
            continue;
        //逻辑2
        } else if (k < EVPOOL_SIZE && pool[k].key == NULL) {
            //正常插入逻辑
            /* Inserting into empty position. No setup needed before insert. */
        } else {
            /* Inserting in the middle. Now k points to the first element
             * greater than the element to insert.  */
            //逻辑3，最后一个为空，，就把k以后的数据都往后挪移，给刚来的数据挪位
            if (pool[EVPOOL_SIZE-1].key == NULL) {
                /* Free space on the right? Insert at k shifting
                 * all the elements from k to end to the right. */

                /* Save SDS before overwriting. */
                sds cached = pool[EVPOOL_SIZE-1].cached;
                /**
                 * 当内存发生局部重叠的时候，memmove保证拷贝的结果是正确的，memcpy不保证拷贝的结果的正确。
                 * 从 pool+k拷贝到 pool+k+1,长度是sizeof(pool[0])*(EVPOOL_SIZE-k-1)
                 */
                memmove(pool+k+1,pool+k,
                    sizeof(pool[0])*(EVPOOL_SIZE-k-1));
                //把最后的cached放到当前
                pool[k].cached = cached;
            } else {//逻辑4
                /* No free space on right? Insert at k-1 */
                k--;
                /* Shift all elements on the left of k (included) to the
                 * left, so we discard the element with smaller idle time. */
                sds cached = pool[0].cached; /* Save SDS before overwriting. */
                if (pool[0].key != pool[0].cached) sdsfree(pool[0].key);
                memmove(pool,pool+1,sizeof(pool[0])*k);
                pool[k].cached = cached;
            }
        }

        /* Try to reuse the cached SDS string allocated in the pool entry,
         * because allocating and deallocating this object is costly
         * (according to the profiler, not my fantasy. Remember:
         * premature optimizbla bla bla bla. */
        int klen = sdslen(key);
        if (klen > EVPOOL_CACHED_SDS_SIZE) {
            //key比较大，cached是最后的或首位的，key和cache可能不一致
            pool[k].key = sdsdup(key);
        } else {
            //直接把key拷贝到k的位置
            memcpy(pool[k].cached,key,klen+1);
            sdssetlen(pool[k].cached,klen);
            //key和cache一致
            pool[k].key = pool[k].cached;
        }
        pool[k].idle = idle;
        pool[k].dbid = dbid;
    }
}

/* ----------------------------------------------------------------------------
 * LFU (Least Frequently Used) implementation.

 * We have 24 total bits of space in each object in order to implement
 * an LFU (Least Frequently Used) eviction policy, since we re-use the
 * LRU field for this purpose.
 *
 * We split the 24 bits into two fields:
 *
 *          16 bits      8 bits
 *     +----------------+--------+
 *     + Last decr time | LOG_C  |
 *     +----------------+--------+
 *
 * LOG_C is a logarithmic counter that provides an indication of the access
 * frequency. However this field must also be decremented otherwise what used
 * to be a frequently accessed key in the past, will remain ranked like that
 * forever, while we want the algorithm to adapt to access pattern changes.
 *
 * So the remaining 16 bits are used in order to store the "decrement time",
 * a reduced-precision Unix time (we take 16 bits of the time converted
 * in minutes since we don't care about wrapping around) where the LOG_C
 * counter is halved if it has an high value, or just decremented if it
 * has a low value.
 *
 * New keys don't start at zero, in order to have the ability to collect
 * some accesses before being trashed away, so they start at COUNTER_INIT_VAL.
 * The logarithmic increment performed on LOG_C takes care of COUNTER_INIT_VAL
 * when incrementing the key, so that keys starting at COUNTER_INIT_VAL
 * (or having a smaller value) have a very high chance of being incremented
 * on access.
 *
 * During decrement, the value of the logarithmic counter is halved if
 * its current value is greater than two times the COUNTER_INIT_VAL, otherwise
 * it is just decremented by one.
 * --------------------------------------------------------------------------*/

/**
 * @brief LFU
 * 1, LFU 使用lru的24位，低8位记录统计次数，高16位记录分钟级的访问时间
 * 2，LFU 给一个初始 LFU_INIT_VAL 防止高频数据刚插入因为counter太小而被淘汰；
 * 3，LFU 使用近似计数算法，counter越大，counter增加的几率反而越小（防止热点数据被刷上去以后淘汰不了）
 * 
 */

/* Return the current time in minutes, just taking the least significant
 * 16 bits. The returned time is suitable to be stored as LDT (last decrement
 * time) for the LFU implementation. */



/**
 * @brief 获取当前时间（分钟级）
 * 
 * @return unsigned long 
 */
unsigned long LFUGetTimeInMinutes(void) {
    /**
     * server.unixtime/60  获取分钟
     * 分钟再&65535，45天溢出
     */
    return (server.unixtime/60) & 65535;
}

/* Given an object last access time, compute the minimum number of minutes
 * that elapsed since the last access. Handle overflow (ldt greater than
 * the current 16 bits minutes time) considering the time as wrapping
 * exactly once. */

/**
 * @brief 获取从上次访问到现在访问的分钟数
 * 
 * @param ldt 
 * @return unsigned long 
 */
unsigned long LFUTimeElapsed(unsigned long ldt) {
    //获取当前时间
    unsigned long now = LFUGetTimeInMinutes();
    //未溢出的情况下
    if (now >= ldt) return now-ldt;
    //溢出的情况下
    return 65535-ldt+now;
}

/* Logarithmically increment a counter. The greater is the current counter value
 * the less likely is that it gets really implemented. Saturate it at 255. */

/**
 * @brief 更新LRU的后8位，也就是LFU的counter
 * LFU 使用近似计数法，counter越大
 * 
 * @param counter 
 * @return uint8_t 
 */
uint8_t LFULogIncr(uint8_t counter) {
    if (counter == 255) return 255;
    /**
     * @brief rand()随机生成一个0~RAND_MAX 的随机数
     * r的范围是0~1
     */
    double r = (double)rand()/RAND_MAX;
    //
    double baseval = counter - LFU_INIT_VAL;
    if (baseval < 0) baseval = 0;
    /**
     * server.lfu_log_factor 默认为10
     * baseval 越大 p的值就越小
     */
    double p = 1.0/(baseval*server.lfu_log_factor+1);
    /**
     * r是随机生成的0~1
     * counter 是以5为起始点
     * baseval=0 时： p的值为1         r的在1以下的概率为100%
     * baseval=1 时： p的值为0.0909    r的在0.09以下的概率只有约9%  10次counter+1
     * baseval=2 时： p的值为0.0476    r的在0.0476以下的概率只有约4.8%  20次counter+1
     * baseval=3 时： p的值为0.0322    r的在0.0322以下的概率只有约3.2%  30次counter+1
     * baseval=4 时： p的值为0.0244    r的在0.0244以下的概率只有约2.4%  40次counter+1
     * baseval=5 时： p的值约0.0196    r的在0.0196 以下的概率只有约2%  50次counter+1
     * baseval=10 时：p的值约0.0099    r的在0.0099 以下的概率只有约1%  100次才可能加1次
     * baseval=100时：p的值约0.000999  r的在0.000999 以下的概率只有约0.1% 1000次才可能加1
     * baseval=200时：p的值约0.0005  r的在0.0005 以下的概率只有约0.05% 2000次才可能加1
     * 想达到100的baseval总次数为（10+20+30+40+...+1000）=49*1000+500 约 5万次
     * 想达到200的baseval总次数为 (10+20+30+40+...+2000) = 99*2000+1000 约20万次
     */
    if (r < p) counter++;
    return counter;
}

/* If the object decrement time is reached decrement the LFU counter but
 * do not update LFU fields of the object, we update the access time
 * and counter in an explicit way when the object is really accessed.
 * And we will times halve the counter according to the times of
 * elapsed time than server.lfu_decay_time.
 * Return the object frequency counter.
 *
 * This function is used in order to scan the dataset for the best object
 * to fit: as we check for the candidate, we incrementally decrement the
 * counter of the scanned objects if needed. */

/**
 * @brief LFU计数衰减，最终衰减到0
 * @param o 
 * @return unsigned long 0~255
 */
unsigned long LFUDecrAndReturn(robj *o) {
    //获取lru中的高16位的值
    unsigned long ldt = o->lru >> 8;
    // 通过&获取lru的counter
    unsigned long counter = o->lru & 255;
    /**
     * 配置衰减时间的情况下
     * server.lfu_decay_time 计数衰减，默认为1
     * num_periods = 上次访问时间/1
     */
    unsigned long num_periods = server.lfu_decay_time ? LFUTimeElapsed(ldt) / server.lfu_decay_time : 0;
    /**
     * 在频繁访问的情况下num_periods=0 （不超过1分钟）不会衰减
     * 超过1分钟没访问就减1，超过n分钟没访问就衰减n，最终衰减到0
     */
    if (num_periods)
        counter = (num_periods > counter) ? 0 : counter - num_periods;
    return counter;
}

/* ----------------------------------------------------------------------------
 * The external API for eviction: freeMemroyIfNeeded() is called by the
 * server when there is data to add in order to make space if needed.
 * --------------------------------------------------------------------------*/

/* We don't want to count AOF buffers and slaves output buffers as
 * used memory: the eviction should use mostly data size. This function
 * returns the sum of AOF and slaves buffer. */

/**
 * @brief 计算主从复制的输出缓冲区大小
 * 
 * @return size_t 
 */
size_t freeMemoryGetNotCountedMemory(void) {
    size_t overhead = 0;
    int slaves = listLength(server.slaves);

    if (slaves) {
        listIter li;
        listNode *ln;

        //计算主从复制的输出缓冲区大小
        listRewind(server.slaves,&li);
        while((ln = listNext(&li))) {
            client *slave = listNodeValue(ln);
            //对slave的输出缓冲区
            overhead += getClientOutputBufferMemoryUsage(slave);
        }
    }
    if (server.aof_state != AOF_OFF) {
        overhead += sdsalloc(server.aof_buf)+aofRewriteBufferSize();
    }
    return overhead;
}

/* Get the memory status from the point of view of the maxmemory directive:
 * if the memory used is under the maxmemory setting then C_OK is returned.
 * Otherwise, if we are over the memory limit, the function returns
 * C_ERR.
 *
 * The function may return additional info via reference, only if the
 * pointers to the respective arguments is not NULL. Certain fields are
 * populated only when C_ERR is returned:
 *
 *  'total'     total amount of bytes used.
 *              (Populated both for C_ERR and C_OK)
 *
 *  'logical'   the amount of memory used minus the slaves/AOF buffers.
 *              (Populated when C_ERR is returned)
 *
 *  'tofree'    the amount of memory that should be released
 *              in order to return back into the memory limits.
 *              (Populated when C_ERR is returned)
 *
 *  'level'     this usually ranges from 0 to 1, and reports the amount of
 *              memory currently used. May be > 1 if we are over the memory
 *              limit.
 *              (Populated both for C_ERR and C_OK)
 */
int getMaxmemoryState(size_t *total, size_t *logical, size_t *tofree, float *level) {
    size_t mem_reported, mem_used, mem_tofree;

    /* Check if we are over the memory usage limit. If we are not, no need
     * to subtract the slaves output buffers. We can just return ASAP. */
    //已使用的
    mem_reported = zmalloc_used_memory();
    if (total) *total = mem_reported;

    /* We may return ASAP if there is no need to compute the level. */
    int return_ok_asap = !server.maxmemory || mem_reported <= server.maxmemory;
    if (return_ok_asap && !level) return C_OK;

    /* Remove the size of slaves output buffers and AOF buffer from the
     * count of used memory. */
    mem_used = mem_reported;
    //计算主从复制的输出缓冲区大小
    size_t overhead = freeMemoryGetNotCountedMemory();
    //主从复制缓冲区大小减去
    mem_used = (mem_used > overhead) ? mem_used-overhead : 0;

    /* Compute the ratio of memory usage. */
    if (level) {
        if (!server.maxmemory) {
            *level = 0;
        } else {
            *level = (float)mem_used / (float)server.maxmemory;
        }
    }

    if (return_ok_asap) return C_OK;

    /* Check if we are still over the memory limit. */
    if (mem_used <= server.maxmemory) return C_OK;

    /* Compute how much memory we need to free. */
    //计算超过server.maxmemory的内存（用于释放）
    mem_tofree = mem_used - server.maxmemory;

    if (logical) *logical = mem_used;
    if (tofree) *tofree = mem_tofree;

    return C_ERR;
}

/* This function is periodically called to see if there is memory to free
 * according to the current "maxmemory" settings. In case we are over the
 * memory limit, the function will try to free some memory to return back
 * under the limit.
 *
 * The function returns C_OK if we are under the memory limit or if we
 * were over the limit, but the attempt to free memory was successful.
 * Otehrwise if we are over the memory limit, but not enough memory
 * was freed to return back under the limit, the function returns C_ERR. */

/**
 * @brief 如果有必要释放内存
 * 1，从库不处理
 * 2，计算需要释放的空间mem_tofree，如果没有需要释放的就不处理（这里会把主从复制的缓冲区减掉）
 * 3，noeviction 淘汰策略直接返回
 * 4，如果已用的内存超了，随机采样key，进行释放，直到释放的空间小于要释放的内存
 *   4.1 LRU、LFU、TTL 这三种策略是一种处理
 *   4.2 RANDOM 策略处理
 * 5，根据采样到的key进行删除
 * @return int 
 */
int freeMemoryIfNeeded(void) {
    /* By default replicas should ignore maxmemory
     * and just be masters exact copies. */
    //从库不用处理
    if (server.masterhost && server.repl_slave_ignore_maxmemory) return C_OK;

    size_t mem_reported, mem_tofree, mem_freed;
    mstime_t latency, eviction_latency;
    long long delta;
    //从库数量
    int slaves = listLength(server.slaves);

    /* When clients are paused the dataset should be static not just from the
     * POV of clients not being able to write, but also from the POV of
     * expires and evictions of keys not being performed. */
    if (clientsArePaused()) return C_OK;
    //计算需要释放的空间mem_tofree，如果没有需要释放的就不处理
    if (getMaxmemoryState(&mem_reported,NULL,&mem_tofree,NULL) == C_OK)
        return C_OK;

    mem_freed = 0;

    if (server.maxmemory_policy == MAXMEMORY_NO_EVICTION)
        goto cant_free; /* We need to free memory, but policy forbids. */

    latencyStartMonitor(latency);
    while (mem_freed < mem_tofree) {
        int j, k, i, keys_freed = 0;
        static unsigned int next_db = 0;
        sds bestkey = NULL;
        int bestdbid;
        redisDb *db;
        dict *dict;
        dictEntry *de;
        //不同的淘汰策略执行不同的逻辑
        if (server.maxmemory_policy & (MAXMEMORY_FLAG_LRU|MAXMEMORY_FLAG_LFU) ||
            server.maxmemory_policy == MAXMEMORY_VOLATILE_TTL)
        {
            //默认16的数组，在initServer里，调用 evictionPoolAlloc 初始化数组
            struct evictionPoolEntry *pool = EvictionPoolLRU;

            while(bestkey == NULL) {
                unsigned long total_keys = 0, keys;

                /* We don't want to make local-db choices when expiring keys,
                 * so to start populate the eviction pool sampling keys from
                 * every DB. */
                //遍历db
                for (i = 0; i < server.dbnum; i++) {
                    db = server.db+i;
                    //根据不同的策略固定采样源，是从全全局hash表里拿，还是从expires哈希表里拿
                    dict = (server.maxmemory_policy & MAXMEMORY_FLAG_ALLKEYS) ?db->dict : db->expires;
                    if ((keys = dictSize(dict)) != 0) {
                        //采集的信息放入数组，idle越大都在左边
                        evictionPoolPopulate(i, dict, db->dict, pool);
                        total_keys += keys;
                    }
                }
                if (!total_keys) break; /* No keys to evict. */

                /* Go backward from best to worst element to evict. */
                /**
                 * 倒序处理
                 */
                for (k = EVPOOL_SIZE-1; k >= 0; k--) {
                    if (pool[k].key == NULL) continue;
                    bestdbid = pool[k].dbid;
                    //根据不同的策略是从全全局hash表里拿，还是从expires哈希表里拿到键值dictEntry
                    if (server.maxmemory_policy & MAXMEMORY_FLAG_ALLKEYS) {
                        de = dictFind(server.db[pool[k].dbid].dict,pool[k].key);
                    } else {
                        de = dictFind(server.db[pool[k].dbid].expires,pool[k].key);
                    }

                    /* Remove the entry from the pool. */
                    if (pool[k].key != pool[k].cached)
                        sdsfree(pool[k].key);
                    pool[k].key = NULL;
                    pool[k].idle = 0;

                    /* If the key exists, is our pick. Otherwise it is
                     * a ghost and we need to try the next element. */
                    if (de) {
                        bestkey = dictGetKey(de);
                        break;
                    } else {
                        /* Ghost... Iterate again. */
                    }
                }
            }
        }

        /* volatile-random and allkeys-random policy */
        else if (server.maxmemory_policy == MAXMEMORY_ALLKEYS_RANDOM ||
                 server.maxmemory_policy == MAXMEMORY_VOLATILE_RANDOM)
        {
            /* When evicting a random key, we try to evict a key for
             * each DB, so we use the static 'next_db' variable to
             * incrementally visit all DBs. */
            for (i = 0; i < server.dbnum; i++) {
                //防止溢出，如果直接取模求db
                j = (++next_db) % server.dbnum;
                //数组的名是首地址
                db = server.db+j;
                //删除的源
                dict = (server.maxmemory_policy == MAXMEMORY_ALLKEYS_RANDOM) ?db->dict : db->expires;
                if (dictSize(dict) != 0) {
                    //随机获取key
                    de = dictGetRandomKey(dict);
                    bestkey = dictGetKey(de);
                    bestdbid = j;
                    break;
                }
            }
        }

        /* Finally remove the selected key. */
        if (bestkey) {
            //释放空间
            db = server.db+bestdbid;
            robj *keyobj = createStringObject(bestkey,sdslen(bestkey));
            propagateExpire(db,keyobj,server.lazyfree_lazy_eviction);
            /* We compute the amount of memory freed by db*Delete() alone.
             * It is possible that actually the memory needed to propagate
             * the DEL in AOF and replication link is greater than the one
             * we are freeing removing the key, but we can't account for
             * that otherwise we would never exit the loop.
             *
             * AOF and Output buffer memory will be freed eventually so
             * we only care about memory used by the key space. */
            delta = (long long) zmalloc_used_memory();
            latencyStartMonitor(eviction_latency);
            //删除方式
            if (server.lazyfree_lazy_eviction)
                dbAsyncDelete(db,keyobj);
            else
                dbSyncDelete(db,keyobj);
            latencyEndMonitor(eviction_latency);
            latencyAddSampleIfNeeded("eviction-del",eviction_latency);
            latencyRemoveNestedEvent(latency,eviction_latency);
            delta -= (long long) zmalloc_used_memory();
            mem_freed += delta;
            server.stat_evictedkeys++;
            notifyKeyspaceEvent(NOTIFY_EVICTED, "evicted",
                keyobj, db->id);
            decrRefCount(keyobj);
            keys_freed++;

            /* When the memory to free starts to be big enough, we may
             * start spending so much time here that is impossible to
             * deliver data to the slaves fast enough, so we force the
             * transmission here inside the loop. */
            if (slaves) flushSlavesOutputBuffers();

            /* Normally our stop condition is the ability to release
             * a fixed, pre-computed amount of memory. However when we
             * are deleting objects in another thread, it's better to
             * check, from time to time, if we already reached our target
             * memory, since the "mem_freed" amount is computed only
             * across the dbAsyncDelete() call, while the thread can
             * release the memory all the time. */
            if (server.lazyfree_lazy_eviction && !(keys_freed % 16)) {
                if (getMaxmemoryState(NULL,NULL,NULL,NULL) == C_OK) {
                    /* Let's satisfy our stop condition. */
                    mem_freed = mem_tofree;
                }
            }
        }

        if (!keys_freed) {
            latencyEndMonitor(latency);
            latencyAddSampleIfNeeded("eviction-cycle",latency);
            goto cant_free; /* nothing to free... */
        }
    }
    latencyEndMonitor(latency);
    latencyAddSampleIfNeeded("eviction-cycle",latency);
    return C_OK;

cant_free:
    /* We are here if we are not able to reclaim memory. There is only one
     * last thing we can try: check if the lazyfree thread has jobs in queue
     * and wait... */
    //如果没有空闲内存，这里会检查lazy-free是否有任务在队列中等待
    while(bioPendingJobsOfType(BIO_LAZY_FREE)) {
        if (((mem_reported - zmalloc_used_memory()) + mem_freed) >= mem_tofree)
            break;
        usleep(1000);
    }
    return C_ERR;
}

/* This is a wrapper for freeMemoryIfNeeded() that only really calls the
 * function if right now there are the conditions to do so safely:
 *
 * - There must be no script in timeout condition.
 * - Nor we are loading data right now.
 *
 */

/**
 * @brief 在server.c中 processCommand
 * 
 * @return int 
 */
int freeMemoryIfNeededAndSafe(void) {
    //lua脚本超时或正在加载持久化，直接返回
    if (server.lua_timedout || server.loading) return C_OK;
    return freeMemoryIfNeeded();
}
