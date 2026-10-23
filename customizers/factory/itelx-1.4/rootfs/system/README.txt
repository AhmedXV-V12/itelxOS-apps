itelx 1.4

/kernel   the kernel (itelx.elf) and its version
/boot     what the boot path uses: IFelxBoot (BOOTX64.EFI), the itelx UEFI
          entry carrying the kernel (vmlinuz), the boot bundle (initrd.img),
          the command line, the BIOS boot code (mbr.bin, stage2.bin)
/system   the system: init, the terminal, top, the XVC interpreter, licences
/dex      applications: XVR executables and XVC programs
/ram      the physical RAM of the machine, read only (provided by the kernel)
/media    what is plugged in: USB keyboards, mice and other devices as empty
          files named by kind and id (keyboard-1, mouse-2), and storage
          volumes as directories: USB sticks and disks (FAT32, FAT16, FAT12)
          and the FAT partitions of this machine's disks, read and write.
          The system disk's EFI partition is there read only to programs;
          only an update you accepted writes it.

What the system is and does:
    sys                                the system: version, start, disks, owner
    sys.info                           processor, memory and graphics use
    sys.info.device                    the hardware, processor to graphics
    sys.all                            all three
    sys.top                            every process, live (q quits)

Put your own programs in /dex. A file /system/startup.xvc, when present, is
run at every start.

Programs from outside: put .xvr executables and .xvc programs on a USB
stick, then in the terminal
    ls /media                          the stick is a directory there
    /media/STICK/app.xvr               run it from the stick
    install /media/STICK/app.xvr       or put it into /dex: then "app" runs it
    eject /media/STICK                 before you unplug the stick
copy, move, del (-r for directories) work on the stick like on any disk.
A stick with an update program (update.xvr) at its top is offered as an
update when it is plugged in ("are you need update ?").
