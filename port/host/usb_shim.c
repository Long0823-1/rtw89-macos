// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Linux USB API subset implemented on IOKit (IOUSBHostDevice via IOUSBDeviceInterface/IOUSBInterfaceInterface).
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <kcompat.h>
#include <linux/usb.h>

static IOUSBDeviceInterface942 **g_dev;
static IOUSBInterfaceInterface942 **g_if;
static u8 pipe_of_in[16], pipe_of_out[16];
static CFRunLoopRef g_rl;
static pthread_t g_rl_thr;
static pthread_mutex_t g_submit_lock = PTHREAD_MUTEX_INITIALIZER;

static int map_status(IOReturn r)
{
	switch (r) {
	case kIOReturnSuccess: return 0;
	case kIOReturnAborted: return -ECONNRESET;
	case kIOReturnNoDevice: return -ENODEV;
	case kIOUSBPipeStalled: return -EPIPE;
	case kIOUSBTransactionTimeout: return -EPROTO;
	default: return -EPROTO;
	}
}

static volatile int g_rl_stop;
static int g_rl_running;
static void *rl_thread(void *arg)
{
	g_rl = CFRunLoopGetCurrent();
	CFRunLoopSourceRef src;
	(*g_if)->CreateInterfaceAsyncEventSource(g_if, &src);
	CFRunLoopAddSource(g_rl, src, kCFRunLoopDefaultMode);
	while (!g_rl_stop)
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);
	CFRunLoopRemoveSource(g_rl, src, kCFRunLoopDefaultMode);
	return NULL;
}

/* ---- anchors / urbs ---- */
void init_usb_anchor(struct usb_anchor *a) { INIT_LIST_HEAD(&a->urb_list); spin_lock_init(&a->lock); }
struct urb *usb_alloc_urb(int iso, gfp_t gfp) { struct urb *u = calloc(1, sizeof(*u)); if (u) { INIT_LIST_HEAD(&u->anchor_list); u->kc_ref = 1; } return u; }
void usb_free_urb(struct urb *urb) { if (urb && __atomic_sub_fetch(&urb->kc_ref, 1, __ATOMIC_SEQ_CST) == 0) free(urb); }
void usb_anchor_urb(struct urb *urb, struct usb_anchor *a)
{
	spin_lock(&a->lock);
	list_add_tail(&urb->anchor_list, &a->urb_list);
	urb->anchor = a;
	spin_unlock(&a->lock);
}
void usb_unanchor_urb(struct urb *urb)
{
	struct usb_anchor *a = urb->anchor;
	if (!a) return;
	spin_lock(&a->lock);
	if (urb->anchor) { list_del_init(&urb->anchor_list); urb->anchor = NULL; }
	spin_unlock(&a->lock);
}

static void urb_done(struct urb *urb, IOReturn result, u32 actual)
{
	urb->actual_length = actual;
	urb->status = map_status(result);
	usb_unanchor_urb(urb);
	__atomic_store_n(&urb->kc_inflight, 0, __ATOMIC_SEQ_CST);
	if (urb->complete)
		urb->complete(urb);
	usb_free_urb(urb);	/* drop the reference taken at submit */
}

