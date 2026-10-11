# The itelxOS SDK

What a program for itelxOS 1.3 is built with. Everything here except
`xvcapp/` is copied unchanged from the itelxOS userspace (MPL-2.0).

| path | |
|---|---|
| `system/lib/include/itx.h` | the library's interface: system calls, memory, strings, output, files |
| `system/lib/libitx.c`, `crt0.S` | libitx and the start of a program |
| `system/lib/xvr.ld` | the link layout of an XVR program |
| `abi/itx_abi.h` | system call numbers, error codes, the structures shared with the kernel |
| `xvr/xvr_format.h` | the XVR executable format |
| `tools/xvrlink.c` | ELF to XVR (`xvrlink [--name N] [--stack B] [--heap B] [--update] in.elf out.xvr`) |
| `system/xvc/` | the XVC interpreter: compiler (`xvc_compile.c`), virtual machine (`xvc_vm.c`), built-in functions (`xvc_lib.c`), `xvc_main.c` is `/system/xvc.xvr` |
| `xvcapp/` | new here: the XVC interpreter with a payload segment for one program; with an empty payload it is `xvc2xvr` |

How to use it: [docs/en/4-xvr-in-c.md](../docs/en/4-xvr-in-c.md)
([بالعربي](../docs/ar/4-xvr-in-c.md)); the `Makefile` at the top builds
everything.
