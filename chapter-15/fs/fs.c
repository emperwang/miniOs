#include "fs.h"
#include "ide.h"
#include "global.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "stdio_kernel.h"
#include "memory.h"
#include "debug.h"
#include "list.h"
#include "string.h"
#include "file.h"
#include "console.h"
#include "keyboard.h"
#include "ioqueue.h"

struct partition* cur_part;     //  默认情况下操作的是哪个分区

// 在分区链表中找到名为 part_name的分区, 并将其指针赋值非cur_part
static bool mount_partition(struct list_elem* pelem, int arg){
    char* part_name = (char*) arg;

    struct partition* part = elem2entry(struct partition,part_tag, pelem);
    // 返回0表示相等
    if(!strcmp(part->name, part_name)){
        struct super_block* sb;
        cur_part = part;
        struct disk* hd = cur_part->my_disk;

        // sb_buf 用来创建存储从磁盘读取的超级块
        struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

        // 在内存中创建分区 cur_part的超级块
        cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));
        if(cur_part->sb == NULL){
            PANIC("mount alloc memory failed.\n");
        }
        // 读取超级块
        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba+1, sb_buf,1);

        // 把sb_buf中超级块信息赋值到分区的超级块sb中
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

         // 这里打印信息,主要用于调试
        sb = cur_part->sb;
        // printk(" %s info:\n", part->name);
        // printk(" magic:0x%x\n, part_lba_base:0x%x\n, all_sectors:0x%x\n, inode_cnt:0x%x\n block_bitmap_lba:0x%x\n block_bitmap_sectors:0x%x\n inode_bitmap_lba:0x%x\n inode_bitmap_sectors:0x%x\n inode_table_lba:0x%x\n, inode_table_sectors:0x%x\n data_start_lba:0x%x\n", sb->magic,sb->part_lba_base, sb->sec_cnt, sb->inode_cnt, sb->block_bitmap_lba,sb->block_bitmap_sects, sb->inode_bitmap_lba,sb->inode_bitmap_sects, sb->inode_table_lba, sb->inode_table_sects, sb->data_start_lba);
        /************将硬盘上的块位图读入到 内存*********/
        cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->block_bitmap_sects*SECTOR_SIZE);
        if(cur_part->block_bitmap.bits == NULL){
            PANIC("alloc memory failed.\n");
        }
        cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects*SECTOR_SIZE;

        // 从硬盘上读入块位图到分区的block_bitmap.bits
        ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);

        /************ 将硬盘上的inode位图读入到内存******/
        cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);

        if(cur_part->inode_bitmap.bits == NULL){
            PANIC(" inode alloc memory failed.\n");
        }

        cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects*SECTOR_SIZE;

        // 硬盘读取
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);

        list_init(&cur_part->open_inodes);
        printk(" mount %s done\n", part->name);

        return true;        // 此只是为了配合 list_traveral的实现,与函数功能无关
    }
    return false;
}

