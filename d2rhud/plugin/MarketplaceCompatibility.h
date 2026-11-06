#pragma once

// Stable marketplace interface, read from the PE export without loading code.
// Change these versions only when the corresponding interface becomes
// incompatible. Unrelated HUD/gameplay changes never change this declaration.
extern "C" __declspec(dllexport) const char D2RHUD_MarketplaceProtocol[] =
    R"({"schema":1,"game_mode":"2.4","live_custody":2,"features":{"open_offers":1,"torch_class":1,"listing_comments":1,"gift_resources":1}})";
