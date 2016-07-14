#ifndef UTIL_LOG_HPP
#define UTIL_LOG_HPP

#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/keywords/channel.hpp>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

#include <boost/optional.hpp>

#include <string>

namespace util { namespace log {

boost::program_options::options_description optionsDescription ();
// Build and return a description of what command line options the logging system supports. Use the
// returned value to parse the command line -- logging sinks will be automatically configured when
// boost::program_options::notify() is called.
//
// This function also adds some common attributes to the logging core.

using Logger = boost::log::sources::severity_channel_logger<>;

using boost::log::keywords::channel;

}} // namespace util::log

#endif
