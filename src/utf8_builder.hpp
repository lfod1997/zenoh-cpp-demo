#ifndef  UTF8_BUILDER_H
#define  UTF8_BUILDER_H

#include <cstdint>
#include <string>
#include <string_view>

template <bool ForcePrintable>
class alignas(uint64_t) Utf8Builder {
public:
	enum class State : std::int8_t { WAITING = 0, GOOD_CODE, BAD_CODE, OVER_FLOWN };

private:
	char _buf[5]{ 0 }; // UTF-8 code points are at most 4-byte, [4] is kept 0
	int8_t _len{ 0 };
	int8_t _expecting{ 0 };
	State _state{ State::WAITING };

public:
	inline void reset() noexcept { *this = {}; } // Forced single-instruction reset at -O3

	State push_back(char ch);

	inline void pour(std::string *dest = nullptr) {
		if (dest && _state == State::GOOD_CODE) {
			dest->append(_buf, static_cast<size_t>(_len));
		}
		reset();
	}

	[[nodiscard]] inline State state() const { return _state; }

	[[nodiscard]] inline const char* data() const { return _buf; }

	[[nodiscard]] inline std::string_view view() const { return { _buf, static_cast<size_t>(_len) }; }
};

template <bool ForcePrintable>
typename Utf8Builder<ForcePrintable>::State Utf8Builder<ForcePrintable>::push_back(char ch) {
	if (_state == State::BAD_CODE || _state == State::OVER_FLOWN) { return _state; }
	if (ch != 0) {
		if (_len < 4) {
			if (_expecting > 0) { // Waiting for an UTF-8 continuation byte
				if ((ch & 0xc0) == 0x80) {
					_buf[_len++] = ch;
					--_expecting;
					if (!_expecting) { _state = State::GOOD_CODE; }
				}
				else { _state = State::BAD_CODE; }
			}
			else { // New code can start
				if ((ch & 0x80) == 0x00) { // 1-byte ASCII code
					if constexpr (ForcePrintable) {
						if (isprint(ch)) {
							_buf[_len++] = ch;
							_state = State::GOOD_CODE;
						}
						else { _state = State::BAD_CODE; }
					}
					else {
						_buf[_len++] = ch;
						_state = State::GOOD_CODE;
					}
				}
				else if ((ch & 0xe0) == 0xc0) {
					// 2-byte UTB-8 code
					if (static_cast<uint8_t>(ch) >= 0xC2) {
						_buf[_len++] = ch;
						_expecting = 1;
						_state = State::WAITING;
					}
					else { _state = State::BAD_CODE; } // 0xC0, 0xC1 not allowed (RFC 3629)
				}
				else if ((ch & 0xf0) == 0xe0) { // 3-byte UTB-8 code
					_buf[_len++] = ch;
					_expecting = 2;
					_state = State::WAITING;
				}
				else if ((ch & 0xf8) == 0xf0) { // 4-byte UTB-8 code
					if (static_cast<uint8_t>(ch) <= 0xF4) {
						_buf[_len++] = ch;
						_expecting = 3;
						_state = State::WAITING;
					}
					else { _state = State::BAD_CODE; } // Over U+10FFFF not allowed (RFC 3629)
				}
				else { _state = State::BAD_CODE; }
			}
		}
		else { _state = State::OVER_FLOWN; }
	}
	else if (_expecting > 0) { _state = State::BAD_CODE; }
	return _state;
}

using PrintableUtf8Builder = Utf8Builder<true>;
using AnyUtf8Builder = Utf8Builder<false>;

#endif // UTF8_BUILDER_H
