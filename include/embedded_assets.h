#pragma once
#include <string>

// A file from public/ compiled into the binary by tools/gen_embedded_assets.sh.
struct EmbeddedAsset {
    const unsigned char* data;
    unsigned int size;
    const char* mime;
};

// Looks up an embedded asset by URL path (e.g. "/index.html").
// Returns nullptr if no such asset was embedded.
const EmbeddedAsset* find_embedded_asset(const std::string& path);
