#ifndef  UTF8_BUILDER_H
#define  UTF8_BUILDER_H

#include <cstdint>
#include <string>

class alignas(8) Utf8Builder {
public:
	enum class State : std::int8_t { WAITING = 0, GOOD_CODE, BAD_CODE, OVER_FLOWN };

private:
	char _buf[4]{ 0 };
	int8_t _len{ 0 };
	int8_t _expecting{ 0 };
	State _state{ State::WAITING };

public:
	State push_back(char ch);

	inline void pour(std::string *dest = nullptr) {
		if (dest && _state == State::GOOD_CODE) { dest->append(_buf); }
		*reinterpret_cast<uint64_t*>(this) = 0; // Reset
	}

	[[nodiscard]] inline State state() const { return _state; }

	[[nodiscard]] inline const char* data() const { return _buf; }
};

#endif // UTF8_BUILDER_H
