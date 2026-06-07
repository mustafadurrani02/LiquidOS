$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

$root = Get-ProjectRoot
$outPath = Join-Path $root "kernel\gfx\system_icons.h"

Add-Type -AssemblyName System.Drawing

function New-IconBitmap {
    param([int]$Size)
    return New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
}

function New-IconGraphics {
    param([System.Drawing.Bitmap]$Bitmap)
    $graphics = [System.Drawing.Graphics]::FromImage($Bitmap)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    return $graphics
}

function New-RoundPen {
    param([System.Drawing.Color]$Color, [float]$Width)
    $pen = New-Object System.Drawing.Pen($Color, $Width)
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    return $pen
}

function Convert-BitmapToRows {
    param([System.Drawing.Bitmap]$Bitmap)
    $rows = New-Object System.Collections.Generic.List[string]
    for ($y = 0; $y -lt $Bitmap.Height; $y++) {
        $values = New-Object System.Collections.Generic.List[string]
        for ($x = 0; $x -lt $Bitmap.Width; $x++) {
            $p = $Bitmap.GetPixel($x, $y)
            $packed = (([uint32]$p.A -shl 24) -bor ([uint32]$p.R -shl 16) -bor ([uint32]$p.G -shl 8) -bor [uint32]$p.B)
            $values.Add(("0x{0:X8}" -f $packed))
        }
        $rows.Add("    " + ($values -join ", ") + ",")
    }
    return $rows
}

$size = 32

$wifi = New-IconBitmap $size
$g = New-IconGraphics $wifi
$white = [System.Drawing.Color]::FromArgb(255, 229, 234, 242)
$shine = [System.Drawing.Color]::FromArgb(95, 255, 255, 255)
$penOuter = New-RoundPen $white 6.0
$penInner = New-RoundPen $white 5.6
$penShine = New-RoundPen $shine 2.2
$g.DrawArc($penOuter, 2, 2, 28, 27, 218, 104)
$g.DrawArc($penInner, 8, 12, 16, 16, 218, 104)
$g.FillEllipse((New-Object System.Drawing.SolidBrush($white)), 12, 23, 8, 8)
$g.DrawArc($penShine, 6, 6, 20, 17, 220, 46)
$g.DrawArc($penShine, 11, 15, 10, 8, 220, 42)
$g.FillEllipse((New-Object System.Drawing.SolidBrush($shine)), 14, 25, 3, 3)
$g.Dispose()

$search = New-IconBitmap $size
$g = New-IconGraphics $search
$grey = [System.Drawing.Color]::FromArgb(255, 207, 209, 214)
$highlight = [System.Drawing.Color]::FromArgb(55, 255, 255, 255)
$penRing = New-RoundPen $grey 5.0
$penHandle = New-RoundPen $grey 6.2
$penHighlight = New-RoundPen $highlight 2.0
$g.DrawEllipse($penRing, 4, 4, 18, 18)
$g.DrawLine($penHandle, 20, 21, 29, 30)
$g.DrawArc($penHighlight, 7, 7, 11, 11, 208, 68)
$g.Dispose()

$header = @()
$header += "#ifndef LIQUIDOS_SYSTEM_ICONS_H"
$header += "#define LIQUIDOS_SYSTEM_ICONS_H"
$header += ""
$header += "#include <liquidos/types.h>"
$header += ""
$header += "#define SYSTEM_ICON_WIFI_WIDTH $size"
$header += "#define SYSTEM_ICON_WIFI_HEIGHT $size"
$header += "static const u32 system_icon_wifi_argb[SYSTEM_ICON_WIFI_WIDTH * SYSTEM_ICON_WIFI_HEIGHT] = {"
$header += Convert-BitmapToRows $wifi
$header += "};"
$header += ""
$header += "#define SYSTEM_ICON_SEARCH_WIDTH $size"
$header += "#define SYSTEM_ICON_SEARCH_HEIGHT $size"
$header += "static const u32 system_icon_search_argb[SYSTEM_ICON_SEARCH_WIDTH * SYSTEM_ICON_SEARCH_HEIGHT] = {"
$header += Convert-BitmapToRows $search
$header += "};"
$header += ""
$header += "#endif"
$header += ""

[System.IO.File]::WriteAllText($outPath, ($header -join "`n"), [System.Text.Encoding]::ASCII)
Write-Host "Generated LiquidOS system icons: $outPath" -ForegroundColor Green
