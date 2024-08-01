// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/module.h>
#include <linux/pm.h>
#include <linux/usb.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "statusLinkProtocol.h"
#include "panelLinkProtocol.h"

#define DRIVER_NAME		"beada"
#define DRIVER_DESC		"BeadaPanel USB Media Display"
#define DRIVER_DATE		"2024"
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0


#define MODEL_5		0
#define MODEL_7		1
#define MODEL_6		2
#define MODEL_3		3
#define MODEL_4		4
#define MODEL_5C		10
#define MODEL_5S		11
#define MODEL_7C		12
#define MODEL_3C		13
#define MODEL_4C		14
#define MODEL_6C		15
#define MODEL_6S		16
#define MODEL_2		17
#define MODEL_2W		18 

#define RGB565_BPP			16
#define CMD_TIMEOUT			msecs_to_jiffies(200)
#define DATA_TIMEOUT			msecs_to_jiffies(1000)
#define PANELLINK_MAX_DELAY		msecs_to_jiffies(2000)
#define CMD_SIZE			512*4

struct beada_device {
	struct drm_device				dev;
	struct drm_simple_display_pipe	pipe;
	struct drm_connector			conn;
	struct usb_device				*udev;
	struct device					*dmadev;

	STATUSLINK_INFO	info;
	unsigned int screen;
	unsigned int version;
	unsigned char id[8];
	char *model;

	unsigned int	width;
	unsigned int	height;
	unsigned int	margin;
	unsigned int	width_mm;
	unsigned int	height_mm;
	unsigned char	*cmd_buf;
	unsigned char	*draw_buf;
	struct iosys_map dest_map;
	struct drm_rect old_rect;

	unsigned int	misc_rcv_ept;
	unsigned int	misc_snd_ept;
	unsigned int	data_snd_ept;
};

#define to_beada(__dev) container_of(__dev, struct beada_device, dev)

void HexDump(unsigned char *buf, int len, unsigned char *addr) {
	int i, j, k;
	char binstr[80];

	for (i = 0; i < len; i++) {
		if (0 == (i % 16)) {
			sprintf(binstr, "%08x -", i + (int)addr);
			sprintf(binstr, "%s %02x", binstr, (unsigned char)buf[i]);
		}
		else if (15 == (i % 16)) {
			sprintf(binstr, "%s %02x", binstr, (unsigned char)buf[i]);
			sprintf(binstr, "%s  ", binstr);
			for (j = i - 15; j <= i; j++) {
				sprintf(binstr, "%s%c", binstr, ('!' < buf[j] && buf[j] <= '~') ? buf[j] : '.');
			}
			printk(KERN_DEBUG "%s\n", binstr);
		}
		else {
			sprintf(binstr, "%s %02x", binstr, (unsigned char)buf[i]);
		}
	}
	if (0 != (i % 16)) {
		k = 16 - (i % 16);
		for (j = 0; j < k; j++) {
			sprintf(binstr, "%s   ", binstr);
		}
		sprintf(binstr, "%s  ", binstr);
		k = 16 - k;
		for (j = i - k; j < i; j++) {
			sprintf(binstr, "%s%c", binstr, ('!' < buf[j] && buf[j] <= '~') ? buf[j] : '.');
		}
		printk(KERN_DEBUG "%s\n", binstr);
	}
}

static int beada_send_tag(struct beada_device *beada, const char* cmd)
{
	int ret;
	unsigned int len, len1;

	len = CMD_SIZE;

	/* prepare tag header */
	ret = fillPLStart(beada->draw_buf, &len, cmd);	
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "fillPLStart() error %d\n", ret);
		return -EIO;
	}

	HexDump(beada->draw_buf, len, beada->draw_buf);

	/* send request */
	ret = usb_bulk_msg(beada->udev,
			usb_sndbulkpipe(beada->udev, beada->data_snd_ept),
			beada->draw_buf, len, &len1, CMD_TIMEOUT);

	if (ret || len != len1) {
		DRM_DEV_ERROR(&beada->udev->dev, "usb_bulk_msg() error %d\n", ret);
		return -EIO;
	}

	return 0;
}