// 格式化分区,也就是初始化分区的元信息, 创建文件系统
static void partition_format(struct partition* part){
    // block_bitmap_init, 方便实现,一个块大小是一个扇区
    uint32_t boot_sector_sects = 1;
    uint32_t super_block_sects = 1;
    // i 结点位图占用的扇区数
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
    // i table 占用的扇区数
    uint32_t inode_table_sects = DIV_ROUND_UP(((sizeof(struct inode) * MAX_FILES_PER_PART)), SECTOR_SIZE);

    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects+ inode_table_sects;

    uint32_t free_sects = part->sec_cnt - used_sects;

    // 简单处理块位图占据的扇区数
    uint32_t  block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    // block_bitmap_bit_len是位图中位的长度,也是可用块的数量
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

    // 超级块初始化
    struct super_block sb;
    sb.magic = 0x19590318;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;

    sb.block_bitmap_lba = sb.part_lba_base + 2; // 第0块是引导块, 第1块是超级快
    sb.block_bitmap_sects = block_bitmap_sects;
    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;
    sb.dir_entry_size = sizeof(struct dir_entry);

    printk(" %s info:\n", part->name);

    printk(" magic:0x%x\n, part_lba_base:0x%x\n, all_sectors:0x%x\n, inode_cnt:0x%x\n block_bitmap_lba:0x%x\n block_bitmap_sectors:0x%x\n inode_bitmap_lba:0x%x\n inode_bitmap_sectors:0x%x\n inode_table_lba:0x%x\n, inode_table_sectors:0x%x\n data_start_lba:0x%x\n", sb.magic,sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba,sb.block_bitmap_sects, sb.inode_bitmap_lba,sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);

    struct disk* hd = part->my_disk;

    /**************************1. 将超级块写入本分区的1扇区**************************************/
    ide_write(hd, part->start_lba+1, &sb, 1);
    printk(" super_block_lba:0x%x\n",part->start_lba+1);

    // 找出数据量最大的元信息,用其尺寸做存储缓冲区
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects:sb.inode_bitmap_sects);

    buf_size = (buf_size>=sb.inode_table_sects?buf_size:sb.inode_table_sects) * SECTOR_SIZE;

    uint8_t* buf = (uint8_t*)sys_malloc(buf_size);  //申请的内存由内存管理系统清0后返回

    /****************************2. 将块位图初始化并写入 sb.block_bitmap_lba************************************/
    // 初始化块位图 block_bitmap
    buf[0] |= 0x01;     // 第0个块预留给根目录, 位图中先占位
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t block_bitmap_last_bit   = block_bitmap_bit_len % 8;
    // last_size 是位图所在最后一个扇区中,不足一扇区的其余部分
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);
    // 2.1 先将位图最后一字节到其所在扇区的结束全设置为1,即超出实际块数的部分直接设置为已占用
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);

    // 2.2 再将上一步中覆盖的最后一字节的有效位设置为0
    uint8_t bit_idx = 0;
    while(bit_idx <= block_bitmap_last_bit){
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
    }

    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

    /**************3. 将inode位图初始化并写入sb.inode_bitmap_lba**************************************************/
    // 先清空缓冲区
    memset(buf, 0, buf_size);
    buf[0] |= 0x01;         // 第0个inode分给了根目录
    /******
     * 由于inode_table 中共4096个inode
     * 位图inode_bitmap占用占用1扇区
     * 即 inode_bitmap_sects 等于1
     * 所以位图中的位全都代表inode_table中的inode,无需再像block_bitmap那种单独处理最后一扇区的剩余部分
     */

    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

    /************4. 将inode数组初始化并写入 sb.inode_table_lba****************************************************/
    // 先清空缓冲区
    memset(buf, 0, buf_size);
    // 准备写inode_Table中第0项, 即根目录所在的inode
    struct inode* i = (struct inode*)buf;
    i->i_size = sb.dir_entry_size * 2;  // .  和 ..
    i->i_no = 0;        // 根目录占据 inode数组中第0个 inode
    i->i_sectors[0] = sb.data_start_lba;    // i_sectors数组的其他元素都初始化为0

    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

    /************5.将根目录写入sb.data_start_lba****************************************************/
    // 先清空缓冲区
    memset(buf, 0, buf_size);

    struct dir_entry* p_de = (struct dir_entry*)buf;

    //初始化当前目录 "."
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    p_de++;
    // 初始化当前目录父目录 ".."
    memcpy(p_de->filename,"..",  2);
    p_de->i_no = 0;     // 根目录的父目录依然是根目录自己
    p_de->f_type = FT_DIRECTORY;

    // sb.data_start_lba已经分配给了根目录,里面是根目录的目录项
    ide_write(hd, sb.data_start_lba, buf,1);

    printk(" root_dir_lba:0x%x\n", sb.data_start_lba);
    printk(" %s format done\n", part->name);
    sys_free(buf);
}

// 向屏幕上输出一个字符
void sys_putchar(char char_asci){
    console_put_char(char_asci);
}

