# A small static file server for the page, for Windows PCs without Python (started by serve.bat).
# http://127.0.0.1:8000/ only; serves files under this script's own folder; no directory listing.  Ctrl+C to stop.
$root = [System.IO.Path]::GetFullPath($PSScriptRoot)
$types = @{ '.html'='text/html; charset=utf-8'; '.js'='text/javascript; charset=utf-8'; '.mjs'='text/javascript; charset=utf-8'; '.css'='text/css; charset=utf-8'; '.json'='application/json'; '.svg'='image/svg+xml'; '.png'='image/png'; '.wasm'='application/wasm'; '.ttf'='font/ttf'; '.otf'='font/otf'; '.txt'='text/plain; charset=utf-8' }
$l = New-Object System.Net.HttpListener
$l.Prefixes.Add('http://127.0.0.1:8000/')
$l.Start()
Write-Host 'http://localhost:8000/  (Ctrl+C to stop)'
try {
  while ($l.IsListening) {
    $c = $l.GetContext(); $rel = [System.Uri]::UnescapeDataString($c.Request.Url.AbsolutePath).TrimStart('/')
    if ($rel -eq '') { $rel = 'index.html' }
    $p = $null
    if ($rel -notmatch '\.\.' -and -not [System.IO.Path]::IsPathRooted($rel)) { $p = [System.IO.Path]::GetFullPath((Join-Path $root $rel)) }
    if ($p -and $p.StartsWith($root + [System.IO.Path]::DirectorySeparatorChar) -and (Test-Path -LiteralPath $p -PathType Leaf)) {
      $t = $types[[System.IO.Path]::GetExtension($p).ToLower()]; if (-not $t) { $t = 'application/octet-stream' }
      $b = [System.IO.File]::ReadAllBytes($p); $c.Response.ContentType = $t; $c.Response.ContentLength64 = $b.Length; $c.Response.OutputStream.Write($b, 0, $b.Length)
    } else { $c.Response.StatusCode = 404 }
    $c.Response.Close()
  }
} finally { $l.Stop() }
