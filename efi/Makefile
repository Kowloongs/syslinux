## -----------------------------------------------------------------------
##
##   Copyright 2011 Intel Corporation; author: Matt Fleming
##
##   This program is free software; you can redistribute it and/or modify
##   it under the terms of the GNU General Public License as published by
##   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
##   Boston MA 02111-1307, USA; either version 2 of the License, or
##   (at your option) any later version; incorporated herein by reference.
##
## -----------------------------------------------------------------------

VPATH = $(SRC)
include $(MAKEDIR)/lib.mk
include $(MAKEDIR)/efi.mk

CC_FOR_BUILD ?= $(CC)

# Upstream gnu-efi has old-style function definitions.
CFLAGS += -Wno-strict-prototypes

CORE_CSRC := $(sort $(shell find '$(core)' -name '*.c' -print))
CORE_COBJ := $(subst $(core),$(OBJ)/../core,$(patsubst %.c,%.o,$(CORE_CSRC)))

# We don't want to include any of the networking stack or the thread
# code since it will be implemented completely differently for EFI.
FILTERED_OBJS := $(OBJ)/../core/bios/% \
		 $(patsubst $(core)/%,$(OBJ)/../core/%/%,\
			$(shell find '$(core)' -type d -name tests))

# Ubuntu's orig core keeps the BIOS console/serial/font/graphics objects in the
# core (they provide write_serial/sirq_install/loadfont/loadkeys/UsingVGA/etc.
# that ldlinux still references on EFI).  Our self-built tree shelves all of
# core/bios for EFI, which strips these exports and makes ldlinux's initial
# symbol resolution fail.  Re-add just the objects that define the strong
# symbols ldlinux imports.
CORE_OBJS = $(filter-out $(FILTERED_OBJS),$(CORE_COBJ)) \
	$(addprefix $(OBJ)/../core/bios/, \
		conio.o serirq.o graphics.o font.o call16.o)

# The $(OBJ)/../core/bios objects cannot be found through the VPATH-based
# implicit %.o:%.c rule (VPATH only resolves same-stem targets), so give them
# explicit rules pointing directly at their sources in $(core)/bios.
# core/bios is normally compiled by core/Makefile with -D__SYSLINUX_CORE__,
# which is what makes __lowmem/__bss16 (in core/include/core.h) available.
# Compiling these objects directly from efi/Makefile must set the same define,
# otherwise font.o fails on 'expected ; before char' at the __lowmem attribute.
$(OBJ)/../core/bios/%.o: $(core)/bios/%.c $(LIBEFI)
	$(CC) $(CFLAGS) -D__SYSLINUX_CORE__ -c -o $@ $<

LIB_OBJS = $(addprefix $(objdir)/com32/lib/,$(CORELIBOBJS)) \
	$(LIBEFI)

CSRC = $(sort $(wildcard $(SRC)/*.c))
OBJS = $(subst $(SRC)/,,$(filter-out %wrapper.o, $(patsubst %.c,%.o,$(CSRC))))

OBJS += $(objdir)/core/codepage.o $(ARCH)/linux.o

# The DATE is set on the make command line when building binaries for
# official release.  Otherwise, substitute a hex string that is pretty much
# guaranteed to be unique to be unique from build to build.
ifndef HEXDATE
HEXDATE := $(shell $(PERL) $(SRC)/../now.pl $(SRCS))
endif
ifndef DATE
DATE    := $(shell sh $(SRC)/../gen-id.sh $(VERSION) $(HEXDATE))
endif
CFLAGS		+= -DDATE_STR='"$(DATE)"'

$(OBJ)/$(ARCH):
	mkdir -p $@

$(OBJS): | $(OBJ)/$(ARCH)

# The targets to build in this directory
BTARGET  = syslinux.efi

syslinux.so: $(OBJS) $(CORE_OBJS) $(LIB_OBJS)
	$(LD) $(LDFLAGS) --strip-debug -o $@ $^ -lgnuefi -lefi

# We need to rename the .hash section because the EFI firmware
# linker really doesn't like it.
# $(OBJCOPY) --rename-section .gnu.hash=.sdata,load,data,alloc $^ $@
#syslinux.so: syslinux1.so
#	cp $^ $@

wrapper: wrapper.c
	$(CC_FOR_BUILD) $^ -o $@

#
# Build the wrapper app and wrap our .so to produce a .efi
syslinux.efi: syslinux.so wrapper
	$(OBJ)/wrapper syslinux.so $@

all: $(BTARGET)

codepage.o: ../codepage/cp865.cp
	cp $(objdir)/../codepage/cp865.cp codepage.cp
	$(CC) $(SFLAGS) -c -o $@ $(core)/codepage.S

install:
	install -m 755 $(BTARGET) $(INSTALLROOT)$(AUXDIR)

strip:

tidy dist:
	rm -f *.so *.o wrapper
	find . \( -name \*.o -o -name \*.a -o -name .\*.d -o -name \*.tmp \) -print0 | \
		xargs -0r rm -f
	$(topdir)/efi/clean-gnu-efi.sh $(EFI_SUBARCH) $(objdir)

clean: tidy

spotless: clean
	rm -f $(BTARGET)
