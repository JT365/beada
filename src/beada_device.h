// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019 Hans de Goede <hdegoede@redhat.com>
 */

#ifndef _BEADA_DEVICE_H_
#define _BEADA_DEVICE_H_

#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_rect.h>
#include <linux/usb.h>
#include <linux/iosys-map.h>

#include "statusLinkProtocol.h"
#include "panelLinkProtocol.h"

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

#define TRANSMITTER_STAT_BUSY	1
#define TRANSMITTER_STAT_IDLE	0
#define TRANSMITTER_NUM			2

struct transmitter {
	unsigned int		state;
	struct urb 			*urb;
	unsigned char		*tag_buf;
	unsigned char		*draw_buf;
	struct iosys_map	dest_map;
	void 				*crumbs;
	struct delayed_work work;
};

struct beada_device {
	struct drm_device				dev;
	struct backlight_device         *bl_dev;
	struct drm_simple_display_pipe	pipe;
	struct drm_connector			conn;
	struct usb_device				*udev;
	struct device					*dmadev;

	STATUSLINK_INFO	info;
	unsigned int screen;
	unsigned int version;
	unsigned char id[8];
	char *model;

	/*
 	* We use fake EDID info so that userspace know that it is dealing with
 	* an Acer projector, rather then listing this as an "unknown" monitor.
 	* Note this assumes this driver is only ever used with the Acer C120, if we
 	* add support for other devices the vendor and model should be parameterized.
 	*/
	struct edid s_edid;

	unsigned int	width;
	unsigned int	height;
	unsigned int	margin;
	unsigned int	width_mm;
	unsigned int	height_mm;

	unsigned char	*cmd_buf;
	struct transmitter	trans[TRANSMITTER_NUM];
	struct drm_rect old_rect;
	unsigned int	misc_rcv_ept;
	unsigned int	misc_snd_ept;
	unsigned int	data_snd_ept;
};

#define to_beada(__dev) container_of(__dev, struct beada_device, dev)

int beada_send_tag(struct beada_device *beada, struct transmitter *trans, const char* cmd);
int beada_misc_request(struct beada_device *beada);
int beada_buf_copy(void *dst, const struct iosys_map *map, struct drm_framebuffer *fb, struct drm_rect *clip, struct drm_format_conv_state *fmtcnv_state);
void beada_fb_update_work(struct delayed_work *work);
int beada_conn_get_modes(struct drm_connector *connector);
int beada_edid_block_checksum(u8 *raw_edid);
void beada_edid_setup(struct beada_device *beada);
int beada_transmitter_init(struct beada_device *beada);
void beada_fb_mark_dirty(struct drm_framebuffer *fb, const struct iosys_map *map, struct drm_rect *rect, struct drm_format_conv_state *fmtcnv_state);
void beada_stop_fb_update(struct beada_device *beada);
int beada_set_backlight(struct beada_device *beada, int val);

#endif /* _BEADA_DEVICE_H_ */

