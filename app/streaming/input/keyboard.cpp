#include "streaming/session.h"

#include <Limelight.h>
#include "SDL_compat.h"
#include <QCoreApplication>
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <chrono>

#define VK_0 0x30
#define VK_A 0x41

// These are real Windows VK_* codes
#ifndef VK_F1
#define VK_F1 0x70
#define VK_F13 0x7C
#define VK_NUMPAD0 0x60
#endif

#define MAKE_KEYPRESS_STATE(code, modifiers, flags) \
    ((uint32_t)(uint16_t)(code) | \
    ((uint32_t)(uint8_t)((modifiers) & MODIFIER_EXTENDED) << 16) | \
    ((uint32_t)(uint8_t)(flags) << 24))

#define GET_KEYPRESS_CODE(x) ((short)((x) & 0xFFFF))
#define GET_KEYPRESS_EXTENDED_MODIFIER(x) ((char)(((x) >> 16) & 0xFF))
#define GET_KEYPRESS_FLAGS(x) ((char)(((x) >> 24) & 0xFF))

void SdlInputHandler::performSpecialKeyCombo(KeyCombo combo)
{
    switch (combo) {
    case KeyComboQuit:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected quit key combo");

        // Push a quit event to the main loop
        SDL_Event event;
        event.type = SDL_QUIT;
        event.quit.timestamp = SDL_GetTicks();
        SDL_PushEvent(&event);
        break;

    case KeyComboUngrabInput:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected mouse and keyboard capture toggle combo");

        // Stop handling future input
        setCaptureActive(!isCaptureActive());

        // Force raise all keys to ensure they aren't stuck,
        // since we won't get their key up events.
        raiseAllKeys();
        break;

    case KeyComboToggleFullScreen:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected full-screen toggle combo");
        Session::s_ActiveSession->toggleFullscreen();

        // Force raise all keys just be safe across this full-screen/windowed
        // transition just in case key events get lost.
        raiseAllKeys();
        break;

    case KeyComboToggleStatsOverlay:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected stats toggle combo");

        // Toggle the stats overlay
        Session::get()->getOverlayManager().setOverlayState(Overlay::OverlayDebug,
                                                            !Session::get()->getOverlayManager().isOverlayEnabled(Overlay::OverlayDebug));
        break;

    case KeyComboToggleMouseMode:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected mouse mode toggle combo");

        // Uncapture input
        setCaptureActive(false);

        // Toggle mouse mode
        m_AbsoluteMouseMode = !m_AbsoluteMouseMode;

        // Recapture input
        setCaptureActive(true);
        break;

    case KeyComboToggleCursorHide:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected show mouse combo");

        if (!SDL_GetRelativeMouseMode()) {
            m_MouseCursorCapturedVisibilityState = !m_MouseCursorCapturedVisibilityState;
            SDL_ShowCursor(m_MouseCursorCapturedVisibilityState);
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Cursor can only be shown in remote desktop mouse mode");
        }
        break;

    case KeyComboToggleMinimize:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected minimize combo");
        SDL_MinimizeWindow(m_Window);
        break;

    case KeyComboPasteText:
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected type clipboard text combo");

        if (isCaptureActive() && pasteClipboardText(true)) {
            m_ClipboardPasteKeyDown = true;
        }
        break;
    }

    case KeyComboTogglePointerRegionLock:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected pointer region lock toggle combo");
        m_PointerRegionLockActive = !m_PointerRegionLockActive;

        // Remember that the user changed this manually, so we don't mess with it anymore
        // during windowed <-> full-screen transitions.
        m_PointerRegionLockToggledByUser = true;

        // Apply the new region lock
        updatePointerRegionLock();
        break;

    case KeyComboQuitAndExit:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected quitAndExit key combo");

        // Indicate that we want to exit afterwards
        Session::get()->setShouldExit(true);

        // Push a quit event to the main loop
        SDL_Event quitExitEvent;
        quitExitEvent.type = SDL_QUIT;
        quitExitEvent.quit.timestamp = SDL_GetTicks();
        SDL_PushEvent(&quitExitEvent);
        break;

    case KeyComboToggleKeyboardGrab:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected keyboard grab toggle combo");

        // Toggle the system key capture mode
        if (isSystemKeyCaptureActive()) {
            m_CaptureSystemKeysMode = StreamingPreferences::CSK_OFF;
        }
        else {
            m_CaptureSystemKeysMode = StreamingPreferences::CSK_ALWAYS;
        }

        updateKeyboardGrabState();
        break;

    default:
        Q_UNREACHABLE();
    }
}

