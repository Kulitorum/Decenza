.pragma library

// QR Code Generator for QML
// Ported from Project Nayuki's QR Code Generator (MIT License)
// https://www.nayuki.io/page/qr-code-generator-library
// Copyright (c) Project Nayuki. (MIT License)

// Returns a 2D boolean matrix (true=dark, false=light) or null on error.
// Usage: var matrix = generate("Hello World");
//        if (matrix) { /* matrix[y][x] is true for dark modules */ }

function generate(text) {
    if (!text) return null;
    try {
        var segs = QrSegment_makeSegments(text);
        var qr = encodeSegments(segs, 1); // EC Level LOW
        if (!qr) return null;
        // Convert to simple 2D boolean array
        var result = [];
        for (var y = 0; y < qr.size; y++) {
            var row = [];
            for (var x = 0; x < qr.size; x++)
                row.push(qr.modules[y][x]);
            result.push(row);
        }
        return result;
    } catch (e) {
        console.log("QR generate error: " + e);
        return null;
    }
}

// ── Constants ──────────────────────────────────────────────────────────────

var PENALTY_N1 = 3;
var PENALTY_N2 = 3;
var PENALTY_N3 = 40;
var PENALTY_N4 = 10;

var ECC_CODEWORDS_PER_BLOCK = [
    // Index 0 = LOW, 1 = MEDIUM, 2 = QUARTILE, 3 = HIGH
    [-1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30],
    [-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28],
    [-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30],
    [-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30]
];

var NUM_ERROR_CORRECTION_BLOCKS = [
    [-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25],
    [-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49],
    [-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68],
    [-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81]
];

// ECL format bits: LOW=1, MEDIUM=0, QUARTILE=3, HIGH=2
var ECL_FORMAT_BITS = [1, 0, 3, 2];

// ── Utility functions ──────────────────────────────────────────────────────

function getBit(x, i) {
    return ((x >>> i) & 1) !== 0;
}

function appendBits(val, len, bb) {
    for (var i = len - 1; i >= 0; i--)
        bb.push((val >>> i) & 1);
}

// ── Reed-Solomon ───────────────────────────────────────────────────────────

function reedSolomonMultiply(x, y) {
    var z = 0;
    for (var i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >>> 7) * 0x11D);
        z ^= ((y >>> i) & 1) * x;
    }
    return z;
}

function reedSolomonComputeDivisor(degree) {
    var result = [];
    for (var i = 0; i < degree - 1; i++)
        result.push(0);
    result.push(1);

    var root = 1;
    for (var i = 0; i < degree; i++) {
        for (var j = 0; j < result.length; j++) {
            result[j] = reedSolomonMultiply(result[j], root);
            if (j + 1 < result.length)
                result[j] ^= result[j + 1];
        }
        root = reedSolomonMultiply(root, 0x02);
    }
    return result;
}

function reedSolomonComputeRemainder(data, divisor) {
    var result = [];
    for (var i = 0; i < divisor.length; i++)
        result.push(0);

    for (var i = 0; i < data.length; i++) {
        var factor = data[i] ^ result.shift();
        result.push(0);
        for (var j = 0; j < divisor.length; j++)
            result[j] ^= reedSolomonMultiply(divisor[j], factor);
    }
    return result;
}

// ── QR Code number of raw data modules ─────────────────────────────────────

function getNumRawDataModules(ver) {
    var result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        var numAlign = Math.floor(ver / 7) + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7)
            result -= 36;
    }
    return result;
}

function getNumDataCodewords(ver, eclOrdinal) {
    return Math.floor(getNumRawDataModules(ver) / 8) -
        ECC_CODEWORDS_PER_BLOCK[eclOrdinal][ver] *
        NUM_ERROR_CORRECTION_BLOCKS[eclOrdinal][ver];
}

// ── QR Segment encoding ───────────────────────────────────────────────────

var NUMERIC_REGEX = /^[0-9]*$/;
var ALPHANUMERIC_REGEX = /^[A-Z0-9 $%*+.\/:=]*$/;
var ALPHANUMERIC_CHARSET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

