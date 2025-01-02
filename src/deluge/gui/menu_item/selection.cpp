#include "selection.h"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"

namespace deluge::gui::menu_item {
void Selection::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	if (display->have7SEG()) {
		const auto options = this->getOptions();
		display->setScrollingText(options[this->getValue()].data());
	}
}

void Selection::drawPixelsForOled() {
	int32_t current = getValue();
	// Earliest item that might be on the screen.
	int32_t before = std::max<int32_t>(0, current - kOLEDMenuNumOptionsVisible);
	int32_t nBefore = current - before;
	// First item offscreen.
	int32_t after = std::min<int32_t>(current + kOLEDMenuNumOptionsVisible, size());
	int32_t nAfter = after - current;

	// Ideally we'd have the selected item in the middle (rounding down for even cases)
	// ...but sometimes that's not going to happen.
	size_t pos = (kOLEDMenuNumOptionsVisible - 1) / 2;
	size_t tail = kOLEDMenuNumOptionsVisible - pos;
	if (nBefore < pos) {
		pos = nBefore;
	}
	else if (nAfter < tail) {
		tail = nAfter;
		pos = std::min<int32_t>(kOLEDMenuNumOptionsVisible - tail, nBefore);
	}

	MenuItem::drawItemsForOled(std::span{getOptions().data(), getOptions().size()}, pos, current - pos);
}

// renders check box on OLED and dot on 7seg
void Selection::displayToggleValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawName();
	}
}

// handles rendering of the community features menu items that are identified as toggles
void Selection::renderSubmenuItemTypeForOled(int32_t yPixel) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t startX = getSubmenuItemTypeRenderIconStart();

	if (isToggle()) {
		if (getToggleValue()) {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::checkedBoxIcon, startX, yPixel,
			                           kSubmenuIconSpacingX);
		}
		else {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::uncheckedBoxIcon, startX, yPixel,
			                           kSubmenuIconSpacingX);
		}
	}
	else {
		image.drawGraphicMultiLine(deluge::hid::display::OLED::submenuArrowIcon, startX, yPixel, kSubmenuIconSpacingX);
	}
}

void Selection::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	renderColumnLabel(startX, width, startY);

	// Render current value

	std::string_view opt = getShortOptions()[this->getValue()];
	// Grab 6-char prefix with spaces removed.
	DEF_STACK_STRING_BUF(shortOpt, 6);
	for (uint8_t p = 0; p < opt.size() && shortOpt.size() < shortOpt.capacity(); p++) {
		if (opt[p] != ' ') {
			shortOpt.append(opt[p]);
		}
	}
	int32_t pxLen;
	// Trim characters from the end until it fits.
	while ((pxLen = image.getStringWidthInPixels(shortOpt.c_str(), kTextSizeYUpdated)) >= width) {
		shortOpt.data()[shortOpt.size() - 1] = 0;
	}
	// Padding to center the string. If we can't center exactly, 1px right is better than 1px left.
	int32_t pad = (width + 1 - pxLen) / 2;
	image.drawString(shortOpt.c_str(), startX + pad, startY + kTextSpacingY + 2, kTextSpacingX, kTextSpacingY, 0,
	                 startX + width - kTextSpacingX);
}
} // namespace deluge::gui::menu_item
