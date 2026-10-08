// StreamEmber Overlay core: runs CEF (Chromium Embedded Framework) windowless inside the game process and exposes
// the rendered page as BGRA frames plus a JSON message bridge through the C ABI in include/se_overlay.h.
//
// Threads:
//   - "CEF main" thread (ours): CefInitialize ... CefShutdown. multi_threaded_message_loop = true, so CEF runs
//     its own UI thread and we only wait here.
//   - CEF UI thread: browser callbacks (OnPaint, OnAfterCreated, process messages) and every task we post.
//   - Callers (game render thread, script threads): only touch atomics/mutex-protected state and post tasks.
#include "se_overlay.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_task.h"

#include "overlay_log.h"

namespace seo {
namespace {

constexpr size_t kMaxInboxMessages = 1024;
constexpr size_t kMaxMessageBytes = 1 << 20;  // 1 MiB per message
constexpr size_t kMaxPendingToUi = 256;      // game -> UI messages kept while the page is loading

// Everything is heap allocated and never freed: the game process may exit without SEO_Shutdown and running CEF
// destructors after libcef is gone would crash on exit.
struct Shared {
  std::atomic<bool> started{false};  // one-shot: CEF cannot be initialized twice in a process
  std::atomic<int> state{SEO_STATE_STOPPED};
  std::atomic<int> visible{1};
  std::atomic<int> inputMode{SEO_INPUT_GAME};
  std::atomic<int> viewWidth{1280};
  std::atomic<int> viewHeight{720};

  std::wstring baseDir;
  std::string startUrl;
  int frameRate = 60;

  std::mutex frameMutex;
  std::vector<uint8_t> pixels;
  int frameWidth = 0;
  int frameHeight = 0;
  uint64_t frameSerial = 0;

  std::mutex inboxMutex;
  std::deque<std::string> inbox;

  std::mutex browserMutex;
  CefRefPtr<CefBrowser> browser;

  // Guards CefPostTask against CefShutdown: posters hold it shared, shutdown takes it exclusively once.
  std::shared_mutex postGate;
  bool acceptingTasks = false;

  // Game -> UI messages are held until the main frame finished loading (listeners exist by then)
  std::mutex outboxMutex;
  bool pageReady = false;
  std::deque<std::string> pendingToUi;

  std::mutex lifeMutex;
  std::condition_variable lifeCv;
  bool shutdownRequested = false;
  bool browserClosed = false;
  std::thread cefThread;
};

Shared& S() {
  static Shared* shared = new Shared();
  return *shared;
}

CefRefPtr<CefBrowser> GetBrowser() {
  std::lock_guard<std::mutex> lock(S().browserMutex);
  return S().browser;
}

class FnTask : public CefTask {
 public:
  explicit FnTask(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Execute() override {
    if (fn_) {
      fn_();
    }
  }

 private:
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(FnTask);
};

bool IsShutdownRequested() {
  std::lock_guard<std::mutex> lock(S().lifeMutex);
  return S().shutdownRequested;
}

// Posts to the CEF UI thread unless CEF is not running (yet / anymore). Never calls CEF after CefShutdown.
void PostUi(std::function<void()> fn) {
  std::shared_lock<std::shared_mutex> lock(S().postGate);
  if (!S().acceptingTasks || S().state.load() != SEO_STATE_READY) {
    return;
  }
  CefPostTask(TID_UI, new FnTask(std::move(fn)));
}

// UI thread only
void ExecuteDispatch(CefRefPtr<CefBrowser> browser, const std::string& code) {
  browser->GetMainFrame()->ExecuteJavaScript(code, "streamember://bridge", 0);
}

// Encodes text as a JavaScript string literal (with quotes).
std::string ToJsStringLiteral(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 16);
  out.push_back('"');
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '<': out += "\\u003c"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(text[i + 2]) == 0xA8 ||
                    static_cast<unsigned char>(text[i + 2]) == 0xA9)) {
          // U+2028 / U+2029 are line terminators in older JS string literals
          out += (static_cast<unsigned char>(text[i + 2]) == 0xA8) ? "\\u2028" : "\\u2029";
          i += 2;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
  return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Browser process app: command line switches
// ---------------------------------------------------------------------------------------------------------------
class BrowserApp : public CefApp {
 public:
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override {
    if (!process_type.empty()) {
      return;
    }
    // CPU (OnPaint) path for now; the GPU shared-texture path comes later (see README, phase 2b).
    command_line->AppendSwitch("disable-gpu");
    command_line->AppendSwitch("disable-gpu-compositing");
    command_line->AppendSwitch("allow-file-access-from-files");
    command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
    command_line->AppendSwitchWithValue("disable-features", "HardwareMediaKeyHandling");
  }

 private:
  IMPLEMENT_REFCOUNTING(BrowserApp);
};

// ---------------------------------------------------------------------------------------------------------------
// Browser client
// ---------------------------------------------------------------------------------------------------------------
class OverlayClient : public CefClient,
                      public CefLifeSpanHandler,
                      public CefRenderHandler,
                      public CefDisplayHandler,
                      public CefLoadHandler,
                      public CefRequestHandler {
 public:
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

  // CefLifeSpanHandler
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    {
      std::lock_guard<std::mutex> lock(S().browserMutex);
      S().browser = browser;
    }
    if (IsShutdownRequested()) {
      browser->GetHost()->CloseBrowser(true);  // shutdown started while the browser was being created
      return;
    }
    // READY first so that SetVisible/SetInputMode calls racing with us are not dropped by PostUi,
    // then apply the current values (both calls are idempotent).
    S().state.store(SEO_STATE_READY);
    browser->GetHost()->WasHidden(S().visible.load() == 0);
    browser->GetHost()->SetFocus(S().inputMode.load() == SEO_INPUT_UI);
    LogInfo("Browser created.");
  }

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    {
      std::lock_guard<std::mutex> lock(S().browserMutex);
      S().browser = nullptr;
    }
    {
      std::lock_guard<std::mutex> lock(S().lifeMutex);
      S().browserClosed = true;
    }
    S().lifeCv.notify_all();
    LogInfo("Browser closed.");
  }

