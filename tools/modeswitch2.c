// Send SCSI START STOP UNIT (eject) to Realtek 0bda:1a2b via IOKit (seizing the mass-storage interface).
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    CFMutableDictionaryRef m = IOServiceMatching("IOUSBHostDevice");
    io_iterator_t it;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, m, &it)) { fprintf(stderr, "match failed\n"); return 1; }
    io_service_t dev; int found = 0;
    while ((dev = IOIteratorNext(it))) {
        IOCFPlugInInterface **plug; SInt32 score;
        if (IOCreatePlugInInterfaceForService(dev, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plug, &score) || !plug) { IOObjectRelease(dev); continue; }
        IOUSBDeviceInterface942 **di;
        (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID942), (LPVOID *)&di);
        IODestroyPlugInInterface(plug);
        UInt16 v, p; (*di)->GetDeviceVendor(di, &v); (*di)->GetDeviceProduct(di, &p);
        if (v != 0x0bda || p != 0x1a2b) { (*di)->Release(di); IOObjectRelease(dev); continue; }
        found = 1; printf("found %04x:%04x\n", v, p);
        IOUSBFindInterfaceRequest rq = { kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare };
        io_iterator_t ii; (*di)->CreateInterfaceIterator(di, &rq, &ii);
        io_service_t is = IOIteratorNext(ii);
        if (!is) { fprintf(stderr, "no interface\n"); return 2; }
        IOCFPlugInInterface **iplug;
        IOCreatePlugInInterfaceForService(is, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &iplug, &score);
        IOUSBInterfaceInterface942 **intf;
        (*iplug)->QueryInterface(iplug, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID942), (LPVOID *)&intf);
        IODestroyPlugInInterface(iplug);
        IOReturn r = (*intf)->USBInterfaceOpenSeize(intf);
        printf("open seize: 0x%x\n", r);
        if (r) return 3;
        UInt8 n; (*intf)->GetNumEndpoints(intf, &n);
        UInt8 out = 0;
        for (UInt8 i = 1; i <= n; i++) {
            UInt8 dir, num, tt, in; UInt16 mps;
            (*intf)->GetPipeProperties(intf, i, &dir, &num, &tt, &mps, &in);
            printf(" pipe %d dir %d num %d type %d\n", i, dir, num, tt);
            if (dir == kUSBOut && tt == kUSBBulk) out = i;
        }
        unsigned char cbw[31] = {0x55,0x53,0x42,0x43, 0x12,0x34,0x56,0x78, 0,0,0,0, 0x00, 0x00, 0x10,
                                 0xff,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        r = (*intf)->WritePipe(intf, out, cbw, 31);
        printf("write: 0x%x\n", r);
        (*intf)->USBInterfaceClose(intf);
    }
    if (!found) fprintf(stderr, "0bda:1a2b not found\n");
    return 0;
}