// Mode: [modeBits, [charCountBits for version ranges]]
var MODE_NUMERIC      = { modeBits: 0x1, ccBits: [10, 12, 14] };
var MODE_ALPHANUMERIC = { modeBits: 0x2, ccBits: [ 9, 11, 13] };
var MODE_BYTE         = { modeBits: 0x4, ccBits: [ 8, 16, 16] };

function numCharCountBits(mode, ver) {
    return mode.ccBits[Math.floor((ver + 7) / 17)];
}

function toUtf8ByteArray(str) {
    var s = encodeURI(str);
    var result = [];
    for (var i = 0; i < s.length; i++) {
        if (s.charAt(i) !== "%")
            result.push(s.charCodeAt(i));
        else {
            result.push(parseInt(s.substring(i + 1, i + 3), 16));
            i += 2;
        }
    }
    return result;
}

function QrSegment_makeBytes(data) {
    var bb = [];
    for (var i = 0; i < data.length; i++)
        appendBits(data[i], 8, bb);
    return { mode: MODE_BYTE, numChars: data.length, bitData: bb };
}

function QrSegment_makeNumeric(digits) {
    var bb = [];
    for (var i = 0; i < digits.length; ) {
        var n = Math.min(digits.length - i, 3);
        appendBits(parseInt(digits.substring(i, i + n), 10), n * 3 + 1, bb);
        i += n;
    }
    return { mode: MODE_NUMERIC, numChars: digits.length, bitData: bb };
}

function QrSegment_makeAlphanumeric(text) {
    var bb = [];
    var i;
    for (i = 0; i + 2 <= text.length; i += 2) {
        var temp = ALPHANUMERIC_CHARSET.indexOf(text.charAt(i)) * 45;
        temp += ALPHANUMERIC_CHARSET.indexOf(text.charAt(i + 1));
        appendBits(temp, 11, bb);
    }
    if (i < text.length)
        appendBits(ALPHANUMERIC_CHARSET.indexOf(text.charAt(i)), 6, bb);
    return { mode: MODE_ALPHANUMERIC, numChars: text.length, bitData: bb };
}

function QrSegment_makeSegments(text) {
    if (text === "")
        return [];
    else if (NUMERIC_REGEX.test(text))
        return [QrSegment_makeNumeric(text)];
    else if (ALPHANUMERIC_REGEX.test(text))
        return [QrSegment_makeAlphanumeric(text)];
    else
        return [QrSegment_makeBytes(toUtf8ByteArray(text))];
}

function getTotalBits(segs, version) {
    var result = 0;
    for (var i = 0; i < segs.length; i++) {
        var seg = segs[i];
        var ccbits = numCharCountBits(seg.mode, version);
        if (seg.numChars >= (1 << ccbits))
            return Infinity;
        result += 4 + ccbits + seg.bitData.length;
    }
    return result;
}

// ── Main encode function ──────────────────────────────────────────────────

function encodeSegments(segs, eclOrdinal) {
    var minVersion = 1;
    var maxVersion = 40;

    // Find minimal version
    var version, dataUsedBits;
    for (version = minVersion; ; version++) {
        var dataCapacityBits = getNumDataCodewords(version, eclOrdinal) * 8;
        var usedBits = getTotalBits(segs, version);
        if (usedBits <= dataCapacityBits) {
            dataUsedBits = usedBits;
            break;
        }
        if (version >= maxVersion)
            return null; // Data too long
    }

    // Boost ECL if data still fits
    var eclOrder = [1, 2, 3]; // MEDIUM, QUARTILE, HIGH
    for (var i = 0; i < eclOrder.length; i++) {
        if (dataUsedBits <= getNumDataCodewords(version, eclOrder[i]) * 8)
            eclOrdinal = eclOrder[i];
    }

    // Build bit stream
    var bb = [];
    for (var i = 0; i < segs.length; i++) {
        var seg = segs[i];
        appendBits(seg.mode.modeBits, 4, bb);
        appendBits(seg.numChars, numCharCountBits(seg.mode, version), bb);
        for (var j = 0; j < seg.bitData.length; j++)
            bb.push(seg.bitData[j]);
    }

    // Pad
    var dataCapacityBits = getNumDataCodewords(version, eclOrdinal) * 8;
    appendBits(0, Math.min(4, dataCapacityBits - bb.length), bb);
    appendBits(0, (8 - bb.length % 8) % 8, bb);
    for (var padByte = 0xEC; bb.length < dataCapacityBits; padByte ^= 0xEC ^ 0x11)
        appendBits(padByte, 8, bb);

    // Pack into bytes
    var dataCodewords = [];
    for (var i = 0; i < bb.length; i += 8)
        dataCodewords.push(0);
    for (var i = 0; i < bb.length; i++)
        dataCodewords[i >>> 3] |= bb[i] << (7 - (i & 7));

    // Build QR code
    return buildQrCode(version, eclOrdinal, dataCodewords);
}

