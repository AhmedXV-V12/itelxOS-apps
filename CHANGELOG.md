# Changelog

## 1.0 (2026-10-11)

The first release, for itelxOS 1.3.

- 19 XVC apps: welcome, calc, convert, base, primes, cal, timer, todo, notes,
  contacts, wc, search, tree, cipher, typing, table, quiz, hangman, ttt.
- 8 XVR apps in C: fm, edit, sysmon, sum, 2048, sokoban (11 levels checked
  with a solver), mines, life; `apps/lib/ui` for full-screen apps.
- xvc2xvr: XVC programs to XVR executables, on itelx (`xvc2xvr`) and on a PC
  (`tools/xvc2xvr.py`); `tools/xvrinfo.py` checks XVR files as the kernel does.
- The USB kit (`usb/`): `update.xvr` "itelxOS apps 1.0" installs `dex/`.
- The SDK (`sdk/`), and `make host-xvc` for XVC on a PC.
- The guide in Arabic and English (`docs/ar`, `docs/en`).
- Tests: `tests/test_tools.py`, and `tests/e2e_itelx.py` on a real itelxOS 1.3
  in QEMU (43 checks, UEFI and BIOS).
