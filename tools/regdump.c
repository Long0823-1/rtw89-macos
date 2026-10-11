// Userspace prototype: vendor-request register access (rtw89 usb.c protocol) to RTL8852B at 35bc:0108.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdio.h>
#include <string.h>

static IOUSBDeviceInterface942 **di;

static int rd(uint32_t addr, void *buf, uint16_t len) {
    IOUSBDevRequest rq = { .bmRequestType = USBmakebmRequestType(kUSBIn, kUSBVendor, kUSBDevice), .bRequest = 0x05,
        .wValue = addr & 0xffff, .wIndex = (addr >> 16) & 0xff, .wLength = len, .pData = buf };
    return (*di)->DeviceRequest(di, &rq);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    io_iterator_t it; IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOUSBHostDevice"), &it);
    io_service_t d;
    while ((d = IOIteratorNext(it))) {
        IOCFPlugInInterface **pl; SInt32 s;
        if (IOCreatePlugInInterfaceForService(d, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &pl, &s) || !pl) continue;
        (*pl)->QueryInterface(pl, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID942), (LPVOID *)&di);
        IODestroyPlugInInterface(pl);
        UInt16 v, p; (*di)->GetDeviceVendor(di, &v); (*di)->GetDeviceProduct(di, &p);
        if (v == 0x35bc && p == 0x0108) goto found;
    }
    fprintf(stderr, "35bc:0108 not found (run modeswitch3 first)\n"); return 1;
found:;
    IOReturn r = (*di)->USBDeviceOpenSeize(di);
    printf("open: 0x%x\n", r);
    uint32_t regs[] = { 0x0000, 0x0004, 0x0008, 0x00F0, 0x00F4, 0x0100, 0x0200 };
    for (unsigned i = 0; i < sizeof regs / sizeof *regs; i++) {
        uint32_t val = 0xdeadbeef;
        r = rd(regs[i], &val, 4);
        printf("reg 0x%04x = 0x%08x (ret 0x%x)\n", regs[i], val, r);
    }
    (*di)->USBDeviceClose(di);
    return 0;
}
