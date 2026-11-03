// tags.json storage: {"tags": [{"id": "...", "command": "..."}, ...]}
#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xerxes {

class TagFileError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class FileNotFound : public TagFileError {
public:
    using TagFileError::TagFileError;
};

// Ordered map of upper-cased tag ID -> shell command (insertion order kept, like a Python dict).
class TagStore {
public:
    using Entry = std::pair<std::string, std::string>;

    // Throws FileNotFound if the file doesn't exist, TagFileError if it can't be parsed.
    // With require_tags_key, a missing "tags" key is an error (xerxes-rfid.py behaviour);
    // otherwise it is treated as an empty list (tag-management.py behaviour).
    static TagStore load(const std::string& path, bool require_tags_key);

    // Writes the file in the same layout as Python's json.dump(data, f, indent=4).
    void save(const std::string& path) const;

    const std::string* find(const std::string& id) const;
    void set(const std::string& id, const std::string& command);
    bool erase(const std::string& id);

    const std::vector<Entry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }
    size_t size() const { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

std::string to_upper_ascii(std::string s);
std::string trim(const std::string& s);

}  // namespace xerxes
