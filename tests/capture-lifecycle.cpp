#include "streaming/input/input.h"
#include "settings/streamingpreferences.h"
#include <QGuiApplication>
#include <cstdio>
#include <cstdlib>
#include <QAtomicInt>

QAtomicInt g_AsyncLoggingEnabled{0};

static int forwardedKeys;
extern "C" int __wrap_LiSendKeyboardEvent(short, char, char)
{
    ++forwardedKeys;
    return 0;
}
extern "C" int __wrap_LiSendKeyboardEvent2(short, char, char, char)
{
    ++forwardedKeys;
    return 0;
}
static void check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
    std::printf("PASS: %s\n", message);
}
int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Moonlight Capture Test");
    app.setApplicationName("Capture Test");
    check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0, "SDL initializes");
    SDL_Window* window = SDL_CreateWindow("Moonlight input capture check", 0, 0, 640, 360, SDL_WINDOW_SHOWN);
    check(window != nullptr, "Wayland test window opens");
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
    check(renderer != nullptr, "test surface has a renderer");
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    for (int i = 0; i < 100 && !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS); ++i) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(50);
    }
    check(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS, "test window receives keyboard focus");
    {
        StreamingPreferences& prefs = *StreamingPreferences::get(nullptr);
        prefs.absoluteMouseMode = true;
        prefs.captureSysKeysMode = StreamingPreferences::CSK_ALWAYS;
        SdlInputHandler handler(prefs, 640, 360);
        handler.setWindow(window);
        handler.notifyFocusGained();
        SDL_PumpEvents();
        SDL_Delay(30);
        SDL_PumpEvents();
        check(handler.isCaptureActive(), "focus captures input in windowed absolute mode");
        check(handler.isSystemKeyCaptureActive(), "system keyboard capture is enabled");
        check(SDL_GetWindowMouseRect(window) != nullptr, "pointer is confined while captured");
        SDL_KeyboardEvent key = {};
        key.state = SDL_PRESSED;
        key.keysym.sym = SDLK_a;
        key.keysym.scancode = SDL_SCANCODE_A;
        handler.handleKeyEvent(&key);
        check(forwardedKeys == 1, "captured keyboard input is forwarded");
        key.state = SDL_RELEASED;
        handler.handleKeyEvent(&key);
        key.state = SDL_PRESSED;
        key.keysym.sym = SDLK_z;
        key.keysym.scancode = SDL_SCANCODE_Z;
        key.keysym.mod = KMOD_CTRL | KMOD_ALT | KMOD_SHIFT;
        handler.handleKeyEvent(&key);
        check(!handler.isCaptureActive(), "Ctrl+Alt+Shift+Z releases input");
        check(!handler.isSystemKeyCaptureActive(), "release disables system keyboard capture");
        check(SDL_GetWindowMouseRect(window) == nullptr, "release frees pointer");
        int before = forwardedKeys;
        key.keysym.sym = SDLK_a;
        key.keysym.scancode = SDL_SCANCODE_A;
        key.keysym.mod = 0;
        handler.handleKeyEvent(&key);
        check(forwardedKeys == before, "released keyboard input stays local");
        handler.updateKeyboardGrabState();
        check(!handler.isCaptureActive(), "state updates preserve explicit release");
        key.keysym.sym = SDLK_z;
        key.keysym.scancode = SDL_SCANCODE_Z;
        key.keysym.mod = KMOD_CTRL | KMOD_ALT | KMOD_SHIFT;
        handler.handleKeyEvent(&key);
        check(handler.isSystemKeyCaptureActive(), "release shortcut can recapture");
        handler.notifyFocusLost();
        check(!handler.isCaptureActive() && !handler.isSystemKeyCaptureActive(), "focus loss releases both devices");
        handler.notifyFocusGained();
        check(handler.isSystemKeyCaptureActive(), "returning focus recaptures input");
        handler.setCaptureActive(false);
        SDL_PumpEvents();
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
