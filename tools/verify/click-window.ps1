param(
    [string]$Out = "D:\dev\github\winshun-dev\ra9fael.diskview\build\click.png",
    [int]$ClickX = 200,
    [int]$ClickY = 250
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$sig = @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WClick {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
Add-Type -TypeDefinition $sig

$target = [IntPtr]::Zero
$cb = [WClick+EnumWindowsProc]{
    param($h, $l)
    $sb = New-Object System.Text.StringBuilder 256
    [void][WClick]::GetClassName($h, $sb, 256)
    if ($sb.ToString() -eq "WinShunDiskViewWindow") { $script:target = $h; return $false }
    return $true
}
[void][WClick]::EnumWindows($cb, [IntPtr]::Zero)
if ($target -eq [IntPtr]::Zero) { Write-Output "NOWINDOW"; exit 1 }

$r = New-Object WClick+RECT
[void][WClick]::GetWindowRect($target, [ref]$r)
$w = $r.Right - $r.Left
$h = $r.Bottom - $r.Top
Write-Output ("WINDOW rect={0},{1},{2}x{3}" -f $r.Left, $r.Top, $w, $h)

# A left click on the chart: WM_LBUTTONDOWN then WM_LBUTTONUP (the window
# handles the button-up). lParam packs the client coordinates.
$lp = [IntPtr](($ClickY -shl 16) -bor ($ClickX -band 0xFFFF))
[void][WClick]::PostMessage($target, 0x0201, [IntPtr]1, $lp)   # WM_LBUTTONDOWN
[void][WClick]::PostMessage($target, 0x0202, [IntPtr]0, $lp)   # WM_LBUTTONUP
Write-Output "CLICK sent at $ClickX,$ClickY"
Start-Sleep -Milliseconds 600

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [WClick]::PrintWindow($target, $hdc, 0)
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "PRINTWINDOW ok=$ok saved $Out"
