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
unsigned int dictGetSomeKeys(dictEntry **des) {
    // printf("%d \r\n",**des);
    for (size_t i = 0; i < 2; i++)
    {
         dictEntry *entry;
        entry->key = "a";
        entry->val = "123";
        *des = entry; 
        des++;
    }
    
    // printf("*des %s \r\n",*des);
    // printf("des %s \r\n",des);
    return 1;
}

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
     //默认5
    dictEntry *samples[5];

    char s1[] ="https://www.5ycode.com/";
    char t1[] ="5ycode";
    int curlen1 = strlen(s1);
    int len1=6;
    printf("memcpy before s1 :%s  t1:%s \n",s1,t1);
    memcpy(s1, t1, len1);
    printf("memcpy after s1:%s t1:%s \n",s1,t1);

    char s2[] ="https://www.5ycode.com/";
    char t2[] ="5ycode";
    int curlen2 = strlen(s2);
    int len2=6;
    printf("memmove before s2:%s t2:%s \n",s2,t2);
    memmove(s2, t2, len2);
    printf("memmove after s2:%s t2:%s \n",s2,t2);
    return 0;
}