// ── QR Code construction ──────────────────────────────────────────────────

function buildQrCode(version, eclOrdinal, dataCodewords) {
    var size = version * 4 + 17;

    // Initialize modules and isFunction grids
    var modules = [];
    var isFunction = [];
    for (var y = 0; y < size; y++) {
        modules.push([]);
        isFunction.push([]);
        for (var x = 0; x < size; x++) {
            modules[y].push(false);
            isFunction[y].push(false);
        }
    }

    var qr = { size: size, version: version, modules: modules, isFunction: isFunction };

    // Draw function patterns
    drawFunctionPatterns(qr);

    // Add ECC and interleave
    var allCodewords = addEccAndInterleave(qr, dataCodewords, eclOrdinal);

    // Draw codewords
    drawCodewords(qr, allCodewords);

    // Find best mask
    var bestMask = -1;
    var bestPenalty = Infinity;
    for (var mask = 0; mask < 8; mask++) {
        applyMask(qr, mask);
        drawFormatBits(qr, eclOrdinal, mask);
        var penalty = getPenaltyScore(qr);
        if (penalty < bestPenalty) {
            bestMask = mask;
            bestPenalty = penalty;
        }
        applyMask(qr, mask); // Undo
    }

    // Apply best mask
    applyMask(qr, bestMask);
    drawFormatBits(qr, eclOrdinal, bestMask);

    return qr;
}

// ── Drawing function patterns ─────────────────────────────────────────────

function setFunctionModule(qr, x, y, isDark) {
    qr.modules[y][x] = isDark;
    qr.isFunction[y][x] = true;
}

function drawFunctionPatterns(qr) {
    var size = qr.size;
    var version = qr.version;

    // Timing patterns
    for (var i = 0; i < size; i++) {
        setFunctionModule(qr, 6, i, i % 2 === 0);
        setFunctionModule(qr, i, 6, i % 2 === 0);
    }

    // Finder patterns
    drawFinderPattern(qr, 3, 3);
    drawFinderPattern(qr, size - 4, 3);
    drawFinderPattern(qr, 3, size - 4);

    // Alignment patterns
    var alignPos = getAlignmentPatternPositions(version, size);
    var numAlign = alignPos.length;
    for (var i = 0; i < numAlign; i++) {
        for (var j = 0; j < numAlign; j++) {
            if (!(i === 0 && j === 0 || i === 0 && j === numAlign - 1 || i === numAlign - 1 && j === 0))
                drawAlignmentPattern(qr, alignPos[i], alignPos[j]);
        }
    }

    // Format bits (dummy, overwritten later)
    drawFormatBits(qr, 0, 0);
    drawVersion(qr);
}

function drawFinderPattern(qr, x, y) {
    var size = qr.size;
    for (var dy = -4; dy <= 4; dy++) {
        for (var dx = -4; dx <= 4; dx++) {
            var dist = Math.max(Math.abs(dx), Math.abs(dy));
            var xx = x + dx;
            var yy = y + dy;
            if (0 <= xx && xx < size && 0 <= yy && yy < size)
                setFunctionModule(qr, xx, yy, dist !== 2 && dist !== 4);
        }
    }
}

function drawAlignmentPattern(qr, x, y) {
    for (var dy = -2; dy <= 2; dy++)
        for (var dx = -2; dx <= 2; dx++)
            setFunctionModule(qr, x + dx, y + dy, Math.max(Math.abs(dx), Math.abs(dy)) !== 1);
}

function getAlignmentPatternPositions(version, size) {
    if (version === 1)
        return [];
    var numAlign = Math.floor(version / 7) + 2;
    var step = Math.floor((version * 8 + numAlign * 3 + 5) / (numAlign * 4 - 4)) * 2;
    var result = [6];
    for (var pos = size - 7; result.length < numAlign; pos -= step)
        result.splice(1, 0, pos);
    return result;
}

