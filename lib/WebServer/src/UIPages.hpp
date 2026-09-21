#pragma once
#include "StaticContentResponse.hpp"
#include "UIPagesGz.h"

// Each page is one HTML document, authored as readable spans in the sibling
// *UI.h sources and joined by scripts/build_ui_gz.py into a single deflate
// stream per page. Stored verbatim the four pages cost about 198 KB of the
// 1.5 MB app partition; gzipped they cost about 47 KB, and the browser
// inflates them at no cost to the device. UIPagesGz.h is generated -- run
// the script (PlatformIO does it for every build) before compiling.

template <size_t N>
inline StaticContentResponse* uiPageResponse(const uint8_t (&page)[N]) {
    return new StaticContentResponse("text/html", page, N, "gzip");
}
