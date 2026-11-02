#pragma once
#include <string>
#include <functional>

struct DownloadResult {
    bool ok;
    std::string local_path;
    std::string error;
};

DownloadResult download_model(
    const std::string& source_url,
    const std::string& models_dir,
    std::function<void(size_t, size_t)> progress_cb = nullptr
);
