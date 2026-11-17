#include "storage/store.hpp"

namespace ml {

Json JsonStore::load(const std::string &name, std::string *warning)
{
    std::string path = dir_ + "/" + name, text;
    if (!fs_.exists(path) || !fs_.readFile(path, text, 4 * 1024 * 1024)) return Json::object();
    JsonError err;
    Json j = parseJson(text, &err);
    if (!err.ok() || !j.isObject()) {
        fs_.writeFile(path + ".corrupt", text);
        if (warning) *warning = name + " corrompido (" + (err.ok() ? "nao e objeto" : err.message) + "); recomecando vazio";
        return Json::object();
    }
    return j;
}

bool JsonStore::save(const std::string &name, const Json &value)
{
    fs_.makeDirs(dir_);
    return fs_.writeFile(dir_ + "/" + name, value.dump());
}

}  // namespace ml
