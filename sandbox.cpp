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
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cwctype>
#include <string>
#include <utility>
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
	         L"  tjsbox --stdin\n"
	         L"  tjsbox --disasm <file.tjs>   compile and print the bytecode, do not run\n"
	         L"  tjsbox --advise <file.tjs>   run, then report what would bite later\n\n"
	         L"Exit 0   the script ran to the end; its value, if any, goes to stdout\n"
	         L"         (report one with `return` at top level -- a trailing\n"
	         L"         expression is not the script's value)\n"
	         L"Exit 1   the script failed; the error goes to stderr\n"
	         L"Exit 2   the tool could not read its input\n\n"
	         L"Only the TJS2 core is present: Array, Dictionary, Date, Math,\n"
	         L"RandomGenerator, Exception, RegExp. Nothing from the game, and no\n"
	         L"Scripts.getObjectKeys -- that one comes from the scriptsEx plugin.\n"
	         L"Write the objects under test as TJS dictionaries in the script.\n\n"
	         L"--disasm answers what a construct compiles to, for when the language\n"
	         L"reference is silent. It is not a checker.\n\n"
	         L"--advise runs the script and then reports one thing: a function\n"
	         L"installed on an object that calls a member the script saved earlier\n"
	         L"without checking it is still there. That is what takes the game down\n"
	         L"silently. Advice never changes the exit code.\n");
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

// ---------------------------------------------------------------------------
// Advice
//
// One rule, and it earns its place by having cost us two crashes rather than by
// looking suspicious: a function installed on an object calls a member the
// script itself stashed earlier, without ever asking whether that member is
// still there. Such a wrapper cannot be taken off an object a later scene
// replaced, so it outlives its own setup and finds the saved original gone --
// and being on the engine's dispatch path, the exception it throws there is not
// caught by anything and takes the process down with nothing in any log.
//
// The rule is read off the generated bytecode. Three things make that possible:
// an assignment names its member in the line's own comment, a function value
// carries the address that its context header repeats, and a `typeof` guard
// compiles to a `typeofd` naming the member it tested. Nothing here needs the
// script to have run; the pass is done after running it so that one invocation
// answers both "did it work" and "what would bite later".
//
// Only names the script assigns somewhere are considered. That is what keeps
// ordinary calls -- Debug.message, a helper's method -- out of the report: they
// are not saved originals, and losing them is not the failure being looked for.

class CollectingConsole : public TJS::iTJSConsoleOutput {
public:
	void ExceptionPrint(const tjs_char *msg) override { Lines.push_back(msg); }
	void Print(const tjs_char *msg) override { Lines.push_back(msg); }
	std::vector<std::wstring> Lines;
};

// The string a disassembly line names in its own trailing comment.
std::wstring QuotedName(const std::wstring &line) {
	size_t const open = line.find(L"(string)\"");
	if (open == std::wstring::npos) {
		return std::wstring();
	}
	size_t const start = open + 9;
	size_t const close = line.find(L'"', start);
	if (close == std::wstring::npos) {
		return std::wstring();
	}
	return line.substr(start, close - start);
}

// The object address a line mentions, either as a function value in a comment
// or as the address in a context header.
std::wstring ObjectAddress(const std::wstring &line, const wchar_t *after) {
	size_t const at = line.find(after);
	if (at == std::wstring::npos) {
		return std::wstring();
	}
	size_t const hex = line.find(L"0x", at);
	if (hex == std::wstring::npos) {
		return std::wstring();
	}
	size_t end = hex + 2;
	while (end < line.size() && iswxdigit(line[end])) {
		++end;
	}
	return line.substr(hex, end - hex);
}

// The opcode's first operand, when it is a register being written.
int DestRegister(const std::wstring &line) {
	size_t const pct = line.find(L'%');
	if (pct == std::wstring::npos) {
		return INT_MIN;
	}
	return _wtoi(line.c_str() + pct + 1);
}

bool StartsWith(const std::wstring &line, const wchar_t *op) {
	// Lines are "00000000 op ..."; the opcode follows the address.
	size_t const space = line.find(L' ');
	if (space == std::wstring::npos) {
		return false;
	}
	size_t const start = line.find_first_not_of(L' ', space);
	if (start == std::wstring::npos) {
		return false;
	}
	size_t const len = wcslen(op);
	return line.compare(start, len, op) == 0 &&
	       (line.size() == start + len || line[start + len] == L' ');
}

struct Context {
	std::wstring Address;
	bool IsFunction = false;
	std::vector<std::wstring> Lines;
};

