// tjsbox - standalone TJS2 sandbox: runs a script instead of only parsing it.
//
// tjscheck answers "will this parse". That leaves the errors that actually cost
// us: a call on a member that turned out to be void, a name resolving against
// the wrong `this`. Those are runtime errors in a dynamically typed language, so
// no amount of static checking finds them -- but running the code does.
//
// The engine needed for that is already in the tree: the checker's project
// compiles the whole of tjs2/, virtual machine included, and tTJS::ExecScript is
// public. This host adds nothing to the engine; it only calls it.
//
// What it does not have is the game: no kag, no layers, no plugin-provided
// Scripts.getObjectKeys. A bare tTJS registers Array, Dictionary, Date, Math,
// RandomGenerator, Exception and RegExp, and nothing else. That is enough,
// because stubs can be written in TJS itself -- a dictionary with the members
// under test, deliberately missing the one the code forgets to check.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#include <string>
#include <vector>
#include "tjsCommHead.h"
#include "tjs.h"
#include "tjsError.h"
#include "tjsException.h"
#include "tjsScriptBlock.h"
#include "tjsVariant.h"

// Not built here, same as in the checker.
namespace TJS {
void TJSDebugger(tTJS *, void *, void *) {}
void TJSAddRefDebugger() {}
void TJSReleaseDebugger() {}
}  // namespace TJS

// The destructor is protected on tTJS.
struct TJSWrapper : public TJS::tTJS {
	~TJSWrapper() {}
};

namespace {

enum ExitCode {
	ExitOk = 0,
	ExitScriptError = 1,
	ExitToolError = 2,
};

std::string ToUtf8(const wchar_t *text) {
	if (!text || !*text) {
		return std::string();
	}
	int const size =
	    WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (size <= 1) {
		return std::string();
	}
	std::string result(static_cast<size_t>(size - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], size, nullptr, nullptr);
	return result;
}

bool ReadAll(FILE *file, std::vector<unsigned char> &bytes) {
	unsigned char buffer[4096];
	size_t read = 0;
	while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		bytes.insert(bytes.end(), buffer, buffer + read);
	}
	return !ferror(file);
}

// UTF-16 LE with a BOM, UTF-8 with or without one -- the same three shapes the
// checker accepts, so a file that checks can be run without being converted.
bool Decode(const std::vector<unsigned char> &bytes, std::wstring &source) {
	if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
		source.assign(reinterpret_cast<const wchar_t *>(bytes.data() + 2),
		              (bytes.size() - 2) / sizeof(wchar_t));
		return true;
	}
	size_t offset = 0;
	if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
	    bytes[2] == 0xBF) {
		offset = 3;
	}
	if (bytes.size() == offset) {
		source.clear();
		return true;
	}
	int const size = MultiByteToWideChar(
	    CP_UTF8, MB_ERR_INVALID_CHARS,
	    reinterpret_cast<const char *>(bytes.data() + offset),
	    static_cast<int>(bytes.size() - offset), nullptr, 0);
	if (size <= 0) {
		return false;
	}
	source.assign(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
	                    reinterpret_cast<const char *>(bytes.data() + offset),
	                    static_cast<int>(bytes.size() - offset), &source[0], size);
	return true;
}

void PrintUsage(FILE *stream) {
	fwprintf(stream,
	         L"tjsbox - run a TJS2 script outside the game\n\n"
	         L"Usage:\n"
	         L"  tjsbox <file.tjs>\n"
	         L"  tjsbox --stdin\n\n"
	         L"Exit 0   the script ran to the end; its value, if any, goes to stdout\n"
	         L"         (report one with `return` at top level -- a trailing\n"
	         L"         expression is not the script's value)\n"
	         L"Exit 1   the script failed; the error goes to stderr\n"
	         L"Exit 2   the tool could not read its input\n\n"
	         L"Only the TJS2 core is present: Array, Dictionary, Date, Math,\n"
	         L"RandomGenerator, Exception, RegExp. Nothing from the game, and no\n"
	         L"Scripts.getObjectKeys -- that one comes from the scriptsEx plugin.\n"
	         L"Write the objects under test as TJS dictionaries in the script.\n");
}

