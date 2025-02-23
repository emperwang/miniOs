
[bits 32]
section .text
global switch_to
switch_to:
    ; 栈中此处是返回地址
    push esi
    push edi
    push ebx
    push ebp

    mov eax, [esp + 20] ; 得到栈中参数cur,cur=[esp + 20]
    mov [eax], esp  ; 保存栈顶指针esp,task_struct的self_kstack字段.self_kstack在task_struct偏移为0
                    ; 所以直接往thread开头处存4字节

    ; 恢复下一个线程的环境
    mov eax, [esp+24]   ; 得到栈中参数next
    mov esp, [eax]      ; pcb的第一个成员是self_kstack成员
                        ; 它用来记录0级栈顶指针, 被换上cpu时用来恢复0级栈
                        ; 0级栈中保存进程或线程所有信息, 包括3级指针

    pop ebp
    pop ebx
    pop edi
    pop esi
    ret                 ; 返回上面swith_to下面那句注释的返回地址
                        ; 未由中断进入, 第一次执行时会返回到 kernel_thread



