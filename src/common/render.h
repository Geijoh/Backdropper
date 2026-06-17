#pragma once

#include "settings.h"

#include <thumbcache.h>

HRESULT RenderBackdropperThumbnail(IStream* stream, UINT maxSize, const BackdropperSettings& settings,
    HBITMAP* bitmap, WTS_ALPHATYPE* alphaType);
