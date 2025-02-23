#include "stdio.h"
#include "global.h"
#include "string.h"
#include "syscall.h"

// 将整型转换为字符(integer to ascii)
static void itoa(uint32_t value, char** buf_ptr_addr,  uint8_t base){
    uint32_t m = value % base;
    uint32_t i = value / base;

    if(i) {
        itoa(i, buf_ptr_addr, base);
    }
    if(m < 10){
        *((*buf_ptr_addr)++) = m + '0'; //数字0-9转换为字符0-9
    }else {
        *((*buf_ptr_addr)++) = m - 10 + 'A';  // 数字A-F 转换为字符 A-F
    }
}

// 将参数ap按照格式format 输出到字符串str,并返回替换后str长度
uint32_t vsprintf(char* str, const char* format, va_list ap){
    char* buf_ptr = str;
    const char* index_ptr = format;
    char index_char = *index_ptr;
    int32_t arg_int;
    char* arg_str;
    while(index_char){
        if(index_char != '%'){
            *(buf_ptr++) = index_char;
            index_char = *(++index_ptr);
            continue;
        }
        index_char = *(++index_ptr);        // 得到%后面的额字符
        switch(index_char){
            case 's':
                arg_str = va_arg(ap, char*);
                strcpy(buf_ptr, arg_str);
                buf_ptr += strlen(arg_str);
                index_char = *(++index_ptr);
                break;
            case 'c':
                *(buf_ptr++) = va_arg(ap,char);
                index_char = *(++index_ptr);
                break;
            case 'd':
                arg_int = va_arg(ap,int);
                if(arg_int < 0){
                    arg_int = 0-arg_int;
                    *buf_ptr = '-';
                    buf_ptr++;
                }
                itoa(arg_int, &buf_ptr, 10);
                index_char = *(++index_ptr);
                break;
            case 'x':
            arg_int = va_arg(ap, int);
            itoa(arg_int, &buf_ptr, 16);
            index_char = *(++index_ptr);
            break;
        }
    }
    return strlen(str);
}

uint32_t sprintf(char* buf, const char*format, ...){
    va_list args;
    uint32_t retval;
    va_start(args, format);
    retval = vsprintf(buf, format, args);
    va_end(args);
    return retval;
}

//格式化输出字符串
uint32_t printf(const char* format, ...){
    va_list args;
    va_start(args, format);
    char buf[1024] = {0};
    vsprintf(buf, format, args);
    va_end(args);
    return write(1, buf, strlen(buf));
}

