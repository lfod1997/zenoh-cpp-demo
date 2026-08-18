#if defined(_WIN32) || defined(_WIN64)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <iterator>
#include <optional>
#include <mutex>
#include <algorithm>
#include <type_traits>
#include <filesystem>
#include <system_error>

#include <zenoh.hxx>
#include <mio/mmap.hpp>

#include "schema/message.hpp"
#include "ansi_sequences.h"
#include "utf8_builder.hpp"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <unistd.h>
#endif

//region Portable Raw Key Getter
#if defined(_WIN32) || defined(_WIN64)
#include <conio.h>

namespace {
UINT sOrigInCP = GetConsoleCP();
UINT sOrigOutCP = GetConsoleOutputCP();

void modify_console_settings() {
	// Enable native ANSI support (Windows 10)
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE) return;
	DWORD dwMode = 0;
	if (!GetConsoleMode(hOut, &dwMode)) return;
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(hOut, dwMode);

	// Change current code page
	if (!SetConsoleCP(CP_UTF8)) {
		printf("%sCould not set input code page to UTF-8 (%lu).%s\n", ANSI_STYLE_REGULAR_YELLOW, GetLastError(), ANSI_STYLE_RESET);
	}
	if (!SetConsoleOutputCP(CP_UTF8)) {
		printf("%sCould not set output code page to UTF-8 (%lu).%s\n", ANSI_STYLE_REGULAR_YELLOW, GetLastError(), ANSI_STYLE_RESET);
	}
}

int getkey() { return _getch(); }

void restore_console_settings() {
	// Restore code page
	SetConsoleCP(sOrigInCP);
	SetConsoleOutputCP(sOrigOutCP);
}

std::string u8_to_cp(const std::string_view &str, const UINT cp) {
	if (cp == CP_UTF8) { return std::string{ str }; }
	const int wlen = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
	if (wlen == 0) { return ""; }
	std::wstring wstr(wlen, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), wstr.data(), wlen);
	const int olen = WideCharToMultiByte(cp, 0, wstr.data(), wlen, nullptr, 0, nullptr, nullptr);
	if (olen == 0) { return ""; }
	std::string ostr(olen, 0);
	WideCharToMultiByte(cp, 0, wstr.data(), wlen, ostr.data(), olen, nullptr, nullptr);
	return ostr;
}
}
#else
#include <termios.h>

namespace {
struct termios t_old;

void modify_console_settings() {
	struct termios t_new;
	tcgetattr(STDIN_FILENO, &t_old);
	t_new = t_old;
	t_new.c_lflag &= ~(ICANON | ECHO | ISIG);
	tcsetattr(STDIN_FILENO, TCSANOW, &t_new);
}

int getkey() { return getchar(); }

void restore_console_settings() {
	tcsetattr(STDIN_FILENO, TCSANOW, &t_old);
}
}
#endif
namespace {
class ConsoleSettingsGuard final {
public:
	ConsoleSettingsGuard() { modify_console_settings(); }

	~ConsoleSettingsGuard() { restore_console_settings(); }

	ConsoleSettingsGuard(const ConsoleSettingsGuard &) = delete;
	ConsoleSettingsGuard& operator=(const ConsoleSettingsGuard &) = delete;
	ConsoleSettingsGuard(ConsoleSettingsGuard &&) = default;
	ConsoleSettingsGuard& operator=(ConsoleSettingsGuard &&) = default;
};
}

//endregion

//region Portable Default Save Dir Getter
#if defined(_WIN32) || defined(_WIN64)
    #include <ShlObj.h>

namespace {
std::filesystem::path get_default_save_dir() {
	PWSTR wpath = nullptr;
	if (SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &wpath) == S_OK) {
		std::filesystem::path dd{ wpath };
		CoTaskMemFree(wpath);
		return dd;
	}

	// Fallbacks
	if (const char *home_env = std::getenv("USERPROFILE")) {
		std::filesystem::path base{ home_env };
		if (std::filesystem::path dd = base / "Downloads"; is_directory(status(dd))) { return dd; }
		return base;
	}
	if (const char *hd = std::getenv("HOMEDRIVE")) {
		if (const char *hp = std::getenv("HOMEPATH")) {
			std::filesystem::path base = std::filesystem::path{ hd } / hp;
			if (std::filesystem::path dd = base / "Downloads"; is_directory(status(dd))) { return dd; }
			return base;
		}
	}
	return {};
}
}
#else
    #include <sys/types.h>
    #include <pwd.h>

