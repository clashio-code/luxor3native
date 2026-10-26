<#
  push-to-github.ps1 - one click: stage, guard assets, commit, push.

  Usage: double-click push-to-github.bat, or from PowerShell:
      .\push-to-github.ps1

  NOTE: this file must stay pure ASCII. Windows PowerShell 5.1 reads .ps1
  files as ANSI, so any non-ASCII byte (em dash, smart quotes) can break
  parsing. Keep it ASCII only.

  This script NEVER commits the copyrighted game assets (data/ folder,
  Luxor3.exe, fmod.dll). A guard stops the commit if any show up.
  See docs/ASSET_POLICY.md.
#>

# Native commands (git) write warnings to stderr. Under 'Stop' PowerShell 5.1
# turns those into terminating errors, so use 'Continue' and check $LASTEXITCODE
# explicitly after every git call instead.
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

function Fail([string]$msg) {
    Write-Host ""
    Write-Host ("GAGAL: " + $msg) -ForegroundColor Red
    Read-Host "Tekan Enter untuk keluar"
    exit 1
}

function Ok([string]$msg) { Write-Host $msg -ForegroundColor Green }
function Info([string]$msg) { Write-Host $msg -ForegroundColor Cyan }
function Warn([string]$msg) { Write-Host $msg -ForegroundColor Yellow }

Write-Host "=== Luxor3 Native - push ke GitHub ===" -ForegroundColor Cyan

# Always work from the folder the script lives in (path has spaces, so use LiteralPath).
Set-Location -LiteralPath $PSScriptRoot
Info ("Folder: " + $PSScriptRoot)

# ---- 1. Is Git installed? --------------------------------------------------
$gitOk = $false
try {
    git --version 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) { $gitOk = $true }
} catch { $gitOk = $false }
if (-not $gitOk) {
    Fail "Git belum terpasang atau tidak ada di PATH. Install dari https://git-scm.com/download/win lalu BUKA ULANG PowerShell."
}
Ok (git --version)

# ---- 2. Repo and branch main -----------------------------------------------
if (Test-Path -LiteralPath '.git') {
    Info "Repo .git sudah ada."
} else {
    git init -b main 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "git init gagal." }
    Ok "Repo baru dibuat (branch: main)."
}

# Ensure the current branch is main, matching the GitHub Actions push trigger.
$currentBranch = git branch --show-current
if ($currentBranch -ne 'main') {
    git branch -M main 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "Gagal mengganti branch ke main." }
    Ok ("Branch diganti dari " + $currentBranch + " ke main.")
}

# ---- 3. Commit identity (git refuses to commit without it) ----------------
if (-not (git config user.name)) {
    $n = Read-Host ("Nama untuk commit [default: " + $env:USERNAME + "]")
    if (-not $n) { $n = $env:USERNAME }
    git config user.name $n
    Ok ("user.name = " + $n)
}
if (-not (git config user.email)) {
    $e = Read-Host ("Email untuk commit [default: " + $env:USERNAME + "@localhost]")
    if (-not $e) { $e = $env:USERNAME + "@localhost" }
    git config user.email $e
    Ok ("user.email = " + $e)
}

# ---- 4. Stage ---------------------------------------------------------------
# Git on Windows defaults to core.autocrlf=true, which makes git warn on
# every LF file (and PowerShell 5.1 treats redirected stderr as a fatal
# error under $ErrorActionPreference='Stop'). The CI runners are Linux and
# need LF for shell scripts and YAML, so force autocrlf off for this repo
# and do not redirect git's stderr into PowerShell's error stream.
git config core.autocrlf false 2>$null
git add -A 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) { Fail "git add gagal." }