static int usbdbg = -1;
static void out_drain(unsigned char ep);
extern unsigned long usb_st_in_sub, usb_st_in_done, usb_st_out_sub, usb_st_out_done, usb_st_err;
static int out_inflight_dec(unsigned char ep);
extern unsigned long usb_st_in_sub, usb_st_in_done, usb_st_out_sub, usb_st_out_done, usb_st_err;
static void io_cb(void *refcon, IOReturn result, void *arg0)
{
	struct urb *urb = refcon;
	u32 actual = (u32)(uintptr_t)arg0;
	if (usb_pipein(urb->pipe)) __atomic_add_fetch(&usb_st_in_done, 1, __ATOMIC_RELAXED); else __atomic_add_fetch(&usb_st_out_done, 1, __ATOMIC_RELAXED);
	if (result != kIOReturnSuccess) __atomic_add_fetch(&usb_st_err, 1, __ATOMIC_RELAXED);
	if (result != kIOReturnSuccess && result != kIOReturnAborted) {
		static int logged2;
		if (logged2++ < 20) fprintf(stderr, "[usb] %s ep%d completion error 0x%x (len %u)\n", usb_pipein(urb->pipe) ? "IN" : "OUT", usb_pipeendpoint(urb->pipe), result, actual);
		if (result == kIOUSBPipeStalled) (*g_if)->ClearPipeStallBothEnds(g_if, usb_pipein(urb->pipe) ? pipe_of_in[usb_pipeendpoint(urb->pipe)] : pipe_of_out[usb_pipeendpoint(urb->pipe)]);
	}
	if (usbdbg < 0) usbdbg = getenv("RTW89_USBDBG") != NULL;
	if (usbdbg) fprintf(stderr, "[usb] %s done ep%d result=0x%x len=%u\n", usb_pipein(urb->pipe) ? "IN" : "OUT", usb_pipeendpoint(urb->pipe), result, actual);
	if (!usb_pipein(urb->pipe)) {
		unsigned char epn = usb_pipeendpoint(urb->pipe);
		pthread_mutex_lock(&g_submit_lock);
		if (out_inflight_dec(epn) >= 0) out_drain(epn);
		pthread_mutex_unlock(&g_submit_lock);
	}
	urb_done(urb, result, actual);
}

/* IOKit rejects async writes with kIOReturnBusy when too many are outstanding on a pipe, so keep a bounded
 * in-flight window per OUT endpoint and queue the rest (completions drain the queue). */
#define OUT_WINDOW 12
static struct list_head out_q[16];
static int out_inflight[16];
static int out_q_init;

static void zlp_cb(void *refcon, IOReturn result, void *arg0) {}

static int submit_now(struct urb *urb)
{
	u8 ep = usb_pipeendpoint(urb->pipe);
	if (usb_pipein(urb->pipe)) __atomic_add_fetch(&usb_st_in_sub, 1, __ATOMIC_RELAXED); else __atomic_add_fetch(&usb_st_out_sub, 1, __ATOMIC_RELAXED);
	if (usb_pipein(urb->pipe))
		return (*g_if)->ReadPipeAsync(g_if, pipe_of_in[ep], urb->transfer_buffer, urb->transfer_buffer_length, io_cb, urb);
	{
		IOReturn r = (*g_if)->WritePipeAsyncTO(g_if, pipe_of_out[ep], urb->transfer_buffer, urb->transfer_buffer_length, 3000, 6000, io_cb, urb);
		/* A transfer that is an exact multiple of the max packet size needs a zero-length packet to terminate it. It must be
		 * queued right behind the data (callers hold g_submit_lock): sending it after completion lets the next queued
		 * transfer run into this one, and the chip's host dispatcher reports a payload overflow and halts. */
		if (r == kIOReturnSuccess && (urb->transfer_flags & URB_ZERO_PACKET) && urb->transfer_buffer_length &&
		    (urb->transfer_buffer_length % 512) == 0)
			(*g_if)->WritePipeAsync(g_if, pipe_of_out[ep], (void *)urb->transfer_buffer, 0, zlp_cb, NULL);
		return r;
	}
}

static int out_inflight_dec(unsigned char ep) { if (out_inflight[ep] > 0) out_inflight[ep]--; return out_inflight[ep]; }
static void out_drain(unsigned char ep)	/* caller holds g_submit_lock */
{
	while (!list_empty(&out_q[ep]) && out_inflight[ep] < OUT_WINDOW) {
		struct urb *u = list_first_entry(&out_q[ep], struct urb, kc_q);
		IOReturn r;
		list_del_init(&u->kc_q);
		u->kc_queued = 0;
		r = submit_now(u);
		if (r == kIOReturnBusy) { list_add(&u->kc_q, &out_q[ep]); u->kc_queued = 1; break; }
		if (r) {
			/* hard failure: complete the urb with an error */
			__atomic_store_n(&u->kc_inflight, 0, __ATOMIC_SEQ_CST);
			u->status = map_status(r);
			u->actual_length = 0;
			usb_unanchor_urb(u);
			if (u->complete) u->complete(u);
			usb_free_urb(u);
			continue;
		}
		out_inflight[ep]++;
	}
}

