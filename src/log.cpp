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

boost::program_options::options_description optionsDescription (boost::optional<std::string> logFileName) {
    auto opts = po::options_description{"Log options"};
    opts.add_options()
        ("log-file",
            logFileName
            ? po::value<std::string>()->default_value(*logFileName)
            : po::value<std::string>(),
            "enable file log sink (empty string disables)")
        ("log-console", po::value<bool>()->default_value(false), "enable console log sink")
        ("log-syslog", po::value<bool>()->default_value(false), "enable syslog sink")
    ;
    return opts;
}

void initialize (std::string appName, const boost::program_options::variables_map& conf) {
    namespace expr = boost::log::expressions;
    namespace keywords = boost::log::keywords;
    namespace attrs = boost::log::attributes;
    namespace sinks = boost::log::sinks;

    boost::log::add_common_attributes();

    auto core = boost::log::core::get();
    core->add_global_attribute("Scope", attrs::named_scope());

    boost::log::formatter formatter =
        expr::stream
            << "[" << expr::attr<attrs::local_clock::value_type, util::LogSafely>("TimeStamp") << "]"
            << "[thread=" << expr::attr<attrs::current_thread_id::value_type>("ThreadID") << "]"
            //<< "[" << expr::attr<attrs::named_scope::value_type>("Scope") << "]"
            << " " << expr::attr<std::string>("Channel")
            << " " << expr::attr<std::string>("Protocol")
            << expr::if_(expr::has_attr<std::string>("SerialId"))[
                expr::stream << " [SerialId=" << expr::attr<std::string>("SerialId") << "]"
            ] // .else_ []
            << expr::if_(expr::has_attr<std::string>("RequestId"))[
                expr::stream << " [RequestId=" << expr::attr<std::string>("RequestId") << "]"
            ]
            << " " << expr::smessage;

    // Boost.Log sets up a default console sink for us. There is no way to
    // explicitly disable it, though it is implicitly disabled when a sink is
    // added. Disable the logging core here and enable the logging core when a
    // sink is added, so that the default sink is never used.
    core->set_logging_enabled(false);

    if (conf.count("log-file") && conf["log-file"].as<std::string>().size()) {
        auto logFile = conf["log-file"].as<std::string>();
        auto parentPath = fs::path{logFile}.parent_path();
        if (!parentPath.empty()) {
            fs::create_directories(parentPath);
        }
        boost::log::add_file_log(
            keywords::file_name = logFile,
            keywords::auto_flush = true
            )->set_formatter(formatter);
        core->set_logging_enabled(true);
    }

    if (conf.count("log-console") && conf["log-console"].as<bool>()) {
        boost::log::add_console_log(
            std::clog,
            keywords::auto_flush = true
            )->set_formatter(formatter);
        core->set_logging_enabled(true);
    }

    if (conf.count("log-syslog") && conf["log-syslog"].as<bool>()) {
        auto backend = boost::make_shared<sinks::syslog_backend>(
            keywords::facility = sinks::syslog::user,
            keywords::use_impl = sinks::syslog::native,
            keywords::ident = appName     // programname property in rsyslog
            );
        using SyslogSink = sinks::synchronous_sink < sinks::syslog_backend > ;
        auto sink = boost::make_shared<SyslogSink>(backend);
        sink->set_formatter(formatter);
        core->add_sink(sink);
        core->set_logging_enabled(true);
    }
}

}} // namespace util::log
