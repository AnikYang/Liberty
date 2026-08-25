$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$outputPath = Join-Path (Split-Path -Parent $PSScriptRoot) "assets\liberty-trinity.ico"
$sizes = @(16, 20, 32, 48, 64, 128, 256)
$images = New-Object System.Collections.Generic.List[byte[]]

foreach ($size in $sizes) {
    $bitmap = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $scale = $size / 256.0
    $penWidth = [Math]::Max(2.0, 22.0 * $scale)
    $colors = @(
        [System.Drawing.Color]::FromArgb(255, 79, 140, 255),
        [System.Drawing.Color]::FromArgb(255, 99, 91, 255),
        [System.Drawing.Color]::FromArgb(255, 32, 199, 217)
    )
    $paths = @(
        @{ X = 38; Y = 31; W = 180; H = 194; Start = -90; Sweep = 96 },
        @{ X = 31; Y = 38; W = 194; H = 180; Start = 0; Sweep = 96 },
        @{ X = 38; Y = 31; W = 180; H = 194; Start = 90; Sweep = 96 }
    )
    for ($index = 0; $index -lt 3; $index++) {
        $pen = New-Object System.Drawing.Pen($colors[$index], $penWidth)
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $path = $paths[$index]
        $graphics.DrawArc($pen, $path.X * $scale, $path.Y * $scale, $path.W * $scale, $path.H * $scale, $path.Start, $path.Sweep)
        $pen.Dispose()
    }

    $center = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(31, 11, 16, 32))
    $points = @(
        [System.Drawing.PointF]::new([float](128 * $scale), [float](83 * $scale)),
        [System.Drawing.PointF]::new([float](166 * $scale), [float](105 * $scale)),
        [System.Drawing.PointF]::new([float](166 * $scale), [float](151 * $scale)),
        [System.Drawing.PointF]::new([float](128 * $scale), [float](173 * $scale)),
        [System.Drawing.PointF]::new([float](90 * $scale), [float](151 * $scale)),
        [System.Drawing.PointF]::new([float](90 * $scale), [float](105 * $scale))
    )
    $graphics.FillPolygon($center, $points)
    $center.Dispose()
    $graphics.Dispose()

    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $images.Add($stream.ToArray())
    $stream.Dispose()
    $bitmap.Dispose()
}

$file = New-Object System.IO.FileStream($outputPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
$writer = New-Object System.IO.BinaryWriter($file)
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$sizes.Count)
$offset = 6 + (16 * $sizes.Count)
for ($index = 0; $index -lt $sizes.Count; $index++) {
    $size = $sizes[$index]
    $writer.Write([Byte]($(if ($size -ge 256) { 0 } else { $size })))
    $writer.Write([Byte]($(if ($size -ge 256) { 0 } else { $size })))
    $writer.Write([Byte]0)
    $writer.Write([Byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$images[$index].Length)
    $writer.Write([UInt32]$offset)
    $offset += $images[$index].Length
}
foreach ($image in $images) { $writer.Write($image) }
$writer.Flush()
$writer.Dispose()
$file.Dispose()
Write-Host "Generated $outputPath"
