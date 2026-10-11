#include "rtw.h"
IOUSBDeviceInterface942 **g_di;

static int xfer(int in, u32 a, void *buf, u16 len) {
    IOUSBDevRequest rq = { .bmRequestType = USBmakebmRequestType(in ? kUSBIn : kUSBOut, kUSBVendor, kUSBDevice), .bRequest = 0x05,
        .wValue = a & 0xffff, .wIndex = (a >> 16) & 0xff, .wLength = len, .pData = buf };
    IOReturn r = (*g_di)->DeviceRequest(g_di, &rq);
    if (r) fprintf(stderr, "xfer %s 0x%x failed 0x%x\n", in ? "rd" : "wr", a, r);
    return r;
}
u32 rd32(u32 a) { u32 v = 0; xfer(1, a, &v, 4); return v; }
u16 rd16(u32 a) { u16 v = 0; xfer(1, a, &v, 2); return v; }
u8  rd8 (u32 a) { u8  v = 0; xfer(1, a, &v, 1); return v; }
void wr32(u32 a, u32 v) { xfer(0, a, &v, 4); }
void wr16(u32 a, u16 v) { xfer(0, a, &v, 2); }
void wr8 (u32 a, u8  v) { xfer(0, a, &v, 1); }

int rtw_open(void) {
    io_iterator_t it; IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOUSBHostDevice"), &it);
    io_service_t d;
    while ((d = IOIteratorNext(it))) {
        IOCFPlugInInterface **pl; SInt32 s;
        if (IOCreatePlugInInterfaceForService(d, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &pl, &s) || !pl) continue;
        (*pl)->QueryInterface(pl, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID942), (LPVOID *)&g_di);
        IODestroyPlugInInterface(pl);
        UInt16 v, p; (*g_di)->GetDeviceVendor(g_di, &v); (*g_di)->GetDeviceProduct(g_di, &p);
        if (v == 0x35bc && p == 0x0108) return (*g_di)->USBDeviceOpenSeize(g_di);
    }
    return -1;
}
void rtw_close(void) { (*g_di)->USBDeviceClose(g_di); }
