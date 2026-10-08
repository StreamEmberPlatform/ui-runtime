// Minimal, allocation-light JSON helpers for the trainer demo (no external dependencies).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace StreamEmber.TrainerDemo
{
    /// <summary>Streaming JSON writer; commas are inserted automatically. Reuse one instance with Reset().</summary>
    public sealed class JsonWriter
    {
        private readonly StringBuilder _sb = new StringBuilder(4096);
        private readonly Stack<bool> _first = new Stack<bool>();
        private bool _afterName;

        public JsonWriter Reset()
        {
            _sb.Clear();
            _first.Clear();
            _afterName = false;
            return this;
        }

        public override string ToString() => _sb.ToString();

        public JsonWriter BeginObject() { Separator(); _sb.Append('{'); _first.Push(true); return this; }
        public JsonWriter EndObject() { _first.Pop(); _sb.Append('}'); return this; }
        public JsonWriter BeginArray() { Separator(); _sb.Append('['); _first.Push(true); return this; }
        public JsonWriter EndArray() { _first.Pop(); _sb.Append(']'); return this; }

        public JsonWriter Name(string name)
        {
            Separator();
            WriteString(name);
            _sb.Append(':');
            _afterName = true;
            return this;
        }

        public JsonWriter Value(string value)
        {
            Separator();
            if (value == null) _sb.Append("null"); else WriteString(value);
            return this;
        }

        public JsonWriter Value(int value) { Separator(); _sb.Append(value.ToString(CultureInfo.InvariantCulture)); return this; }
        public JsonWriter Value(long value) { Separator(); _sb.Append(value.ToString(CultureInfo.InvariantCulture)); return this; }
        public JsonWriter Value(bool value) { Separator(); _sb.Append(value ? "true" : "false"); return this; }
        public JsonWriter Null() { Separator(); _sb.Append("null"); return this; }

        public JsonWriter Value(float value, string format = "0.####")
        {
            Separator();
            if (float.IsNaN(value) || float.IsInfinity(value)) _sb.Append('0');
            else _sb.Append(value.ToString(format, CultureInfo.InvariantCulture));
            return this;
        }

        // Shorthands for "name": value
        public JsonWriter Prop(string name, string value) => Name(name).Value(value);
        public JsonWriter Prop(string name, int value) => Name(name).Value(value);
        public JsonWriter Prop(string name, long value) => Name(name).Value(value);
        public JsonWriter Prop(string name, bool value) => Name(name).Value(value);
        public JsonWriter Prop(string name, float value, string format = "0.####") => Name(name).Value(value, format);

        private void Separator()
        {
            if (_afterName)
            {
                _afterName = false;
                return;
            }
            if (_first.Count == 0)
            {
                return;
            }
            if (_first.Peek())
            {
                _first.Pop();
                _first.Push(false);
            }
            else
            {
                _sb.Append(',');
            }
        }

        private void WriteString(string s)
        {
            _sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': _sb.Append("\\\""); break;
                    case '\\': _sb.Append("\\\\"); break;
                    case '\n': _sb.Append("\\n"); break;
                    case '\r': _sb.Append("\\r"); break;
                    case '\t': _sb.Append("\\t"); break;
                    default:
                        if (c < 0x20) _sb.Append("\\u").Append(((int)c).ToString("x4"));
                        else _sb.Append(c);
                        break;
                }
            }
            _sb.Append('"');
        }
    }

    /// <summary>Small recursive-descent JSON parser: objects become Dictionary, arrays List, numbers double.</summary>
    public static class MiniJson
    {
        public static object Parse(string json)
        {
            if (string.IsNullOrEmpty(json)) return null;
            int i = 0;
            try
            {
                object value = ParseValue(json, ref i);
                return value;
            }
            catch (FormatException)
            {
                return null;
            }
        }

        public static string Str(this IDictionary<string, object> d, string key, string fallback = null)
            => d != null && d.TryGetValue(key, out object v) && v != null ? Convert.ToString(v, CultureInfo.InvariantCulture) : fallback;

        public static double Num(this IDictionary<string, object> d, string key, double fallback = 0)
            => d != null && d.TryGetValue(key, out object v) && v is double n ? n : fallback;

        public static IDictionary<string, object> Obj(this IDictionary<string, object> d, string key)
            => d != null && d.TryGetValue(key, out object v) ? v as IDictionary<string, object> : null;

        private static object ParseValue(string s, ref int i)
        {
            SkipWs(s, ref i);
            if (i >= s.Length) throw new FormatException();
            char c = s[i];
            switch (c)
            {
                case '{': return ParseObject(s, ref i);
                case '[': return ParseArray(s, ref i);
                case '"': return ParseString(s, ref i);
                case 't': Expect(s, ref i, "true"); return true;
                case 'f': Expect(s, ref i, "false"); return false;
                case 'n': Expect(s, ref i, "null"); return null;
                default: return ParseNumber(s, ref i);
            }
        }

        private static Dictionary<string, object> ParseObject(string s, ref int i)
        {
            var result = new Dictionary<string, object>();
            i++; // {
            SkipWs(s, ref i);
            if (i < s.Length && s[i] == '}') { i++; return result; }
            while (true)
            {
                SkipWs(s, ref i);
                string key = ParseString(s, ref i);
                SkipWs(s, ref i);
                if (i >= s.Length || s[i] != ':') throw new FormatException();
                i++;
                result[key] = ParseValue(s, ref i);
                SkipWs(s, ref i);
                if (i >= s.Length) throw new FormatException();
                if (s[i] == ',') { i++; continue; }
                if (s[i] == '}') { i++; return result; }
                throw new FormatException();
            }
        }

        private static List<object> ParseArray(string s, ref int i)
        {
            var result = new List<object>();
            i++; // [
            SkipWs(s, ref i);
            if (i < s.Length && s[i] == ']') { i++; return result; }
            while (true)
            {
                result.Add(ParseValue(s, ref i));
                SkipWs(s, ref i);
                if (i >= s.Length) throw new FormatException();
                if (s[i] == ',') { i++; continue; }
                if (s[i] == ']') { i++; return result; }
                throw new FormatException();
            }
        }

        private static string ParseString(string s, ref int i)
        {
            if (i >= s.Length || s[i] != '"') throw new FormatException();
            i++;
            var sb = new StringBuilder();
            while (i < s.Length)
            {
                char c = s[i++];
                if (c == '"') return sb.ToString();
                if (c != '\\') { sb.Append(c); continue; }
                if (i >= s.Length) break;
                char e = s[i++];
                switch (e)
                {
                    case '"': sb.Append('"'); break;
                    case '\\': sb.Append('\\'); break;
                    case '/': sb.Append('/'); break;
                    case 'b': sb.Append('\b'); break;
                    case 'f': sb.Append('\f'); break;
                    case 'n': sb.Append('\n'); break;
                    case 'r': sb.Append('\r'); break;
                    case 't': sb.Append('\t'); break;
                    case 'u':
                        if (i + 4 > s.Length) throw new FormatException();
                        sb.Append((char)int.Parse(s.Substring(i, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture));
                        i += 4;
                        break;
                    default: throw new FormatException();
                }
            }
            throw new FormatException();
        }

        private static double ParseNumber(string s, ref int i)
        {
            int start = i;
            while (i < s.Length && "+-0123456789.eE".IndexOf(s[i]) >= 0) i++;
            if (i == start) throw new FormatException();
            return double.Parse(s.Substring(start, i - start), NumberStyles.Float, CultureInfo.InvariantCulture);
        }

        private static void Expect(string s, ref int i, string word)
        {
            if (string.CompareOrdinal(s, i, word, 0, word.Length) != 0) throw new FormatException();
            i += word.Length;
        }

        private static void SkipWs(string s, ref int i)
        {
            while (i < s.Length && char.IsWhiteSpace(s[i])) i++;
        }
    }
}
