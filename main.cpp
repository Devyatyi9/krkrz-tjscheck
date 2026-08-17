// tjscheck - standalone TJS2 syntax checker

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <exception>
#include <limits>
#include <string>
#include <vector>
#include "tjsCommHead.h"
#include "tjs.h"
#include "tjsScriptBlock.h"
#include "tjsDebug.h"
#include "tjsError.h"
#include "tjsException.h"
#include "tjsByteCodeLoader.h"
#include "tjsRegExp.h"

// Stubs for features not needed in syntax-only mode.
namespace TJS {
	void TJSDebugger(tTJS *, void *, void *) {}
	void TJSAddRefDebugger() {}
	void TJSReleaseDebugger() {}
}

// Wrapper to access protected ~tTJS.
struct TJSWrapper : public TJS::tTJS {
	~TJSWrapper() {}
};

namespace {

enum ExitCode {
	ExitOk = 0,
	ExitSyntaxError = 1,
	ExitToolError = 2
};

enum class InputMode {
	File,
	Expression,
	Stdin,
	RegExp
};

struct Options {
	InputMode Mode;
	std::wstring Value;
};

std::string ToUtf8(const std::wstring &text);

void PrintUsage(FILE *stream) {
	fputs("Usage:\n"
		"  tjscheck <file.tjs>\n"
		"  tjscheck --expression <code>\n"
		"  tjscheck --stdin\n"
		"  tjscheck --regexp <pattern> [flags]\n"
		"\n"
		"Options:\n"
		"  -e, --expression <code>  Check one TJS expression\n"
		"  -s, --stdin              Read a standalone TJS script from standard input\n"
		"  -r, --regexp <pattern>   Compile a TJS RegExp pattern without running TJS\n"
		"  -h, --help               Show this help\n"
		"\n"
		"Regexp flags: g (global), i (ignore case), l (localized collation).\n"
		"Exit codes: 0 valid input, 1 syntax or RegExp error, 2 usage or tool error.\n", stream);
}

bool ParseOptions(int argc, wchar_t *argv[], Options &options) {
	bool endOfOptions = false;
	bool inputSpecified = false;

	for (int i = 1; i < argc; ++i) {
		const wchar_t *arg = argv[i];
		if (!endOfOptions && wcscmp(arg, L"--") == 0) {
			endOfOptions = true;
			continue;
		}
		if (!endOfOptions && (wcscmp(arg, L"--help") == 0 || wcscmp(arg, L"-h") == 0)) {
			return false;
		}
		if (!endOfOptions && (wcscmp(arg, L"--expression") == 0 || wcscmp(arg, L"-e") == 0)) {
			if (inputSpecified || ++i == argc) return false;
			options.Mode = InputMode::Expression;
			options.Value = argv[i];
			inputSpecified = true;
			continue;
		}
		if (!endOfOptions && (wcscmp(arg, L"--stdin") == 0 || wcscmp(arg, L"-s") == 0)) {
			if (inputSpecified) return false;
			options.Mode = InputMode::Stdin;
			inputSpecified = true;
			continue;
		}
		if (!endOfOptions && (wcscmp(arg, L"--regexp") == 0 || wcscmp(arg, L"-r") == 0)) {
			if (inputSpecified || ++i == argc) return false;
			options.Mode = InputMode::RegExp;
			options.Value = argv[i];
			inputSpecified = true;
			if (i + 1 < argc && argv[i + 1][0] != L'-') {
				options.Value += L'\0';
				options.Value += argv[++i];
			}
			continue;
		}
		if (!endOfOptions && arg[0] == L'-') return false;
		if (inputSpecified) return false;

		options.Mode = InputMode::File;
		options.Value = arg;
		inputSpecified = true;
	}

	return inputSpecified;
}

bool ReadAll(FILE *file, std::vector<unsigned char> &bytes) {
	unsigned char chunk[8192];
	for (;;) {
		size_t count = fread(chunk, 1, sizeof(chunk), file);
		if (count != 0) bytes.insert(bytes.end(), chunk, chunk + count);
		if (count != sizeof(chunk)) return ferror(file) == 0;
	}
}

bool ReadFile(const std::wstring &filename, std::vector<unsigned char> &bytes) {
	FILE *file = _wfopen(filename.c_str(), L"rb");
	if (!file) {
		fprintf(stderr, "Error: cannot open '%s'\n", ToUtf8(filename).c_str());
		return false;
	}
	bool ok = ReadAll(file, bytes);
	if (fclose(file) != 0) ok = false;
	if (!ok) fprintf(stderr, "Error: cannot read '%s'\n", ToUtf8(filename).c_str());
	return ok;
}

bool ReadStdin(std::vector<unsigned char> &bytes) {
	if (_setmode(_fileno(stdin), _O_BINARY) == -1) {
		fprintf(stderr, "Error: cannot set standard input to binary mode\n");
		return false;
	}
	if (!ReadAll(stdin, bytes)) {
		fprintf(stderr, "Error: cannot read standard input\n");
		return false;
	}
	return true;
}

bool DecodeUtf8(const unsigned char *bytes, size_t length, std::wstring &source) {
	if (length > static_cast<size_t>((std::numeric_limits<int>::max)())) return false;
	int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		reinterpret_cast<const char *>(bytes), static_cast<int>(length), NULL, 0);
	if (wideLength == 0 && length != 0) return false;
	source.resize(wideLength);
	if (wideLength != 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		reinterpret_cast<const char *>(bytes), static_cast<int>(length), &source[0], wideLength) == 0) {
		return false;
	}
	return true;
}

