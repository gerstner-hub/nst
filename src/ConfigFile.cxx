// C++
#include <fstream>

// cosmos
#include "cosmos/string.hxx"

// nst
#include "ConfigFile.hxx"

namespace nst {

ConfigFile::ConfigFile(cosmos::ILogger &logger) :
		m_logger{logger} {
}

void ConfigFile::parse(const std::string_view path) {
	std::ifstream fs{path.data()};

	if (!fs) {
		return;
	}

	size_t linenr = 1;
	std::string line;
	while (std::getline(fs, line).good()) {
		parseLine(path, linenr, line);
		linenr++;
	}
}

void ConfigFile::parseLine(const std::string_view file, const size_t linenr, std::string &line) {
	cosmos::strip(line);

	if (line.empty())
		return;
	// comment
	else if (line[0] == '#')
		return;

	auto sep = line.find('=');

	if (sep == line.npos) {
		m_logger.error() << "ConfigFile parse error in " << file << ":" << linenr << ": missing '=' separator\n";
		return;
	}

	auto key = line.substr(0, sep);
	auto value = line.substr(sep+1);

	cosmos::strip(key);
	cosmos::strip(value);

	m_items[key] = value;
}

} // end ns
