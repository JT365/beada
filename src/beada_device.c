// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/module.h>
#include <linux/usb.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>

#include "statusLinkProtocol.h"
#include "panelLinkProtocol.h"
#include "beada_device.h"

#define RGB565_BPP			16
#define CMD_TIMEOUT			msecs_to_jiffies(200)
#define DATA_TIMEOUT			msecs_to_jiffies(1000)
#define PANELLINK_MAX_DELAY		msecs_to_jiffies(2000)
#define CMD_SIZE			512*4

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

int beada_send_tag(struct beada_device *beada, struct transmitter *trans, const char* cmd)
{
	int ret;
	unsigned int len, len1;

	len = CMD_SIZE;

	/* prepare tag header */
	ret = fillPLStart(trans->tag_buf, &len, cmd);	
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "fillPLStart() error %d\n", ret);
		return -EIO;
	}

	HexDump(trans->tag_buf, len, trans->tag_buf);

	/* send request */
	ret = usb_bulk_msg(beada->udev,
			usb_sndbulkpipe(beada->udev, beada->data_snd_ept),
			trans->tag_buf, len, &len1, CMD_TIMEOUT);

	if (ret || len != len1) {
		DRM_DEV_ERROR(&beada->udev->dev, "usb_bulk_msg() error %d\n", ret);
		return -EIO;
	}

	return 0;
}

int beada_misc_request(struct beada_device *beada)
{
	int ret;
	unsigned int len, len1;
	int width, height, margin, width_mm, height_mm;
	char *model;

	/* Check corresponding endpoint number */
	beada->misc_snd_ept = 2;
	beada->misc_rcv_ept = 2;
	beada->data_snd_ept = 1;

	beada->cmd_buf = drmm_kmalloc(&beada->dev, CMD_SIZE, GFP_KERNEL);
	if (!beada->cmd_buf) {
		DRM_DEV_ERROR(&beada->udev->dev, "beada->cmd_buf init failed\n");
		return -EIO;
	}

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

int beada_buf_copy(void *dst, const struct iosys_map *map, struct drm_framebuffer *fb, struct drm_rect *clip, struct drm_format_conv_state *fmtcnv_state)
{
	int ret;
	unsigned int pitch=0;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret)
		return ret;

	drm_fb_xrgb8888_to_rgb565((struct iosys_map *)dst, &pitch, map, fb, clip, fmtcnv_state, false);

	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);

	return 0;
}

static void beada_write_bulk_callback(struct urb *urb)
{
	struct transmitter *trans = urb->context;
	struct beada_device *beada = (struct beada_device *)trans->crumbs;

	/* sync/async unlink faults aren't errors */
	if (urb->status && 
	    !(urb->status == -ENOENT || 
	      urb->status == -ECONNRESET ||
	      urb->status == -ESHUTDOWN)) {
		DRM_DEV_DEBUG(&beada->udev->dev, "%s - nonzero write bulk status received: %d",
		    __FUNCTION__, urb->status);
	}

	if (urb->status) {
		beada->old_rect.x1 = 0;
		beada->old_rect.y1 = 0;
		beada->old_rect.x2 = 0;
		beada->old_rect.y2 = 0;
	}

	trans->state = TRANSMITTER_STAT_IDLE;
}

void beada_fb_update_work(struct delayed_work *work)
{
	struct transmitter *trans =
		container_of(work, struct transmitter, work);	
	struct beada_device *beada = (struct beada_device *)trans->crumbs;
	int height, width, len;
	char fmtstr[256] = {0};
	int ret = 0;

	height = beada->height;
	width = beada->width;
	len = height * width * RGB565_BPP / 8;

	/* send a new tag if rect size changed */
	if (((beada->old_rect.x1 == 0) &&
		(beada->old_rect.y1 == 0) &&
		(beada->old_rect.x2 == 0) &&
		(beada->old_rect.y2 == 0)) ) {

		snprintf(fmtstr, sizeof(fmtstr), "image/x-raw, format=BGR16, height=%d, width=%d, framerate=0/1", height, width);
		ret = beada_send_tag(beada, trans, (const char *)fmtstr);
		if (ret < 0)
			goto err_msg;
		
		beada->old_rect.x1 = 0;
		beada->old_rect.y1 = 0;
		beada->old_rect.x2 = width;
		beada->old_rect.y2 = height;
	}

	/* initialize the urb properly */
	usb_fill_bulk_urb(trans->urb, beada->udev,
		usb_sndbulkpipe(beada->udev, beada->data_snd_ept),
		trans->draw_buf, len, beada_write_bulk_callback, trans);
	trans->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* send the data out the bulk port */
	ret = usb_submit_urb(trans->urb, GFP_KERNEL);
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "%s - failed submitting write urb, error %d", __FUNCTION__, ret);
		goto err_msg;
	}

	return ;

err_msg:

}


/* ------------------------------------------------------------------ */
/* beada connector						      */

int beada_conn_get_modes(struct drm_connector *connector)
{
	struct beada_device *beada = to_beada(connector->dev);

	drm_connector_update_edid_property(connector, &beada->s_edid);
	return drm_add_edid_modes(connector, &beada->s_edid);
}

