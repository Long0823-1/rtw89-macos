// Switch Realtek 0bda:1a2b (virtual CD) to Wi-Fi mode by sending SCSI START STOP UNIT (eject).
#include <libusb.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    libusb_context *ctx;
    if (libusb_init(&ctx)) return 1;
    libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx, 0x0bda, 0x1a2b);
    if (!h) { fprintf(stderr, "0bda:1a2b not found (already switched, or no permission: try sudo)\n"); return 2; }
    libusb_device *dev = libusb_get_device(h);
    struct libusb_config_descriptor *cfg;
    if (libusb_get_active_config_descriptor(dev, &cfg)) return 3;
    const struct libusb_interface_descriptor *ifd = &cfg->interface[0].altsetting[0];
    int out = -1;
    for (int i = 0; i < ifd->bNumEndpoints; i++) {
        const struct libusb_endpoint_descriptor *e = &ifd->endpoint[i];
        if (!(e->bEndpointAddress & 0x80) && (e->bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_BULK) out = e->bEndpointAddress;
    }
    printf("interface %d class %02x, bulk out ep 0x%02x\n", ifd->bInterfaceNumber, ifd->bInterfaceClass, out);
    libusb_set_auto_detach_kernel_driver(h, 1);
    int r = libusb_claim_interface(h, ifd->bInterfaceNumber);
    if (r) { fprintf(stderr, "claim: %s\n", libusb_error_name(r)); return 4; }
    unsigned char cbw[31] = {0x55,0x53,0x42,0x43, 0x78,0x56,0x34,0x12, 0,0,0,0, 0x00, 0x00, 0x06,
                             0x1b,0x00,0x00,0x00,0x02,0x00};
    int n = 0;
    r = libusb_bulk_transfer(h, out, cbw, 31, &n, 3000);
    printf("send: %s (%d bytes)\n", libusb_error_name(r), n);
    libusb_release_interface(h, ifd->bInterfaceNumber);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
