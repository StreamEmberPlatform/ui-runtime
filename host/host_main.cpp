// StreamEmber.Overlay.Host.exe — CEF sub-process (renderer, GPU, utility processes).
//
// The renderer part injects `window.streamember` into every main frame:
//   streamember.post(obj | string)  -> message to the game (SEO_PollFromUi)
//   streamember.on(fn) -> unsubscribe  <- messages from the game (SEO_PostToUi)
// A page crash only kills this process; CEF restarts it and the core reloads the page.
#include <windows.h>

#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_v8.h"

namespace {

constexpr size_t kMaxMessageBytes = 1 << 20;

// Defines the public API on top of the native __seNativePost function.
const char kBootstrapJs[] = R"JS(
(function () {
  var child = window.parent !== window;
  var nativePost = child ? function (json) { parent.postMessage({ sePost: json }, '*'); } : window.__seNativePost;
  var geometry = { screenHeight: innerHeight, atlasOffset: 0 };
  var listeners = [];
  var api = {
    get screenHeight() { return geometry.screenHeight; },
    get atlasOffset() { return geometry.atlasOffset; },
    post: function (message) {
      nativePost(typeof message === 'string' ? message : JSON.stringify(message));
    },
    on: function (listener) {
      if (typeof listener !== 'function') { return function () {}; }
      listeners.push(listener);
      return function () {
        var i = listeners.indexOf(listener);
        if (i >= 0) { listeners.splice(i, 1); }
      };
    },
    _dispatch: function (json) {
      var message;
      try { message = JSON.parse(json); } catch (e) { console.error('streamember: invalid JSON from game', e); return; }
      var snapshot = listeners.slice();
      for (var i = 0; i < snapshot.length; i++) {
        try { snapshot[i](message); } catch (e) { console.error('streamember listener error', e); }
      }
    }
  };
  Object.defineProperty(window, 'streamember', { value: Object.freeze(api), enumerable: false });
  if (child) {
    window.addEventListener('message', function (event) {
      if (event.source !== parent || !event.data) return;
      var d = event.data;
      if (typeof d.seDispatch === 'string') api._dispatch(d.seDispatch);
      if (d.seFocus) window.focus();
      if (d.seLayout) {
        geometry.screenHeight = d.screenHeight; geometry.atlasOffset = d.atlasOffset;
        window.dispatchEvent(new Event('resize'));
        // Keep every HUD inside the actual game viewport even though the atlas extends the browser below it.
        var zoom = parseFloat(document.documentElement.style.zoom) || 1;
        document.querySelectorAll('.mh-screen').forEach(function (s) { s.style.height = (d.screenHeight / zoom) + 'px'; s.style.bottom = 'auto'; });
      }
    });
    window.addEventListener('DOMContentLoaded', function () { parent.postMessage({ seReady: true }, '*'); });
  }
})();
)JS";

class PostHandler : public CefV8Handler {
 public:
  bool Execute(const CefString& name,
               CefRefPtr<CefV8Value> object,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& retval,
               CefString& exception) override {
    if (arguments.size() != 1 || !arguments[0]->IsString()) {
      exception = "streamember.post expects one string or object";
      return true;
    }
    const std::string json = arguments[0]->GetStringValue().ToString();
    if (json.empty()) {
      exception = "streamember.post: empty message";
      return true;
    }
    if (json.size() > kMaxMessageBytes) {
      exception = "streamember.post: message larger than 1 MiB";
      return true;
    }
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
    if (!frame) {
      return true;
    }
    CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("se.post");
    message->GetArgumentList()->SetString(0, json);
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

 private:
  IMPLEMENT_REFCOUNTING(PostHandler);
};

class HostApp : public CefApp, public CefRenderProcessHandler {
 public:
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }

  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override {
    CefRefPtr<CefV8Value> global = context->GetGlobal();
    if (frame->IsMain()) global->SetValue("__seNativePost", CefV8Value::CreateFunction("__seNativePost", new PostHandler()),
                     V8_PROPERTY_ATTRIBUTE_DONTENUM);

    CefRefPtr<CefV8Value> result;
    CefRefPtr<CefV8Exception> error;
    if (!context->Eval(kBootstrapJs, CefString(), 0, result, error)) {
      // Shows up in Overlay.log through the core's console handler
      const std::string line = error ? std::to_string(error->GetLineNumber()) : "?";
      frame->ExecuteJavaScript("console.error('streamember bridge bootstrap failed at line " + line + "');",
                               frame->GetURL(), 0);
    }
  }

 private:
  IMPLEMENT_REFCOUNTING(HostApp);
};

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  CefMainArgs mainArgs(instance);
  CefRefPtr<HostApp> app(new HostApp());
  // Runs the sub-process and returns its exit code. This exe is never the browser process.
  return CefExecuteProcess(mainArgs, app, nullptr);
}
