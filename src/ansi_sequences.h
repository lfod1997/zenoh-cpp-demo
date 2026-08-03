#ifndef ANSI_SEQUENCES_H
#define ANSI_SEQUENCES_H

/// ANSI sequence to clear current line and move caret to line start.
inline constexpr char ANSI_CLEAR_LINE[] = "\r\x1b[2K";
/// ANSI sequence to remove last character of current line and move caret one char backward.
inline constexpr char ANSI_REMOVE_CHAR[] = "\b \b";
/// ANSI sequence to restore default console text style.
inline constexpr char ANSI_STYLE_RESET[] = "\x1b[0m";

inline constexpr char ANSI_STYLE_REGULAR_GRAY[] = "\x1b[0;90m";
inline constexpr char ANSI_STYLE_REGULAR_RED[] = "\x1b[0;31m";
inline constexpr char ANSI_STYLE_REGULAR_GREEN[] = "\x1b[0;32m";
inline constexpr char ANSI_STYLE_REGULAR_YELLOW[] = "\x1b[0;33m";
inline constexpr char ANSI_STYLE_REGULAR_BLUE[] = "\x1b[0;34m";
inline constexpr char ANSI_STYLE_REGULAR_SKY[] = "\x1b[0;94m";
inline constexpr char ANSI_STYLE_REGULAR_PURPLE[] = "\x1b[0;95m";
inline constexpr char ANSI_STYLE_REGULAR_CYAN[] = "\x1b[0;96m";
inline constexpr char ANSI_STYLE_REGULAR_WHITE[] = "\x1b[0;97m";
inline constexpr char ANSI_STYLE_REGULAR_BG_BLACK[] = "\x1b[0;37;40m";
inline constexpr char ANSI_STYLE_REGULAR_BG_GRAY[] = "\x1b[0;97;100m";
inline constexpr char ANSI_STYLE_REGULAR_BG_RED[] = "\x1b[0;37;41m";
inline constexpr char ANSI_STYLE_REGULAR_BG_GREEN[] = "\x1b[0;97;42m";
inline constexpr char ANSI_STYLE_REGULAR_BG_YELLOW[] = "\x1b[0;97;43m";
inline constexpr char ANSI_STYLE_REGULAR_BG_BLUE[] = "\x1b[0;37;44m";
inline constexpr char ANSI_STYLE_REGULAR_BG_SKY[] = "\x1b[0;97;104m";
inline constexpr char ANSI_STYLE_REGULAR_BG_PURPLE[] = "\x1b[0;97;105m";
inline constexpr char ANSI_STYLE_REGULAR_BG_CYAN[] = "\x1b[0;90;106m";
inline constexpr char ANSI_STYLE_REGULAR_BG_WHITE[] = "\x1b[0;30;47m";

#endif // ANSI_SEQUENCES_H
