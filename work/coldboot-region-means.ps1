# Mean RGB of fixed regions (960x540 frame coordinates) in every image of a folder: background (top-left), "PS3"
# letters, footer text. Prints: name bg logo footer. Usage: coldboot-region-means.ps1 -Dir <dir> [-Filter f_*.jpg]
param([string]$Dir,[string]$Filter='*.jpg')
Add-Type -AssemblyName System.Drawing
$regions=@{bg=@(20,20,200,100);logo=@(680,245,880,288);footer=@(738,302,828,318)}
foreach($f in Get-ChildItem $Dir -Filter $Filter | Sort-Object Name) {
    $img=[Drawing.Bitmap]::FromFile($f.FullName); $sx=$img.Width/960.0; $sy=$img.Height/540.0
    $out=@($f.Name)
    foreach($k in 'bg','logo','footer') { $r=$regions[$k]; $sum=0; $n=0
        for($y=$r[1];$y -lt $r[3];$y+=2){ for($x=$r[0];$x -lt $r[2];$x+=2){ $c=$img.GetPixel([int]($x*$sx),[int]($y*$sy)); $sum+=($c.R+$c.G+$c.B)/3; $n++ } }
        $out+=('{0:F1}' -f ($sum/$n)) }
    $img.Dispose(); $out -join ' '
}
