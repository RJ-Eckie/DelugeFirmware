from dmui.dsl import MultiContextMenu

file_selector = MultiContextMenu(
    "FileSelector",
    "deluge::gui::menu_item::fileSelectorMenu",
    ["{name}"],
    name="STRING_FOR_FILE_BROWSER",
)