$staged = @(git diff --cached --name-only)
if ($staged.Count -eq 0) {
    Warn "Tidak ada perubahan baru untuk di-commit. Akan push commit yang sudah ada."
} else {

# ---- 5. GUARD: copyrighted assets must never be committed -------------------
# Note: the data/ pattern is anchored to the repo root. The synthetic fixture
# under tests/fixtures/mini_game/data/ is allowed and must be committed.
$forbidden = @($staged | Where-Object {
    $_ -match '^data/' -or
    $_ -match '(?i)(^|/)(luxor3\.exe|fmod\.dll|unins000\.(exe|dat))$' -or
    $_ -match '\.o$'
})
if ($forbidden.Count -gt 0) {
    Write-Host ""
    Write-Host "GUARD MENOLAK - file berikut terdeteksi di staged:" -ForegroundColor Red
    foreach ($f in $forbidden) { Write-Host ("  " + $f) -ForegroundColor Red }
    Write-Host ""
    Write-Host "Aset game tidak boleh masuk repo. Keluarkan dari stage dulu:" -ForegroundColor Yellow
    foreach ($f in $forbidden) { Write-Host ("  git reset HEAD -- " + $f) }
    Fail "Commit dihentikan agar aset berhak cipta tidak ter-commit."
}
Ok ("Guard aset: bersih (" + $staged.Count + " file).")

# ---- 6. Commit ---------------------------------------------------------------
$msg = "Set up Luxor 3 native port CI"
git commit -m $msg 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) {
    # A failed commit can mean there was nothing new to commit.
    $head = git rev-parse --verify HEAD 2>$null
    if (-not $head) { Fail "git commit gagal (belum ada commit)." }
    Warn "Tidak ada commit baru (perubahan mungkin sudah ter-commit)."
} else {
    Ok ("Commit dibuat: " + $msg)
}

} # End of the staged-files block.

# ---- 7. Remote ---------------------------------------------------------------
$url = git remote get-url origin 2>$null
if (-not $url) {
    Write-Host ""
    Warn "Belum ada remote 'origin'."
    Write-Host "Buat repo KOSONG dulu di github.com (jangan centang README/.gitignore),"
    $url = Read-Host "lalu tempel URL-nya (contoh: https://github.com/USER/nama-repo.git)"
    if (-not $url) { Fail "URL remote kosong, push dibatalkan." }
    git remote add origin $url 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "git remote add gagal." }
    Ok ("Remote origin = " + $url)
} else {
    Info ("Remote origin = " + $url)
}

# ---- 8. Push -----------------------------------------------------------------
$branch = git branch --show-current
if (-not $branch) { Fail "Tidak bisa menentukan nama branch lokal." }
Info ("Branch lokal: " + $branch)

Write-Host ""
Write-Host "Push ke GitHub..." -ForegroundColor Cyan
git push -u origin $branch
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Warn "Push belum berhasil."
    Write-Host ""
    Write-Host "Kemungkinan penyebab:" -ForegroundColor Yellow
    Write-Host "  1. Belum login -> Git akan membuka browser, izinkan, lalu ulangi."
    Write-Host "  2. Repo GitHub belum dibuat / URL remote salah."
    Write-Host "  3. Branch lokal belum punya commit."
    Write-Host ""
    Read-Host "Tekan Enter untuk keluar"
    exit 1
}

# ---- 9. Done -----------------------------------------------------------------
Write-Host ""
Ok ("Push berhasil ke branch " + $branch + ".")
Write-Host ""
Write-Host "Langkah berikutnya:" -ForegroundColor Cyan
Write-Host ("  1. Buka: " + $url)
Write-Host "  2. Tab 'Actions' -> workflow 'CI' sedang jalan."
Write-Host "  3. Selesai -> tab 'Artifacts' -> unduh 'luxor3-aarch64'."
Write-Host "     (berisi luxor3, luxor3_gl, device_probe untuk RG40XXV)"
Write-Host ""
Write-Host "Jalankan ulang nanti: Actions -> CI -> Run workflow." -ForegroundColor DarkGray

Read-Host "Tekan Enter untuk keluar"
