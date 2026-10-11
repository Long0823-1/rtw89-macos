// RTW89 Wi-Fi menu bar app: talks to the rtw89d daemon over its control socket.
import AppKit
import Foundation

// MARK: - control socket client

func socketPath() -> String {
    ProcessInfo.processInfo.environment["RTW89_SOCK"] ?? "/var/run/rtw89d.sock"
}

func ctl(_ command: String) -> String? {
    let fd = socket(AF_UNIX, SOCK_STREAM, 0)
    if fd < 0 { return nil }
    defer { close(fd) }
    var addr = sockaddr_un()
    addr.sun_family = sa_family_t(AF_UNIX)
    let path = socketPath()
    withUnsafeMutablePointer(to: &addr.sun_path) { p in
        p.withMemoryRebound(to: CChar.self, capacity: 104) { cp in
            _ = path.withCString { strlcpy(cp, $0, 104) }
        }
    }
    let rc = withUnsafePointer(to: &addr) {
        $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
            connect(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
        }
    }
    if rc != 0 { return nil }
    let line = command + "\n"
    _ = line.withCString { write(fd, $0, strlen($0)) }
    var out = Data()
    var buf = [UInt8](repeating: 0, count: 4096)
    while true {
        let n = read(fd, &buf, buf.count)
        if n <= 0 { break }
        out.append(buf, count: n)
    }
    return String(data: out, encoding: .utf8)
}

func hex(_ s: String) -> String { s.utf8.map { String(format: "%02x", $0) }.joined() }

struct Status {
    var state = "unreachable"
    var ssid = ""
    var ip = ""
    var signal = 0
    var channel = 0
    var defaultRoute = false
    var uptime = 0
    var error = ""
    var known: [String] = []
}

struct Network {
    var ssid: String
    var signal: Int
    var channel: Int
    var band: String
    var secure: Bool
    var known: Bool
}

func fetchStatus() -> Status {
    guard let r = ctl("status"), let d = r.data(using: .utf8),
          let j = (try? JSONSerialization.jsonObject(with: d)) as? [String: Any] else { return Status() }
    var s = Status()
    s.state = j["state"] as? String ?? "unreachable"
    s.ssid = j["ssid"] as? String ?? ""
    s.ip = j["ip"] as? String ?? ""
    s.signal = j["signal"] as? Int ?? 0
    s.channel = j["channel"] as? Int ?? 0
    s.defaultRoute = j["default_route"] as? Bool ?? false
    s.uptime = j["uptime"] as? Int ?? 0
    s.error = j["error"] as? String ?? ""
    s.known = j["known"] as? [String] ?? []
    return s
}

func fetchNetworks() -> [Network] {
    guard let r = ctl("list"), let d = r.data(using: .utf8),
          let a = (try? JSONSerialization.jsonObject(with: d)) as? [[String: Any]] else { return [] }
    return a.map {
        Network(ssid: $0["ssid"] as? String ?? "", signal: $0["signal"] as? Int ?? -100, channel: $0["channel"] as? Int ?? 0,
                band: $0["band"] as? String ?? "", secure: $0["secure"] as? Bool ?? true, known: $0["known"] as? Bool ?? false)
    }.sorted { $0.signal > $1.signal }
}

func bars(_ dbm: Int) -> String {
    switch dbm {
    case (-55)...: return "▂▄▆█"
    case (-67)...: return "▂▄▆ "
    case (-75)...: return "▂▄  "
    default: return "▂   "
    }
}

// MARK: - app

