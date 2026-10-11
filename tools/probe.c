#include <libusb.h>
#include <stdio.h>
int main(void){libusb_context*c;int r=libusb_init(&c);printf("init=%d\n",r);libusb_set_option(c,LIBUSB_OPTION_LOG_LEVEL,LIBUSB_LOG_LEVEL_WARNING);
libusb_device**l;ssize_t n=libusb_get_device_list(c,&l);printf("n=%zd\n",n);
for(ssize_t i=0;i<n;i++){struct libusb_device_descriptor d;libusb_get_device_descriptor(l[i],&d);
printf("%04x:%04x\n",d.idVendor,d.idProduct);
if(d.idVendor==0x0bda){libusb_device_handle*h;int r=libusb_open(l[i],&h);printf("  open=%s\n",libusb_error_name(r));
if(!r){int k=libusb_claim_interface(h,0);printf("  claim=%s\n",libusb_error_name(k));}}}return 0;}