bool SdlInputHandler::pasteClipboardText(bool forceLocal)
{
    // A helper must be paired before local clipboard paste is available. Never
    // fall back to Moonlight's character-by-character text injection.
    const QString config = QDir::homePath() + "/.config/moonlight-input-capture/clipboard.json";
    if (!QFileInfo::exists(config)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Native clipboard helper has not been configured");
        return false;
    }
    if (m_ClipboardUpload.valid()) {
        return true;
    }
    char* text = SDL_GetClipboardText();
    if (text == nullptr) {
        return false;
    }
    QByteArray clipboard(text);
    SDL_free(text);
    if (clipboard.isEmpty() ||
            (!forceLocal && m_PreferHostClipboard && clipboard == m_ClipboardAtHostCopy)) {
        // An empty local clipboard, or a copy made in the stream, uses native
        // host paste. Never replace that with stale local clipboard contents.
        return false;
    }

    m_PreferHostClipboard = false;
    raiseAllKeys();
    m_ClipboardPasteCancelled = false;
    const QString sender = QCoreApplication::applicationDirPath() + "/clipboard-send.py";
    const QString hostId = m_ClipboardHostId;
    m_ClipboardUpload = std::async(std::launch::async, [clipboard, config, sender, hostId]() -> QString {
        QProcess process;
        process.start("python3", {sender, "--config", config, "--host-id", hostId});
        if (!process.waitForStarted(1000)) {
            return QStringLiteral("Cannot start clipboard transport");
        }
        process.write(clipboard);
        process.closeWriteChannel();
        if (!process.waitForFinished(7000)) {
            process.kill();
            process.waitForFinished(1000);
            return QStringLiteral("Clipboard transfer timed out");
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            const QString detail = QString::fromUtf8(process.readAllStandardError()).left(512);
            return detail.isEmpty() ? QStringLiteral("Clipboard transport failed") : detail;
        }
        return {};
    });
    return true;
}

void SdlInputHandler::pollClipboardPaste()
{
    if (!m_ClipboardUpload.valid() ||
            m_ClipboardUpload.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        return;
    }
    const QString error = m_ClipboardUpload.get();
    if (!error.isEmpty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Native clipboard paste failed: %s", qUtf8Printable(error));
        return;
    }
    if (m_ClipboardPasteCancelled || !isCaptureActive()) {
        return;
    }
    // The real host clipboard has been set and acknowledged. Use a normal paste
    // shortcut with explicit modifier transitions, never Unicode typing events.
    raiseAllKeys();
    LiSendKeyboardEvent2(0xA2, KEY_ACTION_DOWN, MODIFIER_CTRL, 0);
    LiSendKeyboardEvent2(0x56, KEY_ACTION_DOWN, MODIFIER_CTRL, 0);
    LiSendKeyboardEvent2(0x56, KEY_ACTION_UP, MODIFIER_CTRL, 0);
    LiSendKeyboardEvent2(0xA2, KEY_ACTION_UP, 0, 0);
}

