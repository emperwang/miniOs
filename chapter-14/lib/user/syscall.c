#include "syscall.h"

// 无参的系统调用
#define _syscall0(NUMBER)   ({      \
    int retval;                     \
    asm volatile(                   \
        "int $0x80"                 \
        :"=a"(retval)               \
        :"a" (NUMBER)               \
        :"memory"                   \
    );                              \
    retval;                  \
})

// 1个参数的系统调用
#define _syscall1(NUMBER, ARG1)     ({  \
    int retval;                                     \
    asm volatile(                                   \
    "int $0x80"                                     \
    :"=a" (retval)                                  \
    :"a" (NUMBER),"b"(ARG1)                         \
    :"memory"                                       \
    );                                              \
    retval;                                  \
})


// 2个参数的系统调用
#define _syscall2(NUMBER, ARG1, ARG2)     ({        \
    int retval;                                     \
    asm volatile(                                   \
    "int $0x80"                                     \
    :"=a" (retval)                                  \
    :"a" (NUMBER),"b"(ARG1),"c"(ARG2)               \
    :"memory"                                       \
    );                                              \
    retval;                                  \
})

// 三个参数的系统调用
#define _syscall3(NUMBER, ARG1, ARG2, ARG3)     ({  \
    int retval;                                     \
    asm volatile(                                   \
    "int $0x80"                                     \
    :"=a" (retval)                                  \
    :"a" (NUMBER),"b"(ARG1),"c"(ARG2), "d"(ARG3)    \
    :"memory"                                       \
    );                                              \
    retval;                                  \
})

uint32_t get_pid(){
    return _syscall0(SYS_GETPID);
}

// 把buf中count个字符写入到文件描述符fd
uint32_t write(uint32_t fd, const char* buf, uint32_t count){
    return _syscall3(SYS_WRITE, fd, buf, count);
}

// 申请size字节大小的内存
void* malloc(uint32_t size){
    return (void*)_syscall1(SYS_MALLOC, size);
}

// 释放ptr指向的内存
void free(void* ptr){
    _syscall1(SYS_FREE, ptr);
}