bool DecodeSource(const std::vector<unsigned char> &bytes, std::wstring &source) {
	if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
		size_t byteLength = bytes.size() - 2;
		if (byteLength % sizeof(wchar_t) != 0) return false;
		size_t wideLength = byteLength / sizeof(wchar_t);
		if (wideLength > static_cast<size_t>((std::numeric_limits<int>::max)())) return false;
		source.resize(wideLength);
		if (wideLength != 0) memcpy(&source[0], bytes.data() + 2, byteLength);
		return source.find(L'\0') == std::wstring::npos;
	}

	size_t offset = bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
	if (!DecodeUtf8(bytes.data() + offset, bytes.size() - offset, source)) return false;
	return source.find(L'\0') == std::wstring::npos;
}

std::string ToUtf8(const wchar_t *text) {
	int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
	if (length <= 1) return std::string();
	std::vector<char> result(length);
	if (WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, NULL, NULL) == 0) return std::string();
	return std::string(result.data());
}

std::string ToUtf8(const std::wstring &text) {
	return ToUtf8(text.c_str());
}

std::string ToUtf8(const ttstr &text) {
	return ToUtf8(text.c_str());
}

void GetLineAndColumn(const std::wstring &source, tjs_int position, tjs_int &line, tjs_int &column) {
	if (position < 0) position = 0;
	if (static_cast<size_t>(position) > source.size()) position = static_cast<tjs_int>(source.size());

	line = 1;
	column = 1;
	for (tjs_int i = 0; i < position; ++i) {
		if (source[i] == L'\r') {
			++line;
			column = 1;
			if (i + 1 < position && source[i + 1] == L'\n') ++i;
		} else if (source[i] == L'\n') {
			++line;
			column = 1;
		} else {
			++column;
		}
	}
}

int CheckSource(const std::wstring &source, const std::wstring &displayName, bool expression) {
	try {
		TJSWrapper tjs;
		TJS::tTJSScriptBlock block(&tjs, displayName.c_str(), 0);
		// Expression parsing appends a terminator in the lexer, so leave room for it.
		std::vector<wchar_t> parseBuffer(source.begin(), source.end());
		parseBuffer.resize(parseBuffer.size() + 2, L'\0');
		block.Parse(parseBuffer.data(), expression, false);
		fprintf(stdout, "OK\n");
		return ExitOk;
	} catch (TJS::eTJSScriptError &e) {
		tjs_int line;
		tjs_int column;
		GetLineAndColumn(source, e.GetPosition(), line, column);
		std::string message = ToUtf8(e.GetMessage());
		fprintf(stderr, "%s:%d:%d: syntax error: %s\n", ToUtf8(displayName).c_str(), line, column, message.c_str());
		return ExitSyntaxError;
	} catch (TJS::eTJS &e) {
		std::string message = ToUtf8(e.GetMessage());
		fprintf(stderr, "%s: error: %s\n", ToUtf8(displayName).c_str(), message.c_str());
		return ExitToolError;
	}
}

