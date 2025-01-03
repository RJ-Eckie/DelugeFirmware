/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#pragma once

#include "gui/l10n/l10n.h"
#include "gui/menu_item/selection.h"

namespace deluge::gui::menu_item::mpe {

class DirectionSelector final : public Selection {
public:
	using Selection::Selection;
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	deluge::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		return {
		    l10n::get(STRING_FOR_IN),
		    l10n::get(STRING_FOR_OUT),
		};
	}
	void readCurrentValue() override { this->setValue(whichDirection); }
	void writeCurrentValue() override { whichDirection = this->getValue(); }
	MenuItem* selectButtonPress() override;
	uint8_t whichDirection;
	[[nodiscard]] std::string_view getTitle() const override {
		return whichDirection ? l10n::get(l10n::String::STRING_FOR_MPE_OUTPUT)
		                      : l10n::get(l10n::String::STRING_FOR_MPE_INPUT);
	}
};

extern DirectionSelector directionSelectorMenu;

} // namespace deluge::gui::menu_item::mpe