static int beada_misc_request(struct beada_device *beada)
{
	int ret;
	unsigned int len, len1;
	int width, height, margin, width_mm, height_mm;
	char *model;

	len = CMD_SIZE;
	margin = 0;

	// prepare statuslink command
	ret = fillSLGetInfo(beada->cmd_buf, &len);
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "fillSLGetInfo() error %d\n", ret);
		return -EIO;
	}

	HexDump(beada->cmd_buf, len, beada->cmd_buf);

	/* send request */
	ret = usb_bulk_msg(beada->udev,
			usb_sndbulkpipe(beada->udev, beada->misc_snd_ept),
			beada->cmd_buf, len, &len1, CMD_TIMEOUT);

	if (ret || len1 != len) {
		DRM_DEV_ERROR(&beada->udev->dev, "usb_bulk_msg() write error %d\n", ret);
		return -EIO;
	}

	/* read value */
	len += sizeof(STATUSLINK_INFO);
	ret = usb_bulk_msg(beada->udev,
			usb_rcvbulkpipe(beada->udev, beada->misc_rcv_ept),
			beada->cmd_buf, len, &len1,
			DATA_TIMEOUT);
	if (ret || len1 != len) {
		DRM_DEV_ERROR(&beada->udev->dev, "usb_bulk_msg() read error %d\n", ret);
		return -EIO;
	}

	HexDump(beada->cmd_buf, len, beada->cmd_buf);

	/* retrive BeadaPanel device info into a local structure */
	ret = retrivSLGetInfo(beada->cmd_buf, len, &beada->info);
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "retrivSLGetInfo() error %d\n", ret);
		return -EIO;
	}

	switch (beada->info.os_version) {
	case MODEL_2:
		model = "2";
		width = 480;
		height = 480;
		width_mm = 53;
		height_mm = 53;
		break;
	case MODEL_2W:
		model = "2W";
		width = 480;
		height = 480;
		width_mm = 70;
		height_mm = 70;
		break;
	case MODEL_3:
		model = "3";
		height = 480;
		width = 320;
		height_mm = 62;
		width_mm = 40;
		break;
	case MODEL_4:
		model = "4";
		height = 800;
		width = 480;
		height_mm = 94;
		width_mm = 56;
		break;
	case MODEL_3C:
		model = "3C";
		width = 480;
		height = 320;
		width_mm = 62;
		height_mm = 40;
		break;
	case MODEL_4C:
		model = "4C";
		width = 800;
		height = 480;
		width_mm = 94;
		height_mm = 56;
		break;
	case MODEL_5:
		model = "5";
		width = 800;
		height = 480;
		width_mm = 108;
		height_mm = 65;
		break;
	case MODEL_5S:
		model = "5S";
		width = 800;
		height = 480;
		width_mm = 108;
		height_mm = 65;
		break;
	case MODEL_6:
		model = "6";
		height = 1280;
		width = 480;
		height_mm = 161;
		width_mm = 60;
		break;
	case MODEL_6C:
		model = "6C";
		width = 1280;
		height = 480;
		width_mm = 161;
		height_mm = 60;
		break;
	case MODEL_6S:
		model = "6S";
		width = 1280;
		height = 480;
		width_mm = 161;
		height_mm = 60;
		break;
	case MODEL_7C:
		model = "7C";
		width = 800;
		height = 480;
		width_mm = 62;
		height_mm = 110;
		break;
	default:
		model = "5";
		width  = 800;
		height = 480;
		margin = 0;
		width_mm = 108;
		height_mm = 65;	
		break;
	}

	beada->width = width;
	beada->height = height;
	beada->margin = margin;
	beada->model = model;
	beada->width_mm = width_mm;
	beada->height_mm = height_mm;
	
	return 0;
}

static int beada_buf_copy(void *dst, const struct iosys_map *map, struct drm_framebuffer *fb, struct drm_rect *clip)
{
	int ret;
	unsigned int pitch=0;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret)
		return ret;

	drm_fb_xrgb8888_to_rgb565((struct iosys_map *)dst, &pitch, map, fb, clip, false);

	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);

	return 0;
}

