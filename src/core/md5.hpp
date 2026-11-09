// MD5 (RFC 1321). Used only for Subsonic token authentication (t = md5(password + salt)),
// which is what that protocol defines; never use it for anything security-sensitive of our own.
#pragma once
#include <string>

namespace ml {
std::string md5Hex(const std::string &data);   // lower-case hex digest
}
