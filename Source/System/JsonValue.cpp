#include "JsonValue.h"

#include <cstdlib>
#include <stdexcept>

namespace RTE {

	/// A recursive descent JSON parser. Throws std::runtime_error with a description of the problem on invalid input.
	class JsonParser {

	public:
		explicit JsonParser(const std::string& text) :
		    m_Text(text) {}

		JsonValue ParseDocument() {
			JsonValue value = ParseValue(0);
			SkipWhitespace();
			if (m_Position != m_Text.size()) {
				Fail("Unexpected text after the end of the document");
			}
			return value;
		}

	private:
		static constexpr int c_MaxDepth = 256; //!< Deeper nesting than this is refused, so malicious input can't overflow the stack.

		const std::string& m_Text;
		size_t m_Position = 0;

		[[noreturn]] void Fail(const std::string& reason) const {
			throw std::runtime_error(reason + " at character " + std::to_string(m_Position));
		}

		void SkipWhitespace() {
			while (m_Position < m_Text.size() && (m_Text[m_Position] == ' ' || m_Text[m_Position] == '\t' || m_Text[m_Position] == '\n' || m_Text[m_Position] == '\r')) {
				++m_Position;
			}
		}

		char Peek() const {
			return m_Position < m_Text.size() ? m_Text[m_Position] : '\0';
		}

		void Expect(char expected) {
			if (Peek() != expected) {
				Fail(std::string("Expected '") + expected + "'");
			}
			++m_Position;
		}

		void ExpectWord(const char* word) {
			for (const char* letter = word; *letter; ++letter) {
				Expect(*letter);
			}
		}

		JsonValue ParseValue(int depth) {
			if (depth > c_MaxDepth) {
				Fail("Nested too deeply");
			}
			SkipWhitespace();
			JsonValue value;
			switch (Peek()) {
				case '{':
					value.m_Type = JsonValue::Type::Object;
					ParseObject(value, depth);
					break;
				case '[':
					value.m_Type = JsonValue::Type::Array;
					ParseArray(value, depth);
					break;
				case '"':
					value.m_Type = JsonValue::Type::String;
					value.m_String = ParseString();
					break;
				case 't':
					ExpectWord("true");
					value.m_Type = JsonValue::Type::Bool;
					value.m_Bool = true;
					break;
				case 'f':
					ExpectWord("false");
					value.m_Type = JsonValue::Type::Bool;
					value.m_Bool = false;
					break;
				case 'n':
					ExpectWord("null");
					break;
				default:
					value.m_Type = JsonValue::Type::Number;
					value.m_Number = ParseNumber();
					break;
			}
			return value;
		}

		void ParseObject(JsonValue& object, int depth) {
			Expect('{');
			SkipWhitespace();
			if (Peek() == '}') {
				++m_Position;
				return;
			}
			while (true) {
				SkipWhitespace();
				std::string key = ParseString();
				SkipWhitespace();
				Expect(':');
				object.m_Object.emplace_back(std::move(key), ParseValue(depth + 1));
				SkipWhitespace();
				if (Peek() == ',') {
					++m_Position;
				} else {
					Expect('}');
					return;
				}
			}
		}

		void ParseArray(JsonValue& array, int depth) {
			Expect('[');
			SkipWhitespace();
			if (Peek() == ']') {
				++m_Position;
				return;
			}
			while (true) {
				array.m_Array.emplace_back(ParseValue(depth + 1));
				SkipWhitespace();
				if (Peek() == ',') {
					++m_Position;
				} else {
					Expect(']');
					return;
				}
			}
		}

		unsigned int ParseHexDigits() {
			if (m_Position + 4 > m_Text.size()) {
				Fail("Truncated unicode escape");
			}
			unsigned int codePoint = 0;
			for (int digit = 0; digit < 4; ++digit) {
				char character = m_Text[m_Position++];
				codePoint <<= 4;
				if (character >= '0' && character <= '9') {
					codePoint |= character - '0';
				} else if (character >= 'a' && character <= 'f') {
					codePoint |= character - 'a' + 10;
				} else if (character >= 'A' && character <= 'F') {
					codePoint |= character - 'A' + 10;
				} else {
					Fail("Invalid unicode escape");
				}
			}
			return codePoint;
		}

		static void AppendUTF8(std::string& text, unsigned int codePoint) {
			if (codePoint < 0x80) {
				text += static_cast<char>(codePoint);
			} else if (codePoint < 0x800) {
				text += static_cast<char>(0xC0 | (codePoint >> 6));
				text += static_cast<char>(0x80 | (codePoint & 0x3F));
			} else if (codePoint < 0x10000) {
				text += static_cast<char>(0xE0 | (codePoint >> 12));
				text += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
				text += static_cast<char>(0x80 | (codePoint & 0x3F));
			} else {
				text += static_cast<char>(0xF0 | (codePoint >> 18));
				text += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
				text += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
				text += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
		}

		std::string ParseString() {
			Expect('"');
			std::string result;
			while (true) {
				if (m_Position >= m_Text.size()) {
					Fail("Unterminated string");
				}
				char character = m_Text[m_Position++];
				if (character == '"') {
					return result;
				}
				if (character != '\\') {
					result += character;
					continue;
				}
				if (m_Position >= m_Text.size()) {
					Fail("Unterminated escape");
				}
				char escape = m_Text[m_Position++];
				switch (escape) {
					case '"': result += '"'; break;
					case '\\': result += '\\'; break;
					case '/': result += '/'; break;
					case 'b': result += '\b'; break;
					case 'f': result += '\f'; break;
					case 'n': result += '\n'; break;
					case 'r': result += '\r'; break;
					case 't': result += '\t'; break;
					case 'u': {
						unsigned int codePoint = ParseHexDigits();
						// Characters outside the basic multilingual plane come as a surrogate pair.
						if (codePoint >= 0xD800 && codePoint <= 0xDBFF && m_Position + 6 <= m_Text.size() && m_Text[m_Position] == '\\' && m_Text[m_Position + 1] == 'u') {
							m_Position += 2;
							unsigned int lowSurrogate = ParseHexDigits();
							codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (lowSurrogate - 0xDC00);
						}
						AppendUTF8(result, codePoint);
						break;
					}
					default:
						Fail("Invalid escape");
				}
			}
		}

		double ParseNumber() {
			const char* start = m_Text.c_str() + m_Position;
			char* end = nullptr;
			double number = std::strtod(start, &end);
			if (end == start) {
				Fail("Unexpected character");
			}
			m_Position += end - start;
			return number;
		}
	};

	JsonValue JsonValue::Parse(const std::string& text, std::string* error) {
		try {
			return JsonParser(text).ParseDocument();
		} catch (const std::runtime_error& exception) {
			if (error) {
				*error = exception.what();
			}
			return JsonValue();
		}
	}

	const JsonValue& JsonValue::operator[](size_t index) const {
		static const JsonValue nullValue;
		return (m_Type == Type::Array && index < m_Array.size()) ? m_Array[index] : nullValue;
	}

	const JsonValue& JsonValue::operator[](const std::string& key) const {
		static const JsonValue nullValue;
		if (m_Type == Type::Object) {
			for (const auto& [memberKey, memberValue]: m_Object) {
				if (memberKey == key) {
					return memberValue;
				}
			}
		}
		return nullValue;
	}
} // namespace RTE
