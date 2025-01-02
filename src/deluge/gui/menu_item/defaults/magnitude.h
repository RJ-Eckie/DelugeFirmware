/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/enumeration.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/sync.h"
#include "storage/flash_storage.h"
#include "util/string.h"

namespace deluge::gui::menu_item::defaults {
class Magnitude final : public Enumeration {
public:
	using Enumeration::Enumeration;
	void readCurrentValue() override { this->setValue(FlashStorage::defaultMagnitude); }
	void writeCurrentValue() override {
		int32_t oldMagnitude = FlashStorage::defaultMagnitude;
		FlashStorage::defaultMagnitude = this->getValue();
		// Adjust swing interval to keep the same musical meaning if possible: magnitude affects its interpretation.
		int32_t delta = FlashStorage::defaultMagnitude - oldMagnitude;
		int32_t oldSwing = FlashStorage::defaultSwingInterval;
		FlashStorage::defaultSwingInterval = clampSwingIntervalSyncLevel(FlashStorage::defaultSwingInterval - delta);
	}
	void drawPixelsForOled() override {
		hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;

		canvas.drawStringCentred(string::fromInt(96 << this->getValue()), 18 + OLED_MAIN_TOPMOST_PIXEL, 18, 20);
	}

	void drawValue() override { display->setTextAsNumber(96 << this->getValue()); }
	size_t size() override { return 7; }
};
} // namespace deluge::gui::menu_item::defaults