  // CefRenderHandler
  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override {
    rect = CefRect(0, 0, std::max(1, S().viewWidth.load()), std::max(1, S().viewHeight.load()));
  }

  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirtyRects,
               const void* buffer,
               int width,
               int height) override {
    if (type != PET_VIEW || buffer == nullptr || width <= 0 || height <= 0) {
      return;  // popups (<select> dropdowns) are not composited yet
    }
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    std::lock_guard<std::mutex> lock(S().frameMutex);
    if (S().pixels.size() != bytes) {
      S().pixels.resize(bytes);
    }
    std::memcpy(S().pixels.data(), buffer, bytes);
    S().frameWidth = width;
    S().frameHeight = height;
    S().frameSerial++;
  }

  // CefDisplayHandler
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                        cef_log_severity_t level,
                        const CefString& message,
                        const CefString& source,
                        int line) override {
    const char* lvl = level >= LOGSEVERITY_ERROR ? "JS-ERROR" : (level >= LOGSEVERITY_WARNING ? "JS-WARN" : "JS");
    Log(lvl, message.ToString() + " (" + source.ToString() + ":" + std::to_string(line) + ")");
    return true;
  }

  // CefLoadHandler
  void OnLoadStart(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   TransitionType transition_type) override {
    if (frame->IsMain()) {
      std::lock_guard<std::mutex> lock(S().outboxMutex);
      S().pageReady = false;
    }
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override {
    if (!frame->IsMain()) {
      return;
    }
    std::deque<std::string> pending;
    {
      std::lock_guard<std::mutex> lock(S().outboxMutex);
      S().pageReady = true;
      pending.swap(S().pendingToUi);
    }
    for (const std::string& code : pending) {
      ExecuteDispatch(browser, code);
    }
    LogInfo("Page loaded (HTTP " + std::to_string(httpStatusCode) + ")" +
            (pending.empty() ? "." : ", delivered " + std::to_string(pending.size()) + " queued message(s)."));
  }

  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode,
                   const CefString& errorText,
                   const CefString& failedUrl) override {
    if (errorCode == ERR_ABORTED) {
      return;
    }
    LogError("Load failed (" + std::to_string(static_cast<int>(errorCode)) + " " + errorText.ToString() +
             "): " + failedUrl.ToString());
  }

  // CefRequestHandler
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override {
    LogError("Render process terminated (status " + std::to_string(static_cast<int>(status)) + ", code " +
             std::to_string(error_code) + "). Reloading in 1 s.");
    CefRefPtr<CefBrowser> keep = browser;
    CefPostDelayedTask(TID_UI, new FnTask([keep]() { keep->Reload(); }), 1000);
  }

  // CefClient
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override {
    if (message->GetName() != "se.post") {
      return false;
    }
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args->GetSize() < 1 || args->GetType(0) != VTYPE_STRING) {
      return true;
    }
    std::string json = args->GetString(0).ToString();
    if (json.empty()) {
      return true;  // SEO_PollFromUi uses 0 for "empty queue"
    }
    if (json.size() > kMaxMessageBytes) {
      LogWarn("Dropped a UI message larger than 1 MiB.");
      return true;
    }
    std::lock_guard<std::mutex> lock(S().inboxMutex);
    if (S().inbox.size() >= kMaxInboxMessages) {
      S().inbox.pop_front();
      LogWarn("UI message queue is full; dropped the oldest message (is the game polling SEO_PollFromUi?).");
    }
    S().inbox.push_back(std::move(json));
    return true;
  }

 private:
  IMPLEMENT_REFCOUNTING(OverlayClient);
};

