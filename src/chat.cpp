#include <cstdio>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <mutex>
#include <zenoh.hxx>

#include "schema/message.h"
#include "ansi_sequences.h"

//region Portable Raw Key Getter
#if defined(_WIN32) || defined(_WIN64)
#include <conio.h>
#include <windows.h>

namespace {
void enable_ansi_support() {
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE) return;
	DWORD dwMode = 0;
	if (!GetConsoleMode(hOut, &dwMode)) return;
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(hOut, dwMode);
}

int getkey() { return _getch(); }
}
#else
#include <termios.h>
#include <unistd.h>
#include <cstdio>

namespace {
void enable_ansi_support() {}

int getkey() {
	struct termios t_old, t_new;
	tcgetattr(STDIN_FILENO, &t_old);
	t_new = t_old;
	t_new.c_lflag &= ~(ICANON | ECHO | ISIG);
	tcsetattr(STDIN_FILENO, TCSANOW, &t_new);
	int ch = getchar();
	tcsetattr(STDIN_FILENO, TCSANOW, &t_old);
	return ch;
}
}
#endif
//endregion

using namespace std;
using namespace ZenohCppDemo::Schema;

// Globals
namespace {
string current_input{};
string current_input_prompt{};
mutex current_input_mutex{};
}

// Impl
namespace {
void print_usage(const char *progname) {
	cout << "Usage: " << progname << ' ' << ANSI_STYLE_REGULAR_WHITE << "KEY_EXPR" << ANSI_STYLE_RESET << endl;
}

enum class Command {
	/// Invalid command.
	INVALID = -1,
	/// Escape sequence.
	ESCAPE = 0,
	/// Quit the program.
	QUIT,
	/// Subscribe to new channel.
	SUBSCRIBE,
};

Command categorize_command(const string_view &line) {
	// Begin with 2 slashes
	if (line.size() >= 2 && line[1] == '/') { return Command::ESCAPE; }
	// /quit
	if (line.substr(1, 4) == "quit") { return Command::QUIT; }
	// /sub key/expr/to/topic
	if (line.substr(1, 3) == "sub") { return Command::SUBSCRIBE; }
	return Command::INVALID;
}

flatbuffers::DetachedBuffer build_chat_message_metadata(const std::string_view &text, const char *mime_type = "text/plain") {
	auto fbb = flatbuffers::FlatBufferBuilder{ 128 };
	const auto mime_type_ser = fbb.CreateString(mime_type);
	fbb.Finish(CreateChatMessageMetadata(fbb, mime_type_ser, text.size()));
	return fbb.Release();
}

optional<vector<zenoh::Subscriber<void>>> handle_command_sub(const string_view &line, const zenoh::Session &session, zenoh::ZResult *err = nullptr) {
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
			}
			while (line[end] == '/'); // Exit = found space or end of line
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
				if (!attachment.has_value()) {
					return;
				}

				// Read & verify the metadata itself
				auto metadata_buffer = attachment->get().as_vector(); // Copied here; fine
				if (flatbuffers::Verifier v{ metadata_buffer.data(), metadata_buffer.size() }; !VerifyChatMessageMetadataBuffer(v)) {
					lock_guard _{ current_input_mutex };
					cout << ANSI_CLEAR_LINE;
					cerr << ANSI_STYLE_REGULAR_RED << "Corrupted payload received from " << key << ": bad metadata." << ANSI_STYLE_RESET << endl;
					cout << current_input_prompt << current_input << flush;
					return;
				}

				// Use the metadata to verify the payload
				const auto *metadata = GetChatMessageMetadata(metadata_buffer.data());
				const string_view mime_type = metadata->mime_type()->string_view();
				if (mime_type.find("text/") == string_view::npos) {
					lock_guard _{ current_input_mutex };
					cout << ANSI_CLEAR_LINE;
					cerr << ANSI_STYLE_REGULAR_RED << "Unsupported message type \"" << mime_type << "\"." << ANSI_STYLE_RESET << endl;
					cout << current_input_prompt << current_input << flush;
					return;
				}
				const auto &payload = sample.get_payload();
				if (payload.size() != metadata->content_size()) {
					lock_guard _{ current_input_mutex };
					cout << ANSI_CLEAR_LINE;
					cerr << ANSI_STYLE_REGULAR_RED << "Corrupted payload received from " << key << ": broken payload." << ANSI_STYLE_RESET << endl;
					cout << current_input_prompt << current_input << flush;
					return;
				}

