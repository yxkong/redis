#include <stdio.h>
#include <typeinfo.h>
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
} robj;//一个robj

void setGenericCommand(robj *key, robj *val){
    printf(typeid(key).name());
    printf(typeid(*key).name());

}
int main()
{
    dictEntry *entry;
    robj *robj;
    robj->ptr = "yxkong";
    setGenericCommand(robj,robj);
    printf("\r");
    return 0;
}