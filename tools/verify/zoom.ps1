param(
    [string]$In = "D:\dev\github\winshun-dev\ra9fael.diskview\build\treemap-printwindow.png",
    [string]$Out = "D:\dev\github\winshun-dev\ra9fael.diskview\build\toolbar-zoom.png",
    [int]$X = 880, [int]$Y = 30, [int]$W = 310, [int]$H = 45, [int]$Scale = 4
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile($In)
$dst = New-Object System.Drawing.Bitmap(($W * $Scale), ($H * $Scale))
$g = [System.Drawing.Graphics]::FromImage($dst)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.DrawImage($src, (New-Object System.Drawing.Rectangle(0, 0, ($W * $Scale), ($H * $Scale))),
             (New-Object System.Drawing.Rectangle($X, $Y, $W, $H)), [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
$dst.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$dst.Dispose()
$src.Dispose()
Write-Output "saved $Out"
