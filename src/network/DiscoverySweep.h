// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - /24 sweep cadence decision, kept pure so it is unit-testable
// without a live socket or wall clock.
#pragma once

#include <QtGlobal>

namespace koutnet::discovery
{

// Decides whether the noisy /24 sweep fires on this tick and advances its
// exponential backoff. Pure: no sockets, no clock, no RNG - the caller feeds
// the current time, the state by reference, and the jitter it already drew.
//
// Returns true when a sweep is due now. Behaviour:
//   - peers known     : sweep suppressed entirely, gap reset to minMs.
//   - peers empty     : when now - lastScan exceeds gap*jitter/1000 the sweep
//                        fires, lastScan is advanced, and the gap doubles
//                        (capped at maxMs); otherwise nothing happens.
inline bool sweepTick(double nowSec, double &lastScanSec, double &intervalMs, bool peersEmpty, double minMs, double maxMs, double jitter)
{
    if (!peersEmpty) {
        intervalMs = minMs;
        lastScanSec = nowSec;
        return false;
    }

    const double due = (intervalMs * jitter) / 1000.0;
    if (nowSec - lastScanSec > due) {
        lastScanSec = nowSec;
        intervalMs = qMin(intervalMs * 2.0, maxMs);
        return true;
    }
    return false;
}

} // namespace koutnet::discovery
