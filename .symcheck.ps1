$repo = 'C:\Users\Antdr\OneDrive\Computers\Work Computers\Prusa\Documents\Claude\Prusa\Prusa-Firmware-Buddy'
$log = Join-Path $repo '.symcheck_log.txt'
'=== symlink check ===' | Out-File $log -Encoding ascii
$paths = @(
  'lib\Prusa-Error-Codes\prusaerrors\buddy\errors.yaml',
  'lib\Prusa-Error-Codes\prusaerrors\mmu\errors.yaml',
  'lib\Prusa-Error-Codes\prusaerrors\sl1\errors.yaml',
  'lib\tinyusb\docs\contributing\code_of_conduct.rst',
  'lib\tinyusb\docs\info\contributors.rst',
  'src\can\data_types\reg',
  'src\can\data_types\uavcan'
)
foreach ($rel in $paths) {
  $p = Join-Path $repo $rel
  ('--- ' + $rel + ' ---') | Out-File $log -Append -Encoding ascii
  if (Test-Path -LiteralPath $p) {
    $item = Get-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue
    ('  exists: yes, attributes: ' + $item.Attributes) | Out-File $log -Append -Encoding ascii
    ('  linkType: ' + $item.LinkType) | Out-File $log -Append -Encoding ascii
    ('  target: ' + ($item.Target -join '; ')) | Out-File $log -Append -Encoding ascii
    ('  length: ' + $item.Length) | Out-File $log -Append -Encoding ascii
  } else {
    '  exists: NO' | Out-File $log -Append -Encoding ascii
  }
}
'=== git config core.symlinks ===' | Out-File $log -Append -Encoding ascii
$cfg = Join-Path $repo '.git\config'
if (Test-Path -LiteralPath $cfg) {
  Get-Content -LiteralPath $cfg | Select-String -Pattern 'symlinks|autocrlf|filemode' | Out-File $log -Append -Encoding ascii
}
'=== done ===' | Out-File $log -Append -Encoding ascii