// 在磁盘上搜索文件系统, 若没有, 则格式化分区创建文件系统
void filesys_init(){

    uint8_t channel_no = 0, dev_no, part_idx=0;

    // sb_buf 用来存储从硬盘上读入的超级块
    struct super_block* sb_buf  = (struct super_block*)sys_malloc(SECTOR_SIZE);

    if(sb_buf == NULL){
        PANIC(" alloc memory failed.\n");
    }

    printk(" searching filesystem....\n");

    while(channel_no < channel_cnt){
        dev_no = 0;
        while(dev_no < 2) {
            if(dev_no == 0){        // 跨过系统盘 hd60M.img
                dev_no++;
                continue;
            }

            struct disk* hd = &channels[channel_no].devices[dev_no];
            struct partition* part = hd->prim_parts;
            while(part_idx < 12) {      // 4个主分区 + 8个逻辑分区
                if(part_idx == 4){
                    part = hd->logic_parts;
                }
                /******
                 * channels数组为全局变量, 默认值为0, disk属于其嵌套结构,partition又为disk的嵌套
                 * 结构,因此partition中的成员默认也为0. 若partition未初始化,则partition中的成员
                 * 仍为0, 下面处理存在的分区
                 */
                if(part->sec_cnt != 0) {        // 分区存在
                    memset(sb_buf,0, SECTOR_SIZE);
                    // 读处分区的超级块
                    ide_read(hd, part->start_lba+1, sb_buf, 1);

                    // 只支持自己的文件系统,若磁盘上已经有文件系统就不再格式化了
                    if(sb_buf->magic == 0x19590318) {
                        printk(" %s had filesystem\n", part->name);
                    }else { // 其他文件系统不支持, 一律按无文件系统处理
                        printk("(formatting %s partition %s....\n", hd->name, part->name);
                        partition_format(part);
                    }
                }
                part_idx++;
                part++;
            }   // part_idx
            dev_no++;
        }   // dev_no
        channel_no++;
    }     // channel_no
    sys_free(sb_buf);

    // 确定默认操作的分区
    char default_part[8] = "sdb1";

    // 挂载分区
    list_traversal(&partition_list, mount_partition, (int)default_part);

    // 将当前分区的根目录打开
    open_root_dir(cur_part);
    // 初始化文件表
    uint32_t fd_idx = 0;
    while(fd_idx < MAX_FILE_OPEN){
        file_table[fd_idx++].fd_inode = NULL;
    }
}

// 将最上层的路径名称解析出来
char* path_parse(char* pathname, char* name_store){
    if(pathname[0] == '/') {
        // 跳过路径中出现的1个或多个连续的字符  '////a/b'
        while(*(++pathname) == '/');
    }
    // 一般的路径解析
    while(*pathname != '/' && *pathname != 0){
        *name_store++ = *pathname++;
    }

    if(pathname[0] == 0){       // 路径字符串为空,则返回NULL
        return NULL;
    }
    return pathname;
}


// 返回路径深度, 比如/a/b/c 深度为3
int32_t path_depth_cnt(char* pathname){
    ASSERT(pathname != NULL);

    char* p = pathname;
    char name[MAX_FILE_NAME_LEN];

    uint32_t depth = 0;

    p = path_parse(p, name);
    while(name[0]){
        depth++;
        memset(name, 0, MAX_FILE_NAME_LEN);
        if(p){
             p = path_parse(p, name);
        }
    }
    return depth;
}

// 搜索文件pathname, 若找到则返回其inode号, 否则返回-1
static int search_file(const char* pathname, struct path_search_record* search_record){
    // 如果待查找的是根目录, 为避免下面无用的查找,直接返回已知的根目录信息

    if(!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")){
        search_record->parent_dir = &root_dir;
        search_record->file_type = FT_DIRECTORY;
        search_record->searched_path[0] = 0;        // 搜索路径情况
        return 0;
    }

    uint32_t path_len = strlen(pathname);
    // 保证pathname 小于最大长度
    ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LENGTH);

    char* sub_path = (char*) pathname;
    struct dir* parent_dir = &root_dir;

    struct dir_entry dir_e;

    // 记录路径解析出来的各级名称,如路径/a/b/c, 数组name 每次的值分别是 a b c
    char name[MAX_FILE_NAME_LEN] = {0};

    search_record->parent_dir = parent_dir;
    search_record->file_type = FT_UNKNOWN;
    uint32_t parent_inode_no = 0;

    sub_path = path_parse(sub_path, name);
    while(name[0]){ // 若第一个字符就是结束符, 结束循环
        // 记录查找过的路径, 但不能超过  search_path的长度 512字节
        ASSERT(strlen(search_record->searched_path) < 512);

        // 记录已存在的父目录
        strcat(search_record->searched_path, "/");
        strcat(search_record->searched_path, name);

        // 在所给的目录中查找文件
        if(search_dir_entry(cur_part, parent_dir, name, &dir_e)){
            memset(name,0, MAX_FILE_NAME_LEN);
            //若sub_path 不等于NULL, 也就是未结束, 继续拆分路径
            if(sub_path){
                sub_path = path_parse(sub_path, name);
            }

            //如果被打开的是目录
            if(FT_DIRECTORY == dir_e.f_type){
                parent_inode_no = parent_dir->inode->i_no;
                dir_close(parent_dir);
                parent_dir = dir_open(cur_part, dir_e.i_no);
                search_record->parent_dir = parent_dir;
                continue;
            }else if(FT_REGULAR == dir_e.f_type) {  // 普通文件
                search_record->file_type = FT_REGULAR;
                return dir_e.i_no;
            }
        }else { //  找不到
            /******
             *  找不到目录项时, 要李留着parent_dir不要关闭
             *  若是要创建新文件的话,需要在parent_dir中创建
             */
            return -1;
        }
    }
    // 执行到此,必然是遍历了完整路径,并且找到的文件或目录 只有同名目录存在
    dir_close(search_record->parent_dir);

    // 保存被查找目录的直接父目录
    search_record->parent_dir = dir_open(cur_part, parent_inode_no);
    search_record->file_type = FT_DIRECTORY;
    return dir_e.i_no;
}

