// libc++ 18 built for the PS4 (toolchain/build-libcxx.sh, with localization/stdio off) leaves
// __libcpp_verbose_abort for the program to provide: it is the handler libc++ calls on a failed
// hardening check or where it would otherwise throw (__throw_bad_function_call, __throw_length_error,
// __throw_bad_array_new_length ...). The app is built with -fno-exceptions, so there is nothing to
// unwind to; abort cleanly. No orbis headers, no static constructors.
#include <cstdlib>

namespace std {
inline namespace __1 {
[[noreturn]] void __libcpp_verbose_abort(const char *, ...) { ::abort(); }
}  // namespace __1
}  // namespace std
