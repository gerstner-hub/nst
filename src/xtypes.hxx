#ifndef NST_XTYPES_HXX
#define NST_XTYPES_HXX

// stdlib
#include <bitset>

// libc
#include <limits.h>

/* types used in nst_config.h */
struct Shortcut {
	uint mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
};

struct MouseShortcut {
	uint mod;
	uint button;
	void (*func)(const Arg *);
	const Arg arg;
	uint  release;
};

struct Key {
	KeySym k;
	uint mask;
	const char *s;
	/* three-valued logic variables: 0 indifferent, 1 on, -1 off */
	signed char appkey;    /* application keypad */
	signed char appcursor; /* application cursor */
};

class PressedButtons : public std::bitset<11> {
public: // data

	static constexpr size_t NO_BUTTON = 12;
public:

	/// returns the position of the lowest button pressed
	size_t getFirstButton() const {
		for (size_t bit = 0; bit < size(); bit++) {
			if (this->test(bit))
				return bit + 1;
		}

		return NO_BUTTON;
	}

	bool valid(const size_t button) const {
		return button >= 1 && button <= size();
	}

	void setPressed(const size_t button) {
		this->set(button - 1, true);
	}

	void setReleased(const size_t button) {
		this->set(button - 1, false);
	}
};

/* X modifiers */
#define XK_ANY_MOD    UINT_MAX
#define XK_NO_MOD     0
#define XK_SWITCH_MOD (1<<13|1<<14)

/* XEMBED messages */
#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

#endif
