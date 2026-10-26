#include "../d2rhud/plugin/HUDControlsStore.h"
#include <cassert>
#include <iostream>
#include <iterator>

using namespace HUDControls;
using Json = nlohmann::json;

static std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
static void Write(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
    assert(output.good());
}
static Json Document() {
    return Json{{"version", 1}, {"customRoot", "keep"}, {"profiles", {
        {"RMD-MP|Sorceress", {{"primary", 0x2041}, {"secondary", Unbound}, {"customProfile", 17}}},
        {"other|Hero", {{"primary", 0x1042}, {"secondary", 0x4001}, {"unknown", {1,2,3}}}}
    }}};
}

int wmain(int argc, wchar_t** argv) {
    assert(argc == 2);
    const auto root = std::filesystem::path(argv[1]) / L"HUD controls \u6e2c\u8a66 \u00e9";
    std::filesystem::create_directories(root);
    const auto path = root / L"\u9078\u9805.json";
    std::error_code error;
    std::filesystem::remove(path, error);

    Store state;
    assert(state.Load(path, "RMD-MP|Sorceress"));
    assert(state.saved == Keys{} && state.editing == Keys{} && !state.readOnly);
    assert(!std::filesystem::exists(path));
    state.editing = Keys{0x2041, 0x4002};
    state.Revert();
    assert(state.editing == Keys{});
    state.editing = Keys{0x2041, Unbound};
    state.Apply();
    assert(state.saved == state.editing && !std::filesystem::exists(path));
    assert(state.Save());
    Store reloaded;
    assert(reloaded.Load(path, "RMD-MP|Sorceress"));
    assert((reloaded.saved == Keys{0x2041, Unbound}));
    reloaded.Defaults();
    assert(reloaded.editing == Keys{} && reloaded.saved != Keys{});
    reloaded.Revert();
    assert(reloaded.editing == reloaded.saved);

    const auto document = Document();
    Write(path, document.dump());
    Store profiles;
    assert(profiles.Load(path, "missing|Hero", Keys{0x107b, 0x1043}));
    assert((profiles.saved == Keys{0x107b, 0x1043}));
    assert(Read(path) == document.dump());
    assert(profiles.Load(path, "RMD-MP|Sorceress"));
    profiles.editing = Keys{Unbound, Unbound};
    profiles.Apply();
    assert(profiles.Save());
    const auto changed = Json::parse(Read(path));
    assert(changed["customRoot"] == "keep");
    assert(changed["profiles"]["other|Hero"] == document["profiles"]["other|Hero"]);
    assert(changed["profiles"]["RMD-MP|Sorceress"]["customProfile"] == 17);
    assert(changed["profiles"]["RMD-MP|Sorceress"]["primary"] == Unbound);
    assert(profiles.Load(path, "other|Hero"));
    assert((profiles.saved == Keys{0x1042, 0x4001}));
    assert(profiles.Load(path, "RMD-MP|Sorceress"));
    assert((profiles.saved == Keys{Unbound, Unbound}));

    for (const auto& invalid : {Json(-1), Json(0x8000), Json(0x10000), Json("F12"), Json(123.0), Json(true), Json(nullptr)}) {
        auto broken = document;
        broken["profiles"]["other|Hero"]["primary"] = invalid;
        const auto contents = broken.dump();
        Write(path, contents);
        Store rejected;
        assert(!rejected.Load(path, "RMD-MP|Sorceress"));
        assert(rejected.readOnly && rejected.saved == Keys{});
        rejected.editing = Keys{0x41, 0x42};
        rejected.Apply();
        assert(!rejected.Save() && Read(path) == contents);
    }
    for (const auto& broken : {std::string("{"), std::string("{}"), std::string("[]"),
        std::string("{\"version\":2,\"profiles\":{}}"), document.dump() + "trailing"}) {
        Write(path, broken);
        Store rejected;
        assert(!rejected.Load(path, "RMD-MP|Sorceress"));
        assert(rejected.readOnly && !rejected.Save() && Read(path) == broken);
    }
    auto incomplete = document;
    incomplete["profiles"]["other|Hero"].erase("secondary");
    Write(path, incomplete.dump());
    Store missingField;
    assert(!missingField.Load(path, "RMD-MP|Sorceress") && !missingField.Save());

    // A failed replacement keeps the existing target and allows a later retry.
    Write(path, document.dump());
    Store retry;
    assert(retry.Load(path, "RMD-MP|Sorceress"));
    retry.editing = Keys{0x7041, Unbound};
    retry.Apply();
    const auto locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    assert(!retry.Save() && Read(path) == document.dump());
    assert(retry.Load(path, "other|Hero"));
    assert((retry.saved == Keys{0x1042, 0x4001}));
    assert(Read(path) == document.dump());
    CloseHandle(locked);
    assert(retry.Save());
    Store afterRetry;
    assert(afterRetry.Load(path, "RMD-MP|Sorceress"));
    assert((afterRetry.saved == Keys{0x7041, Unbound}));
    const auto directoryTarget = root / L"directory-target";
    std::filesystem::create_directories(directoryTarget);
    Store failed;
    assert(!failed.Load(directoryTarget, "scope") && failed.readOnly && !failed.Save());

    std::filesystem::remove(path, error);
    std::cout << "HUD Controls persistence, apply/revert, Unicode paths, profiles and corrupt-file preservation passed.\n";
    return 0;
}
