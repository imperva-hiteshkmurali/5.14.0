/*
 *  Copyright (C) 2015       Red Hat Inc.
 *                           Hans de Goede <hdegoede@redhat.com>
 *  Copyright (C) 2008       SuSE Linux Products GmbH
 *                           Thomas Renninger <trenn@suse.de>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *
 * video_detect.c:
 * After PCI devices are glued with ACPI devices
 * acpi_get_pci_dev() can be called to identify ACPI graphics
 * devices for which a real graphics card is plugged in
 *
 * Depending on whether ACPI graphics extensions (cmp. ACPI spec Appendix B)
 * are available, video.ko should be used to handle the device.
 *
 * Otherwise vendor specific drivers like thinkpad_acpi, asus-laptop,
 * sony_acpi,... can take care about backlight brightness.
 *
 * Backlight drivers can use acpi_video_get_backlight_type() to determine which
 * driver should handle the backlight. RAW/GPU-driver backlight drivers must
 * use the acpi_video_backlight_use_native() helper for this.
 *
 * If CONFIG_ACPI_VIDEO is neither set as "compiled in" (y) nor as a module (m)
 * this file will not be compiled and acpi_video_get_backlight_type() will
 * always return acpi_backlight_vendor.
 */

#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/backlight.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_data/x86/nvidia-wmi-ec-backlight.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <acpi/video.h>

void acpi_video_unregister_backlight(void);

static enum acpi_backlight_type acpi_backlight_cmdline = acpi_backlight_undef;
static enum acpi_backlight_type acpi_backlight_dmi = acpi_backlight_undef;

static void acpi_video_parse_cmdline(void)
{
	if (!strcmp("vendor", acpi_video_backlight_string))
		acpi_backlight_cmdline = acpi_backlight_vendor;
	if (!strcmp("video", acpi_video_backlight_string))
		acpi_backlight_cmdline = acpi_backlight_video;
	if (!strcmp("native", acpi_video_backlight_string))
		acpi_backlight_cmdline = acpi_backlight_native;
	if (!strcmp("nvidia_wmi_ec", acpi_video_backlight_string))
		acpi_backlight_cmdline = acpi_backlight_nvidia_wmi_ec;
	if (!strcmp("none", acpi_video_backlight_string))
		acpi_backlight_cmdline = acpi_backlight_none;
}

static acpi_status
find_video(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	struct acpi_device *acpi_dev = acpi_fetch_acpi_dev(handle);
	long *cap = context;
	struct pci_dev *dev;

	static const struct acpi_device_id video_ids[] = {
		{ACPI_VIDEO_HID, 0},
		{"", 0},
	};

	if (acpi_dev && !acpi_match_device_ids(acpi_dev, video_ids)) {
		dev = acpi_get_pci_dev(handle);
		if (!dev)
			return AE_OK;
		pci_dev_put(dev);
		*cap |= acpi_is_video_device(handle);
	}
	return AE_OK;
}

/* This depends on ACPI_WMI which is X86 only */
#ifdef CONFIG_X86
static bool nvidia_wmi_ec_supported(void)
{
	struct wmi_brightness_args args = {
		.mode = WMI_BRIGHTNESS_MODE_GET,
		.val = 0,
		.ret = 0,
	};
	struct acpi_buffer buf = { (acpi_size)sizeof(args), &args };
	acpi_status status;

	status = wmi_evaluate_method(WMI_BRIGHTNESS_GUID, 0,
				     WMI_BRIGHTNESS_METHOD_SOURCE, &buf, &buf);
	if (ACPI_FAILURE(status))
		return false;

	/*
	 * If brightness is handled by the EC then nvidia-wmi-ec-backlight
	 * should be used, else the GPU driver(s) should be used.
	 */
	return args.ret == WMI_BRIGHTNESS_SOURCE_EC;
}
#else
static bool nvidia_wmi_ec_supported(void)
{
	return false;
}
#endif