int usb_submit_urb(struct urb *urb, gfp_t gfp)
{
	u8 ep = usb_pipeendpoint(urb->pipe);
	IOReturn r = 0;
	if (!g_if) return -ENODEV;
	if (!out_q_init) { for (int i = 0; i < 16; i++) INIT_LIST_HEAD(&out_q[i]); out_q_init = 1; }
	urb->status = -EINPROGRESS;
	urb->actual_length = 0;
	INIT_LIST_HEAD(&urb->kc_q);
	urb->kc_queued = 0;
	__atomic_add_fetch(&urb->kc_ref, 1, __ATOMIC_SEQ_CST);
	__atomic_store_n(&urb->kc_inflight, 1, __ATOMIC_SEQ_CST);
	pthread_mutex_lock(&g_submit_lock);
	if (!usb_pipein(urb->pipe)) {
		if (out_inflight[ep] >= OUT_WINDOW || !list_empty(&out_q[ep])) {
			list_add_tail(&urb->kc_q, &out_q[ep]);
			urb->kc_queued = 1;
			pthread_mutex_unlock(&g_submit_lock);
			return 0;
		}
		r = submit_now(urb);
		if (r == kIOReturnBusy) {
			list_add_tail(&urb->kc_q, &out_q[ep]);
			urb->kc_queued = 1;
			pthread_mutex_unlock(&g_submit_lock);
			return 0;
		}
		if (!r) out_inflight[ep]++;
	} else {
		r = submit_now(urb);
	}
	pthread_mutex_unlock(&g_submit_lock);
	if (r) {
		static int logged;
		if (logged++ < 20) fprintf(stderr, "[usb] submit %s ep%d failed: 0x%x\n", usb_pipein(urb->pipe) ? "IN" : "OUT", ep, r);
		if (r == kIOUSBPipeStalled)
			(*g_if)->ClearPipeStallBothEnds(g_if, usb_pipein(urb->pipe) ? pipe_of_in[ep] : pipe_of_out[ep]);
		__atomic_store_n(&urb->kc_inflight, 0, __ATOMIC_SEQ_CST);
		__atomic_sub_fetch(&urb->kc_ref, 1, __ATOMIC_SEQ_CST);
		urb->status = map_status(r);
		return r == kIOReturnNoDevice ? -ENODEV : -EIO;
	}
	return 0;
}

void usb_kill_urb(struct urb *urb)
{
	u8 ep = usb_pipeendpoint(urb->pipe);
	if (!__atomic_load_n(&urb->kc_inflight, __ATOMIC_SEQ_CST)) return;
	pthread_mutex_lock(&g_submit_lock);
	if (urb->kc_queued) {
		list_del_init(&urb->kc_q);
		urb->kc_queued = 0;
		pthread_mutex_unlock(&g_submit_lock);
		urb_done(urb, kIOReturnAborted, 0);
		return;
	}
	pthread_mutex_unlock(&g_submit_lock);
	(*g_if)->AbortPipe(g_if, usb_pipein(urb->pipe) ? pipe_of_in[ep] : pipe_of_out[ep]);
	for (int i = 0; i < 2000 && __atomic_load_n(&urb->kc_inflight, __ATOMIC_SEQ_CST); i++)
		usleep(1000);
}

void usb_kill_anchored_urbs(struct usb_anchor *a)
{
	for (;;) {
		struct urb *u = NULL;
		spin_lock(&a->lock);
		if (!list_empty(&a->urb_list)) u = list_first_entry(&a->urb_list, struct urb, anchor_list);
		spin_unlock(&a->lock);
		if (!u) break;
		usb_kill_urb(u);
		spin_lock(&a->lock);
		if (u->anchor) { list_del_init(&u->anchor_list); u->anchor = NULL; }
		spin_unlock(&a->lock);
	}
}

