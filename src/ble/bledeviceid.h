#pragma once

// QBluetoothAddress explicitly: qbluetoothdeviceinfo.h only forward-declares it,
// so address().isNull() needs the full type. blemanager.h happened to get it
// transitively, which is part of why this was never a standalone header.
#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QString>

// Canonical identity for a discovered BLE device — the string persisted as a
// saved scale / DE1 / refractometer address.
//
// This lives in its own header so every layer can reach it without pulling in
// blemanager.h, which is where it used to sit. It did not stay there: the
// expression was hand-copied into bletransport.cpp, difluidr1.cpp, difluidr2.cpp,
// qtscalebletransport.cpp and bookooscale.cpp, and the two versions that mattered
// most — the ones whose output is WRITTEN TO SETTINGS — were the ones that got it
// wrong (see below). A canonical form nobody can include is a canonical form in
// name only.
//
// All five copies now call this. Do not write a sixth: every one of them carried
// the comment "UUID on iOS, address on other platforms", which is the belief that
// produced the bug below — the rule is not restatable in a comment, because it is
// not about iOS.
//
// The check is at RUNTIME, on whether the backend actually gave us a MAC. It used
// to be `#ifdef Q_OS_IOS`, which was wrong on macOS: Qt's Bluetooth backend there
// is CoreBluetooth too, and CoreBluetooth never exposes MAC addresses. So
// `address().toString()` returned the null address "00:00:00:00:00:00" for EVERY
// device, and every scale, DE1 and refractometer paired on a Mac was persisted
// under that one colliding identity — which also made deviceIdentifiersMatch()
// return true for any device at all.
inline QString getDeviceIdentifier(const QBluetoothDeviceInfo& device) {
    return device.address().isNull() ? device.deviceUuid().toString()
                                     : device.address().toString();
}

// Compare a discovered device against a saved identifier. Must derive the
// device's side through getDeviceIdentifier so the two stay in lockstep — a
// direct address()/deviceUuid() comparison here is how the platform-guard bug
// above stayed hidden.
//
// Identifiers persisted by a pre-fix build on macOS are the null address, which
// identifies nothing. Those are deliberately NOT special-cased: such an entry
// matches no real device now, so the stale pairing simply stops connecting and
// the user re-scans. (Before this fix it matched EVERY device, which is worse
// than not matching — with two BLE scales paired, it connected whichever was
// seen first.)
inline bool deviceIdentifiersMatch(const QBluetoothDeviceInfo& device, const QString& identifier) {
    if (identifier.isEmpty()) return false;
    return getDeviceIdentifier(device).compare(identifier, Qt::CaseInsensitive) == 0;
}
