# The customizer apps (customizers/README.md), included by the Makefile.
#
#   make customizers    every customizer app and recustomize, built and
#                       copied to customizers/release/ (.xvr and .txt)
#
# customizers/<id>/payload.txt says what a customizer installs (see
# tools/mkczpayload.py); every folder customizers/<id>/apps/<app> is one
# program, built to build/cz/apps/<app>.xvr with the kit (customizers/kit).
# The kernels the customizers carry are built from the itelx sources by
# customizers/kernel/build-kernel.sh and kept in customizers/kernel/out.

CZ      := customizers
CZB     := $(B)/cz
CZVER   := 1.0
CZFLAGS := $(XCFLAGS) -I$(CZ)/common -I$(CZ)/kit -I$(CZ)/common/term -I$(CZ)/common/net -I$(CZ)/common/vault
LIBGCC  := $(shell $(CC) -print-libgcc-file-name)

# shared parts an app takes when its folder has a file "uses" naming them:
# term (the itelx terminal with hooks), net (libnet), vault (File saver),
# sqlite (SQLite with its itelx file system), db (data_place's engines)
$(foreach m,term net vault sqlite db doc vec,$(eval CZ_MOD_$(m) := $(patsubst %.c,$(B)/%.o,$(wildcard $(CZ)/common/$(m)/*.c))))
cz_uses = $(foreach m,$(shell cat $(1)/uses 2>/dev/null),$(CZ_MOD_$(m)))

# SQLite (third_party/sqlite, public domain), built for itelx: its own
# file system (common/sqlite/itxvfs.c), one thread, no shared memory files,
# memory from one block (MEMSYS5), math functions, full-text search (FTS5)
SQLITE_DEFS := -DSQLITE_OS_OTHER=1 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION=1 -DSQLITE_OMIT_WAL=1 \
               -DSQLITE_TEMP_STORE=3 -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_LOCALTIME=1 -DSQLITE_OMIT_DEPRECATED=1 \
               -DSQLITE_OMIT_SHARED_CACHE=1 -DSQLITE_DQS=0 -DSQLITE_MAX_MMAP_SIZE=0 -DSQLITE_ENABLE_MEMSYS5=1 \
               -DSQLITE_ENABLE_MATH_FUNCTIONS=1 -DSQLITE_ENABLE_FTS5=1 -DSQLITE_ENABLE_DBSTAT_VTAB=1 -DNDEBUG=1
SQLITE_CFLAGS := -std=gnu11 -O2 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin \
                 -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -w \
                 -I$(CZ)/common/sqlite/shim $(SQLITE_DEFS)
$(B)/$(CZ)/third_party/sqlite/sqlite3.o: $(CZ)/third_party/sqlite/sqlite3.c $(wildcard $(CZ)/common/sqlite/shim/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(SQLITE_CFLAGS) -c $< -o $@
CZ_MOD_sqlite += $(B)/$(CZ)/third_party/sqlite/sqlite3.o
CZFLAGS += -I$(CZ)/third_party/sqlite -I$(CZ)/common/sqlite -I$(CZ)/common/db -I$(CZ)/common/doc -I$(CZ)/common/vec

CZ_TITLE_noter       := Noter
CZ_TITLE_school_mode := School mode
CZ_TITLE_work_mode   := Work mode
CZ_TITLE_file_saver  := File saver
CZ_TITLE_data_place  := data_place

CZ_IDS  := $(sort $(notdir $(patsubst %/payload.txt,%,$(wildcard $(CZ)/*/payload.txt))))
CZ_KIT  := $(patsubst %.c,$(B)/%.o,$(wildcard $(CZ)/kit/*.c))
CZ_COM  := $(B)/$(CZ)/common/cz.o

$(B)/$(CZ)/%.o: $(CZ)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CZFLAGS) -c $< -o $@

# ------------------------------------------------- the programs (apps)
CZ_APPDIRS := $(sort $(patsubst %/,%,$(dir $(wildcard $(CZ)/*/apps/*/*.c))))
CZ_APPS    := $(foreach d,$(CZ_APPDIRS),$(CZB)/apps/$(notdir $(d)).xvr)

# extra xvrlink options of an app: customizers/<id>/apps/<app>/xvrlink.opt
define cz_app_rule
$(CZB)/apps/$(notdir $(1)).elf: $(patsubst %.c,$(B)/%.o,$(wildcard $(1)/*.c)) $(call cz_uses,$(1)) $(CZ_KIT) $(LIBITX) sdk/system/lib/xvr.ld
	@mkdir -p $$(dir $$@)
	$(LD) -T sdk/system/lib/xvr.ld $(LDFLAGS) -o $$@ $(LIBITX) $(patsubst %.c,$(B)/%.o,$(wildcard $(1)/*.c)) $(call cz_uses,$(1)) $(CZ_KIT) $(LIBGCC)
$(CZB)/apps/$(notdir $(1)).xvr: $(CZB)/apps/$(notdir $(1)).elf $(XVRLINK)
	$(XVRLINK) --name $(notdir $(1)) $$(shell cat $(1)/xvrlink.opt 2>/dev/null) $$< $$@ >/dev/null
endef
$(foreach d,$(CZ_APPDIRS),$(eval $(call cz_app_rule,$(d))))

# --------------------------------------------------------- recustomize
$(CZB)/recustomize/payload.bin: $(CZ)/tools/mkfactory.py $(CZ)/tools/mkczpayload.py $(shell find $(CZ)/factory -type f 2>/dev/null)
	@mkdir -p $(dir $@)
	$(PY) $(CZ)/tools/mkfactory.py $(CZ)/factory/itelx-1.4 $@ $(CZVER) >/dev/null
$(CZB)/%/payload.o: $(CZ)/common/payload.S $(CZB)/%/payload.bin
	$(CC) $(XCFLAGS) -Wa,-I$(CZB)/$* -c $< -o $@
$(CZB)/recustomize.elf: $(B)/$(CZ)/common/recustomize.o $(CZ_COM) $(CZB)/recustomize/payload.o $(LIBITX)
	$(LD) -T sdk/system/lib/xvr.ld $(LDFLAGS) -o $@ $(LIBITX) $(B)/$(CZ)/common/recustomize.o $(CZ_COM) $(CZB)/recustomize/payload.o
$(CZB)/recustomize.xvr: $(CZB)/recustomize.elf $(XVRLINK)
	$(XVRLINK) --name "recustomize $(CZVER)" --update $< $@ >/dev/null

# ----------------------------------------------------- the customizers
define cz_rule
$(CZB)/$(1)/payload.bin: $(CZ)/$(1)/payload.txt $(wildcard $(CZ)/$(1)/*.txt) $(CZ_APPS) $(CZB)/recustomize.xvr $(XVR_OUT) \
                         $(CZ)/tools/mkczpayload.py $(shell find $(CZ)/kernel/out -type f 2>/dev/null)
	@mkdir -p $$(dir $$@)
	CZ_BUILD=$(B) $(PY) $(CZ)/tools/mkczpayload.py $$@ $(1) "$(CZ_TITLE_$(1))" $(CZVER) @$(CZ)/$(1)/payload.txt >/dev/null
$(CZB)/$(1).elf: $(B)/$(CZ)/common/cz_install.o $(CZ_COM) $(CZB)/$(1)/payload.o $(LIBITX)
	$(LD) -T sdk/system/lib/xvr.ld $(LDFLAGS) -o $$@ $(LIBITX) $(B)/$(CZ)/common/cz_install.o $(CZ_COM) $(CZB)/$(1)/payload.o
$(CZB)/$(1).xvr: $(CZB)/$(1).elf $(XVRLINK)
	$(XVRLINK) --name "$(CZ_TITLE_$(1)) $(CZVER) (customizer)" --update $$< $$@ >/dev/null
endef
$(foreach i,$(CZ_IDS),$(eval $(call cz_rule,$(i))))

# --------------------------------------------------------- the release
CZ_REL := $(CZ)/release
customizers: $(foreach i,$(CZ_IDS) recustomize,$(CZB)/$(i).xvr) $(CZ)/tools/mkcztxt.py
	@mkdir -p $(CZ_REL)
	rm -f $(CZ_REL)/*.xvr $(CZ_REL)/*.txt
	for i in $(CZ_IDS) recustomize; do \
	    cp $(CZB)/$$i.xvr $(CZ_REL)/ && \
	    $(PY) $(CZ)/tools/mkcztxt.py $(CZB)/$$i/payload.bin $(CZ_REL)/$$i.xvr > $(CZ_REL)/$$i.txt || exit 1; \
	done
	cd $(CZ_REL) && sha256sum *.xvr *.txt > SHA256SUMS
	@echo "$(CZ_REL)/ is ready: $(CZ_IDS) recustomize"

.PHONY: customizers
