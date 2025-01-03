

#include "gui/context_menu/clip_settings/launch_style.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "hid/display/display.h"
#include "model/clip/clip.h"
#include <cstddef>

namespace deluge::gui::context_menu::clip_settings {

constexpr size_t kNumValues = 3;

LaunchStyleMenu launchStyle{};

std::string_view LaunchStyleMenu::getTitle() {
	static std::string_view title = "Clip Mode";
	return title;
}

Sized<std::string_view*> LaunchStyleMenu::getOptions() {
	using enum l10n::String;
	static std::string_view options[] = {
	    l10n::get(STRING_FOR_DEFAULT_LAUNCH),
	    l10n::get(STRING_FOR_FILL_LAUNCH),
	    l10n::get(STRING_FOR_ONCE_LAUNCH),
	};
	return {options, kNumValues};
}

bool LaunchStyleMenu::setupAndCheckAvailability() {
	currentUIMode = UI_MODE_NONE;
	this->currentOption = static_cast<int32_t>(clip->launchStyle);

	if (display->haveOLED()) {
		scrollPos = this->currentOption;
	}

	return true;
}

void LaunchStyleMenu::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
	clip->launchStyle = static_cast<LaunchStyle>(this->currentOption);
}

} // namespace deluge::gui::context_menu::clip_settings
