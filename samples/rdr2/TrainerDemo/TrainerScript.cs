// StreamEmber Trainer Demo (RDR2) — ScriptHookRDR2DotNet script driving an MHud UI through the StreamEmber overlay.
//
//   F5            open / close the trainer menu (↑ ↓ ← → Enter Backspace, or numpad 8 2 4 6 5 0)
//   F7 / F8       overlay show/hide, mouse+keyboard to the UI (backend hotkeys, see overlay.ini)
//
// Message flow: C# -> page uses MHud's NUI protocol ({ action, data }, see MHud/integration/mhud/client/main.lua),
// page -> C# uses MHud callbacks ({ cb, data }: ready, menuSelect, menuClose) plus 'ack' for latency measurement.
// Same page and protocol as the GTA V demo (samples/gtav); only the game side differs.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Windows.Forms;
using RDR2;
using StreamEmber.Overlay;

namespace StreamEmber.TrainerDemo
{
    public sealed class TrainerScript : Script
    {
        // Game controls the menu keys would otherwise trigger: frontend navigation, wheels, journal, satchel
        private static readonly eInputType[] MenuBlockedControls =
        {
            eInputType.FrontendUp, eInputType.FrontendDown, eInputType.FrontendLeft, eInputType.FrontendRight,
            eInputType.FrontendAccept, eInputType.FrontendCancel, eInputType.OpenWheelMenu, eInputType.SelectItemWheel,
            eInputType.OpenJournal, eInputType.OpenSatchelMenu, eInputType.QuickUseItem, eInputType.Whistle,
        };

        private readonly WorldTags _tags = new WorldTags(new RdrTagWorld());
        private readonly HudFeed _hud = new HudFeed();
        private readonly MenuController _menus = new MenuController();
        private readonly Trainer _trainer;
        private readonly Stopwatch _clock = Stopwatch.StartNew();
        private readonly Stopwatch _tickTimer = new Stopwatch();
        private long _nextPerf;
        private long _lastPerf;
        private bool _helloSent;
        private long _bytesAtLastPerf;
        private long _messagesAtLastPerf;
        private double _gameFps;
        private double _avgTickMs;
        private bool _announcedNotInstalled;
        private bool _paused;   // death / respawn / loading / fade: trainer work suspended

        public TrainerScript()
        {
            _trainer = new Trainer(_tags, _hud, _menus);
            Tick += OnTick;
            KeyDown += OnKeyDown;
            Aborted += (s, e) => _trainer.Shutdown();
        }

        private void OnTick(object sender, EventArgs e)
        {
            OverlayState state = OverlayBridge.State;
            if (state != OverlayState.Ready)
            {
                if (state == OverlayState.NotInstalled && !_announcedNotInstalled)
                {
                    _announcedNotInstalled = true;
                    Native.Notify("StreamEmber Trainer: overlay kurulu değil (StreamEmber\\Overlay).");
                }
                return;
            }

            _tickTimer.Restart();
            if (!_helloSent)
            {
                // If the page was already loaded (e.g. scripts reloaded with Insert), ask it to announce itself again
                _helloSent = true;
                Ui.Begin("trainer:hello").BeginObject().EndObject();
                Ui.Send();
            }
            ReadMessages();

            Player player = Game.Player;
            Ped ped = player.Character;
            float dt = Game.FrameTime;
            if (dt > 0) _gameFps = _gameFps <= 0 ? 1.0 / dt : _gameFps * 0.95 + (1.0 / dt) * 0.05;

            // Input: UI mode (F8) owns mouse+keyboard; menu mode only blocks the menu keys
            bool uiInput = OverlayBridge.Visible && OverlayBridge.InputMode == OverlayInputMode.Ui;
            if (uiInput)
            {
                Game.DisableAllControlsThisFrame();
            }
            else if (_menus.IsOpen)
            {
                foreach (eInputType c in MenuBlockedControls) Native.DisableControl(c);
            }

            // Death, respawn, loading screens and fades: the game is streaming the world and runs its own scripted
            // sequence. World tags scan every ped/vehicle with several natives each (each one a thread hand-off in
            // ScriptHookRDRDotNet), so stay out of the way until the screen is back.
            bool busy = ped == null || !ped.Exists() || ped.IsDead || Game.IsLoading ||
                        Game.IsScreenFadedOut || Game.IsScreenFadingOut || Game.IsScreenFadingIn;
            if (busy != _paused)
            {
                _paused = busy;
                if (busy) _tags.Clear();
            }

            if (!busy) _trainer.Tick(ped, dt);

            if (Ui.Ready && !busy)
            {
                _hud.Tick(player, ped);
                _tags.Tick(Game.FrameCount);
                PublishPerf();
            }

            _tickTimer.Stop();
            _avgTickMs = _avgTickMs * 0.95 + _tickTimer.Elapsed.TotalMilliseconds * 0.05;
        }

