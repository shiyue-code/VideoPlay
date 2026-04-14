Add-Type -AssemblyName System.Drawing

$SIZE = 24

function Save-Icon($name, [scriptblock]$draw) {
    $bmp = New-Object System.Drawing.Bitmap($SIZE, $SIZE)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    & $draw $g
    $bmp.Save("$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
}

$white = [System.Drawing.Brushes]::White
$redPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 100, 100), 2)

function To-Points($arr) {
    $pts = @()
    foreach ($p in $arr) {
        $pts += New-Object System.Drawing.Point($p[0], $p[1])
    }
    return $pts
}

Save-Icon "play" {
    param($g)
    $g.FillPolygon($white, (To-Points @(@(8, 6), @(8, 18), @(19, 12))))
}

Save-Icon "pause" {
    param($g)
    $g.FillRectangle($white, 7, 6, 4, 12)
    $g.FillRectangle($white, 14, 6, 4, 12)
}

Save-Icon "stop" {
    param($g)
    $g.FillRectangle($white, 6, 6, 12, 12)
}

Save-Icon "prev" {
    param($g)
    $g.FillRectangle($white, 5, 6, 3, 12)
    $g.FillPolygon($white, (To-Points @(@(9, 12), @(19, 6), @(19, 18))))
}

Save-Icon "next" {
    param($g)
    $g.FillPolygon($white, (To-Points @(@(5, 6), @(5, 18), @(15, 12))))
    $g.FillRectangle($white, 16, 6, 3, 12)
}

Save-Icon "volume" {
    param($g)
    $g.FillPolygon($white, (To-Points @(@(4, 9), @(10, 9), @(16, 5), @(16, 19), @(10, 15), @(4, 15))))
    $g.DrawArc((New-Object System.Drawing.Pen($white, 2)), 16, 6, 6, 12, -60, 120)
}

Save-Icon "mute" {
    param($g)
    $gray = [System.Drawing.SolidBrush](New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(200, 200, 200)))
    $g.FillPolygon($gray, (To-Points @(@(4, 9), @(10, 9), @(16, 5), @(16, 19), @(10, 15), @(4, 15))))
    $g.DrawLine($redPen, 17, 7, 23, 17)
    $g.DrawLine($redPen, 17, 17, 23, 7)
}

Write-Host "Icons generated."
