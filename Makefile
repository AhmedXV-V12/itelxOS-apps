# itelxOS apps: builds xvc2xvr, the XVR apps written in C and the USB kit.
#
# Needs Linux (or WSL) with gcc, ld (binutils) and python3. Nothing else:
# every program is linked with libitx only (sdk/system/lib), the library
# itelxOS's own programs are built with.
#
#   make             everything, then copies the results into usb/
#   make host-xvc    build/host/xvc: the XVC interpreter for this PC, to try
#                    XVC programs before they go to itelx
#   make check       tests of the PC tools (xvc2xvr.py, xvrinfo.py)
#   make customizers the customizer apps for itelxOS 1.4 -> customizers/release
#                    (customizers/customizers.mk, customizers/README.md)
#   make clean
#
# make cannot work in a path with spaces: clone the repository into a
# folder without spaces (or reach it through a symlink).

B  ?= build
CC := gcc
LD := ld
PY := python3

# the flags itelxOS 1.3 builds its own programs with
XCFLAGS := -std=gnu11 -O2 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables \
           -Wall -Wextra -Wno-unused-parameter -Isdk/system/lib/include -Iapps/lib -MMD -MP
HOSTCFLAGS := -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS := -nostdlib -static -z noexecstack

LIBITX  := $(B)/sdk/system/lib/crt0.o $(B)/sdk/system/lib/libitx.o
XVRLINK := $(B)/xvrlink

all: usb

# ------------------------------------------------------------ the tools
$(XVRLINK): sdk/tools/xvrlink.c sdk/xvr/xvr_format.h
	@mkdir -p $(dir $@)
	$(CC) $(HOSTCFLAGS) -o $@ $<

$(B)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(XCFLAGS) -c $< -o $@
$(B)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(XCFLAGS) -c $< -o $@

# ---------------------------------------------- xvc2xvr (and its payload)
XVCAPP_OBJS := $(patsubst %.c,$(B)/%.o,sdk/system/xvc/xvc_compile.c sdk/system/xvc/xvc_vm.c \
                 sdk/system/xvc/xvc_lib.c sdk/xvcapp/xvcapp_main.c)
$(B)/xvr/xvc2xvr.elf: $(XVCAPP_OBJS) $(LIBITX) sdk/xvcapp/xvcapp.ld
	@mkdir -p $(dir $@)
	$(LD) -T sdk/xvcapp/xvcapp.ld $(LDFLAGS) -o $@ $(LIBITX) $(XVCAPP_OBJS)
$(B)/xvr/xvc2xvr.xvr: $(B)/xvr/xvc2xvr.elf $(XVRLINK)
	$(XVRLINK) --name xvc2xvr $< $@ >/dev/null

# ------------------------------------- the apps in C: apps/xvr/NAME/*.c
XVR_APPS := $(sort $(notdir $(patsubst %/,%,$(dir $(wildcard apps/xvr/*/*.c)))))
APPLIB   := $(patsubst %.c,$(B)/%.o,$(wildcard apps/lib/*.c))

define app_rule
$(B)/xvr/$(1).elf: $(patsubst %.c,$(B)/%.o,$(wildcard apps/xvr/$(1)/*.c)) $(APPLIB) $(LIBITX) sdk/system/lib/xvr.ld
	@mkdir -p $$(dir $$@)
	$(LD) -T sdk/system/lib/xvr.ld $(LDFLAGS) -o $$@ $(LIBITX) $(APPLIB) $(patsubst %.c,$(B)/%.o,$(wildcard apps/xvr/$(1)/*.c))
$(B)/xvr/$(1).xvr: $(B)/xvr/$(1).elf $(XVRLINK)
	$(XVRLINK) --name $(1) $$< $$@ >/dev/null
endef
$(foreach a,$(XVR_APPS),$(eval $(call app_rule,$(a))))

# --------------------------------------------- the USB kit's installer
$(B)/kit/update.elf: $(B)/kit/update.o $(LIBITX) sdk/system/lib/xvr.ld
	$(LD) -T sdk/system/lib/xvr.ld $(LDFLAGS) -o $@ $(LIBITX) $(B)/kit/update.o
$(B)/kit/update.xvr: $(B)/kit/update.elf $(XVRLINK)
	$(XVRLINK) --name "itelxOS apps 1.0" --update $< $@ >/dev/null

# ------------------------------------------------------------- usb/
XVR_OUT := $(B)/xvr/xvc2xvr.xvr $(foreach a,$(XVR_APPS),$(B)/xvr/$(a).xvr)
usb: $(XVR_OUT) $(B)/kit/update.xvr $(wildcard apps/xvc/*.xvc)
	@mkdir -p usb/dex
	rm -f usb/dex/*.xvr usb/dex/*.xvc usb/update.xvr
	cp apps/xvc/*.xvc usb/dex/
	cp $(XVR_OUT) usb/dex/
	cp $(B)/kit/update.xvr usb/
	cd usb && sha256sum update.xvr dex/* > SHA256SUMS
	@echo "usb/ is ready: copy what is in it to the top of a FAT32 USB stick"

# ------------------------------------------------- XVC on this PC
host-xvc: $(B)/host/xvc
$(B)/host/xvc: tools/host-xvc/host.c tools/host-xvc/itx.h $(wildcard sdk/system/xvc/*.[ch])
	@mkdir -p $(dir $@)
	$(CC) -O2 -Wall -Wno-unused-parameter -Wno-unused-result -Itools/host-xvc -o $@ \
	    sdk/system/xvc/xvc_compile.c sdk/system/xvc/xvc_vm.c sdk/system/xvc/xvc_lib.c \
	    sdk/system/xvc/xvc_main.c tools/host-xvc/host.c

check: usb host-xvc
	$(PY) tests/test_tools.py $(B)/host/xvc

clean:
	rm -rf $(B)

# ------------------------------------------------- the customizer apps
include customizers/customizers.mk

.PHONY: all usb host-xvc check clean
-include $(shell find $(B) -name '*.d' 2>/dev/null)
