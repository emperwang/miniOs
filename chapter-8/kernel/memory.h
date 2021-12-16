#ifndef __KERNEL_MEMORY_H__
#define __KERNEL_MEMORY_H__

#include "stdint.h"
#include "bitmap.h"

struct virtual_addr{
    struct bitmap vaddr_bitmap; // 虚拟地址用到的位图结构
    uint32_t vaddr_start;       // 虚拟地址起始地址
};

extern struct pool kernel_pool, user_pool;

void mem_init(void);

#endif /* __KERNEL_MEM*/

