#!/bin/sh
# Build mac-display and run it as a launch agent (starts at login, restarts if it dies).
# Uninstall: launchctl bootout gui/$(id -u)/home.buddy.mac-display
#            rm ~/Library/LaunchAgents/home.buddy.mac-display.plist ~/.local/bin/buddy-mac-display
set -e
cd "$(dirname "$0")"
LABEL=home.buddy.mac-display
BIN="$HOME/.local/bin/buddy-mac-display"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"

mkdir -p "$(dirname "$BIN")"
swiftc -O -o "$BIN" main.swift

cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array><string>$BIN</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>/tmp/$LABEL.log</string>
  <key>StandardErrorPath</key><string>/tmp/$LABEL.log</string>
</dict>
</plist>
EOF

launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "installed $LABEL"
