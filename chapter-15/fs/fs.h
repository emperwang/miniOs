#ifndef _FS_FS_H_
#define _FS_FS_H_

#include "stdint.h"
#include "ide.h"
#include "string.h"

#define MAX_FILES_PER_PART   4096          // 每个分区支持最大创建的文件数
#define BITS_PER_SECTOR     4096        // 每扇区的位数
#define SECTOR_SIZE         512         //  扇区字节数
#define BLOCK_SIZE      SECTOR_SIZE     // 块字节数
#define MAX_PATH_LENGTH     512     // 最大路径长度

enum file_types{
    FT_UNKNOWN,             // 不支持的类型
    FT_REGULAR,             // 普通文件
    FT_DIRECTORY            // 目录
};

// 打开文件的选项
enum oflags {
    O_RDONLY,        // 只读
    O_WRONLY,       // 只写
    O_RDWR,         // 读写
    O_CREAT = 4     //
};

//文件读写位置偏移量
enum whence {
    SEEK_SET = 1,
    SEEK_CUR,
    SEEK_END
};

// 用来记录查找文件过程中已找到的上级路径, 也就是查找文件过程中 '走过的地址'
struct path_search_record{
    char searched_path[MAX_PATH_LENGTH];        //查找过程中的父路径
    struct dir* parent_dir;         // 文件或目录所在的直接父目录
    enum file_types file_type;          // 找到的文件类型
};

// 文件属性结构体
struct stat{
    uint32_t st_ino;            // inode编号
    uint32_t st_size;           // 尺寸
    enum file_types st_filetype;    // 文件类型
};


extern struct partition* cur_part;

void filesys_init(void);

int32_t  sys_open(const char* pathname, uint8_t flags);

int32_t path_depth_cnt(char* pathname);

char* path_parse(char* pathname, char* name_store);

int32_t sys_close(int32_t fd);

int32_t  sys_write(int32_t fd, const void* buf, uint32_t count);

int32_t sys_read(int32_t fd, void* buf, uint32_t count);

int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence);

int32_t sys_unlink(const char* pathname);

int32_t sys_mkdir(const char* pathname);

struct dir*  sys_opendir(const char* name);

int32_t sys_closedir(struct dir* dir);

struct dir_entry* dir_read(struct dir* dir);

struct dir_entry* sys_readdir(struct dir* dir);

int32_t sys_rmdir(const char* pathname);

void sys_rewinddir(struct dir* dir);

char* sys_getcwd(char* buf, uint32_t size);

int32_t sys_chdir(const char* path);

int32_t sys_stat(const char* path, struct stat* buf);

void sys_putchar(char char_asci);

#endif // _FS_FS_H_