void CreateBrowserOnUiThread() {
  CefWindowInfo windowInfo;
  windowInfo.SetAsWindowless(nullptr);

  CefBrowserSettings browserSettings;
  browserSettings.windowless_frame_rate = S().frameRate;
  browserSettings.background_color = CefColorSetARGB(0, 0, 0, 0);  // transparent page background

  std::string url = S().startUrl;
  if (url.empty()) {
    std::wstring path = S().baseDir + L"\\ui\\index.html";
    std::replace(path.begin(), path.end(), L'\\', L'/');
    url = "file:///" + ToUtf8(path);
  }
  LogInfo("Creating browser: " + url);
  if (!CefBrowserHost::CreateBrowser(windowInfo, new OverlayClient(), url, browserSettings, nullptr, nullptr)) {
    LogError("CefBrowserHost::CreateBrowser failed.");
    S().state.store(SEO_STATE_FAILED);
  }
}

void CefThreadMain() {
  // libcef.dll is delay-loaded (CEF's linker flags). A delay-load uses the normal DLL search order, which starts at
  // the game folder, not ours. Load it explicitly by full path first; later lookups by name find the loaded module.
  const std::wstring libcefPath = S().baseDir + L"\\libcef.dll";
  if (LoadLibraryExW(libcefPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) == nullptr) {
    LogError("Could not load " + ToUtf8(libcefPath) + " (error " + std::to_string(GetLastError()) + ").");
    S().state.store(SEO_STATE_FAILED);
    return;
  }

  CefMainArgs mainArgs(GetModuleHandleW(nullptr));

  CefSettings settings;
  settings.no_sandbox = true;
  settings.multi_threaded_message_loop = true;
  settings.windowless_rendering_enabled = true;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  settings.log_severity = LOGSEVERITY_WARNING;
  const std::wstring& base = S().baseDir;
  CefString(&settings.browser_subprocess_path) = base + L"\\StreamEmber.Overlay.Host.exe";
  CefString(&settings.resources_dir_path) = base;
  CefString(&settings.locales_dir_path) = base + L"\\locales";
  CefString(&settings.root_cache_path) = base + L"\\cache";
  CefString(&settings.cache_path) = base + L"\\cache\\default";
  CefString(&settings.log_file) = base + L"\\logs\\cef.log";

  LogInfo("CefInitialize...");
  if (!CefInitialize(mainArgs, settings, new BrowserApp(), nullptr)) {
    LogError("CefInitialize failed (exit code " + std::to_string(CefGetExitCode()) + "). See logs\\cef.log.");
    S().state.store(SEO_STATE_FAILED);
    return;
  }
  LogInfo("CEF initialized.");
  {
    std::unique_lock<std::shared_mutex> gate(S().postGate);
    S().acceptingTasks = true;
  }

  CefPostTask(TID_UI, new FnTask(&CreateBrowserOnUiThread));

  // Wait until SEO_Shutdown()
  {
    std::unique_lock<std::mutex> lock(S().lifeMutex);
    S().lifeCv.wait(lock, [] { return S().shutdownRequested; });
  }

  LogInfo("Shutting down CEF...");
  {
    // Waits for in-flight PostUi calls; nothing is posted by callers after this point.
    std::unique_lock<std::shared_mutex> gate(S().postGate);
    S().acceptingTasks = false;
    S().state.store(SEO_STATE_STOPPED);
  }
  CefRefPtr<CefBrowser> browser = GetBrowser();
  if (browser) {
    CefPostTask(TID_UI, new FnTask([browser]() { browser->GetHost()->CloseBrowser(true); }));
    browser = nullptr;
  }
  // If the browser was still being created, OnAfterCreated closes it; either way OnBeforeClose signals us.
  {
    std::unique_lock<std::mutex> lock(S().lifeMutex);
    if (!S().lifeCv.wait_for(lock, std::chrono::seconds(3), [] { return S().browserClosed; })) {
      LogWarn("Browser did not close within 3 s; shutting down anyway.");
    }
  }
  CefShutdown();
  LogInfo("CEF shut down.");
}

}  // namespace
}  // namespace seo

