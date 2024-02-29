// C++
#include <codecvt>
#include <fstream>
#include <locale>

// cosmos
#include "cosmos/string.hxx"

// nst
#include "ConfigFile.hxx"

namespace nst {

ConfigFile::ConfigFile(cosmos::ILogger &logger) :
		m_logger{logger} {
}

std::optional<std::string> ConfigFile::asString(const std::string &key) const {
	if (const auto ws = asWideString(key); ws != std::nullopt) {
		return toNarrowString(*ws);
	}

	return std::nullopt;
}

std::optional<std::wstring> ConfigFile::asWideString(const std::string &key) const {
	auto it = m_items.find(key);
	if (it == m_items.end())
		return std::nullopt;

	auto value = it->second;

	if (!unquoteStringValue(value))
		return std::nullopt;

	return value;
}

void ConfigFile::parse(const std::string_view path) {
	std::wifstream fs{path.data()};
	fs.imbue(std::locale("en_US.utf8"));

	if (!fs) {
		return;
	}

	size_t linenr = 1;
	std::wstring line;
	while (std::getline(fs, line).good()) {
		parseLine(path, linenr, line);
		linenr++;
	}
}

void ConfigFile::parseLine(const std::string_view file, const size_t linenr, std::wstring &line) {
	auto parseError = [=](const std::string_view error) {
		m_logger.error() << "ConfigFile parse error in " << file << ":" << linenr << ": " << error << "\n";
	};

	cosmos::strip(line);

	if (line.empty())
		return;
	// comment
	else if (line[0] == L'#')
		return;

	auto sep = line.find(L'=');

	if (sep == line.npos) {
		parseError("missing '=' seperator");
		return;
	}

	auto key = line.substr(0, sep);
	auto value = line.substr(sep+1);

	cosmos::strip(value);

	if (auto narrow_key = toNarrowString(cosmos::stripped(key)); narrow_key != std::nullopt) {
		m_items[*narrow_key] = value;
	} else {
		parseError("key contains non-ascii characters");
		return;
	}
}

std::optional<std::string> ConfigFile::toNarrowString(const std::wstring &s) const {
	std::string ret;

	for (const auto ch: s) {
		if (ch >= 128) {
			m_logger.error() << "ConfigFile parse error, non-ascii characters found in configuration value \""
				<< toUTF8(s) << "\"\n";
			return std::nullopt;
		}

		ret.push_back(static_cast<char>(ch));
	}

	return ret;
}

bool ConfigFile::unquoteStringValue(std::wstring &s) const {
	if (s.size() < 2 || s[0] != L'"' || s.back() != L'"') {
		m_logger.error() << "ConfigFile parse error, badly quoted string value encountered in \"" << toUTF8(s) << "\"\n";
		return false;
	}

	s = s.substr(1, s.size() - 2);

	for (size_t idx = 0; idx < s.size(); idx++) {
		if (const auto ch = s[idx]; ch == L'\\') {
			s.erase(idx, 1);
			if (idx >= s.size()) {
				m_logger.error() << "ConfigFile parse error, stray \\ in \"" << toUTF8(s) << "\"\n";
				return false;
			}

			if (const auto esc = s[idx]; esc != L'"' && esc != '\\') {
				m_logger.error() << "ConfigFile parse error, unsupported backslash escape in \"" << toUTF8(s) << "\"\n";
				return false;
			}
		}
	}

	return true;
}

std::string ConfigFile::toUTF8(const std::wstring &s) const {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
    return myconv.to_bytes(s);
}

} // end ns