// 打开 或 创建文件成功后,返回文件描述符
int32_t  sys_open(const char* pathname, uint8_t flags){
    // 对目录要用dir_open, 这里只有open文件
    if(pathname[strlen(pathname) -1] == '/'){
        printk("can't open a directory %s\n", pathname);
        return -1;
    }

    ASSERT(flags <= 7);

    int32_t fd = -1;        // 默认找不到

    struct path_search_record search_record;

    memset(&search_record, 0, sizeof(struct path_search_record));

    // 记录目录深度, 帮助判断中间某个目录不存在的情况
    uint32_t pathname_depth = path_depth_cnt((char*) pathname);

    // 先检查文件是否存在
    int inode_no = search_file(pathname, &search_record);
    bool found = inode_no != -1 ? true: false;

    if(search_record.file_type == FT_DIRECTORY){
        printk("can't open a directory with open(), use opendir() to instead.\n");
        dir_close(search_record.parent_dir);
        return -1;
    }

    uint32_t path_search_depth = path_depth_cnt(search_record.searched_path);

    // 先判断是否把pathname的各层目录都访问到了,即是否在某个中间目录就失败了
    if(path_search_depth != pathname_depth){
        // 说明没有访问到全部路径,某个中间目录是不存在的
        printk("can't access %s: not a directory, subpath %s isn't exist\n", pathname, search_record.searched_path);
        dir_close(search_record.parent_dir);
        return -1;
    }

    // 若是在最后一个路径上没有找到,并且不是要创建文件, 则直接返回-1
    if(!found && !(flags & O_CREAT)){
        printk("in path %s, file %s isn't exists.\n", search_record.searched_path, (strrchr(search_record.searched_path, '/') + 1));
        dir_close(search_record.parent_dir);
        return -1;
    }else if(found && (flags & O_CREAT)){   // 若要创建的文件已存在
        printk("%s has already exists!.\n", pathname);
        dir_close(search_record.parent_dir);
        return -1;
    }

    switch(flags & O_CREAT){
        case O_CREAT:
            printk(" create file\n");
            fd = file_create(search_record.parent_dir, (strrchr(pathname, '/') + 1), flags);
            dir_close(search_record.parent_dir);
            break;
        default:        // 其他情况,打开存在的文件 O_RDONLY O_WRONLY, O_RDWR
            fd = file_open(inode_no, flags);
    }
    // 此fd是指  pcb->fd_table数组中的元素下标
    return fd;
}

// 将文件描述符转换为文件表的下标
static uint32_t fd_local2global(uint32_t local_fd){
    struct task_struct* cur = running_thread();

    int32_t global_fd =  cur->fd_table[local_fd];

    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);

    return (uint32_t)global_fd;
}

// 关闭文件描述符fd指向的文件,成功返回0, 失败-1
int32_t sys_close(int32_t fd){
    int32_t ret = -1;
    if(fd > 2){
        uint32_t gfd = fd_local2global(fd);
        ret = file_close(&file_table[gfd]);
        running_thread()->fd_table[fd] = -1;
    }
    return ret;
}


// 将buf中连续count个字节写入文件描述符fd中
int32_t  sys_write(int32_t fd, const void* buf, uint32_t count){
    if(fd < 0){
        printk("sys_write: fd error.\n");
        return -1;
    }

    if(fd == stdout_no){
        char tmp_buf[1024] = {0};
        memcpy(tmp_buf, buf, count);
        console_put_str(tmp_buf);
        return count;
    }

    uint32_t _fd = fd_local2global(fd);

    struct file* wr_file = &file_table[_fd];

    if(wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR){
        uint32_t bytes_written = file_write(wr_file, buf, count);
        return bytes_written;
    }else {
        console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY.\n");
        return -1;
    }
}