void SdlInputHandler::handleKeyEvent(SDL_KeyboardEvent* event)
{
    short keyCode;
    char modifiers;
    char flags;
    bool shouldNotConvertToScanCodeOnServer = false;

    if (event->repeat) {
        // Ignore repeat key down events
        SDL_assert(event->state == SDL_PRESSED);
        return;
    }

    if (event->state == SDL_RELEASED && event->keysym.scancode == SDL_SCANCODE_V && m_ClipboardPasteKeyDown) {
        m_ClipboardPasteKeyDown = false;
        return;
    }

    // Check for our special key combos
    if ((event->state == SDL_PRESSED) &&
            (event->keysym.mod & KMOD_CTRL) &&
            (event->keysym.mod & KMOD_ALT) &&
            (event->keysym.mod & KMOD_SHIFT)) {
        // First we test the SDLK combos for matches,
        // that way we ensure that latin keyboard users
        // can match to the key they see on their keyboards.
        // If nothing matches that, we'll then go on to
        // checking scancodes so non-latin keyboard users
        // can have working hotkeys (though possibly in
        // odd positions). We must do all SDLK tests before
        // any scancode tests to avoid issues in cases
        // where the SDLK for one shortcut collides with
        // the scancode of another.

        for (int i = 0; i < KeyComboMax; i++) {
            if (m_SpecialKeyCombos[i].enabled && event->keysym.sym == m_SpecialKeyCombos[i].keyCode) {
                performSpecialKeyCombo(m_SpecialKeyCombos[i].keyCombo);
                return;
            }
        }

        for (int i = 0; i < KeyComboMax; i++) {
            if (m_SpecialKeyCombos[i].enabled && event->keysym.scancode == m_SpecialKeyCombos[i].scanCode) {
                performSpecialKeyCombo(m_SpecialKeyCombos[i].keyCombo);
                return;
            }
        }
    }

    // Release means local input only, including ordinary keys. Keep the
    // shortcut handling above available so capture can be enabled again.
    if (!isCaptureActive()) {
        return;
    }

    if (m_ClipboardUpload.valid() && event->state == SDL_PRESSED &&
            event->keysym.scancode != SDL_SCANCODE_V &&
            event->keysym.scancode != SDL_SCANCODE_LCTRL && event->keysym.scancode != SDL_SCANCODE_RCTRL &&
            event->keysym.scancode != SDL_SCANCODE_LSHIFT && event->keysym.scancode != SDL_SCANCODE_RSHIFT) {
        m_ClipboardPasteCancelled = true;
    }

    // Preserve native copy/paste inside the host, while making Ctrl+V after
    // copying in a local app send this computer's UTF-8 clipboard text.
    if (event->state == SDL_PRESSED && (event->keysym.mod & KMOD_CTRL) &&
            !(event->keysym.mod & (KMOD_ALT | KMOD_GUI))) {
        if (event->keysym.scancode == SDL_SCANCODE_C || event->keysym.scancode == SDL_SCANCODE_X) {
            char* text = SDL_GetClipboardText();
            m_ClipboardAtHostCopy = text ? QByteArray(text) : QByteArray();
            SDL_free(text);
            m_PreferHostClipboard = true;
        }
        else if (event->keysym.scancode == SDL_SCANCODE_V && pasteClipboardText(false)) {
            m_ClipboardPasteKeyDown = true;
            return;
        }
    }

    // Set modifier flags
    modifiers = 0;
    if (event->keysym.mod & KMOD_CTRL) {
        modifiers |= MODIFIER_CTRL;
    }
    if (event->keysym.mod & KMOD_ALT) {
        modifiers |= MODIFIER_ALT;
    }
    if (event->keysym.mod & KMOD_SHIFT) {
        modifiers |= MODIFIER_SHIFT;
    }
    if (event->keysym.mod & KMOD_GUI) {
        if (isSystemKeyCaptureActive()) {
            modifiers |= MODIFIER_META;
        }
    }

    // Set keycode. We explicitly use scancode here because GFE will try to correct
    // for AZERTY layouts on the host but it depends on receiving VK_ values matching
    // a QWERTY layout to work.
    if (event->keysym.scancode >= SDL_SCANCODE_1 && event->keysym.scancode <= SDL_SCANCODE_9) {
        // SDL defines SDL_SCANCODE_0 > SDL_SCANCODE_9, so we need to handle that manually
        keyCode = (event->keysym.scancode - SDL_SCANCODE_1) + VK_0 + 1;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_A && event->keysym.scancode <= SDL_SCANCODE_Z) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_A) + VK_A;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_F1 && event->keysym.scancode <= SDL_SCANCODE_F12) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_F1) + VK_F1;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_F13 && event->keysym.scancode <= SDL_SCANCODE_F24) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_F13) + VK_F13;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_KP_1 && event->keysym.scancode <= SDL_SCANCODE_KP_9) {
        // SDL defines SDL_SCANCODE_KP_0 > SDL_SCANCODE_KP_9, so we need to handle that manually
        keyCode = (event->keysym.scancode - SDL_SCANCODE_KP_1) + VK_NUMPAD0 + 1;
    }
    else {
        switch (event->keysym.scancode) {
            case SDL_SCANCODE_BACKSPACE:
                keyCode = 0x08;
                break;
            case SDL_SCANCODE_TAB:
                keyCode = 0x09;
                break;
            case SDL_SCANCODE_CLEAR:
                keyCode = 0x0C;
                break;
            case SDL_SCANCODE_KP_ENTER:
                modifiers |= MODIFIER_EXTENDED;
                Q_FALLTHROUGH();
            case SDL_SCANCODE_RETURN:
                keyCode = 0x0D;
                break;
            case SDL_SCANCODE_PAUSE:
                keyCode = 0x13;
                break;
            case SDL_SCANCODE_CAPSLOCK:
                keyCode = 0x14;
                break;
            case SDL_SCANCODE_ESCAPE:
                keyCode = 0x1B;
                break;
            case SDL_SCANCODE_SPACE:
                keyCode = 0x20;
                break;
            case SDL_SCANCODE_PAGEUP:
                keyCode = 0x21;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_PAGEDOWN:
                keyCode = 0x22;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_END:
                keyCode = 0x23;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_HOME:
                keyCode = 0x24;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_LEFT:
                keyCode = 0x25;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_UP:
                keyCode = 0x26;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_RIGHT:
                keyCode = 0x27;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_DOWN:
                keyCode = 0x28;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_SELECT:
                keyCode = 0x29;
                break;
            case SDL_SCANCODE_EXECUTE:
                keyCode = 0x2B;
                break;
            case SDL_SCANCODE_PRINTSCREEN:
                keyCode = 0x2C;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_INSERT:
                keyCode = 0x2D;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_DELETE:
                keyCode = 0x2E;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_HELP:
                keyCode = 0x2F;
                break;
            case SDL_SCANCODE_KP_0:
                // See comment above about why we only handle SDL_SCANCODE_KP_0 here
                keyCode = VK_NUMPAD0;
                break;
            case SDL_SCANCODE_0:
                // See comment above about why we only handle SDL_SCANCODE_0 here
                keyCode = VK_0;
                break;
            case SDL_SCANCODE_KP_MULTIPLY:
                keyCode = 0x6A;
                break;
            case SDL_SCANCODE_KP_PLUS:
                keyCode = 0x6B;
                break;
            case SDL_SCANCODE_KP_COMMA:
                keyCode = 0x6C;
                break;
            case SDL_SCANCODE_KP_MINUS:
                keyCode = 0x6D;
                break;
            case SDL_SCANCODE_KP_PERIOD:
                keyCode = 0x6E;
                break;
            case SDL_SCANCODE_KP_DIVIDE:
                keyCode = 0x6F;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_NUMLOCKCLEAR:
                keyCode = 0x90;
                break;
            case SDL_SCANCODE_SCROLLLOCK:
                keyCode = 0x91;
                break;
            case SDL_SCANCODE_LSHIFT:
                keyCode = 0xA0;
                break;
            case SDL_SCANCODE_RSHIFT:
                keyCode = 0xA1;
                break;
            case SDL_SCANCODE_LCTRL:
                keyCode = 0xA2;
                break;
            case SDL_SCANCODE_RCTRL:
                keyCode = 0xA3;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_LALT:
                keyCode = 0xA4;
                break;
            case SDL_SCANCODE_RALT:
                keyCode = 0xA5;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_LGUI:
                if (!isSystemKeyCaptureActive()) {
                    return;
                }
                keyCode = 0x5B;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_RGUI:
                if (!isSystemKeyCaptureActive()) {
                    return;
                }
                keyCode = 0x5C;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_APPLICATION:
                keyCode = 0x5D;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_AC_BACK:
                keyCode = 0xA6;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_AC_FORWARD:
                keyCode = 0xA7;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_AC_REFRESH:
                keyCode = 0xA8;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_AC_STOP:
                keyCode = 0xA9;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_AC_SEARCH:
                keyCode = 0xAA;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_AC_BOOKMARKS:
                keyCode = 0xAB;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_AC_HOME:
                keyCode = 0xAC;
                modifiers |= MODIFIER_EXTENDED;
                break;
            case SDL_SCANCODE_SEMICOLON:
                keyCode = 0xBA;
                break;
            case SDL_SCANCODE_EQUALS:
                keyCode = 0xBB;
                break;
            case SDL_SCANCODE_COMMA:
                keyCode = 0xBC;
                break;
            case SDL_SCANCODE_MINUS:
                keyCode = 0xBD;
                break;
            case SDL_SCANCODE_PERIOD:
                keyCode = 0xBE;
                break;
            case SDL_SCANCODE_SLASH:
                keyCode = 0xBF;
                break;
            case SDL_SCANCODE_GRAVE:
                keyCode = 0xC0;
                break;
            case SDL_SCANCODE_LEFTBRACKET:
                keyCode = 0xDB;
                break;
            case SDL_SCANCODE_INTERNATIONAL3:
                shouldNotConvertToScanCodeOnServer = true;
                Q_FALLTHROUGH();
            case SDL_SCANCODE_BACKSLASH:
                keyCode = 0xDC;
                break;
            case SDL_SCANCODE_RIGHTBRACKET:
                keyCode = 0xDD;
                break;
            case SDL_SCANCODE_APOSTROPHE:
                keyCode = 0xDE;
                break;
            case SDL_SCANCODE_INTERNATIONAL1:
                shouldNotConvertToScanCodeOnServer = true;
                Q_FALLTHROUGH();
            case SDL_SCANCODE_NONUSBACKSLASH:
                keyCode = 0xE2;
                break;
            case SDL_SCANCODE_LANG1:
                keyCode = 0x1C;
                break;
            case SDL_SCANCODE_LANG2:
                keyCode = 0x1D;
                break;
            default:
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Unhandled button event: %d",
                             event->keysym.scancode);
                return;
        }
    }

    keyCode |= 0x8000;
    flags = shouldNotConvertToScanCodeOnServer ? SS_KBE_FLAG_NON_NORMALIZED : 0;

    // Track the key state so we always know which keys are down
    if (event->state == SDL_PRESSED) {
        m_KeysDown.insert(MAKE_KEYPRESS_STATE(keyCode, modifiers, flags));
    }
    else {
        m_KeysDown.remove(MAKE_KEYPRESS_STATE(keyCode, modifiers, flags));
    }

    LiSendKeyboardEvent2(keyCode,
                         event->state == SDL_PRESSED ?
                             KEY_ACTION_DOWN : KEY_ACTION_UP,
                         modifiers,
                         flags);
}

void SdlInputHandler::raiseAllKeys()
{
    if (m_KeysDown.isEmpty()) {
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Raising %d keys",
                (int)m_KeysDown.count());

    for (auto keyDown : std::as_const(m_KeysDown)) {
        LiSendKeyboardEvent2(GET_KEYPRESS_CODE(keyDown),
                             KEY_ACTION_UP,
                             GET_KEYPRESS_EXTENDED_MODIFIER(keyDown),
                             GET_KEYPRESS_FLAGS(keyDown));
    }

    m_KeysDown.clear();
}