static void beada_fb_mark_dirty(struct drm_framebuffer *fb, const struct iosys_map *map, struct drm_rect *rect)
{
	struct beada_device *beada = to_beada(fb->dev);
	int idx, len, height, width, ret;
	char fmtstr[256] = {0};

	height = rect->y2 - rect->y1;
	width = rect->x2 - rect->x1;
	len = height * width * RGB565_BPP / 8;

	if (!drm_dev_enter(fb->dev, &idx))
		return;

	ret = beada_buf_copy(&beada->dest_map, map, fb, rect);
	if (ret)
		goto err_msg;

	/* send a new tag if rect size changed */
	if (!((beada->old_rect.x1 == rect->x1) &&
		(beada->old_rect.y1 == rect->y1) &&
		(beada->old_rect.x2 == rect->x2) &&
		(beada->old_rect.y2 == rect->y2)) ) {

		snprintf(fmtstr, sizeof(fmtstr), "video/x-raw, format=RGB16, height=%d, width=%d, framerate=0/1", height, width);
		ret = beada_send_tag(beada, (const char *)fmtstr);
		if (ret < 0)
			goto err_msg;
	}

	ret = usb_bulk_msg(beada->udev, usb_sndbulkpipe(beada->udev, beada->data_snd_ept), beada->draw_buf,
			    len, NULL, PANELLINK_MAX_DELAY);
	
	beada->old_rect.x1 = rect->x1;
	beada->old_rect.y1 = rect->y1;
	beada->old_rect.x2 = rect->x2;
	beada->old_rect.y2 = rect->y2;

err_msg:
	if (ret)
		dev_err_once(fb->dev->dev, "Failed to update display %d\n", ret);

	drm_dev_exit(idx);
}

static void beada_fb_mark_dirty_1(struct drm_framebuffer *fb, const struct iosys_map *map, struct drm_rect *rect)
{
	struct beada_device *beada = to_beada(fb->dev);
	int idx, len, height, width, ret;
	char fmtstr[256] = {0};
	struct drm_rect form = {
		.x1 = 0,
		.x2 = beada->width,
		.y1 = 0,
		.y2 = beada->height,
	};

	height = beada->height;
	width = beada->width;
	len = height * width * RGB565_BPP / 8;

	if (!drm_dev_enter(fb->dev, &idx))
		return;

	ret = beada_buf_copy(&beada->dest_map, map, fb, &form);
	if (ret)
		goto err_msg;

	/* send a new tag if rect size changed */
	if (((beada->old_rect.x1 == 0) &&
		(beada->old_rect.y1 == 0) &&
		(beada->old_rect.x2 == 0) &&
		(beada->old_rect.y2 == 0)) ) {

		snprintf(fmtstr, sizeof(fmtstr), "image/x-raw, format=BGR16, height=%d, width=%d, framerate=0/1", height, width);
		ret = beada_send_tag(beada, (const char *)fmtstr);
		if (ret < 0)
			goto err_msg;
		
		beada->old_rect.x1 = 0;
		beada->old_rect.y1 = 0;
		beada->old_rect.x2 = width;
		beada->old_rect.y2 = height;
	}

	ret = usb_bulk_msg(beada->udev, usb_sndbulkpipe(beada->udev, beada->data_snd_ept), beada->draw_buf,
			    len, NULL, PANELLINK_MAX_DELAY);
	
err_msg:
	if (ret) {
		beada->old_rect.x1 = 0;
		beada->old_rect.y1 = 0;
		beada->old_rect.x2 = 0;
		beada->old_rect.y2 = 0;
		dev_err_once(fb->dev->dev, "Failed to update display %d\n", ret);
	}

	drm_dev_exit(idx);
}

/* ------------------------------------------------------------------ */
/* beada connector						      */

/*
 * We use fake EDID info so that userspace know that it is dealing with
 * an Acer projector, rather then listing this as an "unknown" monitor.
 * Note this assumes this driver is only ever used with the Acer C120, if we
 * add support for other devices the vendor and model should be parameterized.
 */
