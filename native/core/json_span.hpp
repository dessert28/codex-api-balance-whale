#pragma once

#include <cstddef>
#include <string>

namespace whale {

// Locates the raw span of a JSON array or object stored under `key`, so a writer
// that does not understand that key can copy it back verbatim instead of wiping
// it. `open`/`close` are the delimiters: '[' / ']' for `quotes`, '{' / '}' for
// `taskEnd`.
bool FindJsonValueSpan(const std::string& text, const char* key, char open, char close,
                       std::size_t& begin, std::size_t& end);

// Replaces the value stored under `key`, or appends `"key":value` before the
// closing brace when the key is not there yet. The delimiters are taken from the
// first character of `value`.
std::string SetJsonValue(const std::string& text, const char* key, const std::string& value);

} // namespace whale
