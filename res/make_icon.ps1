# make_icon.ps1 - generate res\app.ico (rounded green square + white lightning bolt)
#
# Notes:
#   * windres cannot read PNG-compressed .ico entries, so every size is stored
#     as a classic 32bpp DIB (BITMAPINFOHEADER + bottom-up pixels + AND mask).
#   * Sizes 16..128 are enough for taskbar / Alt-Tab / Explorer; skipping 256
#     keeps the exe small (the whole ico is a few dozen KB).
# Re-run to regenerate:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\res\make_icon.ps1
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

# 画一张 size x size 的图：绿色圆角方块 + 白色闪电
function New-IconBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap -ArgumentList $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $r = [Math]::Max(2, [int][Math]::Round($size * 0.24))
    $rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, ($size - 1), ($size - 1)
    $d = $r * 2
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
    $path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
    $path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
    $path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    $c1 = [System.Drawing.Color]::FromArgb(255, 46, 164, 76)    # top-left green
    $c2 = [System.Drawing.Color]::FromArgb(255, 14, 118, 92)    # bottom-right teal
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush -ArgumentList $rect, $c1, $c2, 55.0
    $g.FillPath($brush, $path)

    # 白色闪电（归一化坐标）
    $raw = @(
        @(0.52, 0.06), @(0.20, 0.55), @(0.42, 0.55),
        @(0.36, 0.94), @(0.74, 0.42), @(0.51, 0.42)
    )
    $pts = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
    foreach ($p in $raw) {
        $pts.Add((New-Object System.Drawing.PointF -ArgumentList ([float]($p[0] * $size)), ([float]($p[1] * $size))))
    }
    $g.FillPolygon([System.Drawing.Brushes]::White, $pts.ToArray())

    $g.Dispose()
    $brush.Dispose()
    $path.Dispose()
    return $bmp
}

# 把 Bitmap 编码成 ico 条目要求的 DIB 结构
function ConvertTo-IconDib($bmp, [int]$size) {
    $rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $size, $size
    $locked = $bmp.LockBits($rect,
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $locked.Stride
    $pixels = New-Object byte[] ($stride * $size)
    [System.Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $pixels, 0, $pixels.Length)
    $bmp.UnlockBits($locked)

    $maskStride = [int]([Math]::Floor(($size + 31) / 32) * 4)
    $mask = New-Object byte[] ($maskStride * $size)      # 全 0 = 按 alpha 混合

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)
    # BITMAPINFOHEADER，高度写两倍（XOR 图 + AND 掩码）
    $bw.Write([UInt32]40)
    $bw.Write([Int32]$size)
    $bw.Write([Int32]($size * 2))
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]0)                                  # BI_RGB
    $bw.Write([UInt32]($size * $size * 4))
    $bw.Write([Int32]0); $bw.Write([Int32]0)
    $bw.Write([UInt32]0); $bw.Write([UInt32]0)
    # 像素自下而上
    for ($y = $size - 1; $y -ge 0; $y--) { $bw.Write($pixels, $y * $stride, $size * 4) }
    $bw.Write($mask)
    $bw.Flush()
    return ,$ms.ToArray()
}

$dest  = Join-Path $PSScriptRoot "app.ico"
$sizes = 16, 24, 32, 48, 64, 128

$blobs = @{}
foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    $blobs[$s] = ConvertTo-IconDib $bmp $s
    $bmp.Dispose()
}

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$bw.Write([UInt16]0)                       # reserved
$bw.Write([UInt16]1)                       # type: icon
$bw.Write([UInt16]$sizes.Count)

$offset = 6 + 16 * $sizes.Count
foreach ($s in $sizes) {
    if ($s -ge 256) { $b = [Byte]0 } else { $b = [Byte]$s }
    $bw.Write($b); $bw.Write($b)           # width / height (0 means 256)
    $bw.Write([Byte]0); $bw.Write([Byte]0) # palette count / reserved
    $bw.Write([UInt16]1)                   # color planes
    $bw.Write([UInt16]32)                  # bits per pixel
    $bw.Write([UInt32]$blobs[$s].Length)
    $bw.Write([UInt32]$offset)
    $offset += $blobs[$s].Length
}
foreach ($s in $sizes) { $bw.Write([Byte[]]$blobs[$s]) }
$bw.Flush()

[System.IO.File]::WriteAllBytes($dest, $ms.ToArray())
$bw.Dispose()
$ms.Dispose()

foreach ($s in $sizes) {
    Write-Host ("  {0,4}px -> {1,7} bytes" -f $s, @($blobs[$s]).Count)
}
Write-Host ("[make_icon] OK: {0}  ({1} sizes, {2} KB)" -f $dest, $sizes.Count, [math]::Round((Get-Item $dest).Length / 1KB, 1))