int usb_control_msg(struct usb_device *dev, unsigned int pipe, u8 request, u8 requesttype, u16 value, u16 index, void *data, u16 size, int timeout)
{
	IOUSBDevRequestTO rq = { .bmRequestType = requesttype, .bRequest = request, .wValue = value, .wIndex = index,
				 .wLength = size, .pData = data, .noDataTimeout = timeout, .completionTimeout = timeout };
	IOReturn r = (*g_dev)->DeviceRequestTO(g_dev, &rq);
	if (r) return map_status(r) ? map_status(r) : -EIO;
	return (int)rq.wLenDone;
}

/* ---- device open / probe ---- */
static struct usb_device g_udev;
static struct usb_interface g_intf;
static struct usb_host_interface g_alt;
static struct usb_host_endpoint g_eps[16];

static int open_device(u16 vid, u16 pid)
{
	io_iterator_t it;
	io_service_t s;
	IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOUSBHostDevice"), &it);
	while ((s = IOIteratorNext(it))) {
		IOCFPlugInInterface **pl; SInt32 sc;
		IOUSBDeviceInterface942 **di = NULL;
		if (IOCreatePlugInInterfaceForService(s, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &pl, &sc) || !pl) continue;
		(*pl)->QueryInterface(pl, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID942), (LPVOID *)&di);
		IODestroyPlugInInterface(pl);
		UInt16 v, p;
		(*di)->GetDeviceVendor(di, &v); (*di)->GetDeviceProduct(di, &p);
		if (v != vid || p != pid) { (*di)->Release(di); continue; }
		IOReturn r = (*di)->USBDeviceOpenSeize(di);
		if (r) { fprintf(stderr, "usb: device open failed 0x%x\n", r); return -1; }
		g_dev = di;
		return 0;
	}
	return -1;
}

int kc_usb_find_and_probe(const struct usb_driver *drv)
{
	const struct usb_device_id *id;
	for (id = drv->id_table; id->idVendor; id++)
		if (open_device(id->idVendor, id->idProduct) == 0)
			goto found;
	fprintf(stderr, "usb: no supported adapter found (is it in Wi-Fi mode? run modeswitch3)\n");
	return -ENODEV;
found:;
	/* interface 0 */
	IOUSBFindInterfaceRequest rq = { kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare };
	io_iterator_t it; (*g_dev)->CreateInterfaceIterator(g_dev, &rq, &it);
	io_service_t s = IOIteratorNext(it);
	if (!s) { fprintf(stderr, "usb: no interface\n"); return -ENODEV; }
	IOCFPlugInInterface **pl; SInt32 sc;
	IOCreatePlugInInterfaceForService(s, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &pl, &sc);
	(*pl)->QueryInterface(pl, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID942), (LPVOID *)&g_if);
	IODestroyPlugInInterface(pl);
	IOReturn r = (*g_if)->USBInterfaceOpenSeize(g_if);
	if (r) { fprintf(stderr, "usb: interface open failed 0x%x\n", r); return -EBUSY; }
	UInt8 n; (*g_if)->GetNumEndpoints(g_if, &n);
	for (UInt8 i = 1; i <= n && i <= 16; i++) {
		UInt8 dir, num, tt, in; UInt16 mps;
		(*g_if)->GetPipeProperties(g_if, i, &dir, &num, &tt, &mps, &in);
		struct usb_endpoint_descriptor *e = &g_eps[i - 1].desc;
		e->bLength = 7; e->bDescriptorType = 5;
		e->bEndpointAddress = num | (dir == kUSBIn ? USB_DIR_IN : 0);
		e->bmAttributes = tt == kUSBBulk ? USB_ENDPOINT_XFER_BULK : tt == kUSBInterrupt ? USB_ENDPOINT_XFER_INT : 0;
		e->wMaxPacketSize = mps; e->bInterval = in;
		if (dir == kUSBIn) pipe_of_in[num] = i; else pipe_of_out[num] = i;
	}
	g_alt.desc.bNumEndpoints = n;
	g_alt.endpoint = g_eps;
	UInt8 spd = 0; (*g_dev)->GetDeviceSpeed(g_dev, &spd);
	g_udev.speed = spd >= kUSBDeviceSpeedSuper ? USB_SPEED_SUPER : spd == kUSBDeviceSpeedHigh ? USB_SPEED_HIGH : spd == kUSBDeviceSpeedFull ? USB_SPEED_FULL : USB_SPEED_LOW;
	g_intf.altsetting = &g_alt;
	g_intf.cur_altsetting = &g_alt;
	g_intf.udev = &g_udev;
	g_intf.dev.name = "rtw89-usb";
	g_rl_stop = 0;
	pthread_create(&g_rl_thr, NULL, rl_thread, NULL);
	g_rl_running = 1;
	usleep(50000);
	UInt16 v, p; (*g_dev)->GetDeviceVendor(g_dev, &v); (*g_dev)->GetDeviceProduct(g_dev, &p);
	for (id = drv->id_table; id->idVendor; id++)
		if (id->idVendor == v && id->idProduct == p) break;
	fprintf(stderr, "usb: opened %04x:%04x speed=%s, %d endpoints\n", v, p, g_udev.speed == USB_SPEED_SUPER ? "SS" : "HS", n);
	return drv->probe(&g_intf, id);
}