function drawFormatBits(qr, eclOrdinal, mask) {
    var size = qr.size;
    var data = ECL_FORMAT_BITS[eclOrdinal] << 3 | mask;
    var rem = data;
    for (var i = 0; i < 10; i++)
        rem = (rem << 1) ^ ((rem >>> 9) * 0x537);
    var bits = (data << 10 | rem) ^ 0x5412;

    // First copy
    for (var i = 0; i <= 5; i++)
        setFunctionModule(qr, 8, i, getBit(bits, i));
    setFunctionModule(qr, 8, 7, getBit(bits, 6));
    setFunctionModule(qr, 8, 8, getBit(bits, 7));
    setFunctionModule(qr, 7, 8, getBit(bits, 8));
    for (var i = 9; i < 15; i++)
        setFunctionModule(qr, 14 - i, 8, getBit(bits, i));

    // Second copy
    for (var i = 0; i < 8; i++)
        setFunctionModule(qr, size - 1 - i, 8, getBit(bits, i));
    for (var i = 8; i < 15; i++)
        setFunctionModule(qr, 8, size - 15 + i, getBit(bits, i));
    setFunctionModule(qr, 8, size - 8, true);
}

function drawVersion(qr) {
    if (qr.version < 7)
        return;
    var rem = qr.version;
    for (var i = 0; i < 12; i++)
        rem = (rem << 1) ^ ((rem >>> 11) * 0x1F25);
    var bits = qr.version << 12 | rem;

    for (var i = 0; i < 18; i++) {
        var color = getBit(bits, i);
        var a = qr.size - 11 + i % 3;
        var b = Math.floor(i / 3);
        setFunctionModule(qr, a, b, color);
        setFunctionModule(qr, b, a, color);
    }
}

// ── ECC and interleave ────────────────────────────────────────────────────

function addEccAndInterleave(qr, data, eclOrdinal) {
    var ver = qr.version;
    var numBlocks = NUM_ERROR_CORRECTION_BLOCKS[eclOrdinal][ver];
    var blockEccLen = ECC_CODEWORDS_PER_BLOCK[eclOrdinal][ver];
    var rawCodewords = Math.floor(getNumRawDataModules(ver) / 8);
    var numShortBlocks = numBlocks - rawCodewords % numBlocks;
    var shortBlockLen = Math.floor(rawCodewords / numBlocks);

    var blocks = [];
    var rsDiv = reedSolomonComputeDivisor(blockEccLen);
    for (var i = 0, k = 0; i < numBlocks; i++) {
        var datLen = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1);
        var dat = data.slice(k, k + datLen);
        k += datLen;
        var ecc = reedSolomonComputeRemainder(dat, rsDiv);
        if (i < numShortBlocks)
            dat.push(0);
        blocks.push(dat.concat(ecc));
    }

    var result = [];
    for (var i = 0; i < blocks[0].length; i++) {
        for (var j = 0; j < blocks.length; j++) {
            if (i !== shortBlockLen - blockEccLen || j >= numShortBlocks)
                result.push(blocks[j][i]);
        }
    }
    return result;
}

// ── Draw codewords ────────────────────────────────────────────────────────

function drawCodewords(qr, data) {
    var size = qr.size;
    var i = 0;
    for (var right = size - 1; right >= 1; right -= 2) {
        if (right === 6)
            right = 5;
        for (var vert = 0; vert < size; vert++) {
            for (var j = 0; j < 2; j++) {
                var x = right - j;
                var upward = ((right + 1) & 2) === 0;
                var y = upward ? size - 1 - vert : vert;
                if (!qr.isFunction[y][x] && i < data.length * 8) {
                    qr.modules[y][x] = getBit(data[i >>> 3], 7 - (i & 7));
                    i++;
                }
            }
        }
    }
}

// ── Masking ───────────────────────────────────────────────────────────────

