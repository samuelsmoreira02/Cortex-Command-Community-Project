#include "GUIUtil.h"

#include <SDL3/SDL_clipboard.h>

using namespace RTE;

char* GUIUtil::TrimString(char* String) {
	char* ptr = String;
	// Find the first non-space character
	while (*ptr) {
		if (*ptr != ' ') {
			break;
		}
		ptr++;
	}
	// Add a null terminator after the last character
	for (int i = strlen(ptr) - 1; i >= 0; i--) {
		if (ptr[i] != ' ') {
			ptr[i + 1] = '\0';
			break;
		}
	}
	return ptr;
}

bool GUIUtil::GetClipboardText(std::string* text) {
	if (SDL_HasClipboardText()) {
		// SDL3 hands over ownership of the returned string, so it must be freed after copying.
		char* clipboardText = SDL_GetClipboardText();
		*text = clipboardText;
		SDL_free(clipboardText);
		return true;
	}
	return false;
}

bool GUIUtil::SetClipboardText(const std::string& text) {
	return SDL_SetClipboardText(text.c_str());
}