using namespace seo;

extern "C" {

SEO_API int32_t SEO_CALL SEO_GetApiVersion(void) {
  return SEO_API_VERSION;
}

SEO_API int32_t SEO_CALL SEO_Initialize(const SEO_InitParams* params) {
  if (params == nullptr || params->structSize < sizeof(SEO_InitParams) || params->baseDir == nullptr) {
    return 0;
  }
  if (S().started.exchange(true)) {
    return 0;  // one-shot: already started, failed or shut down
  }
  S().state.store(SEO_STATE_STARTING);

  S().baseDir = params->baseDir;
  S().startUrl = (params->startUrl != nullptr) ? params->startUrl : "";
  S().frameRate = (params->frameRate <= 0 || params->frameRate > 60) ? 60 : params->frameRate;
  if (params->width > 0 && params->height > 0) {
    S().viewWidth.store(params->width);
    S().viewHeight.store(params->height);
  }

  LogOpen(S().baseDir + L"\\logs");
  LogInfo("StreamEmber Overlay core starting (API " + std::to_string(SEO_API_VERSION) + ", view " +
          std::to_string(S().viewWidth.load()) + "x" + std::to_string(S().viewHeight.load()) + ").");

  S().cefThread = std::thread(&CefThreadMain);
  return 1;
}

SEO_API int32_t SEO_CALL SEO_GetState(void) {
  return S().state.load();
}

SEO_API void SEO_CALL SEO_Shutdown(void) {
  {
    std::lock_guard<std::mutex> lock(S().lifeMutex);
    if (S().shutdownRequested) {
      return;
    }
    S().shutdownRequested = true;
  }
  S().lifeCv.notify_all();
  if (S().cefThread.joinable()) {
    S().cefThread.join();
  }
}

SEO_API void SEO_CALL SEO_Resize(int32_t width, int32_t height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  const bool widthChanged = S().viewWidth.exchange(width) != width;
  const bool heightChanged = S().viewHeight.exchange(height) != height;
  if (!widthChanged && !heightChanged) {
    return;
  }
  PostUi([]() {
    if (CefRefPtr<CefBrowser> browser = GetBrowser()) {
      browser->GetHost()->WasResized();
    }
  });
}

SEO_API int32_t SEO_CALL SEO_AcquireFrame(SEO_Frame* outFrame, uint64_t lastSerial) {
  if (outFrame == nullptr) {
    return 0;
  }
  S().frameMutex.lock();
  if (S().frameSerial == lastSerial || S().pixels.empty()) {
    S().frameMutex.unlock();
    return 0;
  }
  outFrame->pixels = S().pixels.data();
  outFrame->width = S().frameWidth;
  outFrame->height = S().frameHeight;
  outFrame->stride = S().frameWidth * 4;
  outFrame->serial = S().frameSerial;
  return 1;  // stays locked until SEO_ReleaseFrame
}

SEO_API void SEO_CALL SEO_ReleaseFrame(void) {
  S().frameMutex.unlock();
}

SEO_API void SEO_CALL SEO_SetVisible(int32_t visible) {
  const int value = visible ? 1 : 0;
  if (S().visible.exchange(value) == value) {
    return;
  }
  // Hidden browsers stop painting, which saves CPU while the overlay is off.
  PostUi([value]() {
    if (CefRefPtr<CefBrowser> browser = GetBrowser()) {
      browser->GetHost()->WasHidden(value == 0);
    }
  });
}

SEO_API int32_t SEO_CALL SEO_IsVisible(void) {
  return S().visible.load();
}

SEO_API void SEO_CALL SEO_SetInputMode(int32_t mode) {
  const int value = (mode == SEO_INPUT_UI) ? SEO_INPUT_UI : SEO_INPUT_GAME;
  if (S().inputMode.exchange(value) == value) {
    return;
  }
  SEO_SetFocus(value == SEO_INPUT_UI ? 1 : 0);
}

SEO_API int32_t SEO_CALL SEO_GetInputMode(void) {
  return S().inputMode.load();
}

SEO_API void SEO_CALL SEO_SendMouseMove(int32_t x, int32_t y, uint32_t modifiers, int32_t mouseLeave) {
  PostUi([=]() {
    if (CefRefPtr<CefBrowser> browser = GetBrowser()) {
      CefMouseEvent e;
      e.x = x;
      e.y = y;
      e.modifiers = modifiers;
      browser->GetHost()->SendMouseMoveEvent(e, mouseLeave != 0);
    }
  });
}

SEO_API void SEO_CALL SEO_SendMouseButton(int32_t x, int32_t y, uint32_t modifiers, int32_t button, int32_t mouseUp,
                                          int32_t clickCount) {
  PostUi([=]() {
    if (CefRefPtr<CefBrowser> browser = GetBrowser()) {
      CefMouseEvent e;
      e.x = x;
      e.y = y;
      e.modifiers = modifiers;
      const cef_mouse_button_type_t type =
          button == SEO_MOUSE_RIGHT ? MBT_RIGHT : (button == SEO_MOUSE_MIDDLE ? MBT_MIDDLE : MBT_LEFT);
      browser->GetHost()->SendMouseClickEvent(e, type, mouseUp != 0, std::max(1, static_cast<int>(clickCount)));
    }
  });
}

SEO_API void SEO_CALL SEO_SendMouseWheel(int32_t x, int32_t y, uint32_t modifiers, int32_t deltaX, int32_t deltaY) {
  PostUi([=]() {
    if (CefRefPtr<CefBrowser> browser = GetBrowser()) {
      CefMouseEvent e;
      e.x = x;
      e.y = y;
      e.modifiers = modifiers;
      browser->GetHost()->SendMouseWheelEvent(e, deltaX, deltaY);
    }
  });
}

SEO_API void SEO_CALL SEO_SendKey(int32_t type, int32_t windowsKeyCode, int32_t nativeKeyCode, uint32_t modifiers,
                                  uint16_t character, int32_t isSystemKey) {
  PostUi([=]() {
    if (CefRefPtr<CefBrowser> browser = GetBrowser()) {
      CefKeyEvent e;
      e.type = type == SEO_KEY_CHAR ? KEYEVENT_CHAR : (type == SEO_KEY_KEYUP ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN);
      e.windows_key_code = windowsKeyCode;
      e.native_key_code = nativeKeyCode;
      e.modifiers = modifiers;
      e.character = static_cast<char16_t>(character);
      e.unmodified_character = static_cast<char16_t>(character);
      e.is_system_key = isSystemKey != 0;
      browser->GetHost()->SendKeyEvent(e);
    }
  });
}

SEO_API void SEO_CALL SEO_SetFocus(int32_t focused) {
  PostUi([focused]() {
    if (CefRefPtr<CefBrowser> browser = GetBrowser()) {
      browser->GetHost()->SetFocus(focused != 0);
    }
  });
}

SEO_API void SEO_CALL SEO_PostToUi(const char* utf8Json) {
  if (utf8Json == nullptr) {
    return;
  }
  std::string code = "window.streamember&&window.streamember._dispatch(" + ToJsStringLiteral(utf8Json) + ");";
  {
    // Before the page has loaded (or while it reloads) keep the message; OnLoadEnd delivers it.
    std::lock_guard<std::mutex> lock(S().outboxMutex);
    if (!S().pageReady) {
      if (S().pendingToUi.size() >= kMaxPendingToUi) {
        S().pendingToUi.pop_front();
      }
      S().pendingToUi.push_back(std::move(code));
      return;
    }
  }
  PostUi([code]() {
    if (CefRefPtr<CefBrowser> browser = GetBrowser()) {
      ExecuteDispatch(browser, code);
    }
  });
}

SEO_API int32_t SEO_CALL SEO_PollFromUi(char* buffer, int32_t bufferSize) {
  std::lock_guard<std::mutex> lock(S().inboxMutex);
  if (S().inbox.empty()) {
    return 0;
  }
  const std::string& message = S().inbox.front();
  const int32_t required = static_cast<int32_t>(message.size()) + 1;
  if (buffer == nullptr || bufferSize < required) {
    return -required;
  }
  std::memcpy(buffer, message.data(), message.size());
  buffer[message.size()] = '\0';
  const int32_t length = static_cast<int32_t>(message.size());
  S().inbox.pop_front();
  return length;
}

SEO_API void SEO_CALL SEO_Log(const char* utf8Message) {
  if (utf8Message != nullptr) {
    Log("EXT", utf8Message);
  }
}

}  // extern "C"