static struct edid beada_edid = {
	.header		= { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 },
	.mfg_id		= { 0x3b, 0x05 },	/* "NXE" */
	.prod_code	= { 0x01, 0x10 },	/* 1001h */
	.serial		= 0xaa55aa55,
	.mfg_week	= 1,
	.mfg_year	= 24,
	.version	= 1,			/* EDID 1.3 */
	.revision	= 3,			/* EDID 1.3 */
	.input		= 0x08,			/* Analog input */
	.features	= 0x0a,			/* Pref timing in DTD 1 */
	.standard_timings = { { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 },
			      { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 } },
	.detailed_timings = { {
		.pixel_clock = 3383,
		/* hactive = 848, hblank = 256 */
		.data.pixel_data.hactive_lo = 0x50,
		.data.pixel_data.hblank_lo = 0x00,
		.data.pixel_data.hactive_hblank_hi = 0x31,
		/* vactive = 480, vblank = 28 */
		.data.pixel_data.vactive_lo = 0xe0,
		.data.pixel_data.vblank_lo = 0x1c,
		.data.pixel_data.vactive_vblank_hi = 0x10,
		/* hsync offset 40 pw 128, vsync offset 1 pw 4 */
		.data.pixel_data.hsync_offset_lo = 0x28,
		.data.pixel_data.hsync_pulse_width_lo = 0x80,
		.data.pixel_data.vsync_offset_pulse_width_lo = 0x14,
		.data.pixel_data.hsync_vsync_offset_pulse_width_hi = 0x00,
		/* Digital separate syncs, hsync+, vsync+ */
		.data.pixel_data.misc = 0x1e,
	}, {
		.pixel_clock = 0,
		.data.other_data.type = 0xfd, /* Monitor ranges */
		.data.other_data.data.range.min_vfreq = 59,
		.data.other_data.data.range.max_vfreq = 61,
		.data.other_data.data.range.min_hfreq_khz = 29,
		.data.other_data.data.range.max_hfreq_khz = 32,
		.data.other_data.data.range.pixel_clock_mhz = 4, /* 40 MHz */
		.data.other_data.data.range.flags = 0,
		.data.other_data.data.range.formula.cvt = {
			0xa0, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20 },
	}, {
		.pixel_clock = 0,
		.data.other_data.type = 0xfc, /* Model string */
		.data.other_data.data.str.str = {
			'P', 'r', 'o', 'j', 'e', 'c', 't', 'o', 'r', '\n',
			' ', ' ',  ' ' },
	}, {
		.pixel_clock = 0,
		.data.other_data.type = 0xfe, /* Unspecified text / padding */
		.data.other_data.data.str.str = {
			'\n', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
			' ', ' ',  ' ' },
	} },
	.checksum = 0x13,
};

static int beada_conn_get_modes(struct drm_connector *connector)
{
	drm_connector_update_edid_property(connector, &beada_edid);
	return drm_add_edid_modes(connector, &beada_edid);
}

static const struct drm_connector_helper_funcs beada_conn_helper_funcs = {
	.get_modes = beada_conn_get_modes,
};

static const struct drm_connector_funcs beada_conn_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int beada_conn_init(struct beada_device *beada)
{
	drm_connector_helper_add(&beada->conn, &beada_conn_helper_funcs);
	return drm_connector_init(&beada->dev, &beada->conn,
				  &beada_conn_funcs, DRM_MODE_CONNECTOR_USB);
}

static void beada_pipe_enable(struct drm_simple_display_pipe *pipe,
				 struct drm_crtc_state *crtc_state,
				 struct drm_plane_state *plane_state)
{
	struct beada_device *beada = to_beada(pipe->crtc.dev);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_rect rect = {
		.x1 = 0,
		.x2 = fb->width,
		.y1 = 0,
		.y2 = fb->height,
	};

	beada_fb_mark_dirty_1(fb, &shadow_plane_state->data[0], &rect);
}

static void beada_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	//struct beada_device *beada = to_beada(pipe->crtc.dev);

}

static void beada_pipe_update(struct drm_simple_display_pipe *pipe,
				 struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	struct drm_rect rect;

	if (!pipe->crtc.state->active)
		return;

	if (drm_atomic_helper_damage_merged(old_state, state, &rect))
		beada_fb_mark_dirty_1(fb, &shadow_plane_state->data[0], &rect);
}

static const struct drm_simple_display_pipe_funcs beada_pipe_funcs = {
	.enable	    = beada_pipe_enable,
	.disable    = beada_pipe_disable,
	.update	    = beada_pipe_update,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
};

static const uint32_t beada_pipe_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const uint64_t beada_pipe_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

