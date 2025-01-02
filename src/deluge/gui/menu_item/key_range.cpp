/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "key_range.h"
#include "gui/menu_item/range.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {

void KeyRange::selectEncoderAction(int32_t offset) {
	int32_t const KEY_MIN = 0;
	int32_t const KEY_MAX = 11;

	// If editing the range
	if (soundEditor.editingRangeEdge != RangeEdit::OFF) {

		// Editing lower
		if (soundEditor.editingRangeEdge == RangeEdit::LEFT) {
			// Do not allow lower to pass upper
			lower = std::clamp(lower + offset, KEY_MIN, upper);
		}
		// Editing upper
		else {
			upper = std::clamp(upper + offset, lower, KEY_MAX);
		}

		drawValueForEditingRange(false);
	}

	else {
		if (upper != lower) {
			return;
		}

		lower = std::clamp(lower + offset, KEY_MIN, KEY_MAX);
		upper = lower;

		drawValue();
	}
}

std::string KeyRange::getText(size_t* getLeftLength, size_t* getRightLength, bool mayShowJustOne) {
	std::string left;
	left += noteCodeToNoteLetter[lower];

	if (noteCodeIsSharp[lower]) {
		left += display->haveOLED() ? '#' : '.';
	}

	if (getLeftLength != nullptr) {
		*getLeftLength = (left.back() == '.') ? 2 : 1;
	}

	if (mayShowJustOne && lower == upper) {
		if (getRightLength != nullptr) {
			*getRightLength = 0;
		}
		return left;
	}

	std::string right;
	right += noteCodeToNoteLetter[upper];

	if (noteCodeIsSharp[upper]) {
		left += display->haveOLED() ? '#' : '.';
	}

	if (getRightLength != nullptr) {
		*getLeftLength = (left.back() == '.') ? 2 : 1;
	}

	// concatenate and return
	left += '-';
	left += right;
	return left;
}

// Call seedRandom() before you call this
int32_t KeyRange::getRandomValueInRange() {
	if (lower == upper) {
		return lower;
	}
	else {
		int32_t range = upper - lower;
		if (range < 0) {
			range += 12;
		}

		int32_t value = lower + random(range);
		if (range >= 12) {
			range -= 12;
		}
		return value;
	}
}

bool KeyRange::isTotallyRandom() {
	int32_t range = upper - lower;
	if (range < 0) {
		range += 12;
	}

	return (range == 11);
}
} // namespace deluge::gui::menu_item
