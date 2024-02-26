#pragma once

// C++
#include <map>
#include <optional>
#include <string>
#include <string_view>

// cosmos
#include "cosmos/io/ILogger.hxx"

namespace nst {

/// Simple configuration file data access.
/**
 * To avoid pulling in a complex configuration file handling library, this
 * class implements lean and mean configuration file parsing logic.
 *
 * It simply parses `key = value` pairs line by line from given input files.
 * Multiple files can be parsed, where newer files override entries found in
 * older files.
 *
 * The class's API offers access to each individual configuration key and also
 * converting the values into typical types used in NST like strings, integers
 * and colors.
 **/
class ConfigFile {
public: // functions

	ConfigFile(cosmos::ILogger &logger);

	/// Try to parse the given configuration file and add its items to internal state.
	/**
	 * If the file does not exist then nothing happens.
	 *
	 * Existing configuration items will overwritten by new configuration
	 * items found in the new configuration file.
	 **/
	void parse(const std::string_view path);

	std::optional<std::string> asString(const std::string key) const {
		auto it = m_items.find(key);
		if (it == m_items.end())
			return std::nullopt;

		return it->second;
	}

protected: // functions

	void parseLine(const std::string_view file, const size_t linenr, std::string &line);

protected: // data

	cosmos::ILogger &m_logger;
	std::map<std::string, std::string> m_items;
};

} // end ns
