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
 *
 * Basic unicode support is necessary for configuration string values e.g. for
 * the word seperator characters. Therefore strings configuration values have
 * to be quoted like "this". Minimal backslash escapes are supported for
 * escaping double quotes '\"' and the backslash character itself '\\'.
 *
 * For configuration keys only ASCII characters are allowed. No multiline
 * continuation is supported.
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

	/// Attempt to access the given configuration key and returns its ASCII string value.
	/**
	 * If the key does not exist or has parsing / type error then
	 * std::nullopt is returned. On parsing / type errors the error will
	 * be logged. Error conditions include:
	 *
	 * - badly quoted string.
	 * - bad backslash escapes in string.
	 * - string contains non-ASCII characters.
	 **/
	std::optional<std::string> asString(const std::string &key) const;

	/// Wide string variant of asString(const std::string&)
	std::optional<std::wstring> asWideString(const std::string &key) const;

protected: // functions

	void parseLine(const std::string_view file, const size_t linenr, std::wstring &line);

	/// Parse a string value from the given string in-place.
	/**
	 * String values have to be quoted and may contain escape sequences.
	 * This function parses and removes these elements. On any parsing
	 * errors diagnostics will be output via logging and \c false is
	 * returned and the contents of \c s are undefined. Otherwise \c true
	 * is returned and \c s contains the properly unquoted string value.
	 **/
	bool unquoteStringValue(std::wstring &s) const;

	/// Turns a wide string into a plain ASCII string.
	/**
	 * If any non-ASCII characters are contained then std::nullopt is
	 * returned and an error is logged.
	 **/
	std::optional<std::string> toNarrowString(const std::wstring &s) const;

	/// Encode a wide string as UTF8 and return the result as a std::string.
	std::string toUTF8(const std::wstring &s) const;

protected: // data

	cosmos::ILogger &m_logger;
	std::map<std::string, std::wstring> m_items;
};

} // end ns
