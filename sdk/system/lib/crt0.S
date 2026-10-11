/* Program entry. The kernel starts a program with RSP pointing at argc,
 * followed by argv[] and a NULL, and also puts argc in RDI and argv in RSI. */
    .section .text.start, "ax"
    .global _start
_start:
    xor     %rbp, %rbp
    and     $-16, %rsp
    mov     %rdi, %r12
    mov     %rsi, %r13
    call    __libitx_init
    mov     %r12, %rdi
    mov     %r13, %rsi
    call    main
    mov     %eax, %edi
    call    exit
1:  jmp     1b
