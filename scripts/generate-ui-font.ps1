param(
    [string]$FontPath = "",
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

$root = Get-ProjectRoot
$outPath = Join-Path $root "kernel\gfx\ui_font.h"

if (-not $FontPath) {
    if ($env:LIQUIDOS_UI_FONT -and (Test-Path $env:LIQUIDOS_UI_FONT)) {
        $FontPath = $env:LIQUIDOS_UI_FONT
    }
    else {
        $candidates = @(
            (Join-Path $root "assets\fonts\Manrope-VariableFont_wght.ttf"),
            (Join-Path $root "assets\fonts\HelveticaNeueMedium.otf"),
            (Join-Path $root "assets\fonts\HelveticaNeueRoman.otf"),
            (Join-Path $root "assets\fonts\WorkSans[wght].ttf")
        )
        foreach ($candidate in $candidates) {
            if (Test-Path $candidate) {
                $FontPath = $candidate
                break
            }
        }
    }
}

if (-not $FontPath -or -not (Test-Path $FontPath)) {
    if (-not $Quiet) {
        Write-Host "UI font not found. Put Manrope-VariableFont_wght.ttf in assets\fonts or set LIQUIDOS_UI_FONT." -ForegroundColor Yellow
    }
    exit 0
}

try {
    Add-Type -AssemblyName System.Drawing

    $privateFonts = New-Object System.Drawing.Text.PrivateFontCollection
    $privateFonts.AddFontFile((Resolve-Path $FontPath))
    $family = $privateFonts.Families[0]

    $first = 32
    $last = 126
    $width = 34
    $height = 30
    $packedWidth = [int][Math]::Ceiling($width / 2.0)
    $nativeScale = 2
    $fontSize = 23
    $font = New-Object System.Drawing.Font($family, $fontSize, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $format = [System.Drawing.StringFormat]::GenericTypographic
    $format.FormatFlags = $format.FormatFlags -bor [System.Drawing.StringFormatFlags]::MeasureTrailingSpaces

    $glyphs = New-Object System.Collections.Generic.List[string]
    $widths = New-Object System.Collections.Generic.List[int]

    for ($code = $first; $code -le $last; $code++) {
        $bitmap = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

        $text = [string][char]$code
        $measure = $graphics.MeasureString($text, $font, 200, $format)
        $graphics.DrawString($text, $font, [System.Drawing.Brushes]::White, 1, -4, $format)

        $rightmost = -1
        for ($y = 0; $y -lt $height; $y++) {
            for ($x = 0; $x -lt $width; $x++) {
                if ($bitmap.GetPixel($x, $y).A -gt 8 -and $x -gt $rightmost) {
                    $rightmost = $x
                }
            }
        }

        $advance = [int][Math]::Ceiling($measure.Width) + 2
        if ($rightmost -ge 0) {
            $advance = [Math]::Max($advance, $rightmost + 3)
        }
        if ($code -eq 32) {
            $advance = [int][Math]::Ceiling($fontSize * 0.34)
        }
        if ($advance -lt 4) {
            $advance = 4
        }
        if ($advance -gt $width) {
            $advance = $width
        }
        $widths.Add($advance)

        $glyphRows = New-Object System.Collections.Generic.List[string]
        for ($y = 0; $y -lt $height; $y++) {
            $values = New-Object System.Collections.Generic.List[string]
            for ($x = 0; $x -lt $width; $x += 2) {
                $a0 = [int][Math]::Min(15, [Math]::Round($bitmap.GetPixel($x, $y).A / 17.0))
                $a1 = 0
                if ($x + 1 -lt $width) {
                    $a1 = [int][Math]::Min(15, [Math]::Round($bitmap.GetPixel($x + 1, $y).A / 17.0))
                }
                $values.Add(("0x{0:X2}" -f (($a0 -shl 4) -bor $a1)))
            }
            $glyphRows.Add("        { " + ($values -join ", ") + " },")
        }
        $glyphs.Add("    {`n" + ($glyphRows -join "`n") + "`n    },")

        $graphics.Dispose()
        $bitmap.Dispose()
    }

    $widthLines = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $widths.Count; $i += 24) {
        $chunk = $widths[$i..([Math]::Min($i + 23, $widths.Count - 1))] | ForEach-Object { "$_" }
        $widthLines.Add("    " + ($chunk -join ", ") + ",")
    }

    $fontName = $family.Name.Replace("\", "\\").Replace("""", "\""")
    $header = @()
    $header += "#ifndef LIQUIDOS_UI_FONT_H"
    $header += "#define LIQUIDOS_UI_FONT_H"
    $header += ""
    $header += "#include <liquidos/types.h>"
    $header += ""
    $header += "#define UI_FONT_FIRST $first"
    $header += "#define UI_FONT_LAST $last"
    $header += "#define UI_FONT_WIDTH $width"
    $header += "#define UI_FONT_HEIGHT $height"
    $header += "#define UI_FONT_PACKED_WIDTH $packedWidth"
    $header += "#define UI_FONT_NATIVE_SCALE $nativeScale"
    $header += "#define UI_FONT_NAME `"$fontName`""
    $header += ""
    $header += "static const u8 ui_font_widths[UI_FONT_LAST - UI_FONT_FIRST + 1] = {"
    $header += $widthLines
    $header += "};"
    $header += ""
    $header += "static const u8 ui_font_alpha[UI_FONT_LAST - UI_FONT_FIRST + 1][UI_FONT_HEIGHT][UI_FONT_PACKED_WIDTH] = {"
    $header += $glyphs
    $header += "};"
    $header += ""
    $header += "#endif"
    $header += ""

    [System.IO.File]::WriteAllText($outPath, ($header -join "`n"), [System.Text.Encoding]::ASCII)
    if (-not $Quiet) {
        Write-Host "Generated $fontName UI font: $outPath" -ForegroundColor Green
    }
}
catch {
    if (-not $Quiet) {
        Write-Host "Could not generate UI font: $($_.Exception.Message)" -ForegroundColor Yellow
    }
    exit 0
}
