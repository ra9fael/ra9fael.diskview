# Capture the diskview window to a PNG, for visual verification.
# Tries PrintWindow (works offscreen) and falls back to a screen grab.
param(
    [string]$Out = "D:\dev\github\winshun-dev\ra9fael.diskview\build\window.png"
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$sig = @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WCap {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
Add-Type -TypeDefinition $sig

$target = [IntPtr]::Zero
$cb = [WCap+EnumWindowsProc]{
    param($h, $l)
    $sb = New-Object System.Text.StringBuilder 256
    [void][WCap]::GetClassName($h, $sb, 256)
    if ($sb.ToString() -eq "WinShunDiskViewWindow") { $script:target = $h; return $false }
    return $true
}
[void][WCap]::EnumWindows($cb, [IntPtr]::Zero)
if ($target -eq [IntPtr]::Zero) { Write-Output "NOWINDOW"; exit 1 }

$r = New-Object WCap+RECT
[void][WCap]::GetWindowRect($target, [ref]$r)
$w = $r.Right - $r.Left
$h = $r.Bottom - $r.Top
Write-Output ("WINDOW visible={0} rect={1},{2},{3}x{4}" -f [WCap]::IsWindowVisible($target), $r.Left, $r.Top, $w, $h)

function Save-Bmp([System.Drawing.Bitmap]$bmp, [string]$path) {
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# 1) PrintWindow (flag 2: PW_RENDERFULLCONTENT, the DWM-composited surface —
#    flag 0 relies on the window answering WM_PRINTCLIENT)
$bmp1 = New-Object System.Drawing.Bitmap($w, $h)
$g1 = [System.Drawing.Graphics]::FromImage($bmp1)
$hdc1 = $g1.GetHdc()
$ok = [WCap]::PrintWindow($target, $hdc1, 2)
$g1.ReleaseHdc($hdc1)
$g1.Dispose()
Save-Bmp $bmp1 ($Out -replace '\.png$', "-printwindow.png")
Write-Output "PRINTWINDOW ok=$ok"

# 2) screen grab of the same region
$bmp2 = New-Object System.Drawing.Bitmap($w, $h)
$g2 = [System.Drawing.Graphics]::FromImage($bmp2)
$g2.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp2.Size)
$g2.Dispose()
Save-Bmp $bmp2 ($Out -replace '\.png$', "-screen.png")
Write-Output "SCREEN ok"