/* Force to use vendor driver when the ACPI device is known to be
 * buggy */
static int video_detect_force_vendor(const struct dmi_system_id *d)
{
	acpi_backlight_dmi = acpi_backlight_vendor;
	return 0;
}

static int video_detect_force_video(const struct dmi_system_id *d)
{
	acpi_backlight_dmi = acpi_backlight_video;
	return 0;
}

static int video_detect_force_native(const struct dmi_system_id *d)
{
	acpi_backlight_dmi = acpi_backlight_native;
	return 0;
}

static int video_detect_portege_r100(const struct dmi_system_id *d)
{
	struct pci_dev *dev;
	/* Search for Trident CyberBlade XP4m32 to confirm Portégé R100 */
	dev = pci_get_device(PCI_VENDOR_ID_TRIDENT, 0x2100, NULL);
	if (dev)
		acpi_backlight_dmi = acpi_backlight_vendor;
	return 0;
}

static const struct dmi_system_id video_detect_dmi_table[] = {
	/* On Samsung X360, the BIOS will set a flag (VDRV) if generic
	 * ACPI backlight device is used. This flag will definitively break
	 * the backlight interface (even the vendor interface) until next
	 * reboot. It's why we should prevent video.ko from being used here
	 * and we can't rely on a later call to acpi_video_unregister().
	 */
	{
	 .callback = video_detect_force_vendor,
	 /* X360 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "X360"),
		DMI_MATCH(DMI_BOARD_NAME, "X360"),
		},
	},
	{
	.callback = video_detect_force_vendor,
	/* Asus UL30VT */
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "UL30VT"),
		},
	},
	{
	.callback = video_detect_force_vendor,
	/* Asus UL30A */
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "UL30A"),
		},
	},
	{
	.callback = video_detect_force_vendor,
	/* GIGABYTE GB-BXBT-2807 */
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
		DMI_MATCH(DMI_PRODUCT_NAME, "GB-BXBT-2807"),
		},
	},
	{
	.callback = video_detect_force_vendor,
	/* Sony VPCEH3U1E */
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VPCEH3U1E"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Vostro 15 3535"),
		},
	},

	/*
	 * Toshiba Portégé R100 has working both acpi_video and toshiba_acpi
	 * vendor driver. But none of them gets activated as it has a VGA with
	 * no kernel driver (Trident CyberBlade XP4m32).
	 * The DMI strings are generic so check for the VGA chip in callback.
	 */
	{
	 .callback = video_detect_portege_r100,
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Portable PC"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "Version 1.0"),
		DMI_MATCH(DMI_BOARD_NAME, "Portable PC")
		},
	},

	/*
	 * Models which need acpi_video backlight control where the GPU drivers
	 * do not call acpi_video_register_backlight() because no internal panel
	 * is detected. Typically these are all-in-ones (monitors with builtin
	 * PC) where the panel connection shows up as regular DP instead of eDP.
	 */
	{
	 .callback = video_detect_force_video,
	 /* Apple iMac14,1 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "iMac14,1"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 /* Apple iMac14,2 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "iMac14,2"),
		},
	},

	/*
	 * Older models with nvidia GPU which need acpi_video backlight
	 * control and where the old nvidia binary driver series does not
	 * call acpi_video_register_backlight().
	 */
	{
	 .callback = video_detect_force_video,
	 /* ThinkPad W530 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad W530"),
		},
	},

	/*
	 * These models have a working acpi_video backlight control, and using
	 * native backlight causes a regression where backlight does not work
	 * when userspace is not handling brightness key events. Disable
	 * native_backlight on these to fix this:
	 * https://bugzilla.kernel.org/show_bug.cgi?id=81691
	 */
	{
	 .callback = video_detect_force_video,
	 /* ThinkPad T420 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad T420"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 /* ThinkPad T520 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad T520"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 /* ThinkPad X201s */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad X201s"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 /* ThinkPad X201T */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad X201T"),
		},
	},

	/* The native backlight controls do not work on some older machines */
	{
	 /* https://bugs.freedesktop.org/show_bug.cgi?id=81515 */
	 .callback = video_detect_force_video,
	 /* HP ENVY 15 Notebook */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP ENVY 15 Notebook PC"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 /* SAMSUNG 870Z5E/880Z5E/680Z5E */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "870Z5E/880Z5E/680Z5E"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 /* SAMSUNG 370R4E/370R4V/370R5E/3570RE/370R5V */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME,
			  "370R4E/370R4V/370R5E/3570RE/370R5V"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1186097 */
	 .callback = video_detect_force_video,
	 /* SAMSUNG 3570R/370R/470R/450R/510R/4450RV */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME,
			  "3570R/370R/470R/450R/510R/4450RV"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1557060 */
	 .callback = video_detect_force_video,
	 /* SAMSUNG 670Z5E */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "670Z5E"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1094948 */
	 .callback = video_detect_force_video,
	 /* SAMSUNG 730U3E/740U3E */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "730U3E/740U3E"),
		},
	},
	{
	 /* https://bugs.freedesktop.org/show_bug.cgi?id=87286 */
	 .callback = video_detect_force_video,
	 /* SAMSUNG 900X3C/900X3D/900X3E/900X4C/900X4D */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME,
			  "900X3C/900X3D/900X3E/900X4C/900X4D"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1272633 */
	 .callback = video_detect_force_video,
	 /* Dell XPS14 L421X */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "XPS L421X"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1163574 */
	 .callback = video_detect_force_video,
	 /* Dell XPS15 L521X */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "XPS L521X"),
		},
	},
	{
	 /* https://bugzilla.kernel.org/show_bug.cgi?id=108971 */
	 .callback = video_detect_force_video,
	 /* SAMSUNG 530U4E/540U4E */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "530U4E/540U4E"),
		},
	},
	/* https://bugs.launchpad.net/bugs/1894667 */
	{
	 .callback = video_detect_force_video,
	 /* HP 635 Notebook */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP 635 Notebook PC"),
		},
	},

	/* Non win8 machines which need native backlight nevertheless */
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1201530 */
	 .callback = video_detect_force_native,
	 /* Lenovo Ideapad S405 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_BOARD_NAME, "Lenovo IdeaPad S405"),
		},
	},
	{
	 /* https://bugzilla.suse.com/show_bug.cgi?id=1208724 */
	 .callback = video_detect_force_native,
	 /* Lenovo Ideapad Z470 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "IdeaPad Z470"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1187004 */
	 .callback = video_detect_force_native,
	 /* Lenovo Ideapad Z570 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "102434U"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Lenovo E41-25 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "81FS"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Lenovo E41-45 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "82BK"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Lenovo ThinkPad X131e (3371 AMD version) */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "3371"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Apple iMac11,3 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "iMac11,3"),
		},
	},
	{
	 /* https://gitlab.freedesktop.org/drm/amd/-/issues/1838 */
	 .callback = video_detect_force_native,
	 /* Apple iMac12,1 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "iMac12,1"),
		},
	},
	{
	 /* https://gitlab.freedesktop.org/drm/amd/-/issues/2753 */
	 .callback = video_detect_force_native,
	 /* Apple iMac12,2 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "iMac12,2"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1217249 */
	 .callback = video_detect_force_native,
	 /* Apple MacBook Pro 12,1 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro12,1"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Dell Inspiron N4010 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron N4010"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Dell Vostro V131 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Vostro V131"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1123661 */
	 .callback = video_detect_force_native,
	 /* Dell XPS 17 L702X */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Dell System XPS L702X"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Dell Precision 7510 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Precision 7510"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Dell Studio 1569 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Studio 1569"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Acer Aspire 3830TG */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 3830TG"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Acer Aspire 5738z */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5738"),
		DMI_MATCH(DMI_BOARD_NAME, "JV50"),
		},
	},
	{
	 /* https://bugzilla.kernel.org/show_bug.cgi?id=207835 */
	 .callback = video_detect_force_native,
	 /* Acer TravelMate 5735Z */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 5735Z"),
		DMI_MATCH(DMI_BOARD_NAME, "BA51_MV"),
		},
	},
	{
	.callback = video_detect_force_native,
	/* ASUSTeK COMPUTER INC. GA401 */
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "GA401"),
		},
	},
	{
	.callback = video_detect_force_native,
	/* ASUSTeK COMPUTER INC. GA502 */
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "GA502"),
		},
	},
	{
	.callback = video_detect_force_native,
	/* ASUSTeK COMPUTER INC. GA503 */
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "GA503"),
		},
	},
	/*
	 * Clevo NL5xRU and NL5xNU/TUXEDO Aura 15 Gen1 and Gen2 have both a
	 * working native and video interface. However the default detection
	 * mechanism first registers the video interface before unregistering
	 * it again and switching to the native interface during boot. This
	 * results in a dangling SBIOS request for backlight change for some
	 * reason, causing the backlight to switch to ~2% once per boot on the
	 * first power cord connect or disconnect event. Setting the native
	 * interface explicitly circumvents this buggy behaviour, by avoiding
	 * the unregistering process.
	 */
	{
	.callback = video_detect_force_native,
	.ident = "Clevo NL5xRU",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "NL5xRU"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "Clevo NL5xRU",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "AURA1501"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "Clevo NL5xRU",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "EDUBOOK1502"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "Clevo NL5xNU",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "NL5xNU"),
		},
	},
	/*
	 * The TongFang PF5PU1G, PF4NU1F, PF5NU1G, and PF5LUXG/TUXEDO BA15 Gen10,
	 * Pulse 14/15 Gen1, and Pulse 15 Gen2 have the same problem as the Clevo
	 * NL5xRU and NL5xNU/TUXEDO Aura 15 Gen1 and Gen2. See the description
	 * above.
	 */
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF5PU1G",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "PF5PU1G"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF4NU1F",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "PF4NU1F"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF4NU1F",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "PULSE1401"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF5NU1G",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "PF5NU1G"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF5NU1G",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "PULSE1501"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF5LUXG",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "PF5LUXG"),
		},
	},

	/*
	 * x86 android tablets which directly control the backlight through
	 * an external backlight controller, typically TI's LP8557.
	 * The backlight is directly controlled by the lp855x driver on these.
	 * This setup means that neither i915's native nor acpi_video backlight
	 * control works. Add a "vendor" quirk to disable both. Note these
	 * devices do not use vendor control in the typical meaning of
	 * vendor specific SMBIOS or ACPI calls being used.
	 */
	{
	 .callback = video_detect_force_vendor,
	 /* Lenovo Yoga Book X90F / X90L */
	 .matches = {
		DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
		DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "CHERRYVIEW D1 PLATFORM"),
		DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "YETI-11"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /*
	  * Lenovo Yoga Tablet 2 830F/L or 1050F/L (The 8" and 10"
	  * Lenovo Yoga Tablet 2 use the same mainboard)
	  */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Intel Corp."),
		DMI_MATCH(DMI_PRODUCT_NAME, "VALLEYVIEW C0 PLATFORM"),
		DMI_MATCH(DMI_BOARD_NAME, "BYT-T FFD8"),
		/* Partial match on beginning of BIOS version */
		DMI_MATCH(DMI_BIOS_VERSION, "BLADE_21"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Lenovo Yoga Tab 3 Pro YT3-X90F */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "CHERRYVIEW D1 PLATFORM"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "Blade3-10A-001"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Xiaomi Mi Pad 2 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Xiaomi Inc"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Mipad2"),
		},
	},
	{ },
};

