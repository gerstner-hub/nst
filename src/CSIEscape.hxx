#ifndef NST_CSIESCAPE_HXX
#define NST_CSIESCAPE_HXX

// C++
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// cosmos
#include "cosmos/algs.hxx"

// nst
#include "codecs.hxx"

namespace nst {

class Nst;

/// Handles CSI and some other types of escape sequences.
/**
 * CSI (Control Sequence Introducer) struct follow the following model:
 *
 * ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]]
 *
 * This class parses such sequences and triggers actions that result from the
 * sequences.
 **/
struct CSIEscape {
public: // functions

	explicit CSIEscape(Nst &nst);

	/// Adds the given character to the sequence, returns whether the sequence is complete.
	bool addCSI(const char ch) {
		m_str.push_back(ch);
		// signal complete either if the maximum sequence length has
		// been reached or a final byte appears
		return m_str.length() >= MAX_STR_SIZE || isFinalByte(ch);
	}

	/// Processes parsed CSI parameters.
	void process();

	/// Parses the current CSI sequence into member variables.
	void parse();

	/// Resets all parsing state and data.
	void reset() {
		m_is_private_csi = false;
		m_mode_suffix.clear();
		m_args.clear();
		m_str.clear();
	}

	/// If focus reporting was enabled, report focus state change on TTY.
	void reportFocus(bool in_focus);

protected: // functions

	/// Makes sure the given argument index exists in m_args, possibly assigning defval.
	/**
	 * If the given argument index is not available then m_args is
	 * extended accordingly. Whether extended or not the function also
	 * makes sure that if the value at the given index is <= 0 that \c
	 * defval is assigned to it.
	 *
	 * \return The current value of the argument at index
	 **/
	int ensureArg(size_t index, int defval);

	/// Dumps the current sequence to stderr prefixed by \c prefix.
	void dump(const std::string_view prefix) const;

	bool isFinalByte(const char ch) const {
		// this range is coming from the CSI spec
		return cosmos::in_range(ch, 0x40, 0x7E);
	}

	/// Calls setMode() or setPrivateMode() depending on current context.
	void setModeGeneric(const bool enable);

	/// Process a set terminal mode request.
	void setMode(const bool set);

	/// Process a private set terminal mode request.
	void setPrivateMode(const bool set);

	/// Forwards a setCursorAttrs() call to Term.
	bool setCursorAttrs() const;

	int32_t parseColor(std::vector<int>::const_iterator &it) const;

	/// Handle fb/bg cursor color settings from dim/bright color ranges.
	bool handleCursorColorSet(const int attr) const;

protected: // data

	std::string m_str; /// The raw escape sequence bytes collected so far
	bool m_is_private_csi = false; /// Whether a private CSI control was parsed
	std::vector<int> m_args; /// Up to 16 integer parameter for the current CSI
	std::string m_mode_suffix; /// The intermediate and final characters of the sequence
	static constexpr size_t MAX_STR_SIZE = 128 * utf8::UTF_SIZE;
	Nst &m_nst;
};

} // end ns

#endif // inc. guard
