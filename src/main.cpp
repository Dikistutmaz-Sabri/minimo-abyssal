// minimo-abyssal: a window as the live wallpaper, for Hyprland 0.57.2 (Minimo OS).
// The window of .../minimo-abyssal/web/wallpaper.html opened by Chrome with --app (see ../run.sh):
//   - never takes the keyboard, is not animated, goes to the hidden workspace special:minimo-abyssal
//     (so the dock, the overview, alt-tab and minimo-desks do not list it) and is hidden (so it
//     takes no clicks, and minimo-genie leaves it alone);
//   - is drawn by this plugin over the static wallpaper layer and under everything else, on the
//     monitor it opened on, on every desk;
//   - is told it is "suspended" (Chrome stops drawing) while it cannot be seen: a fullscreen window
//     or one covering the screen, the screen locked or off, or `hyprctl abyssal pause`.
// If it closes or crashes, the static wallpaper under it is simply seen again.
//   hyprctl abyssal pause|resume|status

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/view/window/WindowMetadata.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/ipc/s1/S1.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <sstream>

static HANDLE                           PHANDLE = nullptr;
static const std::string                CLASS   = "minimo-abyssal";
static PHLWINDOWREF                     g_wall;
static PHLMONITORREF                    g_mon;
static bool                             g_paused = false;
static CHyprSignalListener              g_commit;
static SP<CEventLoopTimer>              g_timer;
static int                              g_said = -1; // last state written for the page
static std::vector<CHyprSignalListener> g_listeners;
static SP<IPC::Socket1::SCommand>       g_cmd;

// renderWindow is protected: reached through a derived class's using-declaration (no object is made)
struct SRendererAccess : Render::IHyprRenderer {
    using Render::IHyprRenderer::renderWindow;
};
static constexpr auto RENDER_WINDOW = static_cast<void (Render::IHyprRenderer::*)(Render::CRenderContext&, PHLWINDOW, PHLMONITOR, const Render::SWindowRenderPresentation&,
                                                                                   const Time::steady_tp&, bool, Render::eRenderPassMode, bool, bool)>(&SRendererAccess::renderWindow);

// Chrome on Wayland ignores --class: an --app window's id is "chrome-<its file path, _ for />-<profile>"
static bool isWall(PHLWINDOW w) {
    const auto& ID = w ? w->metadata().appID() : std::string{};
    return ID.starts_with("chrome-") && ID.contains(CLASS) && ID.contains("wallpaper.html");
}

// can the wallpaper be seen on this monitor now?
static bool seen(PHLMONITOR mon) {
    if (g_paused || !mon->m_dpmsStatus || (g_pSessionLockManager && g_pSessionLockManager->isSessionLocked()))
        return false;
    const auto WS = mon->m_activeWorkspace;
    if (!WS)
        return true;
    if (Fullscreen::controller()->hasFullscreen(WS, true))
        return false;
    // a window covering (almost) all of it
    const CBox   MON{mon->m_position, mon->m_size};
    const double AREA = MON.w * MON.h;
    for (const auto& w : Desktop::windowState()->windows()) {
        if (!w || w == g_wall.lock() || !w->mapped() || w->isHidden() || w->m_workspace != WS)
            continue;
        const auto B = w->getFullWindowBoundingBox().intersection(MON);
        if (B.w * B.h >= AREA * 0.95)
            return false;
    }
    return true;
}

// Chrome keeps drawing when told it is suspended, so the page itself is told too: web/wallpaper.js
// reads this file and holds its frames while it says 0.
static std::string stateFile() {
    const char* rt = getenv("XDG_RUNTIME_DIR");
    return std::string{rt ? rt : "/tmp"} + "/minimo-abyssal.state";
}

static void say(bool draw) {
    if (g_said == sc<int>(draw))
        return;
    g_said           = draw;
    const auto PATH  = stateFile();
    const auto TMP   = PATH + ".new";
    if (FILE* f = fopen(TMP.c_str(), "w")) {
        fputs(draw ? "1\n" : "0\n", f);
        fclose(f);
        rename(TMP.c_str(), PATH.c_str());
    }
}

// full size, edge to edge: no gaps on its workspace. The rule counts when the workspace is made,
// so the window hops out and back in (a config reload drops the rule: then it is all done again).
static void fit(PHLWINDOW w, PHLMONITOR mon) {
    const auto A = std::format("address:0x{:x}", rc<uintptr_t>(w.get()));
    HyprlandAPI::invokeHyprctlCommand("eval", "hl.workspace_rule({ workspace = \"special:minimo-abyssal\", gaps_out = 0 })");
    for (const auto& d : {std::format("hl.dsp.window.float({{ action = \"set\", window = \"{}\" }})", A),
                          std::format("hl.dsp.window.move({{ workspace = \"special:minimo-abyssal-hop\", window = \"{}\", follow = false }})", A),
                          std::format("hl.dsp.window.move({{ workspace = \"special:minimo-abyssal\", window = \"{}\", follow = false }})", A),
                          std::format("hl.dsp.window.resize({{ x = {}, y = {}, window = \"{}\" }})", mon->m_size.x, mon->m_size.y, A)})
        HyprlandAPI::invokeHyprctlCommand("dispatch", d);
}

