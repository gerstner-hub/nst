#ifndef NST_PRESSED_BUTTONS_HXX
#define NST_PRESSED_BUTTONS_HXX

// C++
#include <bitset>

// X++
#include "X++/types.hxx"

namespace nst {

/// Represents the current mouse button press state received via XEvents.
class PressedButtons :
		public std::bitset<11> {
public: // data

	static constexpr xpp::Button NO_BUTTON{12};

public:

	/// returns the position of the lowest button pressed, or NO_BUTTON
	xpp::Button firstButton() const {
		for (size_t bit = 0; bit < size(); bit++) {
			if (this->test(bit))
				return xpp::Button{static_cast<unsigned int>(bit) + 1};
		}

		return NO_BUTTON;
	}

	bool valid(const xpp::Button button) const {
		return button >= xpp::Button::BUTTON1 && button < NO_BUTTON;
	}

	void setPressed(const xpp::Button button) {
		if (valid(button)) {
			this->set(index(button), true);
		}
	}

	void setReleased(const xpp::Button button) {
		if (valid(button)) {
			this->set(index(button), false);
		}
	}

	static bool isScrollWheel(const xpp::Button button) {
		return button == xpp::Button::BUTTON4 || button == xpp::Button::BUTTON5;
	}

protected:

	size_t index(const xpp::Button button) const {
		return xpp::raw_button(button) - 1;
	}
};

} // end ns

#endif // inc. guard
