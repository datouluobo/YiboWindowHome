using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;

internal sealed class ForegroundFixture : Form
{
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", SetLastError = true)] private static extern bool AllowSetForegroundWindow(uint processId);

    protected override void WndProc(ref Message message)
    {
        if (message.Msg == 0x8036)
        {
            bool allowed = GetForegroundWindow() == Handle && AllowSetForegroundWindow((uint)message.WParam.ToInt64());
            message.Result = allowed ? new IntPtr(1) : IntPtr.Zero;
            return;
        }
        base.WndProc(ref message);
    }

    [STAThread]
    private static void Main()
    {
        Application.EnableVisualStyles();
        Application.Run(new ForegroundFixture { Text = "风钩前台切换测试", Width = 720, Height = 480 });
    }
}
