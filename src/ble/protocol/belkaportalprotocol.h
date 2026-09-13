#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QtEndian>

#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

// Protocol reference: variegated-coffee/variegated-rs, revision
// a9cf6e324481ee70e399839bb1f8a68ef2050086,
// crates/variegated-belka-portal-trouble-driver/src/{types,driver}.rs.
// Wire/display agreement checked on one PORTAL; units and firmware coverage remain unverified.
/* Reference implementation license:
MIT License

Copyright (c) 2024 Magnus Nordlander

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
namespace BelkaPortalProtocol {

inline constexpr quint16 ServiceUuid = 0x7400;
inline constexpr quint16 MeasurementUuid = 0x7410;
inline constexpr quint16 CommandUuid = 0x7420;
// Captured wire bytes in reference types.rs; commands do not use the floats' endianness.
inline QByteArray graphCommand(bool show) {
    return show ? QByteArray::fromHex("4000012d") : QByteArray::fromHex("4000012e");
}
inline constexpr qsizetype MeasurementBytes = 13;

struct Measurement {
    float ecRaw;                 // Unit/compensation unverified; never write to drinkTds.
    float auxiliaryTemperature;  // Reference driver only presumes this is internal temperature.
    float temperatureC;
    quint8 statusRaw;             // Battery/status semantics unverified; not a percentage.
};

inline std::optional<Measurement> decodeMeasurement(QByteArrayView packet)
{
    if (packet.size() != MeasurementBytes)
        return std::nullopt;

    static_assert(sizeof(float) == sizeof(quint32)
                  && std::numeric_limits<float>::is_iec559,
                  "PORTAL measurements require IEEE 754 binary32");

    const auto readFloat = [&packet](qsizetype offset) {
        const quint32 bits = qFromLittleEndian<quint32>(packet.data() + offset);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };

    const Measurement result {
        readFloat(0), readFloat(4), readFloat(8),
        static_cast<quint8>(packet[12])
    };
    if (!std::isfinite(result.ecRaw) || !std::isfinite(result.auxiliaryTemperature)
        || !std::isfinite(result.temperatureC))
        return std::nullopt;

    return result;
}

} // namespace BelkaPortalProtocol
