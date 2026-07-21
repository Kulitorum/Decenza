#pragma once

#include <QLowEnergyService>
#include <QString>

// Human-readable name for a QLowEnergyService::ServiceError.
//
// Exists because the DE1 transport used to format the raw enum straight into a
// user-facing dialog. A reporter hit "Service error: 5" and could not say what
// it meant, and neither could the debug log they attached — that path emitted to
// the UI without logging anything at all, so the one error they named was the
// one thing the log could not show. 5 is CharacteristicReadError. See #1586.
//
// The scale transport already carried this mapping; this is that mapping made
// shared, so both links describe the same failure the same way.
//
// No `default:` label on purpose: a new value in Qt's enum trips -Wswitch rather
// than silently falling through to a bare number. That is an error on clang and
// gcc, which build with -Werror; the MSVC arm is /W4 without /WX, so on Windows
// it is a warning and the number-fallback below is what a new value would print.
//
// The return after the switch exists to satisfy control-flow analysis, not as
// reachable behaviour, and is intentionally untested: producing a value outside
// the enum is itself undefined behaviour (UBSan: "load of value N, which is not
// a valid value for type QLowEnergyService::ServiceError"), so a test for it
// passes under macOS's recovering-mode UBSan while failing the halting-mode
// nightly Linux job.
inline QString bleServiceErrorName(QLowEnergyService::ServiceError err) {
    switch (err) {
        case QLowEnergyService::NoError:
            return QStringLiteral("NoError");
        case QLowEnergyService::OperationError:
            return QStringLiteral("OperationError");
        case QLowEnergyService::CharacteristicWriteError:
            return QStringLiteral("CharacteristicWriteError");
        case QLowEnergyService::DescriptorWriteError:
            return QStringLiteral("DescriptorWriteError");
        case QLowEnergyService::UnknownError:
            return QStringLiteral("UnknownError");
        case QLowEnergyService::CharacteristicReadError:
            return QStringLiteral("CharacteristicReadError");
        case QLowEnergyService::DescriptorReadError:
            return QStringLiteral("DescriptorReadError");
    }
    return QString::number(static_cast<int>(err));
}
