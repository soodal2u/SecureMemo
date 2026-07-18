param(
  [string]$Src,
  [string]$Dst = "resources\app.ico"
)

Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Src)) {
  Write-Error "Source not found: $Src"
  exit 1
}

$dir = Split-Path -Parent $Dst
if ($dir -and -not (Test-Path $dir)) {
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

$img = [System.Drawing.Image]::FromFile((Resolve-Path $Src))
$sizes = @(16, 32, 48, 256)
$pngChunks = New-Object System.Collections.Generic.List[byte[]]

foreach ($s in $sizes) {
  $bmp = New-Object System.Drawing.Bitmap $s, $s
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.DrawImage($img, 0, 0, $s, $s)
  $g.Dispose()
  $ms = New-Object System.IO.MemoryStream
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
  $pngChunks.Add($ms.ToArray())
  $bmp.Dispose()
  $ms.Dispose()
}
$img.Dispose()

$msOut = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $msOut
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$sizes.Count)

$offset = 6 + (16 * $sizes.Count)
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $s = $sizes[$i]
  $data = $pngChunks[$i]
  $wByte = 0
  $hByte = 0
  if ($s -lt 256) {
    $wByte = [byte]$s
    $hByte = [byte]$s
  }
  $bw.Write([byte]$wByte)
  $bw.Write([byte]$hByte)
  $bw.Write([byte]0)
  $bw.Write([byte]0)
  $bw.Write([UInt16]1)
  $bw.Write([UInt16]32)
  $bw.Write([UInt32]$data.Length)
  $bw.Write([UInt32]$offset)
  $offset += $data.Length
}

foreach ($data in $pngChunks) {
  $bw.Write($data)
}
$bw.Flush()

[System.IO.File]::WriteAllBytes((Join-Path (Get-Location) $Dst), $msOut.ToArray())
$bw.Dispose()
$msOut.Dispose()
Write-Host "Wrote $Dst ($((Get-Item $Dst).Length) bytes)"