static bool google_cros_ec_present(void)
{
	return acpi_dev_found("GOOG0004") || acpi_dev_found("GOOG000C");
}

/*
 * Windows 8 and newer no longer use the ACPI video interface, so it often
 * does not work. So on win8+ systems prefer native brightness control.
 * Chromebooks should always prefer native backlight control.
 */
static bool prefer_native_over_acpi_video(void)
{
	return acpi_osi_is_win8() || google_cros_ec_present();
}

/*
 * Determine which type of backlight interface to use on this system,
 * First check cmdline, then dmi quirks, then do autodetect.
 */
enum acpi_backlight_type __acpi_video_get_backlight_type(bool native, bool *auto_detect)
{
	static DEFINE_MUTEX(init_mutex);
	static bool nvidia_wmi_ec_present;
	static bool native_available;
	static bool init_done;
	static long video_caps;

	/* Parse cmdline, dmi and acpi only once */
	mutex_lock(&init_mutex);
	if (!init_done) {
		acpi_video_parse_cmdline();
		dmi_check_system(video_detect_dmi_table);
		acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				    ACPI_UINT32_MAX, find_video, NULL,
				    &video_caps, NULL);
		nvidia_wmi_ec_present = nvidia_wmi_ec_supported();
		init_done = true;
	}
	if (native)
		native_available = true;
	mutex_unlock(&init_mutex);

	if (auto_detect)
		*auto_detect = false;

	/*
	 * The below heuristics / detection steps are in order of descending
	 * presedence. The commandline takes presedence over anything else.
	 */
	if (acpi_backlight_cmdline != acpi_backlight_undef)
		return acpi_backlight_cmdline;

	/* DMI quirks override any autodetection. */
	if (acpi_backlight_dmi != acpi_backlight_undef)
		return acpi_backlight_dmi;

	/* Special cases such as nvidia_wmi_ec and apple gmux. */
	if (nvidia_wmi_ec_present)
		return acpi_backlight_nvidia_wmi_ec;

	if (auto_detect)
		*auto_detect = true;

	/* Use ACPI video if available, except when native should be preferred. */
	if ((video_caps & ACPI_VIDEO_BACKLIGHT) &&
	     !(native_available && prefer_native_over_acpi_video()))
		return acpi_backlight_video;

	/* Use native if available */
	if (native_available)
		return acpi_backlight_native;

	/*
	 * The vendor specific BIOS interfaces are only necessary for
	 * laptops from before ~2008.
	 *
	 * For laptops from ~2008 till ~2023 this point is never reached
	 * because on those (video_caps & ACPI_VIDEO_BACKLIGHT) above is true.
	 *
	 * Laptops from after ~2023 no longer support ACPI_VIDEO_BACKLIGHT,
	 * if this point is reached on those, this likely means that
	 * the GPU kms driver which sets native_available has not loaded yet.
	 *
	 * Returning acpi_backlight_vendor in this case is known to sometimes
	 * cause a non working vendor specific /sys/class/backlight device to
	 * get registered.
	 *
	 * Return acpi_backlight_none on laptops with ACPI tables written
	 * for Windows 8 (laptops from after ~2012) to avoid this problem.
	 */
	if (acpi_osi_is_win8())
		return acpi_backlight_none;

	/* No ACPI video/native (old hw), use vendor specific fw methods. */
	return acpi_backlight_vendor;
}

EXPORT_SYMBOL(__acpi_video_get_backlight_type);

/*
 * Set the preferred backlight interface type based on DMI info.
 * This function allows DMI blacklists to be implemented by external
 * platform drivers instead of putting a big blacklist in video_detect.c
 */
void acpi_video_set_dmi_backlight_type(enum acpi_backlight_type type)
{
	acpi_backlight_dmi = type;
	/* Remove acpi-video backlight interface if it is no longer desired */
	if (acpi_video_get_backlight_type() != acpi_backlight_video)
		acpi_video_unregister_backlight();
}
EXPORT_SYMBOL(acpi_video_set_dmi_backlight_type);