###############################################################################
# Configure the Makefile 
###############################################################################
KERNEL_REL          = $(shell uname -r)
KERNEL_MAJOR_VER    = $(shell echo $(KERNEL_REL) | cut -f 1 -d ".")
KERNEL_MINOR_VER    = $(shell echo $(KERNEL_REL) | cut -f 2 -d ".")
KERNEL_PATCH_VER    = $(shell echo $(KERNEL_REL) | cut -f 3 -d "." | cut -f 1 -d "-" | cut -f 1 -d "_")

KERNEL_REL_GE_2_6_9	= $(shell if [ $(KERNEL_MAJOR_VER) -ge 3 ] || ( [ $(KERNEL_MAJOR_VER) -eq 2 ] && [ $(KERNEL_MINOR_VER) -eq 6 ] && [ $(KERNEL_PATCH_VER) -ge 9 ] ); then echo "1"; fi)
KERNEL_REL_GE_2_6_18  = $(shell if [ $(KERNEL_MAJOR_VER) -ge 3 ] || ( [ $(KERNEL_MAJOR_VER) -eq 2 ] && [ $(KERNEL_MINOR_VER) -eq 6 ] && [ $(KERNEL_PATCH_VER) -ge 18 ] ); then echo "1"; fi)
KERNEL_REL_GE_2_6_19  = $(shell if [ $(KERNEL_MAJOR_VER) -ge 3 ] || ( [ $(KERNEL_MAJOR_VER) -eq 2 ] && [ $(KERNEL_MINOR_VER) -eq 6 ] && [ $(KERNEL_PATCH_VER) -ge 19 ] ); then echo "1"; fi)
KERNEL_REL_GE_2_6_35  = $(shell if [ $(KERNEL_MAJOR_VER) -ge 3 ] || ( [ $(KERNEL_MAJOR_VER) -eq 2 ] && [ $(KERNEL_MINOR_VER) -eq 6 ] && [ $(KERNEL_PATCH_VER) -ge 35 ] ); then echo "1"; fi)
KERNEL_REL_GE_3_3_0 = $(shell if ( [ $(KERNEL_MAJOR_VER) -ge 3 ] && [ $(KERNEL_MINOR_VER) -ge 3 ] ); then echo "1"; fi)

VENDOR_REDHAT       = $(shell if [ -f /etc/redhat-release ]; then echo "1" ; fi)
VENDOR_SUSE         = $(shell if [ -f /etc/SuSE-release ]; then echo "1" ; fi)
VENDOR_DEBIAN       = $(shell if [ -f /etc/debian_version ] ; then echo "1"; fi)
VENDOR_GENTOO       = $(shell if [ -f /etc/gentoo-release ]; then echo "1"; fi)
VENDOR_ARCH         = $(shell if [ -f /etc/arch-release ]; then echo "1"; fi)

ifeq ($(VENDOR_DEBIAN),1)
	ARCH = $(shell uname -m | sed -e "s/i.86/i386/g" )
else
	ifeq ($(VENDOR_GENTOO),1)
		ARCH = $(shell uname -m)
	else
		ifeq ($(VENDOR_ARCH),1)
			ARCH = $(shell uname -m)
		else
			ARCH = $(shell uname -i)
		endif
	endif
endif

# fixup 32-bit x86 variants
ifeq ($(ARCH),i486)
	ARCH = i386
else
	ifeq ($(ARCH),i586)
		ARCH = i386
	else
		ifeq ($(ARCH),i686)
			ARCH = i386
		endif
	endif
endif


EXTRA_CFLAGS        += -DRRPROFILE
MKDIR               = mkdir -p
INSTALL_FILE        = cp -fp
CHK_DIR_EXISTS      = test -d
INST_DIR            = /lib/modules/$(KERNEL_REL)/extra
KERNEL_SOURCE       = /lib/modules/$(KERNEL_REL)/build
ARCH_VALID          = 0

ifeq ($(KERNEL_REL_GE_2_6_35),1)
TIMER_INT_OBJ = timer_int_2.6.35.o
else
TIMER_INT_OBJ = timer_int_2.6.9.o
endif

ifeq ($(KERNEL_REL_GE_3_3_0),1)
NMI_TIMER_INT_OBJ = nmi_timer_int_3.3.0.o
else
ifeq ($(KERNEL_REL_GE_2_6_19),1)
NMI_TIMER_INT_OBJ = nmi_timer_int_2.6.19.o
else
NMI_TIMER_INT_OBJ = nmi_timer_int_2.6.9.o
endif
endif

