#ifndef THREAD_TOOLS_
#define THREAD_TOOLS_

#include <atomic>
namespace xp
{
    class SpinLock
    {
    public:
        SpinLock() noexcept = default;
        SpinLock(SpinLock &&) noexcept = default;
        SpinLock &operator=(SpinLock &&) noexcept = default;
        SpinLock(const SpinLock &) = delete;
        SpinLock &operator=(const SpinLock &) = delete;
        void lock() noexcept
        {
            while (flag_.test_and_set(std::memory_order::acquire))
                continue;
        }
        void unlock() noexcept
        {
            flag_.clear(std::memory_order::release);
        }
        bool try_lock() noexcept
        {
            return flag_.test_and_set(std::memory_order::acquire);
        }

    private:
        std::atomic_flag flag_;
    };

    
}

#endif