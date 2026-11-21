# Changelog

## Unreleased

## 0.1.0 - 2026-11-21

First release.

- Bridges controllers to virtual gamepads (vpad): Linux uinput, and whatever vpad supports
  elsewhere. A text profile maps the controller's report to buttons and axes; `learn` writes one.
- Sources: hidraw on Linux and FreeBSD, hid.dll on Windows, IOHIDManager on macOS, raw L2CAP
  for Bluetooth controllers that do not speak HID to the system, and pcapng replay.
- Rumble and force feedback from games reach the controller as the profile's motor packet;
  motors stop when the bridge exits and the last effect is resent after a reconnect.
- Bluetooth: scan, probe, pair and release through BlueZ; a dropped controller is reconnected
  with its current key before pairing is tried again.
- `record` and `replay` with pcapng; event hooks run as commands.
- Service files for systemd (user unit), FreeBSD rc.d and launchd.
