/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/context_menu.h"

class SaveUI;

namespace deluge::gui::context_menu {
class OverwriteFile final : public ContextMenuForSaving {
public:
	OverwriteFile() = default;

	std::string_view getTitle() override;
	std::span<std::string_view> getOptions() override;
	bool acceptCurrentOption() override;
	ActionResult padAction(int32_t x, int32_t y, int32_t on) override;

	SaveUI* currentSaveUI = nullptr;
};

extern OverwriteFile overwriteFile;
} // namespace deluge::gui::context_menu
