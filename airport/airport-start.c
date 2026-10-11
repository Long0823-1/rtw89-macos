// Publishes the "RTW89Airport" resource so the kext's personality matches and the driver starts. Must run as root.
// Build: clang -o airport-start airport-start.c -framework IOKit -framework CoreFoundation
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
int main(void)
{
	io_registry_entry_t r = IORegistryEntryFromPath(kIOMainPortDefault, "IOService:/IOResources");
	if (!r) { fprintf(stderr, "IOResources not found\n"); return 1; }
	kern_return_t k = IORegistryEntrySetCFProperty(r, CFSTR("RTW89Airport"), kCFBooleanTrue);
	printf("set RTW89Airport on IOResources: 0x%x\n", k);
	return k ? 1 : 0;
}
