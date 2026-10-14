#pragma once

#include <string>
#include <utility>
#include <vector>

namespace RTE {

	/// A parsed JSON value, enough to read web API responses: null, booleans, numbers, strings, arrays and objects.
	/// Missing keys and out of range indices give a null value instead of failing, so nested lookups can be chained without checking each step.
	class JsonValue {

	public:
		/// The kinds of JSON values.
		enum class Type {
			Null,
			Bool,
			Number,
			String,
			Array,
			Object
		};

#pragma region Creation
		/// Constructor method used to instantiate a null JsonValue.
		JsonValue() = default;

		/// Parses JSON text.
		/// @param text The JSON text to parse.
		/// @param error Set to a description of what went wrong if the text isn't valid JSON. Left untouched otherwise.
		/// @return The parsed value, or a null value if the text isn't valid JSON.
		static JsonValue Parse(const std::string& text, std::string* error = nullptr);
#pragma endregion

#pragma region Getters
		/// Gets what kind of value this is.
		/// @return The type of this value.
		Type GetType() const { return m_Type; }

		/// Gets whether this value is null, which is also what missing keys and out of range indices give.
		/// @return Whether this value is null.
		bool IsNull() const { return m_Type == Type::Null; }

		/// Gets this value as a boolean.
		/// @param defaultValue What to return if this isn't a boolean.
		/// @return The boolean value, or the default.
		bool AsBool(bool defaultValue = false) const { return m_Type == Type::Bool ? m_Bool : defaultValue; }

		/// Gets this value as a number.
		/// @param defaultValue What to return if this isn't a number.
		/// @return The number, or the default.
		double AsNumber(double defaultValue = 0) const { return m_Type == Type::Number ? m_Number : defaultValue; }

		/// Gets this value as a whole number.
		/// @param defaultValue What to return if this isn't a number.
		/// @return The number, truncated, or the default.
		long long AsInteger(long long defaultValue = 0) const { return m_Type == Type::Number ? static_cast<long long>(m_Number) : defaultValue; }

		/// Gets this value as a string.
		/// @param defaultValue What to return if this isn't a string.
		/// @return The string, or the default.
		std::string AsString(const std::string& defaultValue = "") const { return m_Type == Type::String ? m_String : defaultValue; }

		/// Gets how many elements this array, or members this object, has.
		/// @return The number of elements or members, or 0 for any other kind of value.
		size_t Size() const { return m_Type == Type::Array ? m_Array.size() : (m_Type == Type::Object ? m_Object.size() : 0); }

		/// Gets an element of this array.
		/// @param index The index of the element.
		/// @return The element, or a null value if this isn't an array or the index is out of range.
		const JsonValue& operator[](size_t index) const;

		/// Gets a member of this object.
		/// @param key The member's key.
		/// @return The member's value, or a null value if this isn't an object or has no such member.
		const JsonValue& operator[](const std::string& key) const;

		/// Gets a member of this object.
		/// @param key The member's key.
		/// @return The member's value, or a null value if this isn't an object or has no such member.
		const JsonValue& operator[](const char* key) const { return (*this)[std::string(key)]; }
#pragma endregion

	private:
		Type m_Type = Type::Null; //!< What kind of value this is.
		bool m_Bool = false; //!< The value, if this is a boolean.
		double m_Number = 0; //!< The value, if this is a number.
		std::string m_String; //!< The value, if this is a string.
		std::vector<JsonValue> m_Array; //!< The elements, if this is an array.
		std::vector<std::pair<std::string, JsonValue>> m_Object; //!< The members, in the order they appear, if this is an object.

		friend class JsonParser;
	};
} // namespace RTE
