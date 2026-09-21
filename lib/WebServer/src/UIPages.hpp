#pragma once
#include "StaticContentResponse.hpp"
#include "UIDialog.h"
#include "WebUI.h"
#include "ManualUI.h"
#include "SettingsUI.h"
#include "FileUI.h"

// Each page is one HTML document assembled from flash spans as it is sent.
// The shared dialog CSS and script therefore cost their bytes once, not once
// per page -- worth doing on a nearly full 1.5 MB app partition.
#define UI_SPAN(text) {reinterpret_cast<const uint8_t *>(text), sizeof(text) - 1}

const StaticContentResponse::Segment WEB_UI_PAGE[] = {
    UI_SPAN(WEB_UI_HEAD), UI_SPAN(UI_DIALOG_CSS),
    UI_SPAN(WEB_UI_BODY), UI_SPAN(UI_DIALOG_JS),
    UI_SPAN(WEB_UI_SCRIPT)
};

const StaticContentResponse::Segment MANUAL_UI_PAGE[] = {
    UI_SPAN(MANUAL_UI_HEAD), UI_SPAN(UI_DIALOG_CSS),
    UI_SPAN(MANUAL_UI_BODY), UI_SPAN(UI_DIALOG_JS),
    UI_SPAN(MANUAL_UI_SCRIPT)
};

const StaticContentResponse::Segment SETTINGS_UI_PAGE[] = {
    UI_SPAN(SETTINGS_UI_HEAD), UI_SPAN(UI_DIALOG_CSS),
    UI_SPAN(SETTINGS_UI_BODY), UI_SPAN(UI_DIALOG_JS),
    UI_SPAN(SETTINGS_UI_SCRIPT)
};

const StaticContentResponse::Segment FILE_UI_PAGE[] = {
    UI_SPAN(FILE_UI_HEAD), UI_SPAN(UI_DIALOG_CSS),
    UI_SPAN(FILE_UI_BODY), UI_SPAN(UI_DIALOG_JS),
    UI_SPAN(FILE_UI_SCRIPT)
};

#undef UI_SPAN

template <size_t N>
inline StaticContentResponse* uiPageResponse(const StaticContentResponse::Segment (&page)[N]) {
    return new StaticContentResponse("text/html", page, N);
}