void kc_usb_disconnect(const struct usb_driver *drv)
{
	drv->disconnect(&g_intf);
}

/* ---- lifecycle helpers used by the daemon ---- */


void kc_usb_close(void)
{
	if (g_rl_running) { g_rl_stop = 1; pthread_join(g_rl_thr, NULL); g_rl_running = 0; g_rl_stop = 0; }
	if (g_if) { (*g_if)->USBInterfaceClose(g_if); (*g_if)->Release(g_if); g_if = NULL; }
	if (g_dev) { (*g_dev)->USBDeviceClose(g_dev); (*g_dev)->Release(g_dev); g_dev = NULL; }
	memset(pipe_of_in, 0, sizeof pipe_of_in);
	memset(pipe_of_out, 0, sizeof pipe_of_out);
}

/* true if a USB device with this vid:pid is attached (without opening it) */
int kc_usb_present(u16 vid, u16 pid)
{
	io_iterator_t it;
	io_service_t s;
	int found = 0;
	IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOUSBHostDevice"), &it);
	while ((s = IOIteratorNext(it))) {
		CFNumberRef v = IORegistryEntryCreateCFProperty(s, CFSTR("idVendor"), 0, 0);
		CFNumberRef p = IORegistryEntryCreateCFProperty(s, CFSTR("idProduct"), 0, 0);
		int vv = 0, pp = 0;
		if (v) { CFNumberGetValue(v, kCFNumberIntType, &vv); CFRelease(v); }
		if (p) { CFNumberGetValue(p, kCFNumberIntType, &pp); CFRelease(p); }
		IOObjectRelease(s);
		if (vv == vid && pp == pid) found = 1;
	}
	IOObjectRelease(it);
	return found;
}

/* Realtek adapters enumerate first as a virtual CD (0bda:1a2b). Send the vendor command that makes them re-enumerate as Wi-Fi. */
/* The virtual CD is an ordinary IOCDMedia to macOS, and its mass-storage driver owns the USB interface, so the supported way to
 * send the switching SCSI eject is to let macOS do it (diskutil eject) rather than seizing the interface. */
