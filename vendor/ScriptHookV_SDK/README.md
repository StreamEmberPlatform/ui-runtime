# ScriptHookV SDK (subset)

`inc/main.h` and `lib/ScriptHookV.lib` from the ScriptHookV SDK by Alexander Blade
(http://www.dev-c.com/gtav/scripthookv/), the same subset that ScriptHookVDotNet and gtav-runtime-scripthook keep
in their repositories. Only the GTA V backend (`StreamEmber.Overlay.GTAV.asi`) uses it: it registers ScriptHookV's
`IDXGISwapChain::Present` callback. Nothing from this folder is shipped; players install ScriptHookV themselves.