int Advise(const std::wstring &source, const std::wstring &name) {
	TJSWrapper tjs;
	CollectingConsole console;
	tjs.SetConsoleOutput(&console);

	TJS::tTJSScriptBlock block(&tjs, name.c_str(), 0);
	std::vector<wchar_t> buffer(source.begin(), source.end());
	buffer.resize(buffer.size() + 2, L'\0');
	block.Parse(buffer.data(), false, false);
	block.Dump();
	tjs.SetConsoleOutput(nullptr);

	// Split the listing back into the contexts it was printed from.
	std::vector<Context> contexts;
	for (size_t i = 0; i < console.Lines.size(); ++i) {
		std::wstring const &line = console.Lines[i];
		if (line.find(L"(function expression)") != std::wstring::npos ||
		    line.find(L"(top level script)") != std::wstring::npos ||
		    line.find(L"(class)") != std::wstring::npos ||
		    line.find(L"(property") != std::wstring::npos) {
			Context context;
			context.Address = ObjectAddress(line, L")");
			context.IsFunction =
			    line.find(L"(function expression)") != std::wstring::npos;
			contexts.push_back(context);
			continue;
		}
		if (!contexts.empty()) {
			contexts.back().Lines.push_back(line);
		}
	}

	// Which member names the script stores anywhere, and which function value
	// ends up stored under which name.
	std::vector<std::wstring> stored;
	std::vector<std::pair<std::wstring, std::wstring> > installed;
	for (size_t c = 0; c < contexts.size(); ++c) {
		std::vector<std::pair<int, std::wstring> > held;
		for (size_t i = 0; i < contexts[c].Lines.size(); ++i) {
			std::wstring const &line = contexts[c].Lines[i];

			// A register stops holding a function as soon as it is written again.
			int const dest = DestRegister(line);
			if (dest != INT_MIN && !StartsWith(line, L"spde") &&
			    !StartsWith(line, L"spds")) {
				for (size_t h = 0; h < held.size();) {
					if (held[h].first == dest) {
						held.erase(held.begin() + h);
					} else {
						++h;
					}
				}
			}
			if (StartsWith(line, L"const")) {
				std::wstring const address = ObjectAddress(line, L"(object)");
				if (!address.empty() && dest != INT_MIN) {
					held.push_back(std::make_pair(dest, address));
				}
				continue;
			}
			if (StartsWith(line, L"spde") || StartsWith(line, L"spds")) {
				std::wstring const member = QuotedName(line);
				if (member.empty()) {
					continue;
				}
				stored.push_back(member);
				// The value being stored is the line's last register operand.
				size_t const last = line.rfind(L'%');
				if (last != std::wstring::npos) {
					int const source_reg = _wtoi(line.c_str() + last + 1);
					for (size_t h = 0; h < held.size(); ++h) {
						if (held[h].first == source_reg) {
							installed.push_back(std::make_pair(held[h].second, member));
						}
					}
				}
			}
		}
	}

	int found = 0;
	for (size_t c = 0; c < contexts.size(); ++c) {
		if (!contexts[c].IsFunction) {
			continue;
		}
		std::wstring where;
		for (size_t i = 0; i < installed.size(); ++i) {
			if (installed[i].first == contexts[c].Address) {
				where = installed[i].second;
			}
		}
		if (where.empty()) {
			continue;  // Not installed on anything; nothing calls it but us.
		}

		// Every member this context tests with typeof is considered guarded.
		std::vector<std::wstring> guarded;
		for (size_t i = 0; i < contexts[c].Lines.size(); ++i) {
			if (StartsWith(contexts[c].Lines[i], L"typeofd") ||
			    StartsWith(contexts[c].Lines[i], L"typeofm")) {
				guarded.push_back(QuotedName(contexts[c].Lines[i]));
			}
		}

		std::vector<std::wstring> reported;
		int protectedDepth = 0;
		for (size_t i = 0; i < contexts[c].Lines.size(); ++i) {
			std::wstring const &line = contexts[c].Lines[i];
			// A try block brackets its body with entry/extry. A call inside one
			// is not the failure being looked for: the exception is caught, and
			// the wrapper carries on. Only what escapes reaches the engine.
			if (StartsWith(line, L"entry")) {
				++protectedDepth;
				continue;
			}
			if (StartsWith(line, L"extry")) {
				if (protectedDepth > 0) {
					--protectedDepth;
				}
				continue;
			}
			if (protectedDepth > 0) {
				continue;
			}
			if (!StartsWith(line, L"calld") && !StartsWith(line, L"callm")) {
				continue;
			}
			std::wstring const callee = QuotedName(line);
			if (callee.empty()) {
				continue;
			}
			if (std::find(stored.begin(), stored.end(), callee) == stored.end()) {
				continue;  // Not something this script saved; not the failure.
			}
			if (std::find(guarded.begin(), guarded.end(), callee) != guarded.end()) {
				continue;
			}
			if (std::find(reported.begin(), reported.end(), callee) != reported.end()) {
				continue;
			}
			reported.push_back(callee);
			++found;
			fprintf(stdout,
			        "advice: %s installed as '%s' calls '%s' without checking it "
			        "is still there\n",
			        ToUtf8(name.c_str()).c_str(), ToUtf8(where.c_str()).c_str(),
			        ToUtf8(callee.c_str()).c_str());
			fprintf(stdout,
			        "        guard it: if (typeof x.%s == \"Object\") x.%s(...)\n",
			        ToUtf8(callee.c_str()).c_str(), ToUtf8(callee.c_str()).c_str());
		}
	}

	if (found == 0) {
		fprintf(stdout, "advice: nothing to report\n");
	}
	return found;
}

}  // namespace

int wmain(int argc, wchar_t *argv[]) {
	bool disasm = false;
	bool advise = false;
	int arg = 1;
	if (argc > 1 && (wcscmp(argv[arg], L"--disasm") == 0 ||
	                 wcscmp(argv[arg], L"-d") == 0)) {
		disasm = true;
		++arg;
	} else if (argc > 1 && (wcscmp(argv[arg], L"--advise") == 0 ||
	                        wcscmp(argv[arg], L"-a") == 0)) {
		advise = true;
		++arg;
	}

	if (argc - arg != 1 || wcscmp(argv[arg], L"--help") == 0 ||
	    wcscmp(argv[arg], L"-h") == 0) {
		bool const asked = argc == 2 && !disasm && !advise;
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

	if (disasm) {
		return Disasm(source, name);
	}

	int const code = Run(source, name);
	if (advise) {
		// After the run, so one invocation answers both questions -- and only
		// when the script got that far: advice on something that did not even
		// compile would bury the error that matters.
		if (code == ExitOk) {
			try {
				Advise(source, name);
			} catch (TJS::eTJS &e) {
				fprintf(stderr, "advice skipped: %s\n",
				        ToUtf8(e.GetMessage().c_str()).c_str());
			}
		}
	}
	return code;
}
