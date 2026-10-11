# RTW89Hold.kext — keep macOS's mass-storage driver off the TX20U's virtual CD

The adapter first shows up as `0bda:1a2b` ("DISK", a virtual CD) and must be switched to Wi-Fi mode with a SCSI command.
macOS's `IOUSBMassStorageDriver` claims that interface and often will not let go, so the daemon's mode switch is unreliable.

This kext has **no executable code**: it only adds a high-probe-score personality for `idVendor 0x0bda / idProduct 0x1a2b`
that attaches a plain `IOService` to the interface, so Apple's driver never binds and the daemon can open it at once.

## Install (OpenCore)
1. **Back up your EFI folder first.**
2. Copy `RTW89Hold.kext` to `EFI/OC/Kexts/`.
3. In `config.plist` → `Kernel` → `Add`, add an entry:
   `BundlePath` = `RTW89Hold.kext`, `Enabled` = `true`, `PlistPath` = `Contents/Info.plist`, `ExecutablePath` = empty
   (copy an existing codeless entry, e.g. a USB map kext, and change the name).
4. Reboot. Check with: `ioreg -p IOUSB -l -w0 | grep -A30 DISK@ | grep -E "IOService|IOUSBMassStorage"`
   (there should be no `IOUSBMassStorageInterfaceNub` under the DISK device).

To undo: remove the entry and the kext, reboot.
