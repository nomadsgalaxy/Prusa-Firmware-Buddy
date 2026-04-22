$repo = 'C:\Users\Antdr\OneDrive\Computers\Work Computers\Prusa\Documents\Claude\Prusa\Prusa-Firmware-Buddy'
$log = Join-Path $repo '.symfix_log.txt'
'=== symfix start ===' | Out-File $log -Encoding ascii

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
  ('--- ' + $f.rel + ' -> ' + $target + ' ---') | Out-File $log -Append -Encoding ascii
  # Delete whatever is currently there (broken reparse point, file, or symlink)
  try {
    if (Test-Path -LiteralPath $p) {
      $item = Get-Item -LiteralPath $p -Force -ErrorAction Stop
      if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        & cmd.exe /c "rmdir `"$p`"" 2>&1 | Out-File $log -Append -Encoding ascii
        if (Test-Path -LiteralPath $p) {
          & cmd.exe /c "del /f /q `"$p`"" 2>&1 | Out-File $log -Append -Encoding ascii
        }
      } else {
        Remove-Item -LiteralPath $p -Force -Recurse -ErrorAction Stop
      }
    }
  } catch {
    ('  delete err: ' + $_.Exception.Message) | Out-File $log -Append -Encoding ascii
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

'=== symfix done ===' | Out-File $log -Append -Encoding ascii
