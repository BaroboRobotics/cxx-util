#ifndef UTIL_LOG_HPP
#define UTIL_LOG_HPP

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

#include <boost/optional.hpp>

#include <string>

namespace util { namespace log {

// Build and return a description of what command line options the logging
// system supports. Use the returned value to parse the command line, then pass
// the generated variables_map to initialize.
// If logFileName is specified, the description will configure the logging
// system to log to that file by default, unless overridden on the command
// line by --log-file.
boost::program_options::options_description optionsDescription (boost::optional<std::string> logFileName);

// Add some common attributes to the logging core and enable sinks as specified
// by the configuring variables_map.
void initialize (std::string appName, const boost::program_options::variables_map& conf);

}} // namespace util::log

#endif
