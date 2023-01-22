// libcosmos
#include "cosmos/formatting.hxx"

// nst
#include "EscapeHandler.hxx"
#include "nst.hxx"

namespace nst {

EscapeHandler::EscapeHandler(Nst &nst) :
	m_nst(nst),
	m_str_escape(nst),
	m_csi_escape(nst) {}

bool EscapeHandler::handleCommandTerminator() {
	if (m_state[Escape::STR_END]) {
		resetStringEscape();
		m_str_escape.process();
		return true;
	}

	return false;
}

void EscapeHandler::handleControlCode(const RuneInfo &rinfo) {

	auto &term = m_nst.getTerm();
	const auto &cursor = term.getCursor();
	const auto code = rinfo.asChar();

	switch (code) {
	case '\t':   /* HT */
		term.moveToNextTab();
		return;
	case '\b':   /* BS */
		term.moveCursorTo(cursor.getPos().prevCol());
		return;
	case '\r':   /* CR */
		term.moveCursorTo(cursor.getPos().startOfLine());
		return;
	case '\f':   /* LF */
	case '\v':   /* VT */
	case '\n':   /* LF */ {
		/* go also to first col if CRLF mode is set */
		term.moveToNewline(term.getCarriageReturn());
		return;
	}
	case '\a':   /* BEL */ {
		/* backwards compatibility to xterm, which also accepts BEL
		 * (instead of 'ST') as OSC command terminator. */
		const auto handled = handleCommandTerminator();
		if (!handled)
			// otherwise process as a regular bell
			m_nst.getX11().ringBell();
		break;
	}
	case '\033': /* ESC */
		markNewCSI();
		return;
	case '\016': /* SO (LS1 -- Locking shift 1) */
	case '\017': /* SI (LS0 -- Locking shift 0) */ {
		// switch between predefined character sets
		const auto charset = 1 - (code - '\016');
		term.setCharset(charset);
		return;
	}
	case '\032': /* SUB */
		term.showSubMarker();
		/* FALLTHROUGH */
	case '\030': /* CAN */
		m_csi_escape.reset();
		break;
	case '\005': /* ENQ (IGNORED) */
	case '\000': /* NUL (IGNORED) */
	case '\021': /* XON (IGNORED) */
	case '\023': /* XOFF (IGNORED) */
	case 0177:   /* DEL (IGNORED) */
		return;
	case 0x80:   /* TODO: PAD */
	case 0x81:   /* TODO: HOP */
	case 0x82:   /* TODO: BPH */
	case 0x83:   /* TODO: NBH */
	case 0x84:   /* TODO: IND */
		break;
	case 0x85:   /* NEL -- Next line */
		term.moveToNewline(); /* always go to first col */
		break;
	case 0x86:   /* TODO: SSA */
	case 0x87:   /* TODO: ESA */
		break;
	case 0x88:   /* HTS -- Horizontal tab stop */
		term.setTabAtCursor(true);
		break;
	case 0x89:   /* TODO: HTJ */
	case 0x8a:   /* TODO: VTS */
	case 0x8b:   /* TODO: PLD */
	case 0x8c:   /* TODO: PLU */
	case 0x8d:   /* TODO: RI */
	case 0x8e:   /* TODO: SS2 */
	case 0x8f:   /* TODO: SS3 */
	case 0x91:   /* TODO: PU1 */
	case 0x92:   /* TODO: PU2 */
	case 0x93:   /* TODO: STS */
	case 0x94:   /* TODO: CCH */
	case 0x95:   /* TODO: MW */
	case 0x96:   /* TODO: SPA */
	case 0x97:   /* TODO: EPA */
	case 0x98:   /* TODO: SOS */
	case 0x99:   /* TODO: SGCI */
		break;
	case 0x9a:   /* DECID -- Identify Terminal */
		m_nst.getTTY().write(config::VTIDEN, TTY::MayEcho(false));
		break;
	case 0x9b:   /* TODO: CSI */
	case 0x9c:   /* TODO: ST */
		break;
	case 0x90:   /* DCS -- Device Control String */
	case 0x9d:   /* OSC -- Operating System Command */
	case 0x9e:   /* PM -- Privacy Message */
	case 0x9f:   /* APC -- Application Program Command */ {
		const auto esc_type = static_cast<StringEscape::Type>(code);
		initStringEscape(esc_type);
		return;
	}
	} // end switch

	/* only CAN, SUB, \a and C1 chars interrupt a sequence */
	resetStringEscape();
}

EscapeHandler::WasProcessed EscapeHandler::process(const RuneInfo &rinfo) {

	/*
	 * STR sequence must be checked before anything else because it uses
	 * all following characters until it receives an ESC, SUB, ST or
	 * any other C1 control character.
	 */
	if (inStringEscape()) {
		if (m_str_escape.isTerminator(rinfo)) {
			/*
			 * NOTE: this is a bit of weird spot here, we're not
			 * returning yet, but process the actual terminator
			 * further down below. Since ST consists of two bytes
			 * 'ESC \', the actual StringEscape completion can be
			 * parsed in handleInitialEscape(). Alternatively a BEL
			 * character is also supported which is parsed in
			 * handleControlCode()
			 */
			markStringEscapeFinal();
		} else {
			m_str_escape.add(rinfo.getEncoded());
			return WasProcessed(true);
		}
	}

	/*
	 * Actions of control codes must be performed as soon they arrive
	 * because they can be embedded inside a control sequence, and they
	 * must not cause conflicts with sequences.
	 */
	if (rinfo.isControlChar()) {
		handleControlCode(rinfo);
		// control codes are not shown ever
		if (m_state.none())
			m_nst.getTerm().resetLastChar();

		return WasProcessed(true);
	} else if (m_state[Escape::START]) {
		const bool finished = checkCSISequence(rinfo);

		if (finished) {
			m_state.reset();
		}

		// All characters which form part of a sequence are not printed
		return WasProcessed(true);
	}

	return WasProcessed(false);
}

bool EscapeHandler::checkCSISequence(const RuneInfo &rinfo) {
	const auto rune = rinfo.rune();
	auto &term = m_nst.getTerm();

	if (m_state[Escape::CSI]) {
		const bool finished = m_csi_escape.addCSI(rinfo.rune());
		if (finished) {
			m_csi_escape.parse();
			m_csi_escape.process();
		}
		return finished;
	} else if (m_state[Escape::UTF8]) {
		switch (rune) {
		case 'G':
			term.setUTF8(true);
			break;
		case '@':
			term.setUTF8(false);
			break;
		}
	} else if (m_state[Escape::ALTCHARSET]) {
		// this is DEC VT100 spec related
		switch(rune) {
		default:
			std::cerr << "esc unhandled charset: ESC ( " << rune << "\n";
			break;
		case '0':
			term.setCharsetMapping(m_esc_charset, Term::Charset::GRAPHIC0);
			break;
		case 'B':
			term.setCharsetMapping(m_esc_charset, Term::Charset::USA);
			break;
		}
	} else if (m_state[Escape::TEST]) {
		if (rune == '8') {
			term.runDECTest();
		}
	} else {
		auto state = handleInitialEscape(rune);

		if (state != Escape::RESET) {
			m_state.set(state);
			/* sequence not yet finished */
			return false;
		}
	}

	return true;
}

EscapeHandler::Escape EscapeHandler::handleInitialEscape(const char ch) {
	auto &term = m_nst.getTerm();
	auto &x11 = m_nst.getX11();

	// for reference see `man 4 console_codes`

	// these are, apart from '[', non-CSI escape sequences that we handle
	// directly in this class, CSI is handled by m_csi_escape
	switch (ch) {
	case '[':
		return Escape::CSI;
	case '#':
		return Escape::TEST;
	case '%': // character set selection
		return Escape::UTF8;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
	case ']': /* OSC -- Operating System Command */
	case 'k': /* old title set compatibility */ {
		// hand over to StringEscape
		const auto esc_type = static_cast<StringEscape::Type>(ch);
		initStringEscape(esc_type);
		return Escape::STR;
	}
	case 'n': /* LS2 -- Locking shift 2 */
	case 'o': /* LS3 -- Locking shift 3 */
		term.setCharset(2 + (ch - 'n'));
		break;
	case '(': /* GZD4 -- set primary charset G0 */
	case ')': /* G1D4 -- set secondary charset G1 */
	case '*': /* G2D4 -- set tertiary charset G2 */
	case '+': /* G3D4 -- set quaternary charset G3 */
		m_esc_charset = ch - '(';
		return Escape::ALTCHARSET;
	case 'D': /* IND -- Linefeed */
		term.doLineFeed();
		break;
	case 'E': /* NEL -- Next line */
		term.moveToNewline(); /* always go to first col */
		break;
	case 'H': /* HTS -- Horizontal tab stop */
		term.setTabAtCursor(true);
		break;
	case 'M': /* RI -- Reverse index / linefeed */
		term.doReverseLineFeed();
		break;
	case 'Z': /* DECID -- Identify Terminal */
		m_nst.getTTY().write(config::VTIDEN, TTY::MayEcho(false));
		break;
	case 'c': /* RIS -- Reset to initial state */
		term.reset();
		x11.resetState();
		break;
	case '=': /* DECPAM -- Application keypad */
		x11.setMode(WinMode::APPKEYPAD, true);
		break;
	case '>': /* DECPNM -- Normal keypad */
		x11.setMode(WinMode::APPKEYPAD, false);
		break;
	case '7': /* DECSC -- Save Cursor */
		term.cursorControl(Term::TCursor::Control::SAVE);
		break;
	case '8': /* DECRC -- Restore Cursor */
		term.cursorControl(Term::TCursor::Control::LOAD);
		break;
	case '\\': /* ST -- String Terminator for StringEscape (!) */
		/* this likely is the second byte of the ST := ESC \ */
		handleCommandTerminator();
		break;
	default:
		std::cerr << "erresc: unknown sequence ESC " << cosmos::hexnum(ch, 2)
			<< " '" << (std::isprint(ch) ? ch : '.') << "'\n";
		break;
	}

	return Escape::RESET;
}

} // end ns
