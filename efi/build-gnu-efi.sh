#!/bin/sh

set -e

# Initialise the gnu-efi submodule and ensure the source is up-to-date.
# Then build and install it for the given architecture.

if [ $# -lt 2 ]; then
cat <<EOF
Usage: $0: <arch> <objdir>

Build the <arch> gnu-efi libs and header files and install in <objdir>.

  <arch>   - A gnu-efi \$ARCH argument, i.e. ia32, x86_64
  <objdir> - The Syslinux object directory

EOF
    exit 1
fi

ARCH="$1"
objdir="$(readlink -f $2)"

if [ ! -e ../version.h ]; then
    printf "build-gnu-efi.sh: Cannot be run outside Syslinux object tree\n"
    pwd
    exit 1
fi

(
	cd ../..
	if [ -d .git ]; then
	    git submodule update --init
	fi
)

mkdir -p "$objdir/gnu-efi"
cd "$objdir/gnu-efi"

EFIDIR="$(readlink -f "$objdir/../gnu-efi")"

# gnu-efi 3.0.x took SRCDIR on the make command line, but gnu-efi 4.0.x
# re-derives SRCDIR from VPATH inside each recursive sub-make.  Passing
# SRCDIR as a command-line variable would leak into the sub-makes and
# override that derivation, breaking the build.  TOPDIR must point at the
# source tree because include paths use $(TOPDIR)/inc.
#
# Only build and install the parts Syslinux needs (lib/inc/gnuefi).  The
# "apps" target fails to link (hidden CopyMem symbol) and is not
# required, so it is excluded: the build uses explicit make goals (the
# prereqs of `all` are frozen at parse time, so a SUBDIRS override would
# not help there), while install's recipe expands $(SUBDIRS) at run time,
# so the command-line SUBDIRS is safe there (the sub-installs never read
# SUBDIRS).
make TOPDIR="$EFIDIR" -f "$EFIDIR/Makefile" ARCH=$ARCH lib inc gnuefi
make TOPDIR="$EFIDIR" SUBDIRS="lib inc gnuefi" -f "$EFIDIR/Makefile" ARCH=$ARCH PREFIX="$objdir" install

# The build generates its objects under $(TOPDIR)/$(ARCH) inside the
# source tree and lib/ms_va_print.c from va_print.c.  Remove those so the
# submodule working tree stays clean.
rm -rf "$EFIDIR/$ARCH"
rm -f "$EFIDIR/lib/ms_va_print.c"

cd "$objdir/efi"