namespace {
std::filesystem::path get_default_save_dir() {
	struct passwd *pw = getpwuid(getuid());
	if (pw && pw->pw_dir) { return { pw->pw_dir }; }

	// Fallbacks
	if (const char *home_env = std::getenv("HOME")) { return { home_env }; }
	return {};
}
}
#endif
//endregion

using namespace std;
using namespace ZenohCppDemo;

// Globals
namespace {
constexpr auto CDR_ENDIANNESS = endianness::little_endian;

string current_input{};
string current_input_prompt{};
bool current_input_is_open{ false };
mutex current_input_mutex{};
}

// Printing Impl
namespace {
void print_usage(const char *progname) {
	cout << "Usage: " << progname << ' ' << ANSI_STYLE_REGULAR_WHITE << "KEY_EXPR" << ANSI_STYLE_RESET << endl;
}

/// Locks `current_input_mutex`.
template <typename T>
void insert_system_message(T &&message, const char *ansi_style = ANSI_STYLE_REGULAR_GRAY) {
	lock_guard _{ current_input_mutex };
	if (current_input_is_open) {
		cout << ANSI_CLEAR_LINE;
		cerr << ansi_style << std::forward<T>(message) << ANSI_STYLE_RESET << '\n';
		cout << current_input_prompt << current_input << flush;
	}
	else {
		cerr << ansi_style << std::forward<T>(message) << ANSI_STYLE_RESET << endl;
	}
}

template <>
void insert_system_message(ostringstream &&oss, const char *ansi_style) {
	insert_system_message(std::move(oss).str(), ansi_style);
}

template <>
void insert_system_message(const ostringstream &oss, const char *ansi_style) {
	insert_system_message(oss.str(), ansi_style);
}

/// Locks `current_input_mutex`.
template <typename T>
void insert_error(T &&message) {
	insert_system_message(std::forward<T>(message), ANSI_STYLE_REGULAR_RED);
}

/// Locks `current_input_mutex`.
template <typename T>
void insert_warning(T &&message) {
	insert_system_message(std::forward<T>(message), ANSI_STYLE_REGULAR_YELLOW);
}
}

