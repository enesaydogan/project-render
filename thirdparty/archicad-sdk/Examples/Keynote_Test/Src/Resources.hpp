#ifndef RESOURCES_HPP
#define RESOURCES_HPP

#define	KEYNOTE_TEST_ADDON_NAME				32000	//'STR#' 32000
#define	KEYNOTE_TEST_MENU_STRINGS			32500	//'STR#' 32500
#define	KEYNOTE_TEST_MENU_PROMPT_STRINGS	32600	//'STR#' 32600

enum class MenuItem {
	DumpKeynoteTree = 1,
	ModifyFolder = 2,
	ModifyItem = 3,
	AddFolder = 4,
	AddItem = 5,
	RemoveFolder = 6,
	RemoveItem = 7,
	PlaceKeynoteLabel = 8,
};

#endif