        private void ReadMessages()
        {
            while (OverlayBridge.TryReceive(out string json))
            {
                if (!(MiniJson.Parse(json) is IDictionary<string, object> msg)) continue;
                IDictionary<string, object> data = msg.Obj("data");
                switch (msg.Str("cb"))
                {
                    case "ready":
                        // app.js loaded (first time or after a page reload)
                        Ui.Ready = true;
                        _tags.OnPageReady();
                        _hud.PushConfig();
                        _menus.Refresh();
                        break;
                    case "menuSelect":
                        object value = null;
                        data?.TryGetValue("value", out value);
                        _menus.OnSelect(data.Str("id"), value);
                        break;
                    case "menuClose":
                        _menus.OnBack();
                        break;
                    case "ack":
                        _tags.OnAck((int)data.Num("seq"), Game.FrameCount);
                        break;
                    case "atlasReady":
                        if (data != null && data.TryGetValue("s", out object pairs)) _tags.OnAtlasReady(pairs as List<object>);
                        break;
                }
            }
        }

        private void PublishPerf()
        {
            long now = _clock.ElapsedMilliseconds;
            if (now < _nextPerf) return;
            long elapsed = now - _lastPerf;
            _lastPerf = now;
            _nextPerf = now + 500;

            long bytes = Ui.BytesSent - _bytesAtLastPerf;
            long messages = Ui.MessagesSent - _messagesAtLastPerf;
            _bytesAtLastPerf = Ui.BytesSent;
            _messagesAtLastPerf = Ui.MessagesSent;
            double seconds = Math.Max(0.001, elapsed / 1000.0);

            Ui.Begin("trainer:perf").BeginObject()
                .Prop("show", _trainer.ShowPerfPanel)
                .Prop("gameFps", (float)_gameFps, "0.0")
                .Prop("tags", _tags.LastCount)
                .Prop("collectMs", (float)_tags.AvgCollectMs, "0.00")
                .Prop("tickMs", (float)_avgTickMs, "0.00")
                .Prop("tagMsgs", _tags.TagMessagesPerSecond)
                .Prop("msgs", (float)(messages / seconds), "0")
                .Prop("kbps", (float)(bytes / seconds / 1024.0), "0.0")
                .Prop("rttMs", (float)_tags.RttMs, "0.0")
                .Prop("rttFrames", (float)_tags.RttFrames, "0.0")
                .Prop("radius", (int)_tags.Radius)
                .Prop("max", _tags.MaxCount)
                .Prop("rate", _tags.RateHz)
                .Prop("mode", _tags.Positioning == WorldTags.Mode.Atlas ? "atlas" : "html")
                .Prop("delay", OverlayBridge.SpriteDelay)
                .Prop("predict", _tags.PredictFrames)
                .Prop("slots", _tags.AtlasSlots)
                .Prop("contentUpdates", _tags.ContentUpdatesPerSecond)
                .Prop("distStep", _tags.DistanceStep)
                .Prop("refs", _tags.NativeReferences)
                .Prop("spin", (int)_trainer.CameraSpinDegreesPerSecond)
                .EndObject();
            Ui.Send();
        }

        private void OnKeyDown(object sender, KeyEventArgs e)
        {
            if (e.KeyCode == Keys.F5)
            {
                if (_menus.IsOpen) _menus.Close();
                else _menus.Open(_trainer.Root);
                return;
            }

            // While the UI has real keyboard focus (F8) the page receives keys directly
            if (!_menus.IsOpen || OverlayBridge.InputMode == OverlayInputMode.Ui) return;

            string key = null;
            switch (e.KeyCode)
            {
                case Keys.Up: case Keys.NumPad8: key = "ArrowUp"; break;
                case Keys.Down: case Keys.NumPad2: key = "ArrowDown"; break;
                case Keys.Left: case Keys.NumPad4: key = "ArrowLeft"; break;
                case Keys.Right: case Keys.NumPad6: key = "ArrowRight"; break;
                case Keys.Enter: case Keys.NumPad5: key = "Enter"; break;
                case Keys.Back: case Keys.NumPad0: key = "Backspace"; break;
            }
            if (key == null) return;
            Ui.Begin("trainer:key").BeginObject().Prop("key", key).EndObject();
            Ui.Send();
        }
    }
}
