// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_LOG_USE_NATIVE_SYSLOG

#include <util/log.hpp>

#include <util/logsafely.hpp>

#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes/clock.hpp>
#include <boost/log/sinks/syslog_backend.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace util {
namespace log {

namespace {
    void initFileSink (const std::string&);
    void initConsoleSink (bool);
    void initSyslogSink (const std::string&);
} // anonymous namespace

boost::program_options::options_description optionsDescription (ConsoleDefault consoleDefault) {
    boost::log::add_common_attributes();
    boost::log::core::get()->add_global_attribute("Scope", boost::log::attributes::named_scope());
    boost::log::core::get()->set_logging_enabled(false);

    auto opts = po::options_description{"Log options"};
    opts.add_options()
        ("log-file", po::value<std::string>()
            ->value_name("<file>")
            ->notifier(&initFileSink), "log to file with given path")
        ("log-console", po::value<bool>()
            ->value_name("0|1")
            ->default_value(static_cast<bool>(consoleDefault))
            ->notifier(&initConsoleSink),
            "log to console")
        ("log-syslog", po::value<std::string>()
            ->value_name("<name>")->notifier(&initSyslogSink),
            "log to syslog with given program name")
    ;
    return opts;
}

namespace {

boost::log::formatter defaultFormatter () {
    namespace expr = boost::log::expressions;
    namespace attrs = boost::log::attributes;
    return expr::stream
        << "[" << expr::attr<attrs::local_clock::value_type, util::LogSafely>("TimeStamp") << "]"
        //<< "[thread=" << expr::attr<attrs::current_thread_id::value_type>("ThreadID") << "]"
        //<< "[" << expr::attr<attrs::named_scope::value_type>("Scope") << "]"
        << expr::if_(expr::has_attr<std::string>("Channel"))[
            expr::stream << " " << expr::attr<std::string>("Channel")
        ]
        << expr::if_(expr::has_attr<std::string>("Protocol"))[
            expr::stream << " " << expr::attr<std::string>("Protocol")
        ]
        << expr::if_(expr::has_attr<std::string>("DevicePath"))[
        expr::stream << " [" << expr::attr<std::string>("DevicePath") << "]"
        ]
        << expr::if_(expr::has_attr<std::string>("RemoteEndpoint"))[
        expr::stream << " [" << expr::attr<std::string>("RemoteEndpoint") << "]"
        ]
        << expr::if_(expr::has_attr<std::string>("SerialId"))[
            expr::stream << " [" << expr::attr<std::string>("SerialId") << "]"
        ] // .else_ []
        << expr::if_(expr::has_attr<std::string>("RequestId"))[
            expr::stream << " [RequestId=" << expr::attr<std::string>("RequestId") << "]"
        ]
        << " " << expr::smessage;
}

void initFileSink (const std::string& logFile) {
    if (!logFile.size()) return;

    auto absLogFile = fs::absolute(logFile);
    auto canonParentPath = fs::weakly_canonical(absLogFile.parent_path());
    // Canonicalize (make absolute without symlinks, '.', or '..') the parent path so we end
    // up with a path we can pass to `create_directories`.

    if (!fs::exists(canonParentPath)) {
        fs::create_directories(canonParentPath);
    }
    else if (!fs::is_directory(canonParentPath)) {
        throw std::runtime_error{canonParentPath.string() + " is not a directory"};
    }

    namespace keywords = boost::log::keywords;
    boost::log::add_file_log(
        keywords::file_name = absLogFile,
        keywords::auto_flush = true
    )->set_formatter(defaultFormatter());
    boost::log::core::get()->set_logging_enabled(true);
}

void initConsoleSink (bool enabled) {
    if (!enabled) return;

    namespace keywords = boost::log::keywords;
    boost::log::add_console_log(
        std::clog,
        keywords::auto_flush = true
    )->set_formatter(defaultFormatter());
    boost::log::core::get()->set_logging_enabled(true);
}

void initSyslogSink (const std::string& programName) {
    if (!programName.size()) return;

    namespace keywords = boost::log::keywords;
    namespace sinks = boost::log::sinks;
    auto backend = boost::make_shared<sinks::syslog_backend>(
        keywords::facility = sinks::syslog::user,
        keywords::use_impl = sinks::syslog::native,
        keywords::ident = programName     // programname property in rsyslog
        );
    using SyslogSink = sinks::synchronous_sink < sinks::syslog_backend > ;
    auto sink = boost::make_shared<SyslogSink>(backend);
    sink->set_formatter(defaultFormatter());

    boost::log::core::get()->add_sink(sink);
    boost::log::core::get()->set_logging_enabled(true);
}

} // anonymous namespace

}} // namespace util::log
