using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Forms = System.Windows.Forms;
using Wpf = System.Windows;
using Interop = System.Windows.Interop;
[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8")]

// API compatibility fixture only; no controls or interactive test steps.
class FrameworkFixture {
    class PassiveForm : Forms.Form { protected override bool ShowWithoutActivation { get { return true; } } }
    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] static extern IntPtr GetWindowLongPtr(IntPtr window, int index);
    [DllImport("user32.dll", EntryPoint="SetWindowLongPtrW")] static extern IntPtr SetWindowLongPtr(IntPtr window, int index, IntPtr value);
    [StructLayout(LayoutKind.Sequential)] struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out Rect rect);
    static void Publish(string file, IntPtr window) {
        SetWindowLongPtr(window, -20, new IntPtr(GetWindowLongPtr(window, -20).ToInt64() | 0x08000000L));
        File.WriteAllText(file, window.ToInt64() + " " + Process.GetCurrentProcess().Id, new UTF8Encoding(false));
    }
    [STAThread] static int Main(string[] args) {
        Forms.Application.EnableVisualStyles();
        Forms.Application.SetCompatibleTextRenderingDefault(false);
        if(args.Length != 2) return 2;
        SetProcessDpiAwarenessContext(new IntPtr(-4));
        var work = Forms.Screen.PrimaryScreen.WorkingArea;
        if(args[0] == "wpf") {
            var app = new Wpf.Application(); app.ShutdownMode = Wpf.ShutdownMode.OnExplicitShutdown;
            var window = new Wpf.Window(); window.Title = "YiboWindowHome WPF fixture";
            window.Width = 640; window.Height = 420; window.Left = work.Left + 100; window.Top = work.Top + 100;
            window.ShowActivated = false; window.ShowInTaskbar = false;
            var handle = new Interop.WindowInteropHelper(window).EnsureHandle();
            var source = Interop.HwndSource.FromHwnd(handle);
            source.AddHook(delegate(IntPtr hwnd, int message, IntPtr wp, IntPtr lp, ref bool handled) {
                if(message == 0x10) app.Dispatcher.BeginInvoke(new Action(delegate { app.Shutdown(); }));
                return IntPtr.Zero;
            });
            SetWindowLongPtr(handle, -20, new IntPtr(GetWindowLongPtr(handle, -20).ToInt64() | 0x08000000L));
            window.Show(); window.UpdateLayout();
            // Loaded/idle alone can precede the first render/DPI settling work.
            // Publish only after three responsive dispatcher samples with stable
            // native bounds; the caller must not race framework initialization.
            var timer = new System.Windows.Threading.DispatcherTimer(System.Windows.Threading.DispatcherPriority.ApplicationIdle);
            timer.Interval = TimeSpan.FromMilliseconds(100); Rect previous = new Rect(); int stable = 0;
            timer.Tick += delegate {
                Rect current; bool measured = GetWindowRect(handle, out current);
                bool equal = measured && current.Left == previous.Left && current.Top == previous.Top && current.Right == previous.Right && current.Bottom == previous.Bottom;
                stable = window.IsLoaded && window.IsVisible && equal ? stable + 1 : 0;
                previous = current;
                if(stable >= 3) { timer.Stop(); Publish(args[1], handle); }
            };
            timer.Start();
            app.Run(); return 0;
        }
        if(args[0] == "winforms" || args[0] == "fixed") {
            var form = new PassiveForm(); form.Text = "YiboWindowHome WinForms fixture";
            form.StartPosition = Forms.FormStartPosition.Manual; form.SetBounds(work.Left + 100, work.Top + 100, 640, 420);
            form.ShowInTaskbar = false;
            if(args[0] == "fixed") { form.FormBorderStyle = Forms.FormBorderStyle.FixedDialog; form.MaximizeBox = false; form.MinimumSize = form.Size; form.MaximumSize = form.Size; }
            form.FormClosed += delegate { Forms.Application.ExitThread(); };
            var handle = form.Handle;
            SetWindowLongPtr(handle, -20, new IntPtr(GetWindowLongPtr(handle, -20).ToInt64() | 0x08000000L));
            form.Show(); Publish(args[1], handle); Forms.Application.Run(); form.Dispose(); return 0;
        }
        return 2;
    }
}
