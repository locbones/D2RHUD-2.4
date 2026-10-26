// Read-only RMD-MP runtime capture, 2026-10-24. Full compiled ItemStatCost rows.
// Elemental resistance has an additive per-level op even with no bonus equipped.
struct CapturedResistanceCost { uint32_t stat, immunity; std::string_view bytes; };
constexpr std::array<CapturedResistanceCost,4> capturedResistanceCosts{{
    {39,189,
        "270000000200000009000000070000001a00000000090900c8000000c80000000000000000000000000000000000ffff240013009a0d"
        "9a0d061502001300282728270615ffffffff00000000000001000000ffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffff0c00e700020300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},
    {41,190,
        "290000000200000009000000070000001a00000000090900c8000000c80000000000000000000000000000000000ffff260013009c0d"
        "9c0d061502001300282728270615ffffffff00000000000001000000ffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffff0c00e800020300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},
    {43,187,
        "2b0000000200000009000000070000001a00000000090900c8000000c80000000000000000000000000000000000ffff280013009b0d"
        "9b0d061502001300282728270615ffffffff00000000000001000000ffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffff0c00e600020300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},
    {45,191,
        "2d0000000200000009000000070000001a00000000090900c8000000c80000000000000000000000000000000000ffff220013009e0d"
        "9e0d061502001300282728270615ffffffff00000000000001000000ffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffff0c00e900020300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},
}};
