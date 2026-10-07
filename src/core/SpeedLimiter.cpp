#include "core/SpeedLimiter.h"

#include <algorithm>
#include <thread>

SpeedLimiter::SpeedLimiter(int kbps)
{
    setLimit(kbps);
}

void SpeedLimiter::setLimit(int kbps)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_limitBps = kbps > 0 ? static_cast<qint64>(kbps) * 1024 : 0;
    m_tokens = static_cast<double>(m_limitBps.load());
    m_last = Clock::now();
}

int SpeedLimiter::limitKbps() const
{
    return static_cast<int>(m_limitBps.load() / 1024);
}

void SpeedLimiter::throttle(qint64 nbytes)
{
    const qint64 limit = m_limitBps.load();
    if (limit <= 0)
        return;

    double waitSeconds = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto now = Clock::now();
        const double elapsed = std::chrono::duration<double>(now - m_last).count();
        m_last = now;
        // Refill, allowing at most one second worth of burst.
        m_tokens = std::min<double>(static_cast<double>(limit), m_tokens + elapsed * limit);
        m_tokens -= static_cast<double>(nbytes);
        if (m_tokens < 0)
            waitSeconds = std::min(-m_tokens / limit, 1.0);
    }
    if (waitSeconds > 0)
        std::this_thread::sleep_for(std::chrono::duration<double>(waitSeconds));
}
