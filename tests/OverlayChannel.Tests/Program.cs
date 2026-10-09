using System;
using System.Linq;
using System.Collections.Generic;
using System.Web.Script.Serialization;
using StreamEmber.Overlay;

static class Program
{
    static readonly JavaScriptSerializer Json = new JavaScriptSerializer();
    static int checks;
    static void Check(bool condition, string name) { if (!condition) throw new Exception(name); checks++; Console.WriteLine("PASS " + name); }
    static void Main()
    {
        var a = new OverlayChannel("rumble"); var b = new OverlayChannel("trainer");
        a.LoadUrl("https://example.invalid/rumble"); b.LoadUrl("https://example.invalid/trainer");
        a.Send("{\"beforeReady\":true}");
        OverlayBridge.Incoming.Enqueue("{\"surface\":\"ready\"}"); var state = a.State;
        var commands = OverlayBridge.Commands.Select(s => Json.Deserialize<Dictionary<string,object>>(s)).ToArray();
        Check(commands.Count(d => (string)d["op"] == "register") == 2, "both pages registered on one surface");
        Check(commands.Any(d => (string)d["op"] == "send"), "pre-ready message preserved");
        string epoch = (string)commands.First()["epoch"];
        OverlayBridge.Incoming.Enqueue(Json.Serialize(new { surface="message", epoch, id="trainer", json="trainer-message" }));
        Check(!a.TryReceive(out var ignored) && b.TryReceive(out var own) && own == "trainer-message", "inboxes isolated");
        OverlayBridge.Incoming.Enqueue(Json.Serialize(new { surface="message", epoch="stale", id="rumble", json="stale" }));
        Check(!a.TryReceive(out ignored), "stale domain messages ignored");
        a.InputMode = OverlayInputMode.Ui; b.InputMode = OverlayInputMode.Ui;
        Check(a.InputMode == OverlayInputMode.Game && b.InputMode == OverlayInputMode.Ui && a.TryReceive(out own) && own.Contains("close"), "focus transfer closes previous menu");
        a.InputMode = OverlayInputMode.Game;
        Check(b.InputMode == OverlayInputMode.Ui, "old owner cannot release new owner input");
        a.Focus(false);
        Check(OverlayBridge.InputMode == OverlayInputMode.Game && b.TryReceive(out own) && own.Contains("close"), "keyboard-only menu still has exclusive ownership");
        b.InputMode = OverlayInputMode.Ui;
        var layout = new AtlasLayout { SlotWidth=280, SlotHeight=84, Columns=6, Rows=2 };
        a.SetAtlasLayout(layout); b.SetAtlasLayout(layout);
        var sprites = new[] { new OverlaySprite { Slot=0, X=.5f, Y=.5f, Scale=1, Alpha=1 } };
        a.SubmitSprites(sprites, 1); b.SubmitSprites(sprites, 1);
        Check(OverlayBridge.Sprites.Length == 2 && OverlayBridge.Sprites[0].Slot != OverlayBridge.Sprites[1].Slot, "sprite slots do not collide");
        a.Visible = false;
        Check(OverlayBridge.Sprites.Length == 1 && b.Visible, "hiding one HUD preserves another");
        a.DisableAtlas(); b.SubmitSprites(sprites, 1);
        Check(b.GetAtlasLayout().IsEnabled && OverlayBridge.Sprites.Length == 1, "one client cannot disable another atlas");
        a.Dispose(); b.SubmitSprites(sprites, 1);
        Check(b.InputMode == OverlayInputMode.Ui && OverlayBridge.Sprites.Length == 1, "disposing a client preserves another");
        for (int i=0; i<300; i++) OverlayBridge.Incoming.Enqueue(Json.Serialize(new {surface="message",epoch,id="trainer",json=i.ToString()}));
        state=b.State; state=b.State; state=b.State;
        int count=0; while(b.TryReceive(out own)) count++;
        Check(count==256, "per-client inbox stays bounded");
        b.Dispose(); Check(OverlayBridge.InputMode == OverlayInputMode.Game, "disposing focused client restores game input");
        Console.WriteLine(checks + " checks passed");
    }
}
