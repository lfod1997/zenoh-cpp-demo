#include "utf8_builder.h"

Utf8Builder::State Utf8Builder::push_back(char ch) {
	if (_state == State::BAD_CODE || _state == State::OVER_FLOWN) { return _state; }
	if (ch != 0) {
		if (_len < 4) {
			if (_expecting > 0) { // Waiting for an UTF-8 continuation byte
				if ((ch & 0xc0) == 0x80) {
					_buf[_len] = ch;
					++_len;
					--_expecting;
					if (!_expecting) { _state = State::GOOD_CODE; }
				}
				else { _state = State::BAD_CODE; }
			}
			else { // New code can start
				if (!(ch & 0x80) && isprint(ch)) { // 1-byte ASCII code
					_buf[_len] = ch;
					++_len;
					_state = State::GOOD_CODE;
				}
				else if ((ch & 0xe0) == 0xc0) { // 2-byte UTB-8 code
					_buf[_len] = ch;
					++_len;
					_expecting = 1;
					_state = State::WAITING;
				}
				else if ((ch & 0xf0) == 0xe0) { // 3-byte UTB-8 code
					_buf[_len] = ch;
					++_len;
					_expecting = 2;
					_state = State::WAITING;
				}
				else if ((ch & 0xf8) == 0xf0) { // 4-byte UTB-8 code
					_buf[_len] = ch;
					++_len;
					_expecting = 3;
					_state = State::WAITING;
				}
				else { _state = State::BAD_CODE; }
			}
		}
		else { _state = State::OVER_FLOWN; }
	}
	else if (_expecting > 0) { _state = State::BAD_CODE; }
	return _state;
}
