#pragma once

// One size unit for the whole UI. Every hand-written pixel size (a button's
// width, a field's width, a gap, a window's first size) goes through Em, so it
// scales with the font the way the rest of ImGui's layout already does. Sizes
// were tuned against a 14 px font, where Em(px) == px exactly.
float Em(float px);