final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    let menu = NSMenu()
    var status = Status()
    var networks: [Network] = []
    var timer: Timer?

    func applicationDidFinishLaunching(_ n: Notification) {
        menu.delegate = self
        item.menu = menu
        updateIcon()
        rebuildMenu()
        timer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in self?.poll() }
        poll()
    }

    func poll() {
        DispatchQueue.global().async {
            let s = fetchStatus()
            DispatchQueue.main.async {
                self.status = s
                self.updateIcon()
            }
        }
    }

    func updateIcon() {
        let name: String
        switch status.state {
        case "connected": name = "wifi"
        case "connecting", "scanning", "starting": name = "wifi.exclamationmark"
        default: name = "wifi.slash"
        }
        if let img = NSImage(systemSymbolName: name, accessibilityDescription: "Wi-Fi") {
            img.isTemplate = true
            item.button?.image = img
        } else {
            item.button?.title = "Wi-Fi"
        }
    }

    func menuWillOpen(_ menu: NSMenu) {
        DispatchQueue.global().async {
            _ = ctl("scan")
            let s = fetchStatus()
            let nets = fetchNetworks()
            DispatchQueue.main.async {
                self.status = s
                self.networks = nets
                self.rebuildMenu()
            }
            // refresh again once the scan has had time to finish
            DispatchQueue.global().asyncAfter(deadline: .now() + 5) {
                let nets2 = fetchNetworks()
                let s2 = fetchStatus()
                DispatchQueue.main.async {
                    self.networks = nets2
                    self.status = s2
                    self.rebuildMenu()
                }
            }
        }
    }

    func rebuildMenu() {
        menu.removeAllItems()
        let head: String
        switch status.state {
        case "connected":
            head = "Connected to \(status.ssid)"
        case "connecting": head = "Connecting to \(status.ssid)…"
        case "scanning": head = "Scanning…"
        case "no_adapter": head = "No adapter found"
        case "failed": head = status.error.isEmpty ? "Connection failed" : "Failed: \(status.error)"
        case "unreachable": head = "Driver not running"
        default: head = "Wi-Fi: not connected"
        }
        let h = NSMenuItem(title: head, action: nil, keyEquivalent: "")
        h.isEnabled = false
        menu.addItem(h)
        if status.state == "connected" {
            let d = NSMenuItem(title: "\(status.ip)  •  ch\(status.channel)  •  \(status.signal) dBm", action: nil, keyEquivalent: "")
            d.isEnabled = false
            menu.addItem(d)
        }
        menu.addItem(.separator())
        let t = NSMenuItem(title: "Networks", action: nil, keyEquivalent: "")
        t.isEnabled = false
        menu.addItem(t)
        for n in networks.prefix(24) {
            let lock = n.secure ? " 🔒" : ""
            let it = NSMenuItem(title: "\(bars(n.signal))  \(n.ssid)\(lock)", action: #selector(selectNetwork(_:)), keyEquivalent: "")
            it.target = self
            it.representedObject = n.ssid
            if n.ssid == status.ssid && status.state == "connected" { it.state = .on }
            menu.addItem(it)
        }
        if networks.isEmpty {
            let e = NSMenuItem(title: "(no networks yet)", action: nil, keyEquivalent: "")
            e.isEnabled = false
            menu.addItem(e)
        }
        menu.addItem(.separator())
        let primary = NSMenuItem(title: "Use as primary connection", action: #selector(toggleRoute), keyEquivalent: "")
        primary.target = self
        primary.state = status.defaultRoute ? .on : .off
        menu.addItem(primary)
        if status.state == "connected" || status.state == "connecting" {
            let dc = NSMenuItem(title: "Disconnect", action: #selector(disconnect), keyEquivalent: "")
            dc.target = self
            menu.addItem(dc)
        }
        if !status.known.isEmpty {
            let sub = NSMenu()
            for k in status.known {
                let fi = NSMenuItem(title: k, action: #selector(forget(_:)), keyEquivalent: "")
                fi.target = self
                fi.representedObject = k
                sub.addItem(fi)
            }
            let f = NSMenuItem(title: "Forget Network", action: nil, keyEquivalent: "")
            f.submenu = sub
            menu.addItem(f)
        }
        menu.addItem(.separator())
        let q = NSMenuItem(title: "Quit", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        menu.addItem(q)
    }

    @objc func selectNetwork(_ sender: NSMenuItem) {
        guard let ssid = sender.representedObject as? String, let net = networks.first(where: { $0.ssid == ssid }) else { return }
        if !net.secure {
            _ = ctl("connect \(hex(ssid)) -")
            return
        }
        if net.known {
            _ = ctl("use \(hex(ssid))")
            return
        }
        promptPassword(ssid)
    }

    func promptPassword(_ ssid: String) {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = "Password for “\(ssid)”"
        alert.informativeText = "Enter the Wi-Fi password (WPA2-PSK)."
        alert.addButton(withTitle: "Connect")
        alert.addButton(withTitle: "Cancel")
        let field = NSSecureTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
        alert.accessoryView = field
        alert.window.initialFirstResponder = field
        if alert.runModal() == .alertFirstButtonReturn {
            let pw = field.stringValue
            if pw.utf8.count < 8 || pw.utf8.count > 63 {
                let e = NSAlert()
                e.messageText = "The password must be 8 to 63 characters."
                e.runModal()
                return
            }
            DispatchQueue.global().async { _ = ctl("connect \(hex(ssid)) \(hex(pw))") }
        }
    }

    @objc func disconnect() { DispatchQueue.global().async { _ = ctl("disconnect") } }
    @objc func toggleRoute() {
        let on = !status.defaultRoute
        DispatchQueue.global().async { _ = ctl("route \(on ? "on" : "off")") }
    }
    @objc func forget(_ sender: NSMenuItem) {
        if let s = sender.representedObject as? String { DispatchQueue.global().async { _ = ctl("forget \(hex(s))") } }
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
let delegate = AppDelegate()
app.delegate = delegate
app.run()
