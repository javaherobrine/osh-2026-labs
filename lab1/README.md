# Lab 1

## Part 1

### Steps

Initially, the size of bzImage is 14.1MiB

- Remove network support & cryptographic algorithms (14.1MiB->9.9MiB, it seems that networking(like TCP/IP Protocol Stack & TLS) is a huge part of Linux Kernel)
- Remove language support in File System (only remaining ASCII, 9.9MiB->9.9MiB, it seems that it doesn't matter much)
- Disable virtualization(9.9MiB->9.9MiB, oh no...)
- Disable USB & Sound(Alsa)(9.9MiB->9.2MiB)
- Remove support for non-PC x86 platforms(9.2MiB->9.2MiB, it doesn't matter too)
- Remove DRM & AGP(9.2MiB->7.2MiB, graphics is also a problem)
- Remove x86 specific drviers(7.2MiB->7.2MiB)
- Disable multiple security options(7.2MiB->7.2MiB)
- Remove MSDOS FS & VFAT FS(Windows 95) support(7.2MiB->7.1MiB, hmm...)
- Disable loadable module support(7.1MiB->6.9MiB, great)
- Remove IA32 emulation(6.9MiB->6.8MiB, maybe it's easy)
- Remove all FS support except basic support for ext4(6.8MiB->6.8MiB, it makes almost no sense)
- Disable all mitigations for CPU vulerablities(6.8MiB->6.0MiB, it makes sense)
- Remove RAID & LVM Support(6.0MiB->5.9MiB,...)
- Remove support to block devices(5.9MiB->5.9MiB)
- Disable PC-cards((5.9MiB->5.8MiB)
- Disable DMA & HID(5.8MiB->5.7MiB)
- Disable multiple drivers(5.7MiB->5.3MiB)
- Disable all drivers(5.3MiB->5.0MiB)
- Use simple memory management(disable pages & other)(5.0MiB->4.8MiB)

# Conclusion

- disable network & cryptographic algorithms can significantly reduce the size of the kernel
- disable loadable modules support can reduce its size
- remove useless drivers can significantly reduce the size of the kernel
- disable mitigations of CPU vulerablities can also reduce the size of the kernel
- other options can only reduce the size of the kernel slightly

## Part 2

I tried to remove `while(1);` at the end of `init.c`, only receiving this message:
> \[end kernel panic - not syncing: Attempted to kill init! exit code=0x00000000 \]
I know that `kernel panic` will happen in case of critical errors or when function `panic()` or macro `BUG` is called.

Why kernel panic? Process `init` is the base process of the other processes. It's a very important process for Linux. Killing it will result in fatal errors. When I missed `while(1);`, this process terminates when having printed the message.
Linux noticed that process `init` is terminated unexpectedly, but it can't recover from it automatically. As a result, a `kernel panic` happens.

## Part 3

There are 2 ways to trigger a syscall with `Assembly`, `int 0x80` or `syscall`. I chose `syscall` with 64bit registers. Although `libc` has wrapped it as function `syscall`, but I'll stick to `Assembly` in case of `syscall`
