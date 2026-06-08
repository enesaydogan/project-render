#pragma once

// Shared MIME type for dragging assets out of the Asset Manager panel into the
// viewport / other panels. The payload text is "<type>:<assetIdHex>", e.g.
// "model:0123abcd..." or "material:...". Kept tiny and header-only so both the
// source panel and the drop targets agree on the format.
inline constexpr const char *kAssetMimeType = "application/x-prender-asset";
