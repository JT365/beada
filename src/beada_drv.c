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
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
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

#include "beada_device.h"

#define DRIVER_NAME		"beada"
#define DRIVER_DESC		"BeadaPanel USB Media Display"
#define DRIVER_DATE		"2024"
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0

static const struct drm_connector_helper_funcs beada_conn_helper_funcs = {
	.get_modes = beada_conn_get_modes,
};

static int beada_backlight_update_status(struct backlight_device *bd)
{
	struct beada_device *beada = bl_get_data(bd);
	int brightness = backlight_get_brightness(bd);

	return beada_set_backlight(beada, brightness);
}

static const struct backlight_ops beada_bl_ops = {
	.update_status = beada_backlight_update_status,
};

static int beada_conn_late_register(struct drm_connector *connector)
{
	struct beada_device *beada = to_beada(connector->dev);
	struct backlight_device *bl;

	bl = backlight_device_register("backlight",
					connector->kdev, beada,
					&beada_bl_ops, NULL);
	if (IS_ERR(bl)) {
		drm_err(connector->dev, "Unable to register backlight device\n");
		return -EIO;
	}

	bl->props.max_brightness = beada->info.max_brightness;
	bl->props.brightness = beada->info.current_brightness;
	beada->bl_dev = bl;

	return 0;
}

static void beada_conn_early_unregister(struct drm_connector *connector)
{
	struct beada_device *beada = to_beada(connector->dev);

	if (beada->bl_dev)
		backlight_device_unregister(beada->bl_dev);
}

static const struct drm_connector_funcs beada_conn_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.late_register = beada_conn_late_register,
	.early_unregister = beada_conn_early_unregister,
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
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_rect rect = {
		.x1 = 0,
		.x2 = fb->width,
		.y1 = 0,
		.y2 = fb->height,
	};

	beada_fb_mark_dirty(fb, &shadow_plane_state->data[0], &rect, &shadow_plane_state->fmtcnv_state);
}

static void beada_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct beada_device *beada = to_beada(pipe->crtc.dev);

	beada_stop_fb_update(beada);
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
		beada_fb_mark_dirty(fb, &shadow_plane_state->data[0], &rect, &shadow_plane_state->fmtcnv_state);
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
	
	beada->udev = interface_to_usbdev(interface);

	dev = &beada->dev;
	beada->dmadev = usb_intf_get_dma_device(to_usb_interface(dev->dev));
	if (!beada->dmadev)
		DRM_DEV_DEBUG(&beada->udev->dev, "buffer sharing not supported"); /* not an error */

	ret = beada_misc_request(beada);
	if (ret) {
		goto err_put_device;
	}

	ret = drmm_mode_config_init(dev);
	if (ret) {
		DRM_DEV_ERROR(&beada->udev->dev, "drmm_mode_config_init() return %d\n", ret);
		goto err_put_device;
	}

	beada_mode_config_setup(beada);
	beada_edid_setup(beada);
	
	ret = beada_transmitter_init(beada);
	if (ret) {
		goto err_put_device;
	}

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
