/*
 * Copyright 2011-2014 Intel Corporation - All Rights Reserved
 */

#include <string.h>
#include <fs.h>
#include <ilog2.h>
#include <disk.h>
#include <dprintf.h>
#include "efi.h"

#define ISO_SECTOR_SIZE 2048

static int efi_rdwr_sectors(struct disk *disk, void *buf,
			    sector_t lba, size_t count, bool is_write)
{
	struct efi_disk_private *priv = (struct efi_disk_private *)disk->private;
	EFI_DISK_IO *dio = priv->dio;
	EFI_STATUS status;
	UINTN bytes = count * disk->sector_size;
	UINTN offset = lba * disk->sector_size;

	/*
	 * Use the byte-granular Disk I/O protocol instead of the block
	 * I/O protocol.  Some devices (e.g. the virtual disk that EFI
	 * creates for an El Torito CD-ROM boot image) report a block
	 * size larger than the FAT sector size (2048 vs 512), which
	 * would make a single-sector read overflow the caller's buffer
	 * and corrupt the stack.
	 */
	if (is_write)
		status = uefi_call_wrapper(dio->WriteDisk, 5, dio,
					   disk->disk_number, offset, bytes, buf);
	else
		status = uefi_call_wrapper(dio->ReadDisk, 5, dio,
					   disk->disk_number, offset, bytes, buf);

	if (status != EFI_SUCCESS)
		Print(L"Failed to %s blocks: 0x%x\n",
			is_write ? L"write" : L"read",
			status);

	return count << disk->sector_shift;
}

struct disk *efi_disk_init(void *private)
{
    static struct disk disk;
    struct efi_disk_private *priv = (struct efi_disk_private *)private;
    EFI_HANDLE handle = priv->dev_handle;
    EFI_BLOCK_IO *bio;
    EFI_DISK_IO *dio;
    EFI_STATUS status;

    status = uefi_call_wrapper(BS->HandleProtocol, 3, handle,
			       &DiskIoProtocol, (void **)&dio);
    if (status != EFI_SUCCESS)
	    return NULL;

    status = uefi_call_wrapper(BS->HandleProtocol, 3, handle,
			       &BlockIoProtocol, (void **)&bio);
    if (status != EFI_SUCCESS)
	    return NULL;

    /*
     * XXX Do we need to map this to a BIOS disk number?
     */
    disk.disk_number   = bio->Media->MediaId;

    /*
     * The FAT filesystems used by the boot image use 512-byte
     * sectors.  Some devices (e.g. an El Torito CD-ROM boot image)
     * report a different block size (2048 for CD), which would make
     * every single-sector read overflow the caller's buffer.  Always
     * address the media in 512-byte sectors and read through the
     * byte-granular Disk I/O protocol.
     */
    disk.sector_size   = 512;
    disk.rdwr_sectors  = efi_rdwr_sectors;
    disk.sector_shift  = ilog2(disk.sector_size);

    dprintf("sector_size=%d, disk_number=%d\n", disk.sector_size,
	    disk.disk_number);

    priv->bio = bio;
    priv->dio = dio;
    disk.private = private;
#if 0

    disk.part_start    = part_start;
    disk.secpercyl     = disk.h * disk.s;


    disk.maxtransfer   = MaxTransfer;

    dprintf("disk %02x cdrom %d type %d sector %u/%u offset %llu limit %u\n",
	    media_id, cdrom, ebios, sector_size, disk.sector_shift,
	    part_start, disk.maxtransfer);
#endif

    return &disk;
}

/*
 * Return true if the device path "prefix" is a strict prefix of the
 * device path "full".  When the firmware builds a virtual disk for an
 * El Torito boot image, the device path of that disk is the CD-ROM's
 * own device path with a CD-ROM boot entry node appended, so walking
 * the optical device from the boot image's handle down a level yields
 * the underlying CD-ROM.
 */
static bool efi_device_path_prefix(EFI_DEVICE_PATH *prefix,
				   EFI_DEVICE_PATH *full)
{
	while (!IsDevicePathEnd(prefix)) {
		if (IsDevicePathEnd(full))
			return false;

		if (DevicePathType(prefix) != DevicePathType(full) ||
		    DevicePathSubType(prefix) != DevicePathSubType(full) ||
		    DevicePathNodeLength(prefix) != DevicePathNodeLength(full))
			return false;

		if (memcmp((char *)prefix + sizeof(EFI_DEVICE_PATH),
			   (char *)full + sizeof(EFI_DEVICE_PATH),
			   DevicePathNodeLength(prefix) - sizeof(EFI_DEVICE_PATH)))
			return false;

		prefix = NextDevicePathNode(prefix);
		full = NextDevicePathNode(full);
	}

	/* The prefix must terminate strictly before the full path */
	return !IsDevicePathEnd(full);
}

