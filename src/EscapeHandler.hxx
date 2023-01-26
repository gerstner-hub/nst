#ifndef NST_ESCAPEHANDLER_HXX
#define NST_ESCAPEHANDLER_HXX

// libcosmos
#include "cosmos/BitMask.hxx"
#include "cosmos/types.hxx"

// nst
#include "CSIEscape.hxx"
#include "StringEscape.hxx"

namespace nst {

/// Handling of all types of escape and control sequences supported by NST
/**
 * This class handles single byte control codes, XTerm style OSC string
 * sequences (via StringEscape) and CSI sequences (via CSIEscape). Some
 * non-CSI sequences are also supported directly by this class.
 *
 * This class holds the parsing state that is required to deal correctly with
 * the three types of escape codes/sequences mentioned above.
 *
 * This handler an its sub types invoke the appropriate functions in Term or
 * X11 that correspond to the respective control codes or escape sequences.
 **/
class EscapeHandler {
public: // types

	/// escape sequence parsing status to determine what to do with sequential input
	enum class Escape {
		RESET      = 0,      /// no state at all
		START      = 1 << 0, /// \033 escape sequence started
		CSI        = 1 << 1, /// CSI escape sequence is about to be parsed (CSIEscape)
		STR        = 1 << 2, /// DCS, OSC, PM, APC (StringEscape)
		ALTCHARSET = 1 << 3, /// requests setting an alternative character set
		STR_END    = 1 << 4, /// a StringEscape sequence is complete, waiting for ST or BEL
		TEST       = 1 << 5, /// Enter in test mode
		UTF8       = 1 << 6, /// UTF8 (character set change) requested
	};

	typedef cosmos::BitMask<Escape> State;

	using WasProcessed = cosmos::NamedBool<struct continue_proc_t, true>;

public: // functions

	explicit EscapeHandler(Nst &nst);

	/// Processes the given input Rune if it requires special processing
	/**
	 * \return Whether the input was processes as an escape sequence.
	 * Otherwise the input can be processed for graphical display.
	 **/
	WasProcessed process(const RuneInfo &r);

	void reportFocus(bool in_focus) {
		m_csi_escape.reportFocus(in_focus);
	}

protected: // functions

	/// returns whether we're currently passing a StringEscape sequence
	bool inStringEscape() const { return m_state[Escape::STR]; }

	/// initialize a newly starting terminal string escape sequence of the given type
	void initStringEscape(const StringEscape::Type &type) {
		m_str_escape.reset(type);
		m_state.set(Escape::STR);
	}

	/// resets StringEscape related parsing status
	void resetStringEscape() {
		m_state.reset({Escape::STR_END, Escape::STR});
	}

	/// marks that we're waiting for the StringEscape terminator
	void markStringEscapeFinal() {
		m_state.reset({Escape::START, Escape::STR});
		m_state.set(Escape::STR_END);
	}

	/// marks that we're now parsing a new CSI-like escape sequence
	void markNewCSI() {
		m_csi_escape.reset();
		m_state.reset({Escape::CSI, Escape::ALTCHARSET, Escape::TEST});
		m_state.set(Escape::START);
	}

	/// checks subsequent input in an CSI style escape sequence context
	/**
	 * \return Whether the sequence is finished
	 **/
	bool checkCSISequence(const RuneInfo &rinfo);

	/// handle the given input control code
	/**
	 * This handles single byte control codes which may also start a
	 * multi-byte sequence, which will then be handed over to m_str_escape
	 * or m_csi_escape respectively.
	 **/
	void handleControlCode(const RuneInfo &rinfo);

	/// handles the first character after an initial CSI-like escape
	/**
	 * This function checks the further context of a CSI style escape
	 * sequence and processes it as necessary.
	 *
	 * Beyond this it also parses some other types of escape sequences
	 * that are not specified in CSI. This is a certain duplication of
	 * what handleControlCode() does for 8-bit C1 control codes.
	 *
	 * \return The additional escape state to set or RESET if the escape
	 * is already finished which can happen for some non-CSI escape
	 * sequences.
	 **/
	Escape handleInitialEscape(const char ch);

	/// called when a StringEscape terminating code or sequence has been encountered
	/**
	 * \return \c true if the terminator has been processed, otherwise the
	 * input can be used for other purposes, if possible
	 **/
	bool handleCommandTerminator();

protected: // data

	Nst &m_nst;
	State m_state;             /// escape state flags
	StringEscape m_str_escape; /// keeps track of string escape input sequences
	CSIEscape m_csi_escape;    /// keeps track of CSI escape input sequences
	size_t m_esc_charset = 0;  /// selected charset for ALTCHARSET mode
};

} // end ns

#endif // inc. guard
