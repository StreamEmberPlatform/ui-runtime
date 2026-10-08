// Menu tree rendered by MHud's classic menu (app.js 'mhud:menu'); selections come back as 'menuSelect' / 'menuClose'.
using System;
using System.Collections.Generic;

namespace StreamEmber.TrainerDemo
{
    internal sealed class MenuItem
    {
        public string Id;
        public string Label;
        public string Desc;
        public string Right;               // text on the right (price, value)
        public bool? Check;                // checkbox item
        public string[] Options;           // stepper item (←/→ choose, Enter applies)
        public int Index;                  // current option index
        public bool Disabled;              // greyed out, not selectable
        public Menu Submenu;               // opens on select (shows a chevron)
        public Action<MenuItem> OnSelect;  // called after Check/Index were updated
    }

    internal sealed class Menu
    {
        public string Title;
        public string Subtitle;
        public Menu Parent;
        public readonly List<MenuItem> Items = new List<MenuItem>();

        public Menu(string title, string subtitle)
        {
            Title = title;
            Subtitle = subtitle;
        }

        public MenuItem Add(MenuItem item)
        {
            if (item.Id == null) item.Id = "i" + Items.Count;
            if (item.Submenu != null) item.Submenu.Parent = this;
            Items.Add(item);
            return item;
        }

        public MenuItem Action(string label, string desc, Action<MenuItem> onSelect, string right = null)
            => Add(new MenuItem { Label = label, Desc = desc, OnSelect = onSelect, Right = right });

        public MenuItem Toggle(string label, string desc, bool value, Action<MenuItem> onSelect)
            => Add(new MenuItem { Label = label, Desc = desc, Check = value, OnSelect = onSelect });

        public MenuItem Choice(string label, string desc, string[] options, int index, Action<MenuItem> onSelect)
            => Add(new MenuItem { Label = label, Desc = desc, Options = options, Index = index, OnSelect = onSelect });

        public MenuItem Sub(string label, string desc, Menu submenu)
            => Add(new MenuItem { Label = label, Desc = desc, Submenu = submenu });
    }

    internal sealed class MenuController
    {
        private Menu _current;

        public bool IsOpen => _current != null;

        public void Open(Menu menu)
        {
            _current = menu;
            var w = Ui.Begin("mhud:menu").BeginObject().Prop("open", true).Name("menu").BeginObject()
                .Prop("title", menu.Title).Prop("subtitle", menu.Subtitle).Name("items").BeginArray();
            foreach (MenuItem it in menu.Items)
            {
                w.BeginObject().Prop("id", it.Id).Prop("label", it.Label);
                if (it.Desc != null) w.Prop("desc", it.Desc);
                if (it.Check.HasValue) w.Prop("check", it.Check.Value);
                if (it.Options != null)
                {
                    w.Name("options").BeginArray();
                    foreach (string o in it.Options) w.Value(o);
                    w.EndArray().Prop("index", it.Index);
                }
                if (it.Right != null) w.Prop("right", it.Right);
                if (it.Submenu != null) w.Prop("chevron", true);
                if (it.Disabled) w.Prop("disabled", true);
                w.EndObject();
            }
            w.EndArray().EndObject().EndObject();
            Ui.Send();
        }

        public void Close()
        {
            if (_current == null) return;
            _current = null;
            Ui.Begin("mhud:menu").BeginObject().Prop("open", false).EndObject();
            Ui.Send();
        }

        /// <summary>Re-sends the current menu (e.g. after the page reloaded).</summary>
        public void Refresh()
        {
            if (_current != null) Open(_current);
        }

        public void OnSelect(string id, object value)
        {
            if (_current == null) return;
            MenuItem item = _current.Items.Find(x => x.Id == id);
            if (item == null || item.Disabled) return;

            if (item.Submenu != null)
            {
                Open(item.Submenu);
                return;
            }
            if (item.Check.HasValue && value is bool b)
            {
                item.Check = b;
            }
            if (item.Options != null && value is string s)
            {
                int i = Array.IndexOf(item.Options, s);
                if (i >= 0) item.Index = i;
            }
            item.OnSelect?.Invoke(item);
        }

        /// <summary>Backspace / Escape: go to the parent menu or close.</summary>
        public void OnBack()
        {
            if (_current?.Parent != null) Open(_current.Parent);
            else Close();
        }
    }
}