				// Use payload in a zero-copy manner
				{
					lock_guard _{ current_input_mutex };
					cout << ANSI_CLEAR_LINE;
					cout << ANSI_STYLE_REGULAR_BG_SKY << '[' << key << ']' << ANSI_STYLE_REGULAR_GRAY << " >> " << ANSI_STYLE_RESET;
					auto i = payload.slice_iter();
					for (auto it = i.next(); it.has_value(); it = i.next()) {
						const auto slice = it.value();
						// According to the metadata, we are now sure the data is text, so `reinterpret_cast` is safe
						cout << string_view{ reinterpret_cast<const char*>(slice.data), slice.len };
					}
					cout << '\n';
					cout << current_input_prompt << current_input << flush;
				}
			},
#ifdef DEBUG
			[key = string{ key }]() { printf("- [%s]\n", key.c_str()); },
#else
			zenoh::closures::none,
#endif
			zenoh::Session::SubscriberOptions::create_default(), err
		);
		if (*err != Z_OK) {
			subscribers.clear();
			return subscribers; // Return an empty vector to indicate API failure
		}
		else {
			subscribers.push_back(std::move(subscriber));
#ifdef DEBUG
			printf("+ [%s]\n", key.data());
#endif
		}
	}
	return !subscribers.empty() ? optional{ std::move(subscribers) } : nullopt;
}
}