void beada_fb_mark_dirty(struct drm_framebuffer *fb, const struct iosys_map *map, struct drm_rect *rect, struct drm_format_conv_state *fmtcnv_state)
{
	struct beada_device *beada = to_beada(fb->dev);
	struct transmitter *trans;
	int idx, ret;
	struct drm_rect form = {
		.x1 = 0,
		.x2 = beada->width,
		.y1 = 0,
		.y2 = beada->height,
	};

	if (!drm_dev_enter(fb->dev, &idx))
		return;

	for (int i = 0; i < TRANSMITTER_NUM; i++) {
		trans = &beada->trans[i];
		if (trans->state == TRANSMITTER_STAT_IDLE) {
			ret = beada_buf_copy(&trans->dest_map, map, fb, &form, fmtcnv_state);
			if (!ret) {
				beada_fb_update_work(&trans->work);	
				trans->state = TRANSMITTER_STAT_BUSY;
			}
			goto err_msg;
		}
	}

err_msg:
	if (ret) {
		dev_err_once(&beada->udev->dev, "Failed to update display %d\n", ret);
	}

	drm_dev_exit(idx);
}

void beada_stop_fb_update(struct beada_device *beada)
{

}

int beada_edid_block_checksum(u8 *raw_edid)
{
	int i;
	u8 csum = 0, crc = 0;

	for (i = 0; i < EDID_LENGTH - 1; i++)
		csum += raw_edid[i];

	crc = 0x100 - csum;

	return crc;
}

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

void beada_edid_setup(struct beada_device *beada)
{
	unsigned int width, height, width_mm, height_mm;
	char buf[16];

	beada->s_edid = beada_edid;

	width = beada->width;
	height = beada->height;
	width_mm = beada->width_mm;
	height_mm = beada->height_mm;

	beada->s_edid.detailed_timings[0].data.pixel_data.hactive_lo = width % 256;
	beada->s_edid.detailed_timings[0].data.pixel_data.hactive_hblank_hi &= 0x0f;
	beada->s_edid.detailed_timings[0].data.pixel_data.hactive_hblank_hi |= \
						((u8)(width / 256) << 4);

	beada->s_edid.detailed_timings[0].data.pixel_data.vactive_lo = height % 256;
	beada->s_edid.detailed_timings[0].data.pixel_data.vactive_vblank_hi &= 0x0f;
	beada->s_edid.detailed_timings[0].data.pixel_data.vactive_vblank_hi |= \
						((u8)(height / 256) << 4);

	beada->s_edid.detailed_timings[0].data.pixel_data.width_mm_lo = \
							width_mm % 256;
	beada->s_edid.detailed_timings[0].data.pixel_data.height_mm_lo = \
							height_mm % 256;
	beada->s_edid.detailed_timings[0].data.pixel_data.width_height_mm_hi = \
					((u8)(width_mm / 256) << 4) | \
					((u8)(height_mm / 256) & 0xf);

	memcpy(beada->s_edid.detailed_timings[2].data.other_data.data.str.str,
		beada->model, strlen(beada->model));

	snprintf(buf, 16, "%02X%02X%02X%02X\n",
		beada->id[4], beada->id[5], beada->id[6], beada->id[7]);

	memcpy(beada->s_edid.detailed_timings[3].data.other_data.data.str.str,
		buf, strlen(buf));

	beada->s_edid.checksum = beada_edid_block_checksum((u8*)&beada->s_edid);
}

int beada_transmitter_init(struct beada_device *beada)
{
	struct transmitter *trans;

	beada->old_rect.x1 = 0;
	beada->old_rect.y1 = 0;
	beada->old_rect.x2 = 0;
	beada->old_rect.y2 = 0;

	for (int i = 0; i < TRANSMITTER_NUM; i++) {
		trans = &beada->trans[i];

		trans->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!trans->urb) {
			DRM_DEV_ERROR(&beada->udev->dev, "trans[%d].urb init failed\n", i);
			return PTR_ERR(trans->urb);
		}

		trans->crumbs = (void *)beada;
		trans->state = TRANSMITTER_STAT_IDLE;
		trans->tag_buf = drmm_kmalloc(&beada->dev, CMD_SIZE, GFP_KERNEL);
		if (!trans->tag_buf) {
			DRM_DEV_ERROR(&beada->udev->dev, "trans[%d].tag_buf init failed\n", i);
			return PTR_ERR(trans->tag_buf);
		}

		trans->draw_buf = usb_alloc_coherent(beada->udev, beada->height * beada->width * RGB565_BPP / 8 + beada->margin, GFP_KERNEL, &trans->urb->transfer_dma);
		if (!trans->draw_buf) {
			DRM_DEV_ERROR(&beada->udev->dev, "trans[%d].draw_buf init failed\n", i);
			return PTR_ERR(trans->draw_buf);
		}
		iosys_map_set_vaddr(&trans->dest_map, trans->draw_buf);
	}

	return 0;
}

int beada_set_backlight(struct beada_device *beada, int val)
{
	int ret;
	unsigned int len, len1;

	len = CMD_SIZE;

	// prepare statuslink command
	ret = fillSLSetBL(beada->cmd_buf, &len, val & 0xff);
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "fillSLSetBL() error %d\n", ret);
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

	return 0;
}
