﻿using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

using Sce.Atf.Applications;

namespace IterativeSystemDebugger
{
    public partial class RefCFD : BaseWindow
    {
        public RefCFD(GUILayer.CFDRefIterativeSystem system)
        {
            _hasOldMouse = false;
            _system = system;
            _previewWindow.Underlying.AddSystem(_system._overlay);
            _previewSettings.SelectedObject = _system._settings;

            _schemaLoader = new CFDSettingsSchemaLoader();
            _systemSettings.Bind(
                _schemaLoader.CreatePropertyContext(_system._getAndSetProperties));

            // _previewWindow.MouseDown += _previewWindow_MouseDown;
            _previewWindow.MouseMove += _previewWindow_MouseMove;
            _previewWindow.MouseEnter += _previewWindow_MouseEnter;
        }

        void _previewWindow_MouseEnter(object sender, EventArgs e)
        {
            _hasOldMouse = false;
        }

        void _previewWindow_MouseMove(object sender, MouseEventArgs e)
        {
            if (e.Button != System.Windows.Forms.MouseButtons.None)
            {
                float swipeX = 0.0f, swipeY = 0.0f;
                if (_hasOldMouse) {
                    swipeX = (float)(e.X - _oldMouse.X);
                    swipeY = (float)(e.Y - _oldMouse.Y);
                }

                _system.OnMouseDown(
                    e.X / (float)_previewWindow.ClientSize.Width,
                    e.Y / (float)_previewWindow.ClientSize.Height,
                    swipeX, swipeY,
                    (e.Button == System.Windows.Forms.MouseButtons.Left) ? 0u
                        : (e.Button == System.Windows.Forms.MouseButtons.Middle) ? 2u
                        : 1u);
            }
            _oldMouse = new Point(e.X, e.Y);
            _hasOldMouse = true;
        }

        protected override void Dispose(bool disposing)
        {
            if (_system != null) { _system.Dispose(); _system = null; }
            base.Dispose(disposing);
        }

        protected override void DoTick()
        {
            _system.Tick();
        }

        private GUILayer.CFDRefIterativeSystem _system;
        private CFDSettingsSchemaLoader _schemaLoader;
        private Point _oldMouse;
        private bool _hasOldMouse;
    }

    public class CFDSettingsSchemaLoader : XLEBridgeUtils.DataDrivenPropertyContextHelper
    {
        public IPropertyEditingContext CreatePropertyContext(GUILayer.IGetAndSetProperties getAndSet)
        {
            var ps = new GUILayer.BasicPropertySource(
                getAndSet,
                GetPropertyDescriptors("gap:RefCFDSettings"));
            return new XLEBridgeUtils.PropertyBridge(ps);
        }

        public CFDSettingsSchemaLoader()
        {
            SchemaResolver = new Sce.Atf.ResourceStreamResolver(
                System.Reflection.Assembly.GetExecutingAssembly(), 
                "IterativeSystemDebugger.CFD");
            Load("cfd.xsd");
        }
    };
}