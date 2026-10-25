# System tools for itelxOS 2.0

| file | what it does |
|---|---|
| `formater.xvr` | wipes the system disk, all but `/kernel` and `/boot` (`/media` and `/ram` are never touched) |
| `recovery_tester.xvr` | makes a fault on purpose in `/system/xvinit.xvr`, checks that the red recovery screen repairs it, then puts the good copy (kept in `/users/recovery_tester`) back; when the repair fails it writes `report-ar.txt` and `report-en.txt` with the logs |

They need itelxOS 2.0 (its red recovery screen and backup system): the
kernel of itelx 1.x refuses them. Both ask y/n first. They are not part of
the USB kit in `usb/`.

Copy them onto a FAT32 stick and run them from it:

```
itelx:/> /media/STICK/formater.xvr
itelx:/> /media/STICK/recovery_tester.xvr
itelx:/> /media/STICK/recovery_tester.xvr cancel
```

Check the files: `sum -c SHA256SUMS` on itelx, or `sha256sum -c SHA256SUMS`.

Source: [`apps/xvr/formater`](../apps/xvr/formater),
[`apps/xvr/recovery_tester`](../apps/xvr/recovery_tester). Built with
`make system-tools`. The guide: [English](../docs/en/7-system-tools.md),
[Arabic](../docs/ar/7-system-tools.md).
