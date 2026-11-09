// Demonstration catalogue used until real sources are configured / reachable.
// All titles, people and channels are invented for Moonlight HB; ids start with "demo:".
#pragma once
#include <vector>

#include "addons/protocol.hpp"
#include "live/m3u.hpp"

namespace ml::demo {

std::vector<addon::MetaPreview> movies();
std::vector<addon::MetaPreview> series();
bool meta(const std::string &id, addon::MetaDetail &out);   // details for a demo id
std::vector<Channel> channels();
std::vector<std::string> genres(const std::string &type);
inline bool isDemo(const std::string &id) { return id.compare(0, 5, "demo:") == 0; }

}  // namespace ml::demo
