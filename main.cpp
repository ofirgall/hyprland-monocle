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
#include <hyprland/src/event/EventBus.hpp>
#include <format>
#include <set>

inline HANDLE PHANDLE = nullptr;

std::set<int>       activeWorkspaces;
CHyprSignalListener windowCloseListener;
CHyprSignalListener workspaceRemovedListener;

namespace Monocle {

template <typename... Args>
void log(Hyprutils::CLI::eLogLevel level, std::format_string<Args...> fmt, Args&&... args) {
    auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
    Log::logger->log(level, "[Monocle] {}", msg);
}

std::vector<PHLWINDOW> getWindowsOnWorkspace() {
    auto mon = Desktop::focusState()->monitor();
    if (!mon)
        return {};

    int  currentWorkspace = mon->activeWorkspaceID();

    std::vector<PHLWINDOW> windows;
    for (auto& w : g_pCompositor->m_windows) {
        if (w->workspaceID() != currentWorkspace)
            continue;
        if (!w->m_isMapped || w->m_fadingOut || w->m_readyToDelete || w->isHidden())
            continue;
        if (w->m_isFloating)
            continue;
        if (w->m_group)
            continue;
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
    auto mon = Desktop::focusState()->monitor();
    if (!mon)
        return {};

    const auto currentWindow = Desktop::focusState()->window();
    int        currentWorkspace = mon->activeWorkspaceID();

    std::vector<PHLWINDOW> windows = Monocle::getWindowsOnWorkspace();
    if (windows.size() < 2)
        return {};

    activeWorkspaces.insert(currentWorkspace);

    auto firstWindow = windows[0];
    if (!firstWindow->m_group)
        Desktop::View::CGroup::create({firstWindow});

    for (size_t i = 1; i < windows.size(); i++) {
        auto window1 = windows[i - 1];
        auto window2 = windows[i];
        Desktop::focusState()->fullWindowFocus(window2, Desktop::FOCUS_REASON_OTHER);
        Monocle::moveWindowIntoGroup(window2, window1);
    }

    if (currentWindow && !currentWindow->m_fadingOut)
        Desktop::focusState()->fullWindowFocus(currentWindow, Desktop::FOCUS_REASON_OTHER);
    return {};
}

SDispatchResult monocleOff(std::string arg) {
    auto mon = Desktop::focusState()->monitor();
    if (!mon)
        return {};

    int currentWorkspace = mon->activeWorkspaceID();
    activeWorkspaces.erase(currentWorkspace);

    const auto w = Desktop::focusState()->window();
    if (w && w->m_group)
        w->m_group->destroy();

    return {};
}

SDispatchResult monocleToggle(std::string arg) {
    auto mon = Desktop::focusState()->monitor();
    if (!mon)
        return {};

    int currentWorkspace = mon->activeWorkspaceID();
    if (activeWorkspaces.count(currentWorkspace))
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

    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH) {
        throw std::runtime_error("[Monocle] Version mismatch: server=" + HASH + " client=" + CLIENT_HASH);
    }

    HyprlandAPI::addDispatcherV2(PHANDLE, "monocle:on", monocleOn);
    HyprlandAPI::addDispatcherV2(PHANDLE, "monocle:off", monocleOff);
    HyprlandAPI::addDispatcherV2(PHANDLE, "monocle:toggle", monocleToggle);

    windowCloseListener = Event::bus()->m_events.window.close.listen([](PHLWINDOW w) {
        if (!w || !w->m_workspace)
            return;
        int wsid = w->workspaceID();
        if (!activeWorkspaces.count(wsid))
            return;
        // If the group this window belongs to is about to have <= 1 member,
        // Hyprland will dissolve it. Track that by clearing our state.
        if (w->m_group && w->m_group->size() <= 2)
            activeWorkspaces.erase(wsid);
    });

    workspaceRemovedListener = Event::bus()->m_events.workspace.removed.listen([](PHLWORKSPACEREF ws) {
        if (auto locked = ws.lock())
            activeWorkspaces.erase(locked->m_id);
    });

    return {"Monocle", "Groups all tiled windows on a workspace into a single tab group", "ofirgall", "1.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    windowCloseListener.reset();
    workspaceRemovedListener.reset();
    activeWorkspaces.clear();
}
