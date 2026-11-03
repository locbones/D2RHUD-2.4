"""Compile the production resource types and ResetState verbatim in a WARP test."""
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8-sig")
output = Path(sys.argv[2])
output.mkdir(parents=True, exist_ok=True)

def block(signature: str, semicolon: bool = False) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        char = source[end]
        depth += (char == "{") - (char == "}")
        end += 1
    if semicolon:
        assert source[end] == ";"
        end += 1
    return source[start:end]

constants = []
for name in ("kWindowBgSlots", "kBuffAtlasSlots"):
    match = re.search(r"static constexpr [^\n]*\b" + name + r"\b[^\n]*;", source)
    assert match, name
    constants.append(match.group())
types = [block("struct FrameContext", True), *constants,
         block("struct BuffAtlasTexture", True), block("struct GraphicsResources", True),
         block("static GraphicsResources& Graphics()")]
(output / "d3d12-reset-types.inl").write_text("\n".join(types) + "\n", encoding="utf-8")
(output / "d3d12-reset-body.inl").write_text(block("void ResetState()") + "\n", encoding="utf-8")
print("Extracted exact production graphics types, getter, and ResetState.")
