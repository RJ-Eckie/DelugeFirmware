/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "definitions_cxx.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ranges>
#include <string_view>

extern "C" {
#include "util/cfunctions.h"
}

[[gnu::always_inline]] static inline void intToString(int32_t number, char* buffer) {
	intToString(number, buffer, 1);
}

bool memIsNumericChars(char const* mem, int32_t size);
bool stringIsNumericChars(char const* str);

char halfByteToHexChar(uint8_t thisHalfByte);
void intToHex(uint32_t number, char* output, int32_t numChars = 8);
uint32_t hexToInt(char const* string);
uint32_t hexToIntFixedLength(char const* __restrict__ hexChars, int32_t length);

void byteToHex(uint8_t number, char* buffer);
uint8_t hexToByte(char const* firstChar);

extern const char nothing;

class String {
public:
	String() : data_(std::make_shared<std::string>()){};

	void clear() { unique().clear(); }

	Error set(std::string_view otherString);
	void set(String const* otherString);
	size_t getLength() const;
	Error shorten(int32_t newLength);
	Error concatenateAtPos(char const* newChars, int32_t pos, int32_t newCharsLength = -1);
	Error concatenateInt(int32_t number, int32_t minNumDigits = 1);
	Error setInt(int32_t number, int32_t minNumDigits = 1);
	Error setChar(char newChar, int32_t pos);
	Error concatenate(String* otherString);
	Error concatenate(char const* newChars);
	bool equals(char const* otherChars) const;
	bool equalsCaseIrrespective(char const* otherChars) const;

	bool equals(String* otherString) const {
		if (data_ == otherString->data_) {
			return true; // Works if both lengths are 0, too
		}
		if (!data_ || !otherString->data_) {
			return false; // If just one is empty, then not equal
		}
		return equals(otherString->get());
	}

	bool equalsCaseIrrespective(String* otherString) const {
		if (data_ == otherString->data_) {
			return true; // Works if both lengths are 0, too
		}
		if (!data_ || !otherString->data_) {
			return false; // If just one is empty, then not equal
		}
		return equalsCaseIrrespective(otherString->get());
	}

	char const* get() const {
		if (!data_) {
			return &nothing;
		}
		return data_->c_str();
	}

	bool isEmpty() const { return data_->empty(); }

	///// NEW STUFF

	constexpr String(const char* cstr) { assign(cstr); }
	constexpr String(std::string_view str) { assign(str); }

	constexpr String& assign(std::string_view str) {
		if (!data_ || data_.use_count() > 1) {
			data_ = std::make_shared<std::string>(str);
		}
		else {
			*data_ = str;
		}
		return *this;
	}

	constexpr size_t length() { return data_->length(); }

	// void shorten(size_t length);

	void resize(size_t length) {
		if (length < data_->length()) {
			shorten(length);
		}
		else {
			unique().resize(length);
		}
	}

	constexpr void insert(size_t pos, const String& str) { unique().insert(pos, str); }
	constexpr void insert(size_t pos, const std::string& str) { unique().insert(pos, str); }
	constexpr void insert(size_t pos, std::string_view str) { unique().insert(pos, str); }

	constexpr void append(const String& str) { unique() += *str.data_; }
	constexpr void append(const std::string& str) { unique() += str; }
	constexpr void append(std::string_view str) { unique() += str; }
	constexpr void append(const char* cstr) { unique() += cstr; }
	constexpr void append(char c) { unique() += c; }
	void append(int32_t number, int32_t minNumDigits = 1);

	char& front() { return unique().front(); }
	char& back() { return unique().back(); }

	// Cast operators
	operator std::string_view() const { return *data_; }
	explicit operator std::string() { return unique(); }

	/// Subscript operator
	/// @note this will allocate if the string does not hold ownership
	constexpr char& operator[](size_t pos) { return unique().operator[](pos); }

	// Appending operators
	void operator+=(int32_t number) { append(number); }
	constexpr void operator+=(const String& str) { append(str); }
	constexpr void operator+=(const std::string& str) { append(str); }
	constexpr void operator+=(std::string_view str) { append(str); }
	constexpr void operator+=(const char* cstr) { append(cstr); }
	constexpr void operator+=(char c) { append(c); }

