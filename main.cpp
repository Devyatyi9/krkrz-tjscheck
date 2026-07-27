// tjscheck — standalone TJS2 syntax checker
// Usage: tjscheck file.tjs

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "tjsCommHead.h"
#include "tjs.h"
#include "tjsScriptBlock.h"
#include "tjsDebug.h"
#include "tjsError.h"
#include "tjsException.h"

// Stubs for features not needed in syntax-only mode
namespace TJS {
    void TJSDebugger(tTJS *, void *, void *) {}
    void TJSAddRefDebugger() {}
    void TJSReleaseDebugger() {}
}


// Wrapper to access protected ~tTJS
struct TJSWrapper : public TJS::tTJS {
    ~TJSWrapper() {}  // make destructor accessible
};

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: tjscheck file.tjs\n");
        return 1;
    }

    const char *filename = argv[1];

    // Read file
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s'\n", filename);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<char> buf(size + 1);
    fread(buf.data(), 1, size, f);
    buf[size] = 0;
    fclose(f);

    // Detect encoding and convert to UTF-16
    std::vector<wchar_t> wideBuf;
    const unsigned char *bytes = (const unsigned char *)buf.data();
    long dataLen = size;

    if (dataLen >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        // UTF-16 LE with BOM — use directly
        dataLen -= 2;
        int wlen = dataLen / (int)sizeof(wchar_t);
        wideBuf.resize(wlen + 1);
        memcpy(wideBuf.data(), bytes + 2, dataLen);
        wideBuf[wlen] = 0;
    } else if (dataLen >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        // UTF-8 with BOM — strip BOM, convert
        int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.data() + 3, (int)dataLen - 3, NULL, 0);
        wideBuf.resize(wlen + 1);
        MultiByteToWideChar(CP_UTF8, 0, buf.data() + 3, (int)dataLen - 3, wideBuf.data(), wlen);
        wideBuf[wlen] = 0;
    } else {
        // Assume UTF-8 without BOM
        int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)dataLen, NULL, 0);
        wideBuf.resize(wlen + 1);
        MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)dataLen, wideBuf.data(), wlen);
        wideBuf[wlen] = 0;
    }

    // Convert filename to wide
    int fnLen = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    std::vector<wchar_t> fnWide(fnLen);
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, fnWide.data(), fnLen);

    // Keep source text around for manual line counting
    const wchar_t *sourceText = wideBuf.data();

    try {
        TJSWrapper tjs;

        // Create script block with proper Owner (not constparse, but we never execute)
        TJS::tTJSScriptBlock block(&tjs, fnWide.data(), 0);

        // Parse — will throw on syntax error
        block.Parse(wideBuf.data(), false, false);

        // If no exception, syntax is OK
        fprintf(stdout, "OK\n");
        return 0;

    } catch (TJS::eTJSScriptError &e) {
        // LineVector is not populated during Parse-only (only by SetText),
        // so GetSourceLine() always returns 1. Count lines manually instead.
        tjs_int pos = e.GetPosition();
        tjs_int line = 0;
        for (tjs_int i = 0; i < pos && sourceText[i]; i++) {
            if (sourceText[i] == L'\n') line++;
        }
        line++;

        // Convert message to UTF-8
        ttstr msg = e.GetMessage();
        std::string msg8;
        int len8 = WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, NULL, 0, NULL, NULL);
        if (len8 > 0) {
            std::vector<char> buf8(len8);
            WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, buf8.data(), len8, NULL, NULL);
            msg8 = buf8.data();
        }

        fprintf(stderr, "Syntax error at line %d: %s\n", line, msg8.c_str());
        return 1;

    } catch (TJS::eTJS &e) {
        ttstr msg = e.GetMessage();
        std::string msg8;
        int len8 = WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, NULL, 0, NULL, NULL);
        if (len8 > 0) {
            std::vector<char> buf8(len8);
            WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, buf8.data(), len8, NULL, NULL);
            msg8 = buf8.data();
        }
        fprintf(stderr, "Error: %s\n", msg8.c_str());
        return 1;
    }

    return 0;
}
