#ifndef LOGGER_HPP_
#define LOGGER_HPP_

#include <iostream>
#include <string>
#include <optional>
#include <experimental/source_location>
#include <filesystem>
#include <list>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sys/time.h>
#include <fmt/format.h>
#include "thread_tools.h"

namespace xp
{
    
    std::string make_log(const std::string_view message = "",
                         const std::string_view level = "debug",
                         const std::experimental::source_location location = std::experimental::source_location::current())
    {
        return fmt::format("[{}] {} ## {} {} {}\n",
                           level, message, location.function_name(), location.line(), location.file_name());
    }

    void log(const std::string_view message = "", const std::string level = "debug",
             const std::experimental::source_location location = std::experimental::source_location::current()) noexcept
    {
        auto log = make_log(message, level, location);
        fmt::print(log);
    }

    template <typename Lock>
    concept lock_type = requires(Lock lc)
    {
        lc.lock();
        lc.unlock();
    };
    
    template <lock_type Lock = xp::SpinLock>
    class Logger
    {
    public:
        using location_type = std::experimental::source_location;
        using log_type = std::string;
        Logger() noexcept
        {
            //auto file = std::filesystem::
        }
        Logger(std::filesystem::path path)
        {
            //auto file = std::filesystem::
        }
        ~Logger() {}
        bool commit(const std::string_view message = "", const std::string level = "debug",
                    const location_type location = std::experimental::source_location::current()) noexcept
        {
            try
            {
                auto log = make_log(message, level, location);
                std::lock_guard lg{lock_};
                logs_.emplace_back(std::move(log));
            }
            catch (...)
            {
                return false;
            }
            return true;
        }
        void out()
        {
            auto logs = decltype(logs_){};
            {
                std::lock_guard lg{lock_};
                logs = std::move(logs_);
            }

            for (auto &log : logs)
            {
                //std::cout << log;
                fmt::print(log);
            }
        }

    private:
    private:
        std::list<log_type> logs_;
        Lock lock_;
        std::filesystem::path path_;
    };
}

#endif //LOGGER_HPP_