int Run(const std::wstring &source, const std::wstring &name) {
	try {
		TJSWrapper tjs;
		TJS::tTJSVariant result;
		tjs.ExecScript(source.c_str(), &result, nullptr, name.c_str());

		// A script that returns nothing is the normal case, so say nothing about
		// it rather than printing an empty line that looks like output.
		if (result.Type() != TJS::tvtVoid) {
			ttstr const text = result.AsString();
			fprintf(stdout, "%s\n", ToUtf8(text.c_str()).c_str());
		}
		return ExitOk;
	} catch (TJS::eTJSScriptError &e) {
		// Script errors carry where they happened; this is the whole point of
		// running in a host rather than in the game, where the same error takes
		// the process down without printing anything at all.
		fprintf(stderr, "%s(%d): %s\n", ToUtf8(name.c_str()).c_str(),
		        static_cast<int>(e.GetSourceLine()), ToUtf8(e.GetMessage().c_str()).c_str());
		ttstr const trace = e.GetTrace();
		if (!trace.IsEmpty()) {
			fprintf(stderr, "  trace: %s\n", ToUtf8(trace.c_str()).c_str());
		}
		return ExitScriptError;
	} catch (TJS::eTJS &e) {
		fprintf(stderr, "%s: %s\n", ToUtf8(name.c_str()).c_str(),
		        ToUtf8(e.GetMessage().c_str()).c_str());
		return ExitScriptError;
	}
}

// The generator writes its listing through the engine console, so capturing it
// is a matter of giving the engine somewhere to write.
class StdoutConsole : public TJS::iTJSConsoleOutput {
public:
	void ExceptionPrint(const tjs_char *msg) override {
		fprintf(stderr, "%s\n", ToUtf8(msg).c_str());
	}
	void Print(const tjs_char *msg) override {
		fprintf(stdout, "%s\n", ToUtf8(msg).c_str());
	}
};

int Disasm(const std::wstring &source, const std::wstring &name) {
	try {
		TJSWrapper tjs;
		StdoutConsole console;
		tjs.SetConsoleOutput(&console);

		// Compiling by hand rather than through ExecScript: the script block is
		// what holds the generated contexts, and ExecScript does not hand it back.
		TJS::tTJSScriptBlock block(&tjs, name.c_str(), 0);
		// The lexer appends a terminator, so leave it room, same as the checker.
		std::vector<wchar_t> buffer(source.begin(), source.end());
		buffer.resize(buffer.size() + 2, L'\0');
		block.Parse(buffer.data(), false, false);
		block.Dump();

		tjs.SetConsoleOutput(nullptr);
		return ExitOk;
	} catch (TJS::eTJSScriptError &e) {
		fprintf(stderr, "%s: %s\n", ToUtf8(name.c_str()).c_str(),
		        ToUtf8(e.GetMessage().c_str()).c_str());
		return ExitScriptError;
	} catch (TJS::eTJS &e) {
		fprintf(stderr, "%s: %s\n", ToUtf8(name.c_str()).c_str(),
		        ToUtf8(e.GetMessage().c_str()).c_str());
		return ExitScriptError;
	}
}

}  // namespace

int wmain(int argc, wchar_t *argv[]) {
	bool disasm = false;
	int arg = 1;
	if (argc > 1 && (wcscmp(argv[arg], L"--disasm") == 0 ||
	                 wcscmp(argv[arg], L"-d") == 0)) {
		disasm = true;
		++arg;
	}

	if (argc - arg != 1 || wcscmp(argv[arg], L"--help") == 0 ||
	    wcscmp(argv[arg], L"-h") == 0) {
		bool const asked = argc == 2 && !disasm;
		PrintUsage(asked ? stdout : stderr);
		return asked ? ExitOk : ExitToolError;
	}

	std::vector<unsigned char> bytes;
	std::wstring name;

	if (wcscmp(argv[arg], L"--stdin") == 0 || wcscmp(argv[arg], L"-s") == 0) {
		_setmode(_fileno(stdin), _O_BINARY);
		if (!ReadAll(stdin, bytes)) {
			fwprintf(stderr, L"Error: cannot read standard input\n");
			return ExitToolError;
		}
		name = L"<stdin>";
	} else {
		FILE *file = _wfopen(argv[arg], L"rb");
		if (!file) {
			fwprintf(stderr, L"Error: cannot open '%s'\n", argv[arg]);
			return ExitToolError;
		}
		bool const ok = ReadAll(file, bytes);
		fclose(file);
		if (!ok) {
			fwprintf(stderr, L"Error: cannot read '%s'\n", argv[arg]);
			return ExitToolError;
		}
		name = argv[arg];
	}

	std::wstring source;
	if (!Decode(bytes, source)) {
		fwprintf(stderr, L"Error: '%s' is not valid UTF-8 or UTF-16 LE\n",
		         name.c_str());
		return ExitToolError;
	}

	return disasm ? Disasm(source, name) : Run(source, name);
}