static int eject_virtual_cd(void)
{
	io_iterator_t it;
	io_service_t m;
	int ok = -1;
	if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOCDMedia"), &it)) return -1;
	while ((m = IOIteratorNext(it))) {
		CFTypeRef vid = IORegistryEntrySearchCFProperty(m, kIOServicePlane, CFSTR("idVendor"), kCFAllocatorDefault,
								kIORegistryIterateParents | kIORegistryIterateRecursively);
		CFTypeRef pid = IORegistryEntrySearchCFProperty(m, kIOServicePlane, CFSTR("idProduct"), kCFAllocatorDefault,
								kIORegistryIterateParents | kIORegistryIterateRecursively);
		int v = 0, p = 0;
		if (vid) { CFNumberGetValue(vid, kCFNumberIntType, &v); CFRelease(vid); }
		if (pid) { CFNumberGetValue(pid, kCFNumberIntType, &p); CFRelease(pid); }
		fprintf(stderr, "usb: eject: IOCDMedia vendor 0x%x product 0x%x\n", v, p);
		if (v == 0x0bda && p == 0x1a2b) {
			CFTypeRef bsd = IORegistryEntryCreateCFProperty(m, CFSTR("BSD Name"), kCFAllocatorDefault, 0);
			char name[64];
			if (bsd && CFStringGetCString(bsd, name, sizeof name, kCFStringEncodingUTF8)) {
				char cmd[160];
				snprintf(cmd, sizeof cmd, "/usr/sbin/diskutil eject /dev/%s >/dev/null 2>&1", name);
				int r = system(cmd);
				fprintf(stderr, "usb: diskutil eject %s -> %d\n", name, r);
				if (r == 0) ok = 0;
			}
			if (bsd) CFRelease(bsd);
		}
		IOObjectRelease(m);
	}
	IOObjectRelease(it);
	return ok;
}

int kc_usb_modeswitch(void)
{
	if (eject_virtual_cd() == 0) return 0;
	io_iterator_t it;
	io_service_t s;
	int done = 0;
	IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOUSBHostDevice"), &it);
	while ((s = IOIteratorNext(it))) {
		IOCFPlugInInterface **pl; SInt32 sc;
		IOUSBDeviceInterface942 **di = NULL;
		if (IOCreatePlugInInterfaceForService(s, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &pl, &sc) || !pl) continue;
		(*pl)->QueryInterface(pl, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID942), (LPVOID *)&di);
		IODestroyPlugInInterface(pl);
		UInt16 v, p;
		(*di)->GetDeviceVendor(di, &v); (*di)->GetDeviceProduct(di, &p);
		if (v != 0x0bda || p != 0x1a2b) { (*di)->Release(di); continue; }
		if ((*di)->USBDeviceOpenSeize(di)) { (*di)->Release(di); continue; }
		int reconf = 0;
	again:;
		IOUSBFindInterfaceRequest rq = { kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare };
		io_iterator_t ii; (*di)->CreateInterfaceIterator(di, &rq, &ii);
		io_service_t is = IOIteratorNext(ii);
		if (is) {
			IOCFPlugInInterface **ipl;
			IOUSBInterfaceInterface942 **intf = NULL;
			IOCreatePlugInInterfaceForService(is, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &ipl, &sc);
			(*ipl)->QueryInterface(ipl, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID942), (LPVOID *)&intf);
			IODestroyPlugInInterface(ipl);
			/* the kernel mass-storage driver owns this interface and is often busy probing it; keep trying to seize it */
			IOReturn orr = intf ? (*intf)->USBInterfaceOpenSeize(intf) : -1;
			for (int w = 0; intf && orr == 0xe00002c5 && w < 50; w++) { usleep(200000); orr = (*intf)->USBInterfaceOpenSeize(intf); }
			if (orr) {
				fprintf(stderr, "usb: mode switch cannot open the interface (0x%x)\n", orr);
				if (orr == 0xe00002c5 && !reconf) {
					/* the kernel mass-storage driver will not let go: re-applying the configuration (we own the device) makes
					 * macOS rebuild the interface nodes, and we race it for the new one */
					reconf = 1;
					UInt8 cv = 0;
					(*di)->GetConfiguration(di, &cv);
					IOReturn rc = (*di)->SetConfiguration(di, cv ? cv : 1);
					fprintf(stderr, "usb: re-applied configuration %d (0x%x)\n", cv ? cv : 1, rc);
					if (!rc) { (*intf)->Release(intf); usleep(100000); goto again; }
				} else if (orr == 0xe00002c0 && reconf && reconf < 40) {
					/* the interface node we grabbed was torn down by the reconfiguration; look up the new one */
					reconf++;
					(*intf)->Release(intf);
					usleep(100000);
					goto again;
				}
			}
			if (intf && orr == 0) {
				UInt8 n, outp = 0;
				(*intf)->GetNumEndpoints(intf, &n);
				for (UInt8 i = 1; i <= n; i++) {
					UInt8 dir, num, tt, in; UInt16 mps;
					(*intf)->GetPipeProperties(intf, i, &dir, &num, &tt, &mps, &in);
					if (dir == kUSBOut && tt == kUSBBulk) outp = i;
				}
				/* Realtek vendor command first; if the device answers "command failed" (CSW status 1) rotate through the
				 * standard SCSI eject and prevent/allow-removal commands on later attempts. */
				static int attempt;
				unsigned char cbw[31] = { 0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0x00, 0x00, 0x10, 0xff };
				const unsigned char eject[] = { 0x1b, 0, 0, 0, 0x02, 0 };		/* START STOP UNIT, LoEj */
				const unsigned char allow[] = { 0x1e, 0, 0, 0, 0x00, 0 };		/* ALLOW MEDIUM REMOVAL */
				int variant = attempt++ % 3;
				if (variant == 1) { memset(cbw + 15, 0, 16); cbw[14] = 6; memcpy(cbw + 15, eject, sizeof eject); }
				if (variant == 2) { memset(cbw + 15, 0, 16); cbw[14] = 6; memcpy(cbw + 15, allow, sizeof allow); }
				IOReturn r = (*intf)->WritePipeTO(intf, outp, cbw, sizeof cbw, 1000, 2000);
				unsigned char csw[64]; UInt32 cl = sizeof csw; UInt8 inp = 0;
				for (UInt8 i = 1; i <= n; i++) { UInt8 dir, num, tt, in; UInt16 mps; (*intf)->GetPipeProperties(intf, i, &dir, &num, &tt, &mps, &in); if (dir == kUSBIn && tt == kUSBBulk) inp = i; }
				IOReturn r2 = inp ? (*intf)->ReadPipeTO(intf, inp, csw, &cl, 500, 1000) : -1;
				fprintf(stderr, "usb: mode switch variant %d sent (write 0x%x, status read 0x%x, CSW status %d)\n", variant, r, r2, r2 == 0 && cl >= 13 ? csw[12] : -1);
				done = 1;
				(*intf)->USBInterfaceClose(intf);
			}
		}
		(*di)->USBDeviceClose(di);
		(*di)->Release(di);
	}
	IOObjectRelease(it);
	return done ? 0 : -1;
}