/*
 * Check whether a single handle carries the ISO9660 primary volume
 * descriptor of an optical medium.  The PVD always sits at LBA 16 of
 * the volume and starts with the magic "CD001" at byte offset 1.
 *
 * The probe goes through efi_disk_init()/rdwr_sectors() so that it
 * shares the exact same block I/O path the filesystem layer will later
 * use, including the El Torito-safe 512-byte sector addressing.
 */
static bool efi_probe_iso9660(EFI_HANDLE handle)
{
	struct efi_disk_private probe;
	EFI_BLOCK_IO *bio;
	struct disk *disk;
	char buf[ISO_SECTOR_SIZE];
	EFI_STATUS status;

	status = uefi_call_wrapper(BS->HandleProtocol, 3, handle,
				   &BlockIoProtocol, (void **)&bio);
	if (EFI_ERROR(status) || !bio || !bio->Media)
		return false;

	/* Leave partitions and non-removable media (disks, USB sticks) alone */
	if (bio->Media->LogicalPartition || !bio->Media->RemovableMedia)
		return false;

	/* ISO9660 blocks are 2048 bytes; anything larger does not fit the probe */
	if (!bio->Media->BlockSize || bio->Media->BlockSize > sizeof(buf))
		return false;

	memset(&probe, 0, sizeof(probe));
	probe.dev_handle = handle;
	disk = efi_disk_init(&probe);
	if (!disk)
		return false;

	disk->rdwr_sectors(disk, buf, 16 * (sizeof(buf) / disk->sector_size),
			   sizeof(buf) / disk->sector_size, false);
	return memcmp(buf + 1, "CD001", 5) == 0;
}

/*
 * Locate the CD-ROM whose ISO9660 volume backs an El Torito boot.
 *
 * The firmware only hands the bootloader the virtual FAT image that
 * holds bootx64.efi; everything else (kernel, modules, configuration)
 * lives on the ISO9660 filesystem of the optical media.  First prefer
 * the handle whose device path is the parent of the loaded boot image
 * (the CD-ROM itself on well-behaved firmware), then fall back to
 * scanning the removable Block I/O handles for the "CD001" signature.
 *
 * On success priv->dev_handle refers to the optical device.
 */
bool efi_cdrom_probe(struct efi_disk_private *priv, EFI_HANDLE boot_handle)
{
	EFI_DEVICE_PATH *boot_dp, *dp;
	EFI_HANDLE *handles = NULL;
	EFI_HANDLE preferred = NULL, fallback = NULL;
	UINTN nr_handles = 0, i;
	EFI_STATUS status;

	boot_dp = DevicePathFromHandle(boot_handle);

	status = LibLocateHandle(ByProtocol, &BlockIoProtocol, NULL,
				 &nr_handles, &handles);
	if (EFI_ERROR(status) || !nr_handles) {
		dprintf("efi_cdrom_probe: no Block I/O handles\n");
		return false;
	}

	dprintf("efi_cdrom_probe: %d Block I/O handle(s)\n", nr_handles);

	for (i = 0; i < nr_handles; i++) {
		if (!efi_probe_iso9660(handles[i]))
			continue;

		if (boot_dp) {
			dp = DevicePathFromHandle(handles[i]);
			if (dp && efi_device_path_prefix(dp, boot_dp)) {
				preferred = handles[i];
				dprintf("efi_cdrom_probe: CD parent of boot "
					"image matches at handle %p\n",
					handles[i]);
				break;
			}
		}

		if (!fallback) {
			fallback = handles[i];
			dprintf("efi_cdrom_probe: ISO9660 candidate at "
				"handle %p\n", handles[i]);
		}
	}

	if (preferred || fallback) {
		priv->dev_handle = preferred ? preferred : fallback;
		FreePool(handles);
		return true;
	}

	FreePool(handles);
	dprintf("efi_cdrom_probe: no ISO9660 volume found\n");
	return false;
}
