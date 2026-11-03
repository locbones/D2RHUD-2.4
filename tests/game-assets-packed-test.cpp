#define NOMINMAX
#include "../d2rhud/plugin/GameAssets.h"
#include "../d2rhud/plugin/StackSplitSkin.h"
#include <cassert>
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    assert(argc == 2);
    GameAssets source(argv[1], {});
    for (const auto* path : {"hd/global/ui/items/weapon/sword/crystal_sword.sprite",
                             "hd/global/ui/items/misc/ring/ring1.sprite"}) {
        auto bytes = source.Read(path);
        LegacyTransmog::Skin::Sprite sprite;
        if (bytes.empty())
            std::cerr << source.ReadError() << '\n';
        assert(LegacyTransmog::Skin::Decode(bytes, sprite) && source.ReadError().empty());
    }
    assert(source.Read("hd/global/ui/items/does-not-exist.sprite").empty());
    assert(!source.ReadError().empty());
    std::cout
        << "PASS: stock item art read/decoded from packed storage without loose images; failures diagnosed\n";
}