/*
 * FIXME: Dma-buf sharing requires DMA support by the importing device.
 *        This function is a workaround to make USB devices work as well.
 *        See todo.rst for how to fix the issue in the dma-buf framework.
 */
static struct drm_gem_object *beada_gem_prime_import(struct drm_device *dev,
							struct dma_buf *dma_buf)
{
	struct beada_device *beada = to_beada(dev);

	if (!beada->dmadev)
		return ERR_PTR(-ENODEV);

	return drm_gem_prime_import_dev(dev, dma_buf, beada->dmadev);
}

DEFINE_DRM_GEM_FOPS(beada_fops);

static const struct drm_driver beada_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,

	.name		 = DRIVER_NAME,
	.desc		 = DRIVER_DESC,
	.date		 = DRIVER_DATE,
	.major		 = DRIVER_MAJOR,
	.minor		 = DRIVER_MINOR,

	.fops		 = &beada_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	.gem_prime_import = beada_gem_prime_import,
};

static const struct drm_mode_config_funcs beada_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void beada_mode_config_setup(struct beada_device *beada)
{
	struct drm_device *dev = &beada->dev;

	dev->mode_config.funcs = &beada_mode_config_funcs;
	dev->mode_config.min_width = beada->width;
	dev->mode_config.max_width = beada->width;
	dev->mode_config.min_height = beada->height;
	dev->mode_config.max_height = beada->height;
}

static int beada_edid_block_checksum(u8 *raw_edid)
{
	int i;
	u8 csum = 0, crc = 0;

	for (i = 0; i < EDID_LENGTH - 1; i++)
		csum += raw_edid[i];

	crc = 0x100 - csum;

	return crc;
}

static void beada_edid_setup(struct beada_device *beada)
{
	unsigned int width, height, width_mm, height_mm;
	char buf[16];

	width = beada->width;
	height = beada->height;
	width_mm = beada->width_mm;
	height_mm = beada->height_mm;

	beada_edid.detailed_timings[0].data.pixel_data.hactive_lo = width % 256;
	beada_edid.detailed_timings[0].data.pixel_data.hactive_hblank_hi &= 0x0f;
	beada_edid.detailed_timings[0].data.pixel_data.hactive_hblank_hi |= \
						((u8)(width / 256) << 4);

	beada_edid.detailed_timings[0].data.pixel_data.vactive_lo = height % 256;
	beada_edid.detailed_timings[0].data.pixel_data.vactive_vblank_hi &= 0x0f;
	beada_edid.detailed_timings[0].data.pixel_data.vactive_vblank_hi |= \
						((u8)(height / 256) << 4);

	beada_edid.detailed_timings[0].data.pixel_data.width_mm_lo = \
							width_mm % 256;
	beada_edid.detailed_timings[0].data.pixel_data.height_mm_lo = \
							height_mm % 256;
	beada_edid.detailed_timings[0].data.pixel_data.width_height_mm_hi = \
					((u8)(width_mm / 256) << 4) | \
					((u8)(height_mm / 256) & 0xf);

	memcpy(&beada_edid.detailed_timings[2].data.other_data.data.str.str,
		beada->model, strlen(beada->model));

	snprintf(buf, 16, "%02X%02X%02X%02X\n",
		beada->id[4], beada->id[5], beada->id[6], beada->id[7]);

	memcpy(&beada_edid.detailed_timings[3].data.other_data.data.str.str,
		buf, strlen(buf));

	beada_edid.checksum = beada_edid_block_checksum((u8*)&beada_edid);
}