// 从文件描述符fd指向的文件中读取count个字节到buf中,若成功则返回读出的字节数, 到文件尾则返回-1
int32_t sys_read(int32_t fd, void* buf, uint32_t count){
    ASSERT(buf != NULL);
    int32_t ret = -1;
    if(fd < 0 || fd == stdout_no || fd == stderr_no){
        printk("sys_read: fd error: %d\n", fd);
    }else if(fd == stdin_no){
        char* buffer = buf;
        uint32_t bytes_read=0;
        while(bytes_read < count){
            *buffer = ioq_getchar(&kbd_buf);
            bytes_read++;
            buffer++;
        }
        ret = (bytes_read == 0 ? -1:(int32_t)bytes_read);
    }else {
        uint32_t _fd = fd_local2global(fd);
        ret = file_read(&file_table[_fd], buf, count);
    }
    return ret;
}

//重置文件读写操作的偏移指针,成功则返回新的偏移量,错误返回-1
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence){
    if(fd < 0){
        printk("sys_lseek: fd error: %d", fd);
        return -1;
    }

    ASSERT(whence >0 && whence < 4);
    uint32_t _fd = fd_local2global(fd);
    struct file* pf = &file_table[_fd];

    int32_t new_pos = 0;    // 新的偏移了必须在文件大小内
    int32_t file_size = (int32_t)pf->fd_inode->i_size;
    switch(whence) {
        // 新的读写位置相对于文件开头再增加offset个位移量
        case SEEK_SET:
            new_pos = offset;
            break;

        // 新的读写位置是相对于当前增加offset
        case SEEK_CUR:
            new_pos = (int32_t)pf->fd_pos+offset;
            break;
        // 新的读写位置是相对于文件尺寸在增加offset偏移量
        case SEEK_END:
            new_pos = file_size + offset;
            break;
    }
    if(new_pos < 0 || new_pos > (file_size -1)){
        return -1;
    }
    pf->fd_pos = new_pos;

    return pf->fd_pos;
}

// 删除文件(非目录), 成功返回0, 失败返回-1
int32_t sys_unlink(const char* pathname){
    ASSERT(strlen(pathname) < MAX_PATH_LENGTH);

    // 先检查文件是否存在
    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(pathname, &search_record);
    ASSERT(inode_no != 0);
    if(inode_no == -1){
        printk("file %s not found.\n", pathname);
        dir_close(search_record.parent_dir);
        return -1;
    }

    if(search_record.file_type == FT_DIRECTORY){
        printk("can't delete a directory with ulink(), use rmdir() to instead.\n");
        dir_close(search_record.parent_dir);
        return -1;
    }

    // 检查文件是否在已打开文件列表中
    uint32_t file_idx = 0;
    while(file_idx < MAX_FILE_OPEN){
        if(file_table[file_idx].fd_inode != NULL && (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no){
            break;
        }
        file_idx++;
    }

    if(file_idx < MAX_FILE_OPEN){
        dir_close(search_record.parent_dir);
        printk("file %s is in use, not allow to delete.\n", pathname);
        return -1;
    }
    ASSERT(file_idx == MAX_FILE_OPEN);

    // 为delete_dir_entry 申请缓冲区
    void* io_buf = sys_malloc(SECTOR_SIZE + SECTOR_SIZE);
    if(io_buf == NULL){
        dir_close(search_record.parent_dir);
        printk("sys_unlink: malloc for io_buf failed.\n");
        return -1;
    }

    struct dir* parent_dir = search_record.parent_dir;
    delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);
    inode_release(cur_part, inode_no);

    sys_free(io_buf);
    dir_close(search_record.parent_dir);
    return 0;   //成功删除
}