/* ---- diagnostics ---- */
unsigned long usb_st_in_sub, usb_st_in_done, usb_st_out_sub, usb_st_out_done, usb_st_err;

void kc_usb_stats(char *buf, size_t n)
{
	int qlen[16] = {0}, tot = 0;
	pthread_mutex_lock(&g_submit_lock);
	for (int e = 0; e < 16; e++) { struct list_head *p; if (out_q_init) list_for_each(p, &out_q[e]) qlen[e]++; tot += qlen[e]; }
	int infl = 0; for (int e = 0; e < 16; e++) infl += out_inflight[e];
	pthread_mutex_unlock(&g_submit_lock);
	snprintf(buf, n, "IN submitted %lu completed %lu (outstanding %ld) | OUT submitted %lu completed %lu | OUT inflight %d queued %d | errors %lu",
		 usb_st_in_sub, usb_st_in_done, (long)(usb_st_in_sub - usb_st_in_done), usb_st_out_sub, usb_st_out_done, infl, tot, usb_st_err);
}

/* Number of OUT transfers that are queued or in flight on the data endpoints (for TX back-pressure). */
int kc_usb_tx_backlog(void)
{
	int tot = 0;
	pthread_mutex_lock(&g_submit_lock);
	for (int e = 0; e < 16; e++) {
		tot += out_inflight[e];
		if (out_q_init) { struct list_head *p; list_for_each(p, &out_q[e]) tot++; }
	}
	pthread_mutex_unlock(&g_submit_lock);
	return tot;
}
