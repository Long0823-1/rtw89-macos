/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Linux USB API subset implemented on top of IOKit (host/usb_shim.c). */
#ifndef KC_LINUX_USB_H
#define KC_LINUX_USB_H
#include "../kcompat.h"

typedef unsigned long kernel_ulong_t;
enum usb_device_speed { USB_SPEED_UNKNOWN = 0, USB_SPEED_LOW, USB_SPEED_FULL, USB_SPEED_HIGH, USB_SPEED_WIRELESS, USB_SPEED_SUPER, USB_SPEED_SUPER_PLUS };
#define USB_ENDPOINT_NUMBER_MASK 0x0f
#define USB_ENDPOINT_DIR_MASK 0x80
#define USB_DIR_OUT 0
#define USB_DIR_IN 0x80
#define USB_ENDPOINT_XFERTYPE_MASK 0x03
#define USB_ENDPOINT_XFER_CONTROL 0
#define USB_ENDPOINT_XFER_ISOC 1
#define USB_ENDPOINT_XFER_BULK 2
#define USB_ENDPOINT_XFER_INT 3
#define USB_DEVICE_ID_MATCH_VENDOR 1
#define USB_DEVICE_ID_MATCH_PRODUCT 2
#define USB_DEVICE_ID_MATCH_INT_INFO 4
#define URB_ZERO_PACKET 0x40
#define URB_SHORT_NOT_OK 1
#define PIPE_CONTROL 2
#define PIPE_BULK 3
#define USB_TYPE_VENDOR 0x40
#define USB_RECIP_DEVICE 0
#define USB_CTRL_GET_TIMEOUT 5000
#define USB_CTRL_SET_TIMEOUT 5000

struct usb_endpoint_descriptor {
	u8 bLength, bDescriptorType, bEndpointAddress, bmAttributes;
	__le16 wMaxPacketSize;
	u8 bInterval;
};
struct usb_interface_descriptor {
	u8 bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting, bNumEndpoints, bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
};
struct usb_host_endpoint { struct usb_endpoint_descriptor desc; };
struct usb_host_interface { struct usb_interface_descriptor desc; struct usb_host_endpoint *endpoint; };
struct usb_device {
	enum usb_device_speed speed;
	struct device dev;
	void *iokit_dev;	/* IOUSBDeviceInterface942 ** */
};
struct usb_interface {
	struct usb_host_interface *altsetting;
	struct usb_host_interface *cur_altsetting;
	struct device dev;
	struct usb_device *udev;
	void *intfdata;
	void *iokit_if;		/* IOUSBInterfaceInterface942 ** */
};
struct usb_device_id {
	u16 match_flags, idVendor, idProduct;
	u8 bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol;
	kernel_ulong_t driver_info;
};
#define USB_DEVICE_AND_INTERFACE_INFO(vend, prod, cl, sc, pr) \
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR | USB_DEVICE_ID_MATCH_PRODUCT | USB_DEVICE_ID_MATCH_INT_INFO, \
	.idVendor = (vend), .idProduct = (prod), .bInterfaceClass = (cl), .bInterfaceSubClass = (sc), .bInterfaceProtocol = (pr)
struct usb_driver {
	const char *name;
	int (*probe)(struct usb_interface *intf, const struct usb_device_id *id);
	void (*disconnect)(struct usb_interface *intf);
	const struct usb_device_id *id_table;
};
extern struct usb_driver *kc_usb_driver_ptr;
#define module_usb_driver(d) struct usb_driver *kc_usb_driver_ptr = &(d);
struct usb_anchor { struct list_head urb_list; spinlock_t lock; };
struct urb {
	struct list_head anchor_list;
	struct usb_anchor *anchor;
	struct usb_device *dev;
	unsigned int pipe;
	int status;
	unsigned int transfer_flags;
	void *transfer_buffer;
	u32 transfer_buffer_length;
	u32 actual_length;
	void *context;
	void (*complete)(struct urb *);
	void *kc_priv;
	int kc_inflight;
	int kc_ref;
	struct list_head kc_q;
	int kc_queued;
};

#define __usb_pipe(t, ep, dir) (((unsigned)(t) << 30) | ((unsigned)(ep) << 15) | (dir))
#define usb_sndbulkpipe(d, ep) __usb_pipe(PIPE_BULK, ep, USB_DIR_OUT)
#define usb_rcvbulkpipe(d, ep) __usb_pipe(PIPE_BULK, ep, USB_DIR_IN)
#define usb_sndctrlpipe(d, ep) __usb_pipe(PIPE_CONTROL, ep, USB_DIR_OUT)
#define usb_rcvctrlpipe(d, ep) __usb_pipe(PIPE_CONTROL, ep, USB_DIR_IN)
#define usb_pipeendpoint(p) (((p) >> 15) & 0xf)
#define usb_pipein(p) ((p) & USB_DIR_IN)
static inline u8 usb_endpoint_num(const struct usb_endpoint_descriptor *e) { return e->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK; }
static inline int usb_endpoint_dir_in(const struct usb_endpoint_descriptor *e) { return (e->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN; }
static inline int usb_endpoint_dir_out(const struct usb_endpoint_descriptor *e) { return (e->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT; }
static inline int usb_endpoint_xfer_bulk(const struct usb_endpoint_descriptor *e) { return (e->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK; }

#define interface_to_usbdev(i) ((i)->udev)
#define usb_get_intfdata(i) ((i)->intfdata)
#define usb_set_intfdata(i, d) ((i)->intfdata = (d))
#define usb_get_dev(d) (d)
#define usb_put_dev(d) do {} while (0)

extern struct urb *usb_alloc_urb(int iso_packets, gfp_t gfp);
extern void usb_free_urb(struct urb *urb);
extern int usb_submit_urb(struct urb *urb, gfp_t gfp);
extern void usb_kill_urb(struct urb *urb);
extern void init_usb_anchor(struct usb_anchor *a);
extern void usb_anchor_urb(struct urb *urb, struct usb_anchor *a);
extern void usb_unanchor_urb(struct urb *urb);
extern void usb_kill_anchored_urbs(struct usb_anchor *a);
extern int usb_control_msg(struct usb_device *dev, unsigned int pipe, u8 request, u8 requesttype, u16 value, u16 index, void *data, u16 size, int timeout);
static inline void usb_fill_bulk_urb(struct urb *urb, struct usb_device *dev, unsigned int pipe, void *buf, int len, void (*complete)(struct urb *), void *ctx)
{
	urb->dev = dev; urb->pipe = pipe; urb->transfer_buffer = buf; urb->transfer_buffer_length = len; urb->complete = complete; urb->context = ctx;
}

#endif