// 创建目录pathname, 成功返回0,失败返回-1
int32_t sys_mkdir(const char* pathname){
    uint8_t rollback_step = 0;
    void* io_buf = sys_malloc(SECTOR_SIZE*2);
    if(io_buf == NULL){
        printk("sys_mkdir: sys_malloc for io_buf failed.\n");
        return -1;
    }

    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));

    int inode_no = -1;
    inode_no = search_file(pathname, &search_record);
    if(inode_no != -1){     // 找到了同名目录或文件,则失败返回
        printk("sys_mkdir: file or directory %s exist.\n", pathname);
        rollback_step = 1;
        goto rollback;
    }else {
        // 为找到,也要判断是在最终目录没找到, 还是某个中间目录不存在
        uint32_t pathname_depth = path_depth_cnt((char*)pathname);
        uint32_t path_searched_depth = path_depth_cnt(search_record.searched_path);
        /******
         * 先判断是否把pathname的各层目录都访问到了
         */
        if(path_searched_depth != pathname_depth){
            // 没有访问到全部路径
            printk("sys_mkdir: cannt access %s, subpath %s is't exists.\n", pathname, 
                            search_record.searched_path);
            rollback_step = 1;
            goto rollback;
        }
    }

    struct dir* parent_dir = search_record.parent_dir;

    char* dirname = strrchr(search_record.searched_path, '/')+1;
    inode_no = inode_bitmap_alloc(cur_part);
    if(inode_no == -1){
        printk("sys_mkdir: allocate inode failed.\n");
        rollback_step = 1;
        goto rollback;
    }

    struct inode new_dir_inode;
    inode_init(inode_no, &new_dir_inode);

    uint32_t block_bitmap_idx = 0;
    int32_t block_lba = -1;
    // 为目录分配一个block,用来写入目录 .  和 ..
    block_lba = block_bitmap_alloc(cur_part);
    if(block_lba == -1){
        printk("sys_mkdir: block_bitmap_alloc for create directory failed.\n");
        rollback_step = 2;
        goto rollback;
    }

    new_dir_inode.i_sectors[0] = block_lba;
    block_bitmap_idx = block_lba  - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
    // 将目录 .  和 .. 写入目录
    memset(io_buf, 0, SECTOR_SIZE * 2);

    struct dir_entry* p_de = (struct dir_entry*)io_buf;

    //初始化目录 .
    memcpy(p_de->filename,".", 1);
    p_de->i_no = inode_no;
    p_de->f_type = FT_DIRECTORY;
    p_de++;

    // 初始化目录 ..
    memcpy(p_de->filename,"..", 2);
    p_de->i_no = parent_dir->inode->i_no;
    p_de->f_type = FT_DIRECTORY;
    ide_write(cur_part->my_disk, new_dir_inode.i_sectors[0], io_buf, 1);

    new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;

    // 在父目录中添加自己的目录项
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(dirname, inode_no, FT_DIRECTORY, &new_dir_entry);

    memset(io_buf,0, SECTOR_SIZE*2);

    if(!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)){
        printk("sys_mkdir: sync_dir_entry to disk failed.\n");
        rollback_step = 2;
        goto rollback;
    }

    // 父目录的inode同步到硬盘
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, parent_dir->inode, io_buf);

    // 将新创建目录的inode同步到硬盘
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, &new_dir_inode, io_buf);

    // 将inode位图同步
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    sys_free(io_buf);

    dir_close(search_record.parent_dir);
    return 0;


rollback:
    switch(rollback_step){
        case 2:
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
        case 1:
            dir_close(search_record.parent_dir);
            break;
    }
    sys_free(io_buf);
    return -1;

}

// 打开目录. 打开成功后返回目录指针, 失败返回NULL
struct dir*  sys_opendir(const char* name){
    ASSERT(strlen(name) < MAX_PATH_LENGTH);
    // 如果是根目录 '/' , 直接返回root_dir
    if(name[0] == '/' && (name[1] == 0 || name[0]=='.')){
        return &root_dir;
    }

    //检测待打开的目录是否存在
    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(name, &search_record);
    struct dir* ret = NULL;
    if(inode_no == -1){
        // 如果找不到目录,则提示不存在
        printk("in %s , subpath: %s not exist.\n", name, search_record.searched_path);
    }else {
        if(search_record.file_type == FT_REGULAR){
            printk("%s is regular file.\n", name);
        }else if(search_record.file_type = FT_DIRECTORY){
            ret = dir_open(cur_part, inode_no);
        }
    }
    dir_close(search_record.parent_dir);
    return ret;
}

// 关闭目录
int32_t sys_closedir(struct dir* dir){
    int32_t ret = -1;
    if(dir != NULL){
        dir_close(dir);
        ret = 0;
    }
    return ret;
}

// 读取目录,成功则返回1个目录项,失败返回NULL
struct dir_entry* dir_read(struct dir* dir){
    struct dir_entry* dir_e = (struct dir_entry*)dir->dir_buf;
    struct inode* dir_inode = dir->inode;
    uint32_t all_blocks[140] = {0};
    uint32_t block_cnt = 12, block_idx = 0, dir_entry_idx = 0;

