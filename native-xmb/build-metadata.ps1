function Get-NativeSourceFingerprint {
    param([Parameter(Mandatory=$true)][string]$ProjectRoot)
    $nativeRoot=Join-Path $ProjectRoot 'native-xmb'
    $files=@(Get-ChildItem (Join-Path $nativeRoot 'src') -File -Recurse | Where-Object {$_.Extension -in @('.cpp','.h')})
    $files+=@(Get-Item (Join-Path $nativeRoot 'CMakeLists.txt'),(Join-Path $nativeRoot 'build.ps1'),(Join-Path $nativeRoot 'build-metadata.ps1'))
    $records=@($files | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($nativeRoot.Length).Replace('\','/')+' '+(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    })
    $sha=[Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($records -join "`n"))))).Replace('-','') }
    finally { $sha.Dispose() }
}
