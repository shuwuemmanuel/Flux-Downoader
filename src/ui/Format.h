// Human-readable sizes, speeds and durations shared by the UI.
#pragma once

#include <QString>

namespace Fmt {
// Table style: "—" for unknown, one decimal ("12.3 MB").
QString size(double bytes);
QString speed(double bps);  // "" when idle
QString time(double seconds);  // "1h 5m", "—" when unknown

// IDM progress-window style: three decimals, "0 B", "1 min 5 sec", "Unknown".
QString sizeDetailed(double bytes);
QString speedDetailed(double bps);
QString timeDetailed(double seconds);
}  // namespace Fmt
