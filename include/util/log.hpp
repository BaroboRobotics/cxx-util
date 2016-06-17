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

// Build and return a description of what command line options the logging
// system supports. Use the returned value to parse the command line, then pass
// the generated variables_map to initialize.
boost::program_options::options_description optionsDescription ();

// Add some common attributes to the logging core and enable sinks as specified
// by the configuring variables_map.
void initialize (std::string appName, const boost::program_options::variables_map& conf);

using Logger = boost::log::sources::severity_channel_logger<>;

using boost::log::keywords::channel;

}} // namespace util::log

#endif
