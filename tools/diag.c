#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdio.h>
int main(void){
 const char*names[]={"IOUSBDevice","IOUSBHostDevice","AppleUSBDevice"};
 for(int k=0;k<3;k++){
  io_iterator_t it; IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching(names[k]),&it);
  io_service_t d; int n=0;
  while((d=IOIteratorNext(it))){n++;
   CFTypeRef v=IORegistryEntryCreateCFProperty(d,CFSTR("idVendor"),0,0),p=IORegistryEntryCreateCFProperty(d,CFSTR("idProduct"),0,0);
   int vv=0,pp=0; if(v)CFNumberGetValue(v,kCFNumberIntType,&vv); if(p)CFNumberGetValue(p,kCFNumberIntType,&pp);
   if(vv==0x0bda){IOCFPlugInInterface**pl;SInt32 s;
    IOReturn r=IOCreatePlugInInterfaceForService(d,kIOUSBDeviceUserClientTypeID,kIOCFPlugInInterfaceID,&pl,&s);
    printf("%s: %04x:%04x plugin=0x%x\n",names[k],vv,pp,r);}
  }
  printf("%s count=%d\n",names[k],n);
 }}
