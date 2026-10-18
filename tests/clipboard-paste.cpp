#include "streaming/input/input.h"
#include "settings/streamingpreferences.h"
#include <QGuiApplication>
#include <QAtomicInt>
#include <QFile>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>

QAtomicInt g_AsyncLoggingEnabled{0};
static int forwardedKeys;
static int textEvents;
extern "C" int __wrap_LiSendKeyboardEvent(short, char, char) { ++forwardedKeys; return 0; }
extern "C" int __wrap_LiSendKeyboardEvent2(short, char, char, char) { ++forwardedKeys; return 0; }
extern "C" int __wrap_LiSendUtf8TextEvent(const char*, unsigned int) { ++textEvents; return 0; }
static void check(bool ok, const char* message)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok) std::exit(1);
}
static void key(SdlInputHandler& input, SDL_Scancode code, Uint16 mods, Uint8 state = SDL_PRESSED)
{
    SDL_KeyboardEvent event = {};
    event.state = state;
    event.keysym.scancode = code;
    event.keysym.sym = SDL_GetKeyFromScancode(code);
    event.keysym.mod = mods;
    input.handleKeyEvent(&event);
}
static void waitForUpload(SdlInputHandler& input)
{
    for (int i = 0; i < 1000 && input.isClipboardPastePending(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        input.pollClipboardPaste();
    }
    check(!input.isClipboardPastePending(), "transfer finishes without blocking stream input");
}
int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Moonlight Clipboard Test");
    app.setApplicationName("Clipboard Test");
    check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0, "isolated SDL clipboard initializes");
    SDL_Window* window = SDL_CreateWindow("Clipboard test", 0, 0, 640, 360, 0);
    check(window != nullptr, "dummy test window opens");
    {
        auto& prefs = *StreamingPreferences::get(nullptr);
        prefs.absoluteMouseMode = true;
        SdlInputHandler input(prefs, 640, 360);
        input.setWindow(window);
        input.setClipboardHostId("test-host-uuid");
        input.notifyFocusGained();
        QByteArray text = "Hello\r\nRomanian: \xC8\x99\xC8\x9B\nEmoji: \xF0\x9F\x98\x80";
        SDL_SetClipboardText(text.constData());
        key(input, SDL_SCANCODE_V, KMOD_CTRL);
        check(forwardedKeys == 0 && input.isClipboardPastePending(), "Ctrl+V waits for clipboard acknowledgement before sending keys");
        key(input, SDL_SCANCODE_V, KMOD_CTRL, SDL_RELEASED);
        check(forwardedKeys == 0, "handled paste consumes unmatched V release");
        waitForUpload(input);
        QFile hostClipboard(qEnvironmentVariable("TEST_CLIPBOARD_OUTPUT"));
        check(hostClipboard.open(QIODevice::ReadOnly) && hostClipboard.readAll() == text, "real helper backend receives exact Unicode bytes and original newlines");
        check(forwardedKeys == 4 && textEvents == 0, "acknowledged paste uses Ctrl+V with explicit modifier transitions, never text typing");
        key(input, SDL_SCANCODE_C, KMOD_CTRL);
        key(input, SDL_SCANCODE_C, KMOD_CTRL, SDL_RELEASED);
        int before = forwardedKeys;
        key(input, SDL_SCANCODE_V, KMOD_CTRL);
        check(!input.isClipboardPastePending() && forwardedKeys == before + 1, "copying inside stream preserves native host paste");
        key(input, SDL_SCANCODE_V, KMOD_CTRL, SDL_RELEASED);
        before = forwardedKeys;
        key(input, SDL_SCANCODE_V, KMOD_CTRL | KMOD_ALT | KMOD_SHIFT);
        waitForUpload(input);
        check(forwardedKeys == before + 4, "explicit local clipboard shortcut uses the native helper");
        key(input, SDL_SCANCODE_V, 0, SDL_RELEASED);
        before = forwardedKeys;
        key(input, SDL_SCANCODE_V, KMOD_CTRL);
        input.notifyFocusLost();
        waitForUpload(input);
        check(forwardedKeys == before, "focus loss cancels delayed paste");
        input.notifyFocusGained();
        qputenv("TEST_CLIPBOARD_FAIL", "1");
        before = forwardedKeys;
        key(input, SDL_SCANCODE_V, KMOD_CTRL);
        waitForUpload(input);
        check(forwardedKeys == before && textEvents == 0, "failed clipboard transfer sends no paste or fallback typing");
        qunsetenv("TEST_CLIPBOARD_FAIL");
        key(input, SDL_SCANCODE_V, 0, SDL_RELEASED);
        input.setCaptureActive(false);
        key(input, SDL_SCANCODE_V, KMOD_CTRL);
        key(input, SDL_SCANCODE_V, KMOD_CTRL | KMOD_ALT | KMOD_SHIFT);
        check(!input.isClipboardPastePending(), "released capture cannot paste into the host");
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
}
