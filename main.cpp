#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/render/decorations/CHyprGroupBarDecoration.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Group.hpp>
#include <hyprland/src/helpers/math/Direction.hpp>
#include <format>

inline HANDLE PHANDLE = nullptr;

std::vector<int> workspaces;

namespace Monocle {

template <typename... Args>
void log(Hyprutils::CLI::eLogLevel level, std::format_string<Args...> fmt, Args&&... args) {
    auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
    Log::logger->log(level, "[Monocle] {}", msg);
}

std::vector<PHLWINDOW> getWindowsOnWorkspace() {
    std::vector<PHLWINDOW> windows = {};

    for (auto& w : g_pCompositor->m_windows) {
        int workspaceID = w->workspaceID();
        int currentWorkspace = Desktop::focusState()->monitor()->activeWorkspaceID();
        if (workspaceID == currentWorkspace)
            windows.push_back(w);
    }

    return windows;
}

void moveWindowIntoGroup(PHLWINDOW pWindow, PHLWINDOW pWindowInDirection) {
    if (pWindow->m_groupRules & Desktop::View::GROUP_DENY)
        return;

    static auto USECURRPOS = CConfigValue<Hyprlang::INT>("group:insert_after_current");
    pWindowInDirection     = *USECURRPOS ? pWindowInDirection : pWindowInDirection->m_group->tail();

    pWindowInDirection->m_group->add(pWindow);
    pWindow->m_group->setCurrent(pWindow);
    pWindow->updateWindowDecos();
    g_layoutManager->recalculateMonitor(Desktop::focusState()->monitor());
    Desktop::focusState()->fullWindowFocus(pWindow, Desktop::FOCUS_REASON_OTHER);
    g_pCompositor->warpCursorTo(pWindow->middle());

    if (!pWindow->getDecorationByType(DECORATION_GROUPBAR))
        pWindow->addWindowDeco(makeUnique<CHyprGroupBarDecoration>(pWindow));
}

void moveIntoGroup(std::string args) {
    char        arg = args[0];

    static auto PIGNOREGROUPLOCK = CConfigValue<Hyprlang::INT>("binds:ignore_group_lock");

    if (!*PIGNOREGROUPLOCK && g_pKeybindManager->m_groupsLocked)
        return;

    if (!isDirection(args)) {
        Log::logger->log(Log::ERR, "Cannot move into group in direction {}, unsupported direction. Supported: l,r,u/t,d/b", arg);
        return;
    }

    const auto PWINDOW = Desktop::focusState()->window();

    if (!PWINDOW || PWINDOW->m_isFloating || (PWINDOW->m_groupRules & Desktop::View::GROUP_DENY))
        return;

    auto PWINDOWINDIR = g_pCompositor->getWindowInDirection(PWINDOW, Math::fromChar(arg));

    if (!PWINDOWINDIR || !PWINDOWINDIR->m_group)
        return;

    if (!*PIGNOREGROUPLOCK && (PWINDOWINDIR->m_group->locked() || (PWINDOW->m_group && PWINDOW->m_group->locked())))
        return;

    moveWindowIntoGroup(PWINDOW, PWINDOWINDIR);
}

}

SDispatchResult monocleOn(std::string arg) {
    const auto currentWindow = Desktop::focusState()->window();

    int currentWorkspace = Desktop::focusState()->monitor()->activeWorkspaceID();
    workspaces.push_back(currentWorkspace);

    std::vector<PHLWINDOW> windows = Monocle::getWindowsOnWorkspace();
    if (windows.empty())
        return {};

    auto firstWindow = windows[0];
    if (!firstWindow->m_group)
        Desktop::View::CGroup::create({firstWindow});

    for (size_t i = 1; i < windows.size(); i++) {
        auto window1 = windows[i-1];
        auto window2 = windows[i];
        Desktop::focusState()->fullWindowFocus(window2, Desktop::FOCUS_REASON_OTHER);
        Monocle::moveWindowIntoGroup(window2, window1);
    }

    Desktop::focusState()->fullWindowFocus(currentWindow, Desktop::FOCUS_REASON_OTHER);
    return {};
}

SDispatchResult monocleOff(std::string arg) {
    int currentWorkspace = Desktop::focusState()->monitor()->activeWorkspaceID();
    size_t toRemove = -1;
    for (size_t i = 0; i < workspaces.size(); i++) {
        if (workspaces[i] == currentWorkspace)
            toRemove = i;
    }
    if (toRemove != (size_t)-1)
        workspaces.erase(workspaces.begin() + toRemove);

    if (Desktop::focusState()->window()->m_group)
        HyprlandAPI::invokeHyprctlCommand("dispatch", "togglegroup");
    return {};
}

SDispatchResult monocleToggle(std::string arg) {
    int currentWorkspace = Desktop::focusState()->monitor()->activeWorkspaceID();
    if (std::find(workspaces.begin(), workspaces.end(), currentWorkspace) != workspaces.end())
        return monocleOff("");
    else
        return monocleOn("");
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();

    // ALWAYS add this to your plugins. It will prevent random crashes coming from
    // mismatched header versions.
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH) {
        throw std::runtime_error("[Monocle] Version mismatch: server=" + HASH + " client=" + CLIENT_HASH);
    }

    HyprlandAPI::addDispatcherV2(PHANDLE, "monocle:on", monocleOn);
    HyprlandAPI::addDispatcherV2(PHANDLE, "monocle:off", monocleOff);
    HyprlandAPI::addDispatcherV2(PHANDLE, "monocle:toggle", monocleToggle);

    return {"MyPlugin", "An amazing plugin that is going to change the world!", "Me", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // ...
}