###############################################################################
# Driver (Generic Code) 
###############################################################################
DRIVER_OBJS := $(addprefix driver/, \
	oprof.o cpu_buffer.o buffer_sync.o \
	event_buffer.o oprofile_files.o \
	oprofilefs.o oprofile_stats.o \
	$(TIMER_INT_OBJ))

ifeq ($(KERNEL_REL_GE_2_6_19),1)
EXTRA_CFLAGS += -DHAS_IPRIVATE
else
ifeq ($(KERNEL_REL_GE_2_6_18),1)
ifeq ($(VENDOR_REDHAT),1)
EXTRA_CFLAGS += -DHAS_IPRIVATE
endif
endif
endif

###############################################################################
# x86_64 Architecture
###############################################################################
ifeq ($(ARCH),x86_64)
RRPROFILE-y := $(addprefix x86/, \
	init.o backtrace.o process.o)
RRPROFILE-$(CONFIG_X86_LOCAL_APIC) += $(addprefix x86/, \
	nmi_int.o op_model_athlon.o \
	op_model_p4.o op_model_ppro.o)
RRPROFILE-$(CONFIG_X86_IO_APIC)    += $(addprefix x86/, \
	$(NMI_TIMER_INT_OBJ))

ARCH_VALID = 1
endif

###############################################################################
# i386 Architecture
###############################################################################
ifeq ($(ARCH),i386)

RRPROFILE-y := $(addprefix x86/, \
	init.o backtrace.o process.o)
RRPROFILE-$(CONFIG_X86_LOCAL_APIC) += $(addprefix x86/, \
	nmi_int.o op_model_athlon.o \
	op_model_p4.o op_model_ppro.o)
RRPROFILE-$(CONFIG_X86_IO_APIC)    += $(addprefix x86/, \
	$(NMI_TIMER_INT_OBJ))

ARCH_VALID = 1
endif

###############################################################################
# ppc64 Architecture
###############################################################################
ifeq ($(ARCH),ppc64)

RRPROFILE-y := $(addprefix powerpc/, \
	common.o backtrace.o)
RRPROFILE-$(CONFIG_PPC64) += $(addprefix powerpc/, \
	op_model_rs64.o op_model_power4.o)

ARCH_VALID = 1
endif

###############################################################################
# ppc Architecture
###############################################################################
ifeq ($(ARCH),ppc)
RRPROFILE-y := $(addprefix powerpc/, \
	common.o backtrace.o)
RRPROFILE-$(CONFIG_FSL_BOOKE) += $(addprefix powerpc/, \
	op_model_fsl_booke.o)
RRPROFILE-$(CONFIG_PPC32) += $(addprefix powerpc/, \
	op_model_7450.o)

ARCH_VALID = 1
endif

###############################################################################
# Makefile Targets
###############################################################################

ifneq ($(ARCH_VALID),1)
all install default clean:
	$(error "Architecture $(ARCH) is unsupported. Please contact RotateRight for support options.");
else
ifneq ($(KERNEL_REL_GE_2_6_9), 1)
all install default clean:
	$(error "Unsupported kernel version $(KERNEL_MAJOR_VER).$(KERNEL_MINOR_VER).$(KERNEL_PATCH_VER) ($(KERNEL_REL)). Please contact RotateRight for support options.");
else
obj-m += rrprofile.o
rrprofile-y := $(DRIVER_OBJS) $(RRPROFILE-y)

all:
	make -C $(KERNEL_SOURCE) SUBDIRS=`pwd` modules 

default: all

install:
	$(CHK_DIR_EXISTS) "$(DESTDIR)$(INST_DIR)" || \
		$(MKDIR) "$(DESTDIR)$(INST_DIR)"
	$(INSTALL_FILE) rrprofile.ko $(DESTDIR)$(INST_DIR)

clean:
ifeq ($(ARCH),x86_64)
	rm -f x86/*.o x86/.*.cmd
endif
ifeq ($(ARCH),i386)
	rm -f x86/*.o x86/.*.cmd
endif
ifeq ($(ARCH),ppc64)
	rm -f powerpc/*.o powerpc/.*.cmd
endif
ifeq ($(ARCH),ppc)
	rm -f powerpc/*.o powerpc/.*.cmd
endif
	rm -f driver/*.o driver/.*.cmd 
	rm -f *.o *.ko .*.cmd *.mod.c 
	rm -fr .tmp_versions
	rm -f Module.symvers

endif
endif

