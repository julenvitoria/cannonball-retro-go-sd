// Game & Watch port: the small part of libstdc++ the engine needs, without libstdc++ itself.
//
// The engine only uses std::string/std::vector in a few cold places (music titles, config).
// libstdc++ normally provides std::string's out-of-line members from its library binary;
// linking that would drag in exceptions, locales and iostreams. Instead std::string is
// instantiated here, and the "throw" helpers the headers call on misuse stop with a message
// (exceptions are disabled, so they could not be caught anyway).
#include <string>
#include <stdio.h>
#include <stdlib.h>

template class std::__cxx11::basic_string<char>;

[[noreturn]] static void cxx_fatal(const char* what, const char* msg)
{
    printf("Cannonball: C++ runtime error: %s: %s\n", what, msg ? msg : "");
    abort();
}

namespace std {
void __throw_length_error(const char* m)       { cxx_fatal("length_error", m); }
void __throw_logic_error(const char* m)        { cxx_fatal("logic_error", m); }
void __throw_out_of_range(const char* m)       { cxx_fatal("out_of_range", m); }
void __throw_out_of_range_fmt(const char* m, ...) { cxx_fatal("out_of_range", m); }
void __throw_invalid_argument(const char* m)   { cxx_fatal("invalid_argument", m); }
void __throw_bad_alloc()                        { cxx_fatal("bad_alloc", 0); }
void __throw_bad_array_new_length()             { cxx_fatal("bad_array_new_length", 0); }
}

// OAttractAI seeds rand() from time(); the firmware's rand() is already seeded, and has no
// srand on the ABI.
extern "C" void srand(unsigned) {}

// Force feedback (rumble) is a PC controller feature; the G&W has no motor.
namespace forcefeedback {
bool init(int, int, int) { return false; }
void close() {}
int  set(int, int) { return 0; }
bool is_supported() { return false; }
}
