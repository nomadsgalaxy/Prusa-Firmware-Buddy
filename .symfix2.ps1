$repo = 'C:\Users\Antdr\OneDrive\Computers\Work Computers\Prusa\Documents\Claude\Prusa\Prusa-Firmware-Buddy'
$log = Join-Path $repo '.symfix2_log.txt'
'=== symfix2 start ===' | Out-File $log -Encoding ascii

$fixes = @(
  @{ rel = 'lib\Prusa-Error-Codes\prusaerrors\buddy\errors.yaml';    target = '../../yaml/buddy-error-codes.yaml' },
  @{ rel = 'lib\Prusa-Error-Codes\prusaerrors\mmu\errors.yaml';      target = '../../yaml/mmu-error-codes.yaml' },
  @{ rel = 'lib\Prusa-Error-Codes\prusaerrors\sl1\errors.yaml';      target = '../../yaml/sla-error-codes.yaml' },
  @{ rel = 'lib\tinyusb\docs\contributing\code_of_conduct.rst';      target = '../../CODE_OF_CONDUCT.rst' },
  @{ rel = 'lib\tinyusb\docs\info\contributors.rst';                 target = '../../CONTRIBUTORS.rst' },
  @{ rel = 'src\can\data_types\reg';                                 target = './public_regulated_data_types/reg/' },
  @{ rel = 'src\can\data_types\uavcan';                              target = './public_regulated_data_types/uavcan/' }
)

foreach ($f in $fixes) {
  $p = Join-Path $repo $f.rel
  $target = $f.target
  ('--- ' + $f.rel + ' ---') | Out-File $log -Append -Encoding ascii

  $deleted = $false

  # Attempt 1: .NET File.Delete (doesn't follow reparse points)
  try {
    [System.IO.File]::Delete($p)
    if (-not (Test-Path -LiteralPath $p)) { $deleted = $true; '  deleted via File.Delete' | Out-File $log -Append -Encoding ascii }
  } catch { ('  File.Delete err: ' + $_.Exception.Message) | Out-File $log -Append -Encoding ascii }

  # Attempt 2: .NET Directory.Delete (for directory-type reparse points)
  if (-not $deleted -and (Test-Path -LiteralPath $p)) {
    try {
      [System.IO.Directory]::Delete($p, $false)
      if (-not (Test-Path -LiteralPath $p)) { $deleted = $true; '  deleted via Directory.Delete' | Out-File $log -Append -Encoding ascii }
    } catch { ('  Directory.Delete err: ' + $_.Exception.Message) | Out-File $log -Append -Encoding ascii }
  }

  # Attempt 3: fsutil reparsepoint delete + then delete the entry
  if (-not $deleted -and (Test-Path -LiteralPath $p)) {
    $fsOut = & fsutil.exe reparsepoint delete $p 2>&1
    ('  fsutil: ' + ($fsOut -join ' | ')) | Out-File $log -Append -Encoding ascii
    try { [System.IO.File]::Delete($p); if (-not (Test-Path -LiteralPath $p)) { $deleted = $true; '  deleted after fsutil' | Out-File $log -Append -Encoding ascii } } catch { }
  }

  if (-not $deleted) {
    '  STILL PRESENT, skipping write' | Out-File $log -Append -Encoding ascii
    continue
  }

  # Ensure parent exists
  $parent = Split-Path -LiteralPath $p -Parent
  if (-not (Test-Path -LiteralPath $parent)) { New-Item -Path $parent -ItemType Directory -Force | Out-Null }

  # Write the target string with no trailing newline, UTF-8 no BOM
  try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($target)
    [System.IO.File]::WriteAllBytes($p, $bytes)
    $after = Get-Item -LiteralPath $p -Force
    ('  written: ' + $after.Length + ' bytes, attrs=' + $after.Attributes) | Out-File $log -Append -Encoding ascii
  } catch {
    ('  write err: ' + $_.Exception.Message) | Out-File $log -Append -Encoding ascii
  }
}

'=== symfix2 done ===' | Out-File $log -Append -Encoding ascii
