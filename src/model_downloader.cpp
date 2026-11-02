#include "model_downloader.h"
#include <curl/curl.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

size_t write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::ofstream*>(userdata);
    out->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

int progress_trampoline(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                         curl_off_t, curl_off_t) {
    auto* cb = static_cast<std::function<void(size_t, size_t)>*>(clientp);
    if (*cb) (*cb)(static_cast<size_t>(dlnow), static_cast<size_t>(dltotal));
    return 0;
}

std::string filename_from_url(const std::string& url) {
    auto pos = url.find_last_of('/');
    std::string name = (pos == std::string::npos) ? url : url.substr(pos + 1);
    if (name.find(".gguf") == std::string::npos) name += ".gguf";
    return name;
}

} // namespace

DownloadResult download_model(
    const std::string& source_url,
    const std::string& models_dir,
    std::function<void(size_t, size_t)> progress_cb
) {
    if (source_url.rfind("https://huggingface.co/", 0) != 0) {
        return {false, "", "Only https://huggingface.co URLs are supported"};
    }

    fs::create_directories(models_dir);
    std::string local_path = models_dir + "/" + filename_from_url(source_url);

    CURL* curl = curl_easy_init();
    if (!curl) return {false, "", "curl_easy_init failed"};

    std::ofstream out(local_path, std::ios::binary);
    if (!out) {
        curl_easy_cleanup(curl);
        return {false, "", "Could not open " + local_path + " for writing"};
    }

    curl_easy_setopt(curl, CURLOPT_URL, source_url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_trampoline);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_cb);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    out.close();

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        fs::remove(local_path);
        return {false, "", curl_easy_strerror(res)};
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        fs::remove(local_path);
        return {false, "", "HTTP " + std::to_string(http_code)};
    }

    return {true, local_path, ""};
}
