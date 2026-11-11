// SPDX-License-Identifier: GPL-3.0-or-later
// Safe access to TDLib objects. TDLib leaves out fields whose value is null
// (a chat without a last message has no "last_message"), and nlohmann's
// const operator[] asserts on a missing key while value() throws on a
// non-object. Every read of TDLib data goes through these instead.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

#include "../../third_party/nlohmann/json.hpp"

namespace pp {

using json = nlohmann::json;

// The field, or a null when `object` isn't an object or lacks it.
inline const json&
Field(const json& object, const char* key)
{
	static const json kNull;
	if (!object.is_object())
		return kNull;
	auto it = object.find(key);
	return it == object.end() ? kNull : *it;
}

// TDLib sends 64-bit numbers as strings ("order") and 53-bit ones as numbers.
inline int64_t
ToInt64(const json& value)
{
	if (value.is_number_integer())
		return value.get<int64_t>();
	if (value.is_number_unsigned())
		return static_cast<int64_t>(value.get<uint64_t>());
	if (value.is_string())
		return std::strtoll(value.get_ref<const std::string&>().c_str(), nullptr, 10);
	return 0;
}

inline std::string
Str(const json& object, const char* key, const char* fallback = "")
{
	const json& v = Field(object, key);
	return v.is_string() ? v.get<std::string>() : std::string(fallback);
}

inline int64_t
Int(const json& object, const char* key, int64_t fallback = 0)
{
	const json& v = Field(object, key);
	return v.is_number() || v.is_string() ? ToInt64(v) : fallback;
}

inline bool
Bool(const json& object, const char* key, bool fallback = false)
{
	const json& v = Field(object, key);
	return v.is_boolean() ? v.get<bool>() : fallback;
}

inline std::string
Type(const json& object)
{
	return Str(object, "@type");
}

} // namespace pp
