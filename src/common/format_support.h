#pragma once

#include <string>

std::wstring BackdropperFormatLabel(const wchar_t* extension);
bool BackdropperHasBuiltInRenderer(const wchar_t* extension);
bool BackdropperWicSupportsExtension(const wchar_t* extension);
std::wstring FindGhostscriptExecutable();
bool BackdropperHasGhostscript();
bool CanRegisterBackdropperFormat(const wchar_t* extension);