	// Equality operators
	constexpr bool operator==(std::string_view other) const { return static_cast<std::string_view>(*this) == other; }
	constexpr bool operator==(const String& other) const {
		if ((data_ == other.data_) || (data_->empty() && other.data_->empty())) {
			return true; // Works if both lengths are 0, too
		}
		if (data_->empty() || other.data_->empty()) {
			return false; // If just one is empty, then not equal
		}
		return this->data_ == other.data_;
	}

	[[nodiscard]] constexpr bool iequals(std::string_view other) const {
		return std::ranges::all_of(std::views::zip(static_cast<std::string_view>(*data_), other),
		                           [](auto p) { return std::tolower(p.first) == std::tolower(p.second); });
	}

	[[nodiscard]] constexpr bool iequals(const char* other) const {
		return std::ranges::all_of(std::views::zip(static_cast<std::string_view>(*data_), std::string_view{other}),
		                           [](auto p) { return std::tolower(p.first) == std::tolower(p.second); });
	}

	[[nodiscard]] constexpr bool iequals(const String& other) const {
		if ((data_ == other.data_) || (data_->empty() && other.data_->empty())) {
			return true;
		}
		if (data_->empty() || other.data_->empty()) {
			return false; // If just one is empty, then not equal
		}
		return this->iequals(static_cast<std::string_view>(other));
	}

	[[nodiscard]] int icompare(const String& other) const { return strcasecmp(data_->c_str(), other.data_->c_str()); }

	[[nodiscard]] constexpr bool contains(const String& otherChars) const { return data_->contains(*otherChars.data_); }
	[[nodiscard]] constexpr bool contains(const std::string& otherChars) const { return data_->contains(otherChars); }
	[[nodiscard]] constexpr bool contains(std::string_view otherChars) const { return data_->contains(otherChars); }
	[[nodiscard]] constexpr bool contains(const char* otherChars) const { return data_->contains(otherChars); }

	[[nodiscard]] constexpr bool empty() const { return data_->empty(); }

	constexpr const char* c_str() { return unique().c_str(); }

	constexpr void reserve(size_t res) { unique().reserve(res); }

private:
	std::string& unique() noexcept(false) {
		// If any additional reasons, we gotta clone the memory first
		if (data_.use_count() > 1) {
			data_ = std::make_shared<std::string>(*data_);
		}
		return *data_;
	}

	std::shared_ptr<std::string> data_;
};

/// A string buffer with utility functions to append and format contents.
/// does not handle allocation
class StringBuf {
	// Not templated to optimize binary size.
public:
	StringBuf(char* buf, size_t capacity) : capacity_(capacity), buf_(buf) { memset(buf_, '\0', capacity_); }

	void append(const char* str) { ::strncat(buf_, str, capacity_ - size() - 1); }
	void append(char c) { ::strncat(buf_, &c, 1); }
	void clear() { buf_[0] = 0; }
	void truncate(size_t newSize) {
		if (newSize < capacity_) {
			buf_[newSize] = '\0';
		}
	}

	// TODO: Validate buffer size. These can overflow
	void appendInt(int i, int minChars = 1) { intToString(i, buf_ + size(), minChars); }
	void appendHex(int i, int minChars = 1) { intToHex(i, buf_ + size(), minChars); }
	void appendFloat(float f, int32_t minDecimals, int32_t maxDecimals) {
		floatToString(f, buf_ + size(), minDecimals, maxDecimals);
	}

	[[nodiscard]] char* data() { return buf_; }
	[[nodiscard]] const char* data() const { return buf_; }
	[[nodiscard]] const char* c_str() const { return buf_; }

	[[nodiscard]] size_t capacity() const { return capacity_; }
	[[nodiscard]] size_t size() const { return std::strlen(buf_); }
	[[nodiscard]] size_t length() const { return std::strlen(buf_); }

	[[nodiscard]] bool empty() const { return buf_[0] == '\0'; }

	bool operator==(const char* rhs) const { return std::strcmp(buf_, rhs) == 0; }
	bool operator==(const StringBuf& rhs) const { return std::strcmp(buf_, rhs.c_str()) == 0; }

	operator std::string_view() const { return std::string_view{buf_}; }

private:
	size_t capacity_;
	char* buf_;
};

/// Define a `StringBuf` that uses an array placed on the stack.
#define DEF_STACK_STRING_BUF(name, capacity)                                                                           \
	char name##__buf[capacity] = {0};                                                                                  \
	StringBuf name = {name##__buf, capacity}
