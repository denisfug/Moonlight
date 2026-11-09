// JSON persistence on top of the platform FileSystem.
#pragma once
#include <string>

#include "core/json.hpp"
#include "platform/platform.hpp"

namespace ml {

class JsonStore {
public:
    JsonStore(FileSystem &fs, std::string dir) : fs_(fs), dir_(std::move(dir)) {}
    // Missing file -> object {}. Corrupt file -> object {} and the bad file is kept as name.corrupt.
    Json load(const std::string &name, std::string *warning = nullptr);
    bool save(const std::string &name, const Json &value);
    const std::string &dir() const { return dir_; }

private:
    FileSystem &fs_;
    std::string dir_;
};

}  // namespace ml
