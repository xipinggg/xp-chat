#ifndef LOGGER_H_
#define LOGGER_H_

#include <iostream>
#include <string>
#include <experimental/source_location>
#include <fstream>
#include <filesystem>
#include <list>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sys/time.h>
#include <fmt/format.h>
#include  <fmt/chrono.h>
#include "thread_tools.h"
namespace xp
{
    auto make_str_time(std::time_t t = std::time(nullptr)) noexcept
    {
        constexpr size_t maxsize = 20;
        std::string str(maxsize, '\0');
        const char *format = "%F %T";
        std::strftime(str.data(), maxsize, format, std::localtime(&t));
        return str;
    }
    std::string make_log(const std::string_view message,
                         const std::string_view level,
                         std::time_t t,
                         const std::experimental::source_location location = std::experimental::source_location::current()) noexcept
    {
        auto strt = make_str_time(t);
        std::string log{};
        try
        {
            if (message != "")
            {
                log = fmt::format("{} [{}] {} @ {} @ {}\n##   {}\n",
                                       strt,level, location.line(), location.function_name(), location.file_name(), message);
            }
            else
            {
                log = fmt::format("{} [{}] {} @ {} @ {}\n",
                                       strt,level, location.line(), location.function_name(), location.file_name());
            }
        }
        catch (...)
        {
        }
        return log;
    }

    constexpr bool log_filter(const std::string_view level) noexcept
    {
        return false;
    }

    void log(const std::string_view message = "",
     const std::string_view level = "trace", 
     std::time_t t = std::time(nullptr),
    const std::experimental::source_location location = std::experimental::source_location::current()) noexcept
    {
        auto now_point = std::chrono::system_clock::now();
        if (!log_filter(level))
        {
            auto log = make_log(message, level, t, location);
            if (log != "")
                fmt::print(log);
        }
    }

    template <lock_type Lock = xp::SpinLock>
    class Logger
    {
    public:
        using location_type = std::experimental::source_location;
        using log_type = std::string;

        Logger() noexcept : path_{std::filesystem::current_path() / "logs"}
        {
            if (!std::filesystem::exists(path_))
            {
                std::filesystem::create_directory(path_);
            }
        }
        Logger(std::filesystem::path path) noexcept : path_{path}
        {
            if (!std::filesystem::exists(path_))
            {
                std::filesystem::create_directory(path_);
            }
        }
        ~Logger() = default;
        
        bool commit(const std::string_view message = "", const std::string_view level = "trace",
        std::time_t t = std::time(nullptr),
                    const location_type location = std::experimental::source_location::current()) noexcept
        {
            auto now_point = std::chrono::system_clock::now();
            auto onelog = make_log(message, level, t, location);
            if (onelog == "")
                return false;
            logs_.add(std::move(onelog));
            return true;
        }
        void output()
        {
            if (logs_.empty())
                return;
            auto logs = logs_.get();
            std::ofstream fout(path_ / "test.log", std::ios::app | std::ios::out);
            if (fout.bad())
                return;
            for (auto &log : logs)
            {
                //std::cout << log;
                fmt::print(log);
                fout << log;
            }
            fout.close();
        }

    private:
    private:
        xp::SwapBuffer<log_type> logs_;
        std::filesystem::path path_ = std::filesystem::current_path() / "logs";
    };
}

#endif //LOGGER_H_