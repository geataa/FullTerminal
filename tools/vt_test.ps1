# M0 gorsel dogrulama: SGR, 256 renk, truecolor, unicode, cizgi karakterleri.
$e = [char]27

Write-Host "$e[1;38;2;69;181;172mFullTerminal - VT dogrulama$e[0m"
Write-Host ""
Write-Host "$e[31mkirmizi$e[0m $e[32myesil$e[0m $e[33msari$e[0m $e[34mmavi$e[0m $e[35mmor$e[0m $e[36mcyan$e[0m $e[37mbeyaz$e[0m"
Write-Host "$e[91mparlak$e[0m $e[92mparlak$e[0m $e[93mparlak$e[0m $e[94mparlak$e[0m $e[95mparlak$e[0m $e[96mparlak$e[0m"
Write-Host "$e[1mkalin$e[0m  $e[3mitalik$e[0m  $e[4maltcizgi$e[0m  $e[9mustucizili$e[0m  $e[7mters$e[0m  $e[2msonuk$e[0m"
Write-Host "$e[48;5;236m$e[38;5;208m  256 renk indeks 208  $e[0m   $e[38;2;240;104;92mtruecolor 240,104,92$e[0m"
Write-Host ""

$line = ""
foreach ($i in 0..35) { $line += "$e[48;5;$(16 + $i * 6)m  " }
Write-Host "$line$e[0m"

Write-Host ""
Write-Host "unicode : Turkce cgiosu CGIOSU - box drawing - matematik"
Write-Host "genis   : CJK ve emoji testi"
Write-Host ""

foreach ($i in 1..30) {
    $c = 16 + (($i * 7) % 200)
    Write-Host ("{0,3}  " -f $i) -NoNewline
    Write-Host "$e[38;5;${c}m########################################$e[0m"
}
