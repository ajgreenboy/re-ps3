# Dump big-endian fields from a decrypted PS3 PPU ELF/PRX at Ghidra virtual addresses
# (image base 0). Used by audit/PAGE-MOVE.md to read the page-move duration constants
# out of explore_plugin.prx and the paf animation/clock constants out of vsh.elf.
# Usage: powershell -File work/page-move-constants.ps1 [-Module explore|vsh] addr[:count] ...

param(
  [ValidateSet('explore', 'vsh', 'xmb')][string]$Module = 'explore',
  [Parameter(ValueFromRemainingArguments = $true)][string[]]$Specs = @()
)

$ErrorActionPreference = 'Stop'
$File = switch ($Module) {
  'explore' { 'C:\dev\xmb\fw493\vsh_module\explore_plugin.prx' }
  'vsh' { 'C:\dev\xmb\fw493\vsh_module\vsh.elf' }
  'xmb' { 'C:\dev\xmb\fw493\vsh_module\xmb_plugin.prx' }
}

function Read-U16BE([byte[]]$b, [long]$o) { return ([int]$b[$o] -shl 8) -bor [int]$b[$o + 1] }
function Read-U32BE([byte[]]$b, [long]$o) {
  return ([long]$b[$o] -shl 24) -bor ([long]$b[$o + 1] -shl 16) -bor ([long]$b[$o + 2] -shl 8) -bor [long]$b[$o + 3]
}
function Read-U64BE([byte[]]$b, [long]$o) {
  return (Read-U32BE $b $o) * 4294967296 + (Read-U32BE $b ($o + 4))
}
function Read-F32BE([byte[]]$b, [long]$o) {
  $u = [uint32](Read-U32BE $b $o)
  return [BitConverter]::ToSingle([BitConverter]::GetBytes($u), 0)
}

$data = [System.IO.File]::ReadAllBytes($File)
if ($data.Length -lt 64 -or $data[0] -ne 0x7F -or $data[1] -ne 0x45) { throw "not an ELF: $File" }

$phoff = Read-U64BE $data 0x20
$phentsize = Read-U16BE $data 0x36
$phnum = Read-U16BE $data 0x38
Write-Output ("file      : {0} ({1} bytes)" -f $File, $data.Length)
Write-Output ("phoff=0x{0:X} phentsize={1} phnum={2}" -f $phoff, $phentsize, $phnum)

# PS3 PRX may put p_vaddr in the second 8 bytes of the address field; keep both.
$segs = @()
for ($i = 0; $i -lt $phnum; $i++) {
  $p = $phoff + $i * $phentsize
  $type = Read-U32BE $data $p
  $off = Read-U64BE $data ($p + 8)
  $vaddr = Read-U64BE $data ($p + 16)
  $filesz = Read-U64BE $data ($p + 32)
  if ($type -eq 1) {
    $segs += [pscustomobject]@{ Off = $off; Vaddr = $vaddr; Filesz = $filesz; Index = $i }
    Write-Output ("PT_LOAD {0}: off=0x{1:X} vaddr=0x{2:X} filesz=0x{3:X}" -f $i, $off, $vaddr, $filesz)
  }
}

function To-Offset([long]$vaddr) {
  foreach ($s in $segs) {
    if ($vaddr -ge $s.Vaddr -and $vaddr -lt ($s.Vaddr + $s.Filesz)) { return $s.Off + $vaddr - $s.Vaddr }
  }
  return -1
}

if ($Specs.Count -eq 0) { $Specs = @('0x270674', '0x2706c0', '0x270658', '0x270d30', '0x271d0c') }

foreach ($spec in $Specs) {
  $parts = $spec.Split(':')
  $addr = [Convert]::ToInt64(($parts[0] -replace '^0[xX]', ''), 16)
  $count = if ($parts.Length -gt 1) { [int]$parts[1] } else { 1 }
  for ($i = 0; $i -lt $count; $i++) {
    $a = $addr + 4 * $i
    $off = To-Offset $a
    if ($off -lt 0) { Write-Output ("0x{0:X8}  (not mapped)" -f $a); continue }
    $u = [uint32](Read-U32BE $data $off)
    $f = [BitConverter]::ToSingle([BitConverter]::GetBytes($u), 0)
    $iv = [BitConverter]::ToInt32([BitConverter]::GetBytes($u), 0)
    Write-Output ("0x{0:X8}  u32=0x{1:X8}  i32={2}  f32={3}  fileoff=0x{4:X}" -f $a, $u, $iv, $f, $off)
  }
}