function applyMask(qr, mask) {
    var size = qr.size;
    for (var y = 0; y < size; y++) {
        for (var x = 0; x < size; x++) {
            var invert;
            switch (mask) {
                case 0: invert = (x + y) % 2 === 0;                                  break;
                case 1: invert = y % 2 === 0;                                        break;
                case 2: invert = x % 3 === 0;                                        break;
                case 3: invert = (x + y) % 3 === 0;                                  break;
                case 4: invert = (Math.floor(x / 3) + Math.floor(y / 2)) % 2 === 0;  break;
                case 5: invert = x * y % 2 + x * y % 3 === 0;                        break;
                case 6: invert = (x * y % 2 + x * y % 3) % 2 === 0;                  break;
                case 7: invert = ((x + y) % 2 + x * y % 3) % 2 === 0;                break;
            }
            if (!qr.isFunction[y][x] && invert)
                qr.modules[y][x] = !qr.modules[y][x];
        }
    }
}

// ── Penalty scoring ───────────────────────────────────────────────────────

function finderPenaltyCountPatterns(runHistory, size) {
    var n = runHistory[1];
    var core = n > 0 && runHistory[2] === n && runHistory[3] === n * 3 &&
               runHistory[4] === n && runHistory[5] === n;
    return (core && runHistory[0] >= n * 4 && runHistory[6] >= n ? 1 : 0)
         + (core && runHistory[6] >= n * 4 && runHistory[0] >= n ? 1 : 0);
}

function finderPenaltyAddHistory(currentRunLength, runHistory, size) {
    if (runHistory[0] === 0)
        currentRunLength += size;
    runHistory.pop();
    runHistory.unshift(currentRunLength);
}

function finderPenaltyTerminateAndCount(currentRunColor, currentRunLength, runHistory, size) {
    if (currentRunColor) {
        finderPenaltyAddHistory(currentRunLength, runHistory, size);
        currentRunLength = 0;
    }
    currentRunLength += size;
    finderPenaltyAddHistory(currentRunLength, runHistory, size);
    return finderPenaltyCountPatterns(runHistory, size);
}

function getPenaltyScore(qr) {
    var size = qr.size;
    var modules = qr.modules;
    var result = 0;

    // Rule 1 & 3: Adjacent modules in rows having same color, and finder-like patterns
    for (var y = 0; y < size; y++) {
        var runColor = false;
        var runX = 0;
        var runHistory = [0, 0, 0, 0, 0, 0, 0];
        for (var x = 0; x < size; x++) {
            if (modules[y][x] === runColor) {
                runX++;
                if (runX === 5)
                    result += PENALTY_N1;
                else if (runX > 5)
                    result++;
            } else {
                finderPenaltyAddHistory(runX, runHistory, size);
                if (!runColor)
                    result += finderPenaltyCountPatterns(runHistory, size) * PENALTY_N3;
                runColor = modules[y][x];
                runX = 1;
            }
        }
        result += finderPenaltyTerminateAndCount(runColor, runX, runHistory, size) * PENALTY_N3;
    }

    // Rule 1 & 3: Adjacent modules in columns
    for (var x = 0; x < size; x++) {
        var runColor = false;
        var runY = 0;
        var runHistory = [0, 0, 0, 0, 0, 0, 0];
        for (var y = 0; y < size; y++) {
            if (modules[y][x] === runColor) {
                runY++;
                if (runY === 5)
                    result += PENALTY_N1;
                else if (runY > 5)
                    result++;
            } else {
                finderPenaltyAddHistory(runY, runHistory, size);
                if (!runColor)
                    result += finderPenaltyCountPatterns(runHistory, size) * PENALTY_N3;
                runColor = modules[y][x];
                runY = 1;
            }
        }
        result += finderPenaltyTerminateAndCount(runColor, runY, runHistory, size) * PENALTY_N3;
    }

    // Rule 2: 2x2 blocks of same color
    for (var y = 0; y < size - 1; y++) {
        for (var x = 0; x < size - 1; x++) {
            var color = modules[y][x];
            if (color === modules[y][x + 1] &&
                color === modules[y + 1][x] &&
                color === modules[y + 1][x + 1])
                result += PENALTY_N2;
        }
    }

    // Rule 4: Balance of dark and light modules
    var dark = 0;
    for (var y = 0; y < size; y++)
        for (var x = 0; x < size; x++)
            if (modules[y][x]) dark++;
    var total = size * size;
    var k = Math.ceil(Math.abs(dark * 20 - total * 10) / total) - 1;
    result += k * PENALTY_N4;

    return result;
}
