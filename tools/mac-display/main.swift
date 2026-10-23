// mac-display: tells the desk buddy whether the Mac's display is on, so the buddy
// can turn its own screen off while the Mac's is dark.
// Sends POST on=0|1 to /api/display when the displays sleep or wake, and repeats
// the current state every minute (the buddy ignores reports older than 5 minutes).
// Runs as a launch agent, see install.sh.

import AppKit

let url = URL(string: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "http://buddy.local/api/display")!

func report(_ on: Bool) {
  var req = URLRequest(url: url, timeoutInterval: 5)
  req.httpMethod = "POST"
  req.httpBody = "on=\(on ? 1 : 0)".data(using: .utf8)
  req.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
  URLSession.shared.dataTask(with: req) { _, _, err in
    if let err { print("report on=\(on ? 1 : 0) failed: \(err.localizedDescription)") }
  }.resume()
}

func displayOn() -> Bool { CGDisplayIsAsleep(CGMainDisplayID()) == 0 }

let center = NSWorkspace.shared.notificationCenter
center.addObserver(forName: NSWorkspace.screensDidSleepNotification, object: nil, queue: .main) { _ in report(false) }
center.addObserver(forName: NSWorkspace.screensDidWakeNotification, object: nil, queue: .main) { _ in report(true) }
Timer.scheduledTimer(withTimeInterval: 60, repeats: true) { _ in report(displayOn()) }
report(displayOn())
RunLoop.main.run()
