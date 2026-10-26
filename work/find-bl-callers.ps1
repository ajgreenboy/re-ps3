# Find direct relative bl call sites of target addresses in a big-endian PS3 PPU ELF.
# Image-base-0 virtual addresses, like Ghidra. One pass over each executable PT_LOAD segment.
# Usage: powershell -File work/find-bl-callers.ps1 target-hex [target-hex ...]

param(
  [string]$Module = 'vsh',
  [Parameter(ValueFromRemainingArguments = $true)][string[]]$Targets = @()
)

$ErrorActionPreference = 'Stop'
$File = switch ($Module) {
  'vsh' { 'C:\dev\xmb\fw493\vsh_module\vsh.elf' }
  'basic' { 'C:\dev\xmb\fw493\vsh_module\basic_plugins.prx' }
  default { $Module }
}

function Read-U16BE([byte[]]$b, [long]$o) { return ([int]$b[$o] -shl 8) -bor [int]$b[$o + 1] }
function Read-U64BE([byte[]]$b, [long]$o) {
  $v = [long]0
  for ($k = 0; $k -lt 8; $k++) { $v = ($v -shl 8) -bor [long]$b[$o + $k] }
  return $v
}

$data = [System.IO.File]::ReadAllBytes($File)
if ($data.Length -lt 64 -or $data[0] -ne 0x7F -or $data[1] -ne 0x45) { throw "not an ELF: $File" }
$phoff = Read-U64BE $data 0x20
$phentsize = Read-U16BE $data 0x36
$phnum = Read-U16BE $data 0x38

$segs = @()
for ($i = 0; $i -lt $phnum; $i++) {
  $p = $phoff + $i * $phentsize
  $type = ([long]$data[$p] -shl 24) -bor ([long]$data[$p + 1] -shl 16) -bor ([long]$data[$p + 2] -shl 8) -bor [long]$data[$p + 3]
  if ($type -ne 1) { continue }
  $flags = ([long]$data[$p + 4] -shl 24) -bor ([long]$data[$p + 5] -shl 16) -bor ([long]$data[$p + 6] -shl 8) -bor [long]$data[$p + 7]
  $off = Read-U64BE $data ($p + 8)
  $vaddr = Read-U64BE $data ($p + 16)
  $filesz = Read-U64BE $data ($p + 32)
  if ($filesz -le 0) { continue }
  $segs += [pscustomobject]@{ Off = $off; Vaddr = $vaddr; Filesz = $filesz; Exec = (($flags -band 1) -ne 0) }
  Write-Output ("PT_LOAD: vaddr=0x{0:X} filesz=0x{1:X} flags=0x{2:X}" -f $vaddr, $filesz, $flags)
}

$wanted = @{}
foreach ($t in $Targets) { $wanted[[long]([Convert]::ToInt64(($t -replace '^0[xX]', ''), 16))] = $true }

foreach ($s in $segs) {
  if (-not $s.Exec) { continue }
  $first = $s.Vaddr - ($s.Vaddr % 4)
  $end = $s.Vaddr + $s.Filesz - 4
  $hits = 0
  for ($a = $first; $a -le $end; $a += 4) {
    $o = $s.Off + $a - $s.Vaddr
    # big-endian word
    $w = ([long]$data[$o] -shl 24) -bor ([long]$data[$o + 1] -shl 16) -bor ([long]$data[$o + 2] -shl 8) -bor [long]$data[$o + 3]
    if (($w -band 0xFC000003) -ne 0x48000001) { continue }  # relative bl
    $li = $w -band 0x003FFFFC
    if (($li -band 0x00200000) -ne 0) { $li = $li - 0x00400000 }
    $target = $a + $li
    if ($wanted.ContainsKey($target)) { Write-Output ("caller 0x{0:X8} -> 0x{1:X8}" -f $a, $target); $hits++ }
  }
  Write-Output ("# scanned {0} bytes, {1} hit(s)" -f $s.Filesz, $hits)
}