// Service Impl
namespace {
enum class Command {
	/// Invalid command.
	INVALID = -1,
	/// Escape sequence.
	ESCAPE = 0,
	/// Quit the program.
	QUIT,
	/// Subscribe to new channel.
	SUBSCRIBE,
	/// Unsubscribe from a channel.
	UNSUBSCRIBE,
	/// Send a file.
	SEND_FILE,
};

Command categorize_command(const string_view &line) {
	// Begin with 2 slashes
	if (line.size() >= 2 && line[1] == '/') { return Command::ESCAPE; }
	// /quit
	if (line.substr(1, 4) == "quit") { return Command::QUIT; }
	// /sub key/expr/to/topic...
	if (line.substr(1, 3) == "sub") { return Command::SUBSCRIBE; }
	// /unsub key/expr/to/topic...
	if (line.substr(1, 5) == "unsub") { return Command::UNSUBSCRIBE; }
	// /send path/to/single/file
	if (line.substr(1, 4) == "send") { return Command::SEND_FILE; }
	return Command::INVALID;
}

zenoh::Bytes build_metadata(const string_view &mime_type, const size_t content_size, const string_view &file_name = "") {
	Metadata metadata{};
	metadata.mime_type(string{ mime_type }); // Inevitable
	metadata.content_size(content_size);
	metadata.file_name(string{ file_name }); // Inevitable

	basic_cdr_stream cdr{ CDR_ENDIANNESS };
	constexpr uint8_t encap[4]{ 0x00, 0x01, 0x00, 0x00 };

	// TODO: implement this
	return {};
}

optional<Metadata> decode_metadata(const zenoh::Bytes &payload) {
	auto i = payload.slice_iter();
	auto first_slice_opt = i.next();
	if (!first_slice_opt.has_value()) { return nullopt; } // Empty payload

	std::vector<char> fallback_buffer;
	const char *buffer_ptr = nullptr;
	size_t buffer_len = 0;

	if (!i.next().has_value()) { // Single-slice payload
		const auto first_slice = first_slice_opt.value();
		buffer_ptr = reinterpret_cast<const char*>(first_slice.data);
		buffer_len = first_slice.len;
	}
	else { // Multi-slice payload; have to flatten it
		i = payload.slice_iter();
		for (auto s = i.next(); s.has_value(); s = i.next()) {
			const auto slice = s.value();
			fallback_buffer.insert(
				fallback_buffer.end(),
				reinterpret_cast<const char*>(slice.data),
				reinterpret_cast<const char*>(slice.data) + slice.len
			);
		}
		buffer_ptr = fallback_buffer.data();
		buffer_len = fallback_buffer.size();
	}

	basic_cdr_stream cdr;
	cdr.set_buffer(const_cast<char*>(buffer_ptr) + 4, buffer_len - 4); // Will not actually write to
	try {
		Metadata metadata;
		read(cdr, metadata, key_mode::not_key);
		return metadata;
	}
	catch (...) { return nullopt; }
}

optional<vector<zenoh::Subscriber<void>>> handle_command_sub(const string_view &line, const zenoh::Session &session, zenoh::ZResult *zerr = nullptr) {
	if (line.back() == '/') { return nullopt; } // In no case might the last char be '/'

	vector<string_view> keys{};

	// A copy-less line parser (look what we have to do without C++20)
	{
		size_t end = 4;
		while (end != string_view::npos) {
			size_t begin = line.find_first_not_of(" \t", end);
			if (begin == string_view::npos) { break; } // Done
			if (begin == end || line[begin] == '/') { return nullopt; } // Invalid syntax: either command is /subXXX, or key starts with '/' (illegal)
			end = begin; // `line[begin]` must be normal char here
			do {
				end = line.find_first_of("/ \t", end + 1);
				if (end == string_view::npos) { break; } // Done
				if (line[end - 1] == '/') { return nullopt; } // Invalid syntax: either key ends with '/' or contains "//" (both illegal)
			} while (line[end] == '/'); // Exit = found space or end of line
			keys.push_back(line.substr(begin, end - begin));
		}
	}

	vector<zenoh::Subscriber<void>> subscribers{};
	subscribers.reserve(keys.size());
	for (const auto &key : keys) {
		auto subscriber = session.declare_subscriber(
			key,
			[key = string{ key }](const zenoh::Sample &sample) {
				auto attachment = sample.get_attachment();
				if (!attachment) {
					insert_error(ostringstream{} << "Corrupted payload received from " << key << ": missing metadata.");
					return;
				}

				// Use the metadata to verify the payload
				const auto metadata_opt = decode_metadata(attachment->get());
				if (!metadata_opt.has_value()) {
					insert_error(ostringstream{} << "Corrupted payload received from " << key << ": bad metadata.");
					return;
				}
				const auto &metadata = metadata_opt.value();
				const auto &payload = sample.get_payload();
				const uint64_t content_size = metadata.content_size();
				if (payload.size() != content_size) {
					insert_error(ostringstream{} << "Corrupted payload received from " << key << ": broken payload.");
					return;
				}

				// Use payload in a zero-copy manner, according to its MIME type
				const string_view mime_type = metadata.mime_type();
				if (mime_type.find("text/") == string_view::npos) { // Not text
					const string_view file_name = metadata.file_name();
					{
						lock_guard _{ current_input_mutex };
						cout << ANSI_CLEAR_LINE;
						cout << ANSI_STYLE_REGULAR_BG_SKY << '[' << key << ']' << ANSI_STYLE_REGULAR_GRAY << " >> " << ANSI_STYLE_RESET;
						cout << ANSI_STYLE_REGULAR_WHITE << file_name;
						cout << ANSI_STYLE_REGULAR_GRAY << " (" << content_size << " B), saving..." << ANSI_STYLE_RESET << '\n';
						cout << current_input_prompt << current_input << flush;
					}
					filesystem::path save_path = get_default_save_dir() / file_name;
					ofstream f{ save_path, std::ios::binary };
					auto i = payload.slice_iter();
					for (auto s = i.next(); s.has_value(); s = i.next()) {
						const auto slice = s.value();
						f.write(reinterpret_cast<const char*>(slice.data), slice.len);
					}
					f.close();
					insert_system_message(ostringstream{} << "File written to:\n" << save_path, ANSI_STYLE_REGULAR_GREEN);
				}
				else { // Text
					lock_guard _{ current_input_mutex };
					cout << ANSI_CLEAR_LINE;
					cout << ANSI_STYLE_REGULAR_BG_SKY << '[' << key << ']' << ANSI_STYLE_REGULAR_GRAY << " >> " << ANSI_STYLE_RESET;
					auto i = payload.slice_iter();
					for (auto s = i.next(); s.has_value(); s = i.next()) {
						const auto slice = s.value();
						cout << string_view{ reinterpret_cast<const char*>(slice.data), slice.len };
					}
					cout << '\n';
					cout << current_input_prompt << current_input << flush;
				}
			},
			[key = string{ key }]() {
				insert_system_message(ostringstream{} << "Unsubscribed from " << key << '.');
			},
			zenoh::Session::SubscriberOptions::create_default(), zerr
		);
		if (*zerr != Z_OK) {
			subscribers.clear();
			return subscribers; // Return an empty vector to indicate API failure
		}
		else {
			subscribers.push_back(std::move(subscriber));
			insert_system_message(ostringstream{} << "Subscribed to " << key << '.');
		}
	}
	return !subscribers.empty() ? optional{ std::move(subscribers) } : nullopt;
}

optional<size_t> handle_command_send_file(const string_view &line, const zenoh::Publisher &publisher, zenoh::ZResult *zerr = nullptr, error_code *ferr = nullptr) {
	if (line.size() <= 6) { return nullopt; }

	// Interpret everything after "/send " as a single path
	size_t end = 5;
	size_t begin = line.find_first_not_of(" \t", end);
	if (begin == end || begin == string_view::npos) { return nullopt; }
	end = line.find_last_not_of(" \t");
	filesystem::path path{
#if defined(_WIN32) || defined(_WIN64)
		u8_to_cp(line.substr(begin, end - begin + 1), sOrigOutCP)
#else
		line.substr(begin, end - begin + 1)
#endif
	};

	error_code _e; // Backing field
	error_code &e = ferr ? *ferr : _e;

	// Stat file
	if (const auto stat = filesystem::status(path); !is_regular_file(stat)) {
		e = make_error_code(is_directory(stat) ? errc::is_a_directory : errc::no_such_file_or_directory);
		if (ferr) { return 0; }
		else { throw filesystem::filesystem_error{ "not a file", path, e }; }
	}

	// Make mmap
	auto rm = mio::make_mmap_source(path.c_str(), 0, mio::map_entire_file, e);
	if (e) {
		if (ferr) { return 0; }
		else { throw filesystem::filesystem_error{ "failed to map", path, e }; }
	}
	const size_t content_size = rm.size();

	// Send
#if defined(_WIN32) || defined(_WIN64)
	path = line.substr(begin, end - begin + 1); // UTF-8
#endif
	auto put_opt = zenoh::Publisher::PutOptions::create_default();
	put_opt.attachment = build_metadata("application/octet-stream", content_size, path.filename().string());
	put_opt.encoding = zenoh::Encoding::Predefined::application_octet_stream();
	const char *ptr = rm.data(); //NOTE: Must store in variable to enforce execution order!
	publisher.put(
		zenoh::Bytes{ (uint8_t*)ptr, content_size, [moved = std::move(rm)](uint8_t *) {} }, // ptr is never written to; just to satisfy deleter signature
		std::move(put_opt),
		zerr
	);

	return content_size;
}
}

