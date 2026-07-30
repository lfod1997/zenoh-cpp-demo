#include <cstdio>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <mutex>
#include <zenoh.hxx>

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

// Globals
namespace {
/// ANSI sequence to clear current line and move caret to line start.
constexpr char ANSI_CLEAR_LINE[] = "\r\x1b[2K";
/// ANSI sequence to remove last character of current line and move caret one char backward.
constexpr char ANSI_REMOVE_CHAR[] = "\b \b";

string current_input{};
string current_input_prompt{};
mutex current_input_mutex{};
}

// Impl
namespace {
void print_usage(const char *progname) {
	printf("Usage: %s KEY_EXPR\n", progname);
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
				lock_guard _{ current_input_mutex };
				cout << ANSI_CLEAR_LINE;
				cout << '[' << key << "] >> " << sample.get_payload().as_string() << '\n';
				cout << current_input_prompt << current_input << flush;
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
	current_input.reserve(1024);
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
		cout << "Loaded Zenoh config: " << config_path << '.' << endl;
	}
	else {
		cout << "Unable to load " << config_path << " (" << static_cast<int>(err) << "); using default Zenoh config." << endl;
		config = zenoh::Config::create_default();
	}
	auto session = zenoh::Session::open(
		std::move(config),
		zenoh::Session::SessionOptions::create_default(),
		&err
	);
	if (err != Z_OK) {
		cerr << "Failed to create Zenoh session." << endl;
		return err;
	}
	//endregion

	//region Become a publisher
	auto publisher = session.declare_publisher(key, zenoh::Session::PublisherOptions::create_default(), &err);
	if (err != Z_OK) {
		cerr << "Failed to declare Zenoh publisher." << endl;
		return err;
	}
	//endregion

	vector<zenoh::Subscriber<void>> subscribers{};

	// Main loop
	{
		current_input_prompt = (
			ostringstream{} << '[' << key << "] << "
		).str();

		string line{};
		line.reserve(1024);
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
					publisher.put(line, zenoh::Publisher::PutOptions::create_default(), &err);
					if (err != Z_OK) {
						cerr << "Failed to publish text (" << static_cast<int>(err) << ")." << endl;
					}
				}
				else {
					switch (categorize_command(line)) {
					case Command::ESCAPE: {
						publisher.put(line.substr(1), zenoh::Publisher::PutOptions::create_default(), &err);
						if (err != Z_OK) {
							cerr << "Failed to publish text (" << static_cast<int>(err) << ")." << endl;
						}
						break;
					}
					case Command::QUIT: { goto epilogue; }
					case Command::SUBSCRIBE: {
						auto result = handle_command_sub(line, session, &err);
						if (!result.has_value()) {
							cerr << "Unable to process command line \"" << line << "\": invalid syntax." << endl;
						}
						else if (err != Z_OK) { // When API fails, empty vector is returned
							cerr << "Failed to subscribe (" << static_cast<int>(err) << ")." << endl;
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
						cerr << "Unable to process command line \"" << line << "\": unknown command." << endl;
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