int main(int argc, char *argv[]) {
	//region Initialize program
	enable_ansi_support();
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
	zenoh::ZResult err;
	string config_path{ "./config.json5" };
	auto config = zenoh::Config::from_file(config_path, &err);
	if (err == Z_OK) {
		cout << ANSI_STYLE_REGULAR_GRAY << "Loaded Zenoh config: " << config_path << '.' << ANSI_STYLE_RESET << endl;
	}
	else {
		cout << ANSI_STYLE_REGULAR_YELLOW << "Unable to load " << config_path << " (" << static_cast<int>(err) << ").\n" << ANSI_STYLE_RESET;
		cout << ANSI_STYLE_REGULAR_GRAY << "Using default Zenoh config.\n" << ANSI_STYLE_RESET;
		cout << flush;
		config = zenoh::Config::create_default();
	}
	auto session = zenoh::Session::open(
		std::move(config),
		zenoh::Session::SessionOptions::create_default(),
		&err
	);
	if (err != Z_OK) {
		cerr << ANSI_STYLE_REGULAR_RED << "Failed to create Zenoh session." << ANSI_STYLE_RESET << endl;
		return err;
	}
	//endregion

	//region Become a publisher
	auto key_expr = session.declare_keyexpr(key, &err);
	if (err != Z_OK) {
		cerr << ANSI_STYLE_REGULAR_RED << "Failed to declare Zenoh key expression." << ANSI_STYLE_RESET << endl;
		return err;
	}
	auto pub_opt = zenoh::Session::PublisherOptions::create_default();
	pub_opt.congestion_control = Z_CONGESTION_CONTROL_BLOCK; //NOTE: Congestion control must be set
	auto publisher = session.declare_publisher(key_expr, std::move(pub_opt), &err);
	if (err != Z_OK) {
		cerr << ANSI_STYLE_REGULAR_RED << "Failed to declare Zenoh publisher." << ANSI_STYLE_RESET << endl;
		return err;
	}
	//endregion

	vector<zenoh::Subscriber<void>> subscribers{};

	// Main loop
	{
		current_input_prompt = (
			ostringstream{} << ANSI_STYLE_REGULAR_BG_GRAY << '[' << key << ']' << ANSI_STYLE_REGULAR_GRAY << " << " << ANSI_STYLE_RESET
		).str();

		string line{};
		line.reserve(16384);
		while (true) {
			cout << current_input_prompt << flush;

			// Interactive CLI: basically just to read a line into `line`
			{
				int ch;
				do {
					ch = getkey();
					lock_guard _{ current_input_mutex };
					if (ch == '\x08' || ch == '\x7f') { // Backspace key
						if (!current_input.empty()) {
							current_input.pop_back();
							cout << ANSI_REMOVE_CHAR;
						}
					}
					else if (ch == '\x1b') { // Escape key
						current_input.clear();
						cout << ANSI_CLEAR_LINE << current_input_prompt;
					}
					else if (ch == '\x03' || ch == '\x1c') { // Keyboard interrupt (Ctrl+C or Ctrl+\)
						cout << endl;
						goto epilogue;
					}
					else if (ch == '\r' || ch == '\n') { // Enter key
						if (!current_input.empty()) {
							line.swap(current_input);
							cout << '\n' << flush;
							break;
						}
						else { continue; }
					}
					else {
						current_input.push_back(static_cast<char>(ch));
						cout << static_cast<char>(ch);
					}
					cout << flush;
				}
				while (true);
			}

			// If `line` begin with '/', handle as command; otherwise, publish as text
			if (!line.empty()) {
				if (line[0] != '/') {
					const string_view content{ line };
					flatbuffers::DetachedBuffer metadata_buffer = build_chat_message_metadata(content);
					uint8_t *ptr = metadata_buffer.data(); //NOTE: Must store in variable to enforce execution order!
					size_t size = metadata_buffer.size(); //NOTE: Must store in variable to enforce execution order!
					auto put_opt = zenoh::Publisher::PutOptions::create_default();
					put_opt.attachment = zenoh::Bytes{
						ptr, size, [moved = std::move(metadata_buffer)](uint8_t *) {}
					};
					zenoh::Bytes payload{ content }; //NOTE: Copied here; impl zero copy for your specific need!
					publisher.put(std::move(payload), std::move(put_opt), &err);
					if (err != Z_OK) {
						cerr << ANSI_STYLE_REGULAR_RED << "Failed to publish text (" << static_cast<int>(err) << ")." << ANSI_STYLE_RESET << endl;
					}
				}
				else {
					switch (categorize_command(line)) {
					case Command::ESCAPE: {
						const string_view content = string_view{ line }.substr(1);
						flatbuffers::DetachedBuffer metadata_buffer = build_chat_message_metadata(content);
						uint8_t *ptr = metadata_buffer.data(); //NOTE: Must store in variable to enforce execution order!
						size_t size = metadata_buffer.size(); //NOTE: Must store in variable to enforce execution order!
						auto put_opt = zenoh::Publisher::PutOptions::create_default();
						put_opt.attachment = zenoh::Bytes{
							ptr, size, [moved = std::move(metadata_buffer)](uint8_t *) {}
						};
						zenoh::Bytes payload{ content }; //NOTE: Copied here; impl zero copy for your specific need!
						publisher.put(std::move(payload), std::move(put_opt), &err);
						if (err != Z_OK) {
							cerr << ANSI_STYLE_REGULAR_RED << "Failed to publish text (" << static_cast<int>(err) << ")." << ANSI_STYLE_RESET << endl;
						}
						break;
					}
					case Command::QUIT: { goto epilogue; }
					case Command::SUBSCRIBE: {
						auto result = handle_command_sub(line, session, &err);
						if (!result.has_value()) {
							cerr << ANSI_STYLE_REGULAR_RED << "Unable to process command line \"" << line << "\": invalid syntax." << ANSI_STYLE_RESET << endl;
						}
						else if (err != Z_OK) { // When API fails, empty vector is returned
							cerr << ANSI_STYLE_REGULAR_RED << "Failed to subscribe (" << static_cast<int>(err) << ")." << ANSI_STYLE_RESET << endl;
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
	return 0;
}
