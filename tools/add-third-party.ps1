# Baixa os plugins de terceiros (release oficial para Windows) e coloca dentro do pacote,
# no formato que o OBS carrega de qualquer jeito: obs-plugins/64bit + data/obs-plugins/<nome>.
# Uso: pwsh tools/add-third-party.ps1 -Manifest third-party.json -Dest <pasta do programa>
param(
    [Parameter(Mandatory)] [string] $Manifest,
    [Parameter(Mandatory)] [string] $Dest
)
$ErrorActionPreference = 'Stop'

$m = Get-Content $Manifest -Raw | ConvertFrom-Json
$bin = Join-Path $Dest 'obs-plugins/64bit'
$newPlugins = Join-Path $Dest 'plugins'   # formato do OBS 33 em modo portátil: plugins/<nome>/<nome>.dll + data
$data = Join-Path $Dest 'data/obs-plugins'
New-Item -ItemType Directory -Force $bin, $data | Out-Null
$work = Join-Path ([IO.Path]::GetTempPath()) "rds-third-party"
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null

$ok = @()
$failed = @()

function Pick-Asset($assets) {
    $cands = $assets | Where-Object {
        $_.name -match '\.zip$' -and $_.name -match '(?i)win' -and
        $_.name -notmatch '(?i)(pdb|debug|symbol|mac|linux|ubuntu|-source\.|arm64|programdata)'
    }
    # preferência: zip feito para o OBS 33 (formato novo), depois x64, depois qualquer um
    $best = $cands | Where-Object { $_.name -match '(?i)obs ?33' } | Select-Object -First 1
    if (-not $best) { $best = $cands | Where-Object { $_.name -match '(?i)x64|64' } | Select-Object -First 1 }
    if (-not $best) { $best = $cands | Select-Object -First 1 }
    return $best
}

foreach ($p in $m.plugins) {
    Write-Host "::group::$($p.title) $($p.tag)"
    try {
        $json = gh release view $p.tag -R $p.repo --json assets | ConvertFrom-Json
        Write-Host "arquivos da release:"; $json.assets | ForEach-Object { Write-Host "  $($_.name)" }
        $asset = Pick-Asset $json.assets
        if (-not $asset) { throw "nenhum .zip para Windows na release" }
        $dir = Join-Path $work $p.name
        New-Item -ItemType Directory -Force $dir | Out-Null
        gh release download $p.tag -R $p.repo -p $asset.name -D $dir --clobber
        if ($LASTEXITCODE -ne 0) { throw "download falhou" }
        $x = Join-Path $dir 'x'
        Expand-Archive (Join-Path $dir $asset.name) $x -Force

        $copied = 0
        # formato OBS 33: plugins/<nome>/<nome>.dll + plugins/<nome>/data/
        foreach ($pd in Get-ChildItem $x -Recurse -Directory | Where-Object { $_.Parent.Name -eq 'plugins' -and (Test-Path (Join-Path $_.FullName "$($_.Name).dll")) }) {
            $target = Join-Path $newPlugins $pd.Name
            New-Item -ItemType Directory -Force $target | Out-Null
            Copy-Item (Join-Path $pd.FullName '*') $target -Recurse -Force -Exclude *.pdb
            $copied++
        }
        if ($copied -eq 0) {
        # formato antigo: .../obs-plugins/64bit/*.dll  +  .../data/obs-plugins/<nome>/
        foreach ($b in Get-ChildItem $x -Recurse -Directory | Where-Object { $_.Name -eq '64bit' -and $_.Parent.Name -eq 'obs-plugins' }) {
            Copy-Item (Join-Path $b.FullName '*') $bin -Recurse -Force -Exclude *.pdb
            $copied++
        }
        foreach ($d in Get-ChildItem $x -Recurse -Directory | Where-Object { $_.Name -eq 'obs-plugins' -and $_.Parent.Name -eq 'data' }) {
            Copy-Item (Join-Path $d.FullName '*') $data -Recurse -Force
        }
        }
        # formato ProgramData: <nome>/bin/64bit/*.dll  +  <nome>/data/
        if ($copied -eq 0) {
            foreach ($b in Get-ChildItem $x -Recurse -Directory | Where-Object { $_.Name -eq '64bit' -and $_.Parent.Name -eq 'bin' }) {
                $root = $b.Parent.Parent
                Copy-Item (Join-Path $b.FullName '*') $bin -Recurse -Force -Exclude *.pdb
                $dll = Get-ChildItem $b.FullName -Filter *.dll | Select-Object -First 1
                $modName = if ($dll) { $dll.BaseName } else { $root.Name }
                $rd = Join-Path $root.FullName 'data'
                if (Test-Path $rd) {
                    $target = Join-Path $data $modName
                    New-Item -ItemType Directory -Force $target | Out-Null
                    Copy-Item (Join-Path $rd '*') $target -Recurse -Force
                }
                $copied++
            }
        }
        if ($copied -eq 0) {
            Get-ChildItem $x -Recurse | Select-Object -First 40 | ForEach-Object { Write-Host "  $($_.FullName.Substring($x.Length))" }
            throw "formato do zip não reconhecido"
        }
        $ok += "$($p.title) $($p.tag)"
        Write-Host "ok: $($asset.name)"
    } catch {
        Write-Host "::warning::$($p.title): $_"
        $failed += "$($p.title) $($p.tag): $_"
    }
    Write-Host "::endgroup::"
}

$summary = "### Plugins de terceiros`n" + (($ok | ForEach-Object { "- ✅ $_" }) -join "`n") + "`n" + (($failed | ForEach-Object { "- ❌ $_" }) -join "`n")
if ($env:GITHUB_STEP_SUMMARY) { $summary | Out-File -Append $env:GITHUB_STEP_SUMMARY }
Write-Host $summary
$ok | Out-File (Join-Path $Dest 'PLUGINS-INCLUIDOS.txt') -Encoding utf8
if ($failed.Count -gt 0 -and $env:RDS_STRICT -eq '1') { exit 1 }