// twice a second: a fullscreen window, the lock or the screen going off may come with no frame at all
static void check(SP<CEventLoopTimer> self, void*) {
    const auto W = g_wall.lock();
    const auto M = g_mon.lock();
    if (W && W->mapped() && M) {
        // the monitor changed size or scale, or a config reload moved it: fit it again
        if (const auto SZ = W->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT); std::abs(SZ.x - M->m_size.x) > 1 || std::abs(SZ.y - M->m_size.y) > 1)
            fit(W, M);
        if (!W->isHidden())
            W->setHidden(true); // a monitor change can show it again
        const bool SEEN = seen(M);
        W->setSuspended(!SEEN);
        say(SEEN);
    }
    self->updateTimeout(std::chrono::milliseconds(500));
}

static void adopt(PHLWINDOW w) {
    const auto MON = w->m_monitor.lock();
    if (!MON)
        return;
    g_wall = w;
    g_said = -1;
    g_mon  = MON;
    fit(w, MON);
    w->setHidden(true);
    // each new frame of it: the background changed
    if (const auto S = w->wlSurface() ? w->wlSurface()->resource() : nullptr)
        g_commit = S->m_events.commit.listen([] {
            if (const auto M = g_mon.lock())
                g_pHyprRenderer->damageMonitor(M);
        });
    g_pHyprRenderer->damageMonitor(MON);
}

static void onRenderStage(const Event::SRenderStageEvent& ev) {
    const auto W = g_wall.lock();
    if (ev.stage != RENDER_POST_WALLPAPER || !ev.context || !W || !W->mapped() || ev.monitor != g_mon)
        return;
    const auto MON  = ev.monitor;
    const bool SEEN = seen(MON);
    W->setSuspended(!SEEN); // Hyprland suspends it (hidden): undone while it can be seen
    // its last frame is drawn even when it is paused: the edges of a covering window show it
    (g_pHyprRenderer.get()->*RENDER_WINDOW)(ev.context->get(), W, MON, W->presentation().renderPresentation(), Time::steadyNow(), false, Render::RENDER_PASS_ALL, true, true);
    if (SEEN)
        W->wlSurface()->resource()->breadthfirst([](SP<CWLSurfaceResource> s, const Vector2D&, void*) { s->frame(Time::steadyNow()); }, nullptr);
}

static std::string onCommand(const std::string& line) {
    std::istringstream is(line);
    std::string        cmd, verb;
    is >> cmd >> verb;
    if (verb == "pause" || verb == "resume") {
        g_paused = verb == "pause";
        if (g_timer)
            check(g_timer, nullptr); // at once
        if (const auto M = g_mon.lock())
            g_pHyprRenderer->damageMonitor(M);
    } else if (verb != "status")
        return "usage: hyprctl abyssal pause|resume|status";
    const auto W = g_wall.lock();
    const auto M = g_mon.lock();
    return std::format("window {} paused {} drawing {}", W ? "yes" : "no", g_paused ? "yes" : "no", W && M && seen(M) ? "yes" : "no");
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;
    if (std::string{__hyprland_api_get_hash()} != __hyprland_api_get_client_hash()) {
        HyprlandAPI::addNotification(PHANDLE, "[minimo-abyssal] built for another Hyprland: rebuild it (make)", CHyprColor{1.0, 0.3, 0.3, 1.0}, 8000);
        throw std::runtime_error("[minimo-abyssal] version mismatch");
    }

    auto& E = Event::bus()->m_events;
    g_listeners.push_back(E.window.open.listen([](PHLWINDOW w) {
        if (!isWall(w))
            return;
        // before Hyprland focuses or animates it
        w->m_ruleApplicator->noFocus().matchOptional(true, Desktop::Types::PRIORITY_SET_PROP);
        w->m_ruleApplicator->noAnim().matchOptional(true, Desktop::Types::PRIORITY_SET_PROP);
    }));
    g_listeners.push_back(E.window.openLate.listen([](PHLWINDOW w) {
        if (isWall(w))
            adopt(w);
    }));
    g_listeners.push_back(E.window.close.listen([](PHLWINDOW w) {
        if (w == g_wall.lock()) {
            g_commit.reset();
            if (const auto M = g_mon.lock())
                g_pHyprRenderer->damageMonitor(M); // the static wallpaper again
        }
    }));
    g_listeners.push_back(E.render.stage.listen([](const Event::SRenderStageEvent& ev) { onRenderStage(ev); }));
    g_timer = makeShared<CEventLoopTimer>(std::chrono::milliseconds(500), check, nullptr);
    g_pEventLoopManager->addTimer(g_timer);

    // loaded while it is already open (a plugin reload)
    for (const auto& w : Desktop::windowState()->windows())
        if (w && w->mapped() && isWall(w))
            adopt(w);

    g_cmd = HyprlandAPI::registerHyprCtlCommand(PHANDLE, IPC::Socket1::SCommand{
                                                             .name    = "abyssal",
                                                             .match   = IPC::Socket1::COMMAND_MATCH_PREFIX,
                                                             .handler = [](const IPC::Socket1::SRequest& r) -> IPC::Socket1::SResponse { return onCommand(r.command); },
                                                         });
    return {"minimo-abyssal", "a window as the live wallpaper (Abyssal Garden)", "minimo", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_listeners.clear();
    g_commit.reset();
    if (g_cmd)
        HyprlandAPI::unregisterHyprCtlCommand(PHANDLE, g_cmd);
    if (g_timer) {
        g_timer->cancel();
        g_pEventLoopManager->removeTimer(g_timer);
    }
    unlink(stateFile().c_str()); // no file: the page draws
    if (const auto M = g_mon.lock())
        g_pHyprRenderer->damageMonitor(M);
}
