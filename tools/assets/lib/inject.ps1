param([string]$BaseIso,[string]$DataDir,[string]$OutDir,[string]$Name,
      [string]$Xorriso,[string]$Iso2raw)

# PowerShell twin of lib/inject.sh -- keep the two in step. See that file for
# why the data files go to the ISO root and why Rock Ridge must stay off.

# IP.BIN is the first 16 sectors (16 * 2048) of the ISO. xorriso rewrites the
# system area when it commits, so we hold those bytes and put them back after.
$IpBinSize = 32768

# bin/win/xorriso.exe is a Cygwin build: it does not understand drive-letter
# paths and silently treats "C:\..." as *relative*, gluing it onto its own cwd
# until the open fails. Everything handed to xorriso goes through here; .NET and
# iso2raw keep using the native Windows form.
function ConvertTo-CygPath([string]$p) {
    # Not Join-Path: it concatenates blindly, so an already-absolute $p would
    # come back as "C:\cwd\C:\...". GetFullPath alone is no good either -- in
    # Windows PowerShell it resolves against the *process* cwd, not the
    # session's location, and the two are not the same thing here.
    $full = if ([System.IO.Path]::IsPathRooted($p)) { $p }
            else { [System.IO.Path]::Combine((Get-Location).Path, $p) }
    $full = [System.IO.Path]::GetFullPath($full)
    if ($full -notmatch '^[A-Za-z]:') { return $full.Replace('\', '/') }  # UNC: pass through
    return '/cygdrive/' + $full.Substring(0, 1).ToLower() + $full.Substring(2).Replace('\', '/')
}

# Read-Head <path> -> byte[]; the first $IpBinSize bytes only, so a
# several-hundred-MB ISO never lands in memory.
function Read-Head([string]$Path) {
    $buf = New-Object byte[] $IpBinSize
    $fs = [System.IO.File]::OpenRead($Path)
    try {
        $read = 0
        while ($read -lt $IpBinSize) {
            $n = $fs.Read($buf, $read, $IpBinSize - $read)
            if ($n -le 0) { break }
            $read += $n
        }
        if ($read -lt $IpBinSize) { Write-Error "$Path is shorter than IP.BIN"; exit 1 }
    } finally { $fs.Dispose() }
    return $buf
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$inj = Join-Path $OutDir "$Name`_injected.iso"

# 1) hold IP.BIN
$ip = Read-Head $BaseIso

# 2) map each data file to an upper-case root name
$files = Get-ChildItem -LiteralPath $DataDir -File |
         Where-Object { $_.Name -match '^(bank[0-9a-f]{2}|memlist\.bin)$' } |
         Sort-Object Name
if ($files.Count -ne 14) {
    Write-Error "expected 14 data files in $DataDir, found $($files.Count) -- run data.bat first"
    exit 1
}
$xargs = @('-indev', (ConvertTo-CygPath $BaseIso), '-outdev', (ConvertTo-CygPath $inj),
           '-rockridge', 'off', '-joliet', 'off')
# Interpolate rather than concatenate: inside @(...) the comma binds tighter
# than '+', so '/' + $name would land as two separate array elements and
# xorriso would see the bare name as an unknown command.
foreach ($f in $files) { $xargs += @('-map', (ConvertTo-CygPath $f.FullName), "/$($f.Name.ToUpper())") }
$xargs += '-commit'

# Capture rather than discard: xorriso's own diagnostics are the only clue when
# this fails, and they go to stderr mixed in with routine progress chatter.
$xlog = & $Xorriso @xargs 2>&1
if ($LASTEXITCODE -ne 0) {
    $xlog | ForEach-Object { Write-Host $_ }
    Write-Error "xorriso injection failed"; exit 1
}
if (-not (Test-Path $inj) -or (Get-Item $inj).Length -le $IpBinSize) {
    Write-Error "xorriso produced no injected ISO"; exit 1
}

# 3) restore IP.BIN onto the front, in place, without truncating the rest
$fs = [System.IO.File]::OpenWrite($inj)
try { $fs.Write($ip, 0, $IpBinSize) } finally { $fs.Dispose() }

# 4) verify preservation
if (Compare-Object $ip (Read-Head $inj)) { Write-Error "IP.BIN not preserved"; exit 1 }

# 5) ISO -> MODE1/2352 raw, with EDC/ECC
& $Iso2raw $inj -o "$OutDir\$Name.bin"
if ($LASTEXITCODE -ne 0) { Write-Error "iso2raw conversion failed"; exit 1 }

# 6) track-1 cue (SDK canonical form -- matches shared.mk's create_bin_cue)
"FILE `"$Name.bin`" BINARY","  TRACK 01 MODE1/2352","    INDEX 01 00:00:00" |
    Set-Content "$OutDir\$Name.cue"

Remove-Item -Force -Path $inj
Write-Host "Injected $($files.Count) data files -> $OutDir\$Name.bin"