tjs_uint32 GetRegExpOptions(const wchar_t *flags) {
	tjs_uint32 options = ONIG_OPTION_DEFAULT | ONIG_OPTION_CAPTURE_GROUP | ONIG_OPTION_FIND_NOT_EMPTY;
	for (; *flags; ++flags) {
		if (*flags == L'i') options |= ONIG_OPTION_IGNORECASE;
		else if (*flags != L'g' && *flags != L'l') return 0;
	}
	return options;
}

int CheckRegExp(const std::wstring &value) {
	std::wstring::size_type separator = value.find(L'\0');
	std::wstring pattern = value.substr(0, separator);
	std::wstring flags = separator == std::wstring::npos ? L"" : value.substr(separator + 1);
	tjs_uint32 options = GetRegExpOptions(flags.c_str());
	if (options == 0) {
		fprintf(stderr, "<regexp>: invalid flags: %s\n", ToUtf8(flags).c_str());
		return ExitToolError;
	}

	std::wstring normalizedPattern = pattern.empty() ? L"(?:)" : pattern;
	regex_t *regex = NULL;
	OnigErrorInfo errorInfo;
	int result = onig_new(&regex,
		reinterpret_cast<const UChar *>(normalizedPattern.c_str()),
		reinterpret_cast<const UChar *>(normalizedPattern.c_str() + normalizedPattern.length()),
		options & ((ONIG_OPTION_MAXBIT << 1) - 1), ONIG_ENCODING_UTF16_LE, ONIG_SYNTAX_PERL, &errorInfo);
	if (result != ONIG_NORMAL) {
		char message[ONIG_MAX_ERROR_MESSAGE_LEN];
		onig_error_code_to_str(reinterpret_cast<UChar *>(message), result, &errorInfo);
		fprintf(stderr, "<regexp>: invalid pattern: %s\n", message);
		return ExitSyntaxError;
	}
	onig_free(regex);
	fprintf(stdout, "OK\n");
	return ExitOk;
}

} // namespace

int wmain(int argc, wchar_t *argv[]) {
	Options options;
	if (argc == 2 && (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"-h") == 0)) {
		PrintUsage(stdout);
		return ExitOk;
	}
	if (!ParseOptions(argc, argv, options)) {
		PrintUsage(stderr);
		return ExitToolError;
	}

	if (options.Mode == InputMode::RegExp) {
		return CheckRegExp(options.Value);
	}

	std::wstring source;
	std::wstring displayName;
	bool expression = options.Mode == InputMode::Expression;
	if (expression) {
		source = options.Value;
		displayName = L"<expression>";
	} else {
		std::vector<unsigned char> bytes;
		bool read = options.Mode == InputMode::File ? ReadFile(options.Value, bytes) : ReadStdin(bytes);
		if (!read) return ExitToolError;

		if (options.Mode == InputMode::File && bytes.size() >= 8 &&
			TJS::tTJSByteCodeLoader::IsTJS2ByteCode(bytes.data())) {
			fprintf(stdout, "OK (compiled bytecode)\n");
			return ExitOk;
		}

		if (!DecodeSource(bytes, source)) {
			std::wstring inputName = options.Mode == InputMode::File ? options.Value : std::wstring(L"<stdin>");
			fprintf(stderr, "Error: '%s' is not valid UTF-8 or UTF-16 LE text\n",
				ToUtf8(inputName).c_str());
			return ExitToolError;
		}
		displayName = options.Mode == InputMode::File ? options.Value : L"<stdin>";
	}

	try {
		return CheckSource(source, displayName, expression);
	} catch (const std::exception &e) {
		fprintf(stderr, "Error: %s\n", e.what());
	} catch (...) {
		fprintf(stderr, "Error: unexpected checker failure\n");
	}
	return ExitToolError;
}
