#include <stdio.h>
#define ZIPLIST_BYTES(zl)       (*((uint32_t*)(zl)))
#define ZIPLIST_LENGTH(zl)      (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
typedef struct dictEntry {
    void *key;//8字节
    void *val;//8字节
    struct dictEntry *next; //8字节
} dictEntry;


typedef struct redisObject {
    unsigned type:4; //4位
    unsigned encoding:4; //4位
    // 24位
    unsigned lru:24; 
    int refcount; //4字节
    void *ptr; // 8字节
} robj;//一个robj 16直接

int main()
{
    // dictEntry *entry;
    // robj *robj;

    // // //获取entry的指针大小
    // int length = sizeof(entry);
    // // sds copy = sdsdup("");
   
    // printf("entry point size :%d \n",length);
    // // //获取entry结构体大小
    // length = sizeof(*entry);
    // printf("entry size :%d \n",length);
    // robj->ptr = "xxx234444";
    // length = sizeof(*robj);
    // printf("robj size :%d \n",length);
    // printf("robj ptr :%s \n",robj->ptr);
    // length = sizeof("abcd5");
    // printf("a size :%d \n",length);
    // long x = 1;
    // length = sizeof(x);
    // printf("x size :%d \n",length);
    // length = sizeof( (void*)x);
    //  printf("x1 size :%d \n",length);
    static int timelimit_exit = 0; 
    if (timelimit_exit) printf("0");
    if (!timelimit_exit) printf("!0\n");
    timelimit_exit=1;
    if (timelimit_exit) printf("1\n");
    if (!timelimit_exit) printf("!1\n");
    
    return 0;
}