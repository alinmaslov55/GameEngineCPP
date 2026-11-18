#ifdef _MSC_VER
// Third-party stb_vorbis.c TU: disable code-gen warnings (e.g. C4701) that MSVC emits at TU end
#pragma warning(disable: 4100 4127 4244 4245 4267 4389 4456 4457 4459 4701 4702 4703 4706)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#pragma clang diagnostic ignored "-Wpedantic"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include <stb_vorbis.c>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
