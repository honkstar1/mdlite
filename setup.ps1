# Fetches the pinned third-party sources into third_party/ (they are not committed).
$ErrorActionPreference = "Stop"
$tp = Join-Path $PSScriptRoot "third_party"
New-Item -ItemType Directory -Force $tp | Out-Null

$deps = @(
    @{ Name = "md4c";     Url = "https://github.com/mity/md4c.git";          Commit = "c7ba975" },
    @{ Name = "litehtml"; Url = "https://github.com/litehtml/litehtml.git";  Commit = "5624e79" }
)
foreach ($d in $deps) {
    $dir = Join-Path $tp $d.Name
    if (-not (Test-Path $dir)) { git clone $d.Url $dir }
    git -C $dir fetch --quiet origin
    git -C $dir checkout --quiet $d.Commit
    Write-Host "$($d.Name) @ $($d.Commit)"
}