static int beada_usb_probe(struct usb_interface *interface,
			  const struct usb_device_id *id)
{
	struct beada_device *beada;
	struct drm_device *dev;
	int ret;

	/*
	 * The beada presents itself to the system as 2 usb mass-storage
	 * interfaces, we only care about / need the first one.
	 */
	if (interface->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	beada = devm_drm_dev_alloc(&interface->dev, &beada_drm_driver,
				      struct beada_device, dev);
	if (IS_ERR(beada)) {
		DRM_DEV_ERROR(&beada->udev->dev, "devm_drm_dev_alloc() failed\n");
		return PTR_ERR(beada);
	}
	
	/* Check corresponding endpoint number */
	beada->misc_snd_ept = 2;
	beada->misc_rcv_ept = 2;
	beada->data_snd_ept = 1;
	beada->udev = interface_to_usbdev(interface);

	dev = &beada->dev;
	beada->dmadev = usb_intf_get_dma_device(to_usb_interface(dev->dev));
	if (!beada->dmadev)
		DRM_DEV_DEBUG(&beada->udev->dev, "buffer sharing not supported"); /* not an error */

	beada->cmd_buf = drmm_kmalloc(&beada->dev, CMD_SIZE, GFP_KERNEL);
	if (!beada->cmd_buf) {
		DRM_DEV_ERROR(&beada->udev->dev, "beada->cmd_buf init failed\n");
		goto err_put_device;
	}

	ret = beada_misc_request(beada);
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "cant't get screen info.\n");
		goto err_put_device;
	}

	ret = drmm_mode_config_init(dev);
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "drmm_mode_config_init() return %d\n", ret);
		goto err_put_device;
	}

	beada_mode_config_setup(beada);
	beada_edid_setup(beada);
	
	beada->draw_buf = drmm_kmalloc(&beada->dev, beada->height * beada->width * RGB565_BPP / 8 + beada->margin, GFP_KERNEL);
	if (!beada->draw_buf) {
		DRM_DEV_ERROR(&beada->udev->dev, "beada->draw_buf init failed\n");
		goto err_put_device;
	}

	beada->old_rect.x1 = 0;
	beada->old_rect.y1 = 0;
	beada->old_rect.x2 = 0;
	beada->old_rect.y2 = 0;
	iosys_map_set_vaddr(&beada->dest_map, beada->draw_buf);

	ret = beada_conn_init(beada);
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "beada_conn_init() return %d\n", ret);
		goto err_put_device;
	}

	ret = drm_simple_display_pipe_init(&beada->dev,
					   &beada->pipe,
					   &beada_pipe_funcs,
					   beada_pipe_formats,
					   ARRAY_SIZE(beada_pipe_formats),
					   beada_pipe_modifiers,
					   &beada->conn);
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "drm_simple_display_pipe_init() return %d\n", ret);
		goto err_put_device;
	}

	drm_plane_enable_fb_damage_clips(&beada->pipe.plane);

	drm_mode_config_reset(dev);

	usb_set_intfdata(interface, dev);
	ret = drm_dev_register(dev, 0);
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "drm_dev_register() return %d\n", ret);
		goto err_put_device;
	}


	drm_fbdev_generic_setup(dev, 0);

	DRM_DEV_INFO(&beada->udev->dev, "BeadaPanel %s detected\n", beada->model);

	DRM_DEV_DEBUG(&beada->udev->dev, "--------------beada_usb_probe() exit\n");
	return ret;

err_put_device:
	put_device(beada->dmadev);

	DRM_DEV_DEBUG(&beada->udev->dev, "--------------beada_usb_probe() exit from err_put_device\n");
	return ret;
}

static void beada_usb_disconnect(struct usb_interface *interface)
{
	struct drm_device *dev = usb_get_intfdata(interface);
	struct beada_device *beada = to_beada(dev);

	DRM_DEV_DEBUG(&beada->udev->dev, "--------------beada_usb_disconnect() enter\n");

	put_device(beada->dmadev);
	beada->dmadev = NULL;
	drm_dev_unplug(dev);
	drm_atomic_helper_shutdown(dev);

	DRM_DEV_DEBUG(&beada->udev->dev, "--------------beada_usb_disconnect() exit\n");
}

static int beada_suspend(struct usb_interface *interface,
			    pm_message_t message)
{
	struct drm_device *dev = usb_get_intfdata(interface);

	return drm_mode_config_helper_suspend(dev);
}

static int beada_resume(struct usb_interface *interface)
{
	struct drm_device *dev = usb_get_intfdata(interface);
	struct beada_device *beada = to_beada(dev);

	return drm_mode_config_helper_resume(dev);
}

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x4e58, 0x1001) },
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver beada_usb_driver = {
	.name = "beada",
	.probe = beada_usb_probe,
	.disconnect = beada_usb_disconnect,
	.id_table = id_table,
	.suspend = pm_ptr(beada_suspend),
	.resume = pm_ptr(beada_resume),
	.reset_resume = pm_ptr(beada_resume),
};

module_usb_driver(beada_usb_driver);
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
