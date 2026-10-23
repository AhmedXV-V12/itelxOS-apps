/* The files a customizer app (or recustomize) carries, put together by
 * tools/mkczpayload.py; read-only data of the XVR (see cz.h). The build
 * passes the folder that holds this app's payload.bin with -Wa,-I. */
    .section .rodata
    .balign 16
    .globl cz_payload
    .globl cz_payload_end
cz_payload:
    .incbin "payload.bin"
cz_payload_end:
    .section .note.GNU-stack, "", @progbits
