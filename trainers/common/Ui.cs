// Sending MHud protocol messages to the overlay page.
// Same messages as MHud's FiveM resource (integration/mhud/client/main.lua): { action, data }.
using System;
using StreamEmber.Overlay;

namespace StreamEmber.Trainers
{
    internal static class Ui
    {
        private static readonly JsonWriter Writer = new JsonWriter();

        /// <summary>True after the page sent "ready" (MHud app.js loaded). HUD feeds wait for it.</summary>
        public static bool Ready;

        // Statistics for the performance panel
        public static long BytesSent;
        public static long MessagesSent;

        /// <summary>Starts { "action": action, "data": ... }; write the data value, then call Send().
        /// Single shared writer: everything runs on the script's Tick thread, one message at a time.</summary>
        public static JsonWriter Begin(string action)
        {
            Writer.Reset().BeginObject().Prop("action", action).Name("data");
            return Writer;
        }

        /// <summary>Closes the message started with Begin (optionally adding top-level fields first) and sends it.</summary>
        public static void Send(Action<JsonWriter> extraTopLevel = null)
        {
            extraTopLevel?.Invoke(Writer);
            Writer.EndObject();
            SendRaw(Writer.ToString());
        }

        public static void SendRaw(string json)
        {
            if (OverlayBridge.Send(json))
            {
                BytesSent += json.Length;
                MessagesSent++;
            }
        }

        /// <summary>MHud generic call: MH[fn](arg) on the page (only functions in MH.rpcAllow).</summary>
        public static void Call(string fn, Action<JsonWriter> writeArg)
        {
            Writer.Reset().BeginObject().Prop("action", "mhud").Prop("fn", fn).Name("args").BeginArray();
            writeArg(Writer);
            Writer.EndArray().EndObject();
            SendRaw(Writer.ToString());
        }

        public static void Toast(string tone, string title, string text = null, string icon = null)
        {
            Call("toast", w =>
            {
                w.BeginObject().Prop("tone", tone).Prop("title", title);
                if (text != null) w.Prop("text", text);
                if (icon != null) w.Prop("icon", icon);
                w.Prop("duration", 3500).EndObject();
            });
        }
    }
}
