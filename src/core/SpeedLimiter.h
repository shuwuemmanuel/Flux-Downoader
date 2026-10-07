// Token-bucket rate limiter shared across worker threads for a single
// download, or shared globally across all downloads.
#pragma once

#include <QtGlobal>

#include <atomic>
#include <chrono>
#include <mutex>

class SpeedLimiter {
public:
    explicit SpeedLimiter(int kbps = 0);

    void setLimit(int kbps);
    int limitKbps() const;

    // Blocks the calling thread as needed so that the combined throughput of
    // every caller stays under the limit. No-op when unlimited.
    void throttle(qint64 nbytes);

private:
    using Clock = std::chrono::steady_clock;

    std::mutex m_mutex;
    std::atomic<qint64> m_limitBps{0};
    double m_tokens = 0;
    Clock::time_point m_last = Clock::now();
};
