"""Test exact production theme request adapter/uploads with a WARP device."""
from pathlib import Path
import runpy
import sys

helpers = runpy.run_path(str(Path(__file__).with_name("extract-d3d12-reset-test.py")))
block = helpers["block"]
output = Path(sys.argv[2])
parts = [block("static bool LoadTexturePixels("),
         block("static bool LoadThemeTextureFromPath(").replace("stbi_load(", "CountedStbiLoad("),
         block("struct ThemeTextureSlot", True), block("static ThemeTextureSlot ThemeTexture("),
         block("template<class SwapChain> static void ProcessThemeImageRequests(").replace("GetTickCount64()", "TestClock()")]
(output / "theme-image-native.inl").write_text("\n".join(parts) + "\n", encoding="utf-8")
print("Extracted production texture uploads, slot mapping and request adapter.")
