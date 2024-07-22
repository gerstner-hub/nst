#pragma once

// C++
#include <algorithm>
#include <vector>
#include <cassert>

// nst
#include "Glyph.hxx"

namespace nst {

/// A series of Glyphs forming a line on the terminal.
/**
 * This is a rather simple wrapper around a std::vector, because we want to
 * control the iterator ranges applied to Lines.
 *
 * We are not doing full reflow of lines upon window resize, because of the
 * involved complexity, but we also don't want to lose information if a window
 * is decreased in size temporarily (e.g. due to a tiling window manager).
 *
 * To achieve this we keep existing columns that would otherwise be dropped
 * when the number of columns is decreased. The actual vector never shrinks but
 * the iterator interfaces only iterates over the currently set dimension of
 * the screen.
 *
 * Once the size of the window is increased again we can simply change our
 * bookkeeping and the Glyphs that have been lost before will show up again.
 * This behaviour can be disabled via the keep_data_on_shrink setting, which
 * is the default on non-scrolling screens. This is mostly to avoid trouble
 * when being on the alt screen (which is non-scrolled), where the application
 * is usually responsible of restoring screen content upon resize.
 *
 * NOTE: ideally we would be able to better detect changes to existing lines
 * to cut-off hidden Glyphs after all, to avoid then inconsistent content
 * coming back into view. Sadly there is no central spot to detect actual
 * content changes to Lines currently. The dirty attribute is only concerned
 * with drawing changes, not which logical line changes.
 *
 * Currently this is done in Term::clearRegion(), to clear lines that are
 * edited using various operations or when scrolling the screen (not history)
 * up/down. It seems this is enough for most situations.
 **/
class Line {
public: // types

	using GlyphVector = std::vector<Glyph>;
	using iterator = GlyphVector::iterator;
	using const_iterator = GlyphVector::const_iterator;
	using value_type = GlyphVector::value_type;

public: // functions

	explicit Line(const bool keep_data_on_shrink) :
		m_keep_data_on_shrink{keep_data_on_shrink} {}

	Line(const Line &other) :
			m_keep_data_on_shrink{other.m_keep_data_on_shrink} {
		*this = other;
	}

	Line& operator=(const Line &other) {
		assert(m_keep_data_on_shrink == other.m_keep_data_on_shrink);
		m_dirty = other.m_dirty;
		m_glyphs = other.m_glyphs;
		m_cols = other.m_cols;
		return *this;
	}

	/// Returns whether the line has a WRAP attribute set for the last element
	bool isWrapped() const {
		return !empty() && back().mode[Attr::WRAP];
	}

	bool isDirty() const {
		return m_dirty;
	}

	void setDirty(const bool dirty) const {
		m_dirty = dirty;
	}

	void clear() {
		m_glyphs.clear();
		m_cols = 0;
	}

	void resize(GlyphVector::size_type size, const Glyph &defval = Glyph()) {
		if (!m_keep_data_on_shrink || size > m_glyphs.size()) {
			m_glyphs.resize(size, defval);
		}
		m_cols = size;
	}

	bool empty() const { return m_cols == 0; }

	auto size() const { return m_cols; }

	auto begin() { return m_glyphs.begin(); }
	auto end() { return m_glyphs.begin() + m_cols; }
	auto begin() const { return m_glyphs.begin(); }
	auto end() const { return m_glyphs.begin() + m_cols; }

	auto rend() const { return m_glyphs.rend(); }
	auto rbegin() const { return m_glyphs.rend() - m_cols; }

	Glyph& back() { return *(m_glyphs.begin() + m_cols - 1); }
	const Glyph& back() const { return *(m_glyphs.begin() + m_cols - 1); }
	Glyph& front() { return m_glyphs.front(); }
	const Glyph& front() const { return m_glyphs.front(); }

	Glyph& operator[](const GlyphVector::size_type pos) {
		return m_glyphs[pos];
	}

	const Glyph& operator[](const GlyphVector::size_type pos) const {
		return m_glyphs[pos];
	}

	const auto& raw() const { return m_glyphs; }

	/// Returns the number of characters in this line not counting trailing spaces.
	int usedLength() const {
		if (isWrapped()) {
			return m_cols;
		}

		const auto last_col = std::find_if(
				rbegin(), rend(),
				[](const Glyph &g) { return g.hasValue(); });

		return rend() - last_col;
	}

	/// Discard any saved hidden columns.
	void shrinkToPhysical() {
		m_glyphs.resize(m_cols);
	}

protected: // data

	mutable bool m_dirty = false;
	const bool m_keep_data_on_shrink;
	size_t m_cols = 0; ///< number of columns actually used in m_glyphs
	GlyphVector m_glyphs;
};

using LineVector = std::vector<Line>;

} // end ns