    while(block_idx < 12){
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }

    if(dir_inode->i_sectors[12] != 0){
        ide_read(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks+12, 1);
        block_cnt = 140;
    }

    block_idx= 0;

    // 当前目录项的偏移
    uint32_t cur_dir_entry_pos = 0;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entry_per_sec = SECTOR_SIZE / dir_entry_size;

    // 在目录大小内遍历
    while(dir->dir_pos < dir_inode->i_size){
        if(dir->dir_pos >= dir_inode->i_size){
            return NULL;
        }

        if(all_blocks[block_idx] == 0){
            // 如果此块为空,继续下块
            block_idx++;
            continue;
        }

        memset(dir_e, 0, SECTOR_SIZE);
        ide_read(cur_part->my_disk, all_blocks[block_idx], dir_e, 1);
        dir_entry_idx = 0;
        // 遍历扇区内所有目录项
        while(dir_entry_idx < dir_entry_per_sec){
            if((dir_e + dir_entry_idx)->f_type) {   // ftype 不等于 FT_UNKNOWN
            // 判断是不是最新的目录项,避免返回曾经已经返回的目录项
                if(cur_dir_entry_pos < dir->dir_pos){
                    cur_dir_entry_pos += dir_entry_size;
                    dir_entry_idx++;
                    continue;
                }
                ASSERT(cur_dir_entry_pos == dir->dir_pos);
                dir->dir_pos += dir_entry_size;
                // 更新为新位置, 即下一个返回的目录项地址
                return dir_e + dir_entry_idx;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }
    return NULL;
}

// 读取目录dir的1个目录项,成功后返回其目录项地址
struct dir_entry* sys_readdir(struct dir* dir){
    ASSERT(dir != NULL);
    return dir_read(dir);
}

// 把目录dir的指针dir_pos 设置0
void sys_rewinddir(struct dir* dir){
    dir->dir_pos = 0;
}

// 删除空目录,成功返回0, 失败返回-1
int32_t sys_rmdir(const char* pathname){
    /// 先检查待删除的文件是否存在
    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(pathname, &search_record);
    ASSERT(inode_no != 0);
    int retval = -1;

    if(inode_no == -1){
        printk("In %s , sub path %s not exist.\n", pathname, search_record.searched_path);
    }else {
        if (search_record.file_type == FT_REGULAR){
            printk("%s is a regular file!\n", pathname);
        }else {
            struct dir* dir = dir_open(cur_part,inode_no);
            if(!dir_is_empty(dir)){     // 目录非空,不可删除
                printk("dir %s it not empty, it's not allowed to delete a nonempty directory.\n",pathname);
            }else {
                if(!dir_remove(search_record.parent_dir, dir)){
                    retval = 0;
                }
            }
            dir_close(dir);
        }
    }
    dir_close(search_record.parent_dir);
    return retval;
}

// 获取父目录的inode编号
static uint32_t get_parent_dir_inode_nr(uint32_t child_inode_nr, void* io_buf){
    struct inode* child_dir_inode = inode_open(cur_part, child_inode_nr);

    // 目录中的目录项 '..'  包括父目录inode编号. '..' 位于第0块
    uint32_t block_lba = child_dir_inode->i_sectors[0];
    ASSERT(block_lba >= cur_part->sb->data_start_lba);
    inode_close(child_dir_inode);

    ide_read(cur_part->my_disk, block_lba, io_buf,1);
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;

    // 第0个目录项是 '.', 第一个目录项是'..'
    ASSERT(dir_e[1].i_no < 4096 && dir_e[1].f_type == FT_DIRECTORY);

    return dir_e[1].i_no;
}

// 在inode编号为p_inode_nr的目录中查找 inode编号为c_inode_nr的子目录名字.
//将名字存入缓冲区 path, 成功返回0, 失败返回-1
static int get_child_dir_name(uint32_t p_inode_nr, uint32_t c_inode_nr, char* path, void* io_buf){
    struct inode* parent_dir_inode = inode_open(cur_part, p_inode_nr);
    // 填充all_blocks
    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0};
    uint32_t block_cnt = 12;

    while(block_idx < 12){
        all_blocks[block_idx] = parent_dir_inode->i_sectors[block_idx];
        block_idx++;
    }

    if(parent_dir_inode->i_sectors[12]){
        ide_read(cur_part->my_disk, parent_dir_inode->i_sectors[12], all_blocks+12, 1);
        block_cnt=140;
    }
    inode_close(parent_dir_inode);

    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entry_per_sec = (512/ dir_entry_size);
    block_idx = 0;
    //遍历所有块
    while(block_idx < block_cnt){
        if(all_blocks[block_idx]){
            ide_read(cur_part->my_disk,all_blocks[block_idx], io_buf, 1);
            uint8_t dir_e_idx = 0;
            //遍历每个目录项
            while(dir_e_idx < dir_entry_per_sec){
                if((dir_e + dir_e_idx)->i_no == c_inode_nr){
                    strcat(path, "/");
                    strcat(path, (dir_e+dir_e_idx)->filename);
                    return 0;
                }
                dir_e_idx++;
            }
        }
        block_idx++;
    }
    return -1;
}

// 把当前工作目录绝对路径写入buf, size是buf的大小
// 当buf为null时,由操作系统分配存储工作路径的空间并返回地址,失败则返回null
char* sys_getcwd(char* buf, uint32_t size){
    // 确保buf不为null, 若用户进程提供的buf为null, 系统调用getcwd中要为用户进程通过malloc分配内存
    ASSERT(buf != NULL);

    void* io_buf = sys_malloc(SECTOR_SIZE);
    if(io_buf == NULL){
        printk("sys_getcwd: malloc io_buf failed.\n");
        return NULL;
    }

    struct task_struct* cur = running_thread();
    int32_t parent_inode_nr = 0;
    int32_t child_inode_nr = cur->cwd_inode_nr;

    ASSERT(child_inode_nr >=0 && child_inode_nr < 4096);

    // 若当前目录是根目录,直接返回
    if(child_inode_nr == 0){
        buf[0] = '/';
        buf[1] = 0;
        return buf;
    }

    memset(buf, 0, size);

    char full_path_serverse[MAX_PATH_LENGTH] = {0};

    // 从下往上逐层找父目录, 直到根目录为止
    while(child_inode_nr){
        parent_inode_nr = get_parent_dir_inode_nr(child_inode_nr, io_buf);
        // 未找到名字
        if(get_child_dir_name(parent_inode_nr, child_inode_nr, full_path_serverse, io_buf) == -1){
            printk("not fond the name.\n");
            sys_free(io_buf);
            return NULL;
        }
        child_inode_nr = parent_inode_nr;
    }
    ASSERT(strlen(full_path_serverse) <= size);
    // 至此full_path_reverse中路径是反着的,即子目录在前(左)父目录在后(右). 将full_path_reverse中路径反置
    char* last_slash;
    while((last_slash = strrchr(full_path_serverse, '/'))){
        uint16_t len = strlen(buf);
        strcpy(buf+len, last_slash);
        *last_slash = 0;    // 作为边界
    }
    sys_free(io_buf);
    return buf;
}

// 更改当前工作目录绝对路径path, 成功返回0, 失败返回-1
int32_t sys_chdir(const char* path){
    int32_t ret = -1;

    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));

    int inode_no = search_file(path, &search_record);

    if(inode_no != -1){
        if(search_record.file_type == FT_DIRECTORY){
            running_thread()->cwd_inode_nr = inode_no;
            ret = 0;
        }else {
            printk("sys_chdir: %s is regular file or other.\n", path);
        }
    }
    dir_close(search_record.parent_dir);
    return ret;
}


// 在buf中填充文件结构体相关信息,成功返回0, 失败返回-1
int32_t sys_stat(const char* path, struct stat* buf){

    // 若直接看根目录 '/'
    if(!strcmp(path, "/") || !strcmp(path,"/.") || !strcmp(path, "/..")){
        buf->st_filetype = FT_DIRECTORY;
        buf->st_ino = 0;
        buf->st_size = root_dir.inode->i_size;
        return 0;
    }

    int32_t ret = -1;

    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));

    int inode_no = search_file(path, &search_record);
    if(inode_no != -1){
        struct inode* obj_inode = inode_open(cur_part, inode_no);
        // 为获得文件大小
        buf->st_size = obj_inode->i_size;
        inode_close(obj_inode);
        buf->st_filetype = search_record.file_type;
        buf->st_ino = inode_no;
        ret = 0;
    }else {
        printk("sys_stat: %s not found.\n", path);
    }
    dir_close(search_record.parent_dir);
    return ret;
}
