#include "ui/Format.h"

namespace {
QString scaled(double n, int decimals)
{
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    for (const char* unit : units) {
        if (n < 1024) {
            if (QLatin1String(unit) == QLatin1String("B"))
                return QStringLiteral("%1 B").arg(static_cast<qint64>(n));
            return QStringLiteral("%1 %2").arg(n, 0, 'f', decimals).arg(QLatin1String(unit));
        }
        n /= 1024;
    }
    return QStringLiteral("%1 PB").arg(n, 0, 'f', decimals);
}
}  // namespace

QString Fmt::size(double bytes)
{
    if (bytes <= 0)
        return QStringLiteral("—");
    return scaled(bytes, 1);
}

QString Fmt::speed(double bps)
{
    if (bps <= 0)
        return QString();
    return size(bps) + QStringLiteral("/s");
}

QString Fmt::time(double seconds)
{
    if (seconds <= 0)
        return QStringLiteral("—");
    const qint64 s = static_cast<qint64>(seconds);
    if (s < 60)
        return QStringLiteral("%1s").arg(s);
    if (s < 3600)
        return QStringLiteral("%1m %2s").arg(s / 60).arg(s % 60);
    return QStringLiteral("%1h %2m").arg(s / 3600).arg((s % 3600) / 60);
}

QString Fmt::sizeDetailed(double bytes)
{
    if (bytes <= 0)
        return QStringLiteral("0 B");
    return scaled(bytes, 3);
}

QString Fmt::speedDetailed(double bps)
{
    if (bps <= 0)
        return QStringLiteral("0 B/sec");
    return sizeDetailed(bps) + QStringLiteral("/sec");
}

QString Fmt::timeDetailed(double seconds)
{
    if (seconds <= 0)
        return QStringLiteral("Unknown");
    const qint64 s = static_cast<qint64>(seconds);
    if (s < 60)
        return QStringLiteral("%1 sec").arg(s);
    if (s < 3600)
        return QStringLiteral("%1 min %2 sec").arg(s / 60).arg(s % 60);
    return QStringLiteral("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60);
}
