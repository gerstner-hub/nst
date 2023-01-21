#ifndef NST_CSIESCAPE_HXX
#define NST_CSIESCAPE_HXX

// libc
#include <stddef.h>

// stdlib
#include <string>
#include <string_view>
#include <vector>

// cosmos
#include "cosmos/algs.hxx"

// nst
#include "codecs.hxx"
#include "helper.hxx"

namespace nst {

class Nst;

/// Handles CSI and some other types of escape sequences
/**
 * CSI (Control Sequence Introducer) struct follow the following model:
 *
 * ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]]
 *
 * This class parses such sequences and triggers actions that result from the
 * sequences.
 *
 * Beyond this it also parses some other types of escape sequences from within
 * handleInitialEscape(). This is a certain duplication of what
 * Term::handleControlCode() does for 8-bit C1 control codes.
 **/
struct CSIEscape {
public: // functions

	explicit CSIEscape(Nst &nst);

	/// returns true if the sequence is complete which can happen for non-CSI escapes
	bool handleInitialEscape(const char ch);

	/// adds the given character to the sequence, returns whether the sequence is complete
	bool addCSI(const char ch) {
		m_str.push_back(ch);
		// signal complete either if the maximum sequence length has
		// been reached or a final byte appears
		return m_str.length() >= MAX_STR_SIZE || isFinalByte(ch);
	}

	/// processes parsed CSI parameters
	void process();

	/// parses the current CSI sequence into member variables
	void parse();

	/// resets all parsing state and data
	void reset() {
		m_is_private_csi = false;
		m_mode_suffix.clear();
		m_args.clear();
		m_str.clear();
	}

protected: // functions

	/// makes sure the given argument index exists in m_args, possibly assigning defval
	/**
	 * If the given argument index is not available then m_args is
	 * extended accordingly. Whether extended or not the function also
	 * makes sure that if the value at the given index is <= 0 that \c
	 * defval is assigned to it.
	 *
	 * \return The current value of the argument at index
	 **/
	int ensureArg(size_t index, int defval);

	/// dumps the current sequence to stderr prefixed by \c prefix
	void dump(const std::string_view &prefix) const;

	bool isFinalByte(const char ch) const {
		// this range is coming from the CSI spec
		return cosmos::in_range(ch, 0x40, 0x7E);
	}

	/// forwards a setMode() call to Term for the current parse context
	void setMode(const bool enable) const;

protected: // data

	std::string m_str; /// the raw escape sequence bytes collected so far
	bool m_is_private_csi = false; /// whether a private CSI control was parsed
	std::vector<int> m_args; /// up to 16 integer parameter for the current CSI
	std::string m_mode_suffix; /// the intermediate and final characters of the sequence
	static constexpr size_t MAX_STR_SIZE = 128 * utf8::UTF_SIZE;
	Nst &m_nst;

};

} // end ns

#endif // inc. guard