int main(int argc, char *argv[]) {
	//region Initialize program
	[[maybe_unused]] ConsoleSettingsGuard _csg{};
	current_input.reserve(16384);
	ios_base::sync_with_stdio(true);
	//endregion

	//region Parse args
	string_view key;
	for (size_t i = 1; i < argc; ++i) {
		const string_view arg = argv[i];
		if (arg == "-h" || arg == "--help") {
			print_usage(argv[0]);
			return 0;
		}
		else { key = arg; }
	}
	if (key.empty() || key[0] == '/' || key.back() == '/' || key.find("//") != string_view::npos) {
		print_usage(argv[0]);
		return 0;
	}
	//endregion

	//region Initialize Zenoh
	zenoh::ZResult zerr;
	string config_path{ "./config.json5" };
	auto config = zenoh::Config::from_file(config_path, &zerr);
	if (zerr == Z_OK) {
		cout << ANSI_STYLE_REGULAR_GRAY << "Loaded Zenoh config: " << config_path << '.' << ANSI_STYLE_RESET << endl;
	}
	else {
		cout << ANSI_STYLE_REGULAR_YELLOW << "Unable to load " << config_path << " (" << static_cast<int>(zerr) << ").\n" << ANSI_STYLE_RESET;
		cout << ANSI_STYLE_REGULAR_GRAY << "Using default Zenoh config.\n" << ANSI_STYLE_RESET;
		cout << flush;
		config = zenoh::Config::create_default();
	}
	auto session = zenoh::Session::open(
		std::move(config),
		zenoh::Session::SessionOptions::create_default(),
		&zerr
	);
	if (zerr != Z_OK) {
		cerr << ANSI_STYLE_REGULAR_RED << "Failed to create Zenoh session." << ANSI_STYLE_RESET << endl;
		return zerr;
	}
	//endregion

	//region Become a publisher
	auto key_expr = session.declare_keyexpr(key, &zerr);
	if (zerr != Z_OK) {
		cerr << ANSI_STYLE_REGULAR_RED << "Failed to declare Zenoh key expression." << ANSI_STYLE_RESET << endl;
		return zerr;
	}
	auto pub_opt = zenoh::Session::PublisherOptions::create_default();
	pub_opt.congestion_control = Z_CONGESTION_CONTROL_BLOCK; //NOTE: Congestion control must be set
	auto publisher = session.declare_publisher(key_expr, std::move(pub_opt), &zerr);
	if (zerr != Z_OK) {
		cerr << ANSI_STYLE_REGULAR_RED << "Failed to declare Zenoh publisher." << ANSI_STYLE_RESET << endl;
		return zerr;
	}
	//endregion

	vector<zenoh::Subscriber<void>> subscribers{};

	// Main loop
	{
		current_input_prompt = (
			ostringstream{} << ANSI_STYLE_REGULAR_BG_SILVER << '[' << key << ']' << ANSI_STYLE_REGULAR_GRAY << " << " << ANSI_STYLE_RESET
		).str();

		PrintableUtf8Builder u8buf{};
		string line{};
		line.reserve(16384);
		{
			lock_guard _{ current_input_mutex };
			current_input_is_open = true;
		}
		while (true) {
			// Interactive CLI: basically just to read a line into `line`
			{
				{
					lock_guard _{ current_input_mutex };
					cout << ANSI_CLEAR_LINE << current_input_prompt << flush;
				}

				int ch;
				do {
					ch = getkey();
					lock_guard _{ current_input_mutex };
					if (ch == '\x08' || ch == '\x7f') { // Backspace key
						if (!current_input.empty()) {
							// Delete all trailing UTF-8 continuation bytes, if any
							while ((current_input.back() & 0xc0) == 0x80) {
								current_input.pop_back();
							}
							// Delete the UTF-8 leading byte
							current_input.pop_back();
						}
						// Redraw entire line
						cout << ANSI_CLEAR_LINE << current_input_prompt << current_input;
					}
					else if (ch == '\x1b') { // Escape key
						current_input.clear();
						cout << ANSI_CLEAR_LINE << current_input_prompt;
					}
					else if (ch == '\x03' || ch == '\x1c') { // Keyboard interrupt (Ctrl+C or Ctrl+\)
						current_input_is_open = false;
						cout << endl;
						goto epilogue;
					}
					else if (ch == '\r' || ch == '\n') { // Enter key
						if (!current_input.empty()) {
							line.swap(current_input);
							cout << '\n' << flush;
							break;
						}
						else {
							cout << ANSI_CLEAR_LINE << current_input_prompt << current_input;
						}
					}
					else {
						switch (u8buf.push_back(static_cast<char>(ch))) {
						case PrintableUtf8Builder::State::WAITING: { break; }
						case PrintableUtf8Builder::State::GOOD_CODE: {
							u8buf.pour(&current_input);
							cout << ANSI_CLEAR_LINE << current_input_prompt << current_input;
							break;
						}
						case PrintableUtf8Builder::State::BAD_CODE:
						case PrintableUtf8Builder::State::OVER_FLOWN: {
							cout << ANSI_STYLE_REGULAR_BG_GRAY;
							const char *buf = u8buf.data();
							for (int i = 0; i < 4; ++i) {
								if (buf[i] == 0) { break; }
								printf("%02x", buf[i] & 0xff);
							}
							printf("%02x", ch);
							cout << ANSI_STYLE_RESET;
							u8buf.pour();
							break;
						}
						}
					}
					cout << flush;
				} while (true);
			}

			// If `line` begin with '/', handle as command; otherwise, publish as text
			if (!line.empty()) {
				if (line[0] != '/') {
					const string_view content{ line };
					auto put_opt = zenoh::Publisher::PutOptions::create_default();
					put_opt.attachment = build_metadata("text/plain", content.size());
					put_opt.encoding = zenoh::Encoding::Predefined::text_plain();
					do {
						zenoh::Bytes::Writer payload_writer{};
						payload_writer.append(content, &zerr);
						if (zerr != Z_OK) {
							cerr << ANSI_STYLE_REGULAR_RED << "Failed to compose payload (" << static_cast<int>(zerr) << ")." << ANSI_STYLE_RESET << endl;
							break; // do-while(0)
						}
						// ^ More `Bytes` may be appended to `payload_writer`
						publisher.put(std::move(payload_writer).finish(), std::move(put_opt), &zerr); //NOTE: Copied here; impl zero copy for your specific need!
						if (zerr != Z_OK) {
							cerr << ANSI_STYLE_REGULAR_RED << "Failed to publish text (" << static_cast<int>(zerr) << ")." << ANSI_STYLE_RESET << endl;
							break; // do-while(0)
						}
					} while (false);
				}
				else {
					switch (categorize_command(line)) {
					case Command::ESCAPE: {
						const string_view content = string_view{ line }.substr(1);
						auto put_opt = zenoh::Publisher::PutOptions::create_default();
						put_opt.attachment = build_metadata("text/plain", content.size());
						put_opt.encoding = zenoh::Encoding::Predefined::text_plain();
						do {
							zenoh::Bytes::Writer payload_writer{};
							payload_writer.append(content, &zerr);
							if (zerr != Z_OK) {
								cerr << ANSI_STYLE_REGULAR_RED << "Failed to compose payload (" << static_cast<int>(zerr) << ")." << ANSI_STYLE_RESET << endl;
								break; // do-while(0)
							}
							// ^ More `Bytes` may be appended to `payload_writer`
							publisher.put(std::move(payload_writer).finish(), std::move(put_opt), &zerr); //NOTE: Copied here; impl zero copy for your specific need!
							if (zerr != Z_OK) {
								cerr << ANSI_STYLE_REGULAR_RED << "Failed to publish text (" << static_cast<int>(zerr) << ")." << ANSI_STYLE_RESET << endl;
								break; // do-while(0)
							}
						} while (false);
						break; // switch(categorize_command(line))
					}
					case Command::QUIT: {
						{
							lock_guard _{ current_input_mutex };
							current_input_is_open = false;
						}
						goto epilogue;
					}
					case Command::SUBSCRIBE: {
						auto result = handle_command_sub(line, session, &zerr);
						if (!result) {
							cerr << ANSI_STYLE_REGULAR_RED << "Unable to process command line \"" << line << "\": invalid syntax." << ANSI_STYLE_RESET << endl;
						}
						else if (zerr != Z_OK) { // When API fails, empty vector is returned
							cerr << ANSI_STYLE_REGULAR_RED << "Failed to subscribe (" << static_cast<int>(zerr) << ")." << ANSI_STYLE_RESET << endl;
						}
						else {
							subscribers.reserve(subscribers.size() + result.value().size());
							subscribers.insert(
								subscribers.end(),
								make_move_iterator(result.value().begin()),
								make_move_iterator(result.value().end())
							);
						}
						break;
					}
					case Command::SEND_FILE: {
						error_code ferr;
						auto result = handle_command_send_file(line, publisher, &zerr, &ferr);
						if (!result) {
							cerr << ANSI_STYLE_REGULAR_RED << "Unable to process command line \"" << line << "\": invalid syntax." << ANSI_STYLE_RESET << endl;
						}
						else if (ferr) {
							cerr << ANSI_STYLE_REGULAR_RED << "Unable to open file: " << ferr.message() << '.' << ANSI_STYLE_RESET << endl;
						}
						else if (zerr != Z_OK) {
							cerr << ANSI_STYLE_REGULAR_RED << "Failed to send file (" << static_cast<int>(zerr) << ")." << ANSI_STYLE_RESET << endl;
						}
						else {
							cout << ANSI_STYLE_REGULAR_GRAY << "Sent content size: " << result.value() << " B." << ANSI_STYLE_RESET << endl;
						}
						break;
					}
					default: {
						cerr << ANSI_STYLE_REGULAR_RED << "Unable to process command line \"" << line << "\": unknown command." << ANSI_STYLE_RESET << endl;
						break;
					}
					}
				}
				line.clear();
			}
		}
	}

epilogue:
	subscribers.clear();
	cout << ANSI_STYLE_REGULAR_GRAY << "Graceful." << ANSI_STYLE_RESET << endl;
	return 0;
}
