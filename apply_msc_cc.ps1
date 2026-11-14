# Pasang tile "USB" (USB Drive / MSC) ke Control Center di phone.ino.
# Jalankan dari root repo:  powershell -ExecutionPolicy Bypass -File apply_msc_cc.ps1
# Aman: tiap anchor harus ketemu TEPAT 1 kali, kalau tidak -> batal, tidak ada yg diubah.
$f = 'phone.ino'
if (-not (Test-Path $f)) { Write-Host "ERROR: $f tidak ada (jalankan dari root repo)"; exit 1 }
$s = [IO.File]::ReadAllText((Resolve-Path $f), [Text.Encoding]::UTF8)
if ($s.Contains('ccActUsbMsc')) { Write-Host 'Sudah terpasang, dilewati'; exit 0 }
$nl = if ($s.Contains("`r`n")) { "`r`n" } else { "`n" }

function Count($t, $a) { ($t.Length - $t.Replace($a, '').Length) / $a.Length }
function Rep([ref]$t, $old, $new) {
  $n = Count $t.Value $old
  if ($n -ne 1) { Write-Host "ERROR: anchor ketemu $n kali (harus 1): $old"; exit 1 }
  $t.Value = $t.Value.Replace($old, $new)
}

# 1) jumlah tile 11 -> 12 + deklarasi
Rep ([ref]$s) '#define CC_ITEMS 11 // +Bluetooth (index 9), +Tangkap layar (index 10)' ('#define CC_ITEMS 12 // +Bluetooth (index 9), +Tangkap layar (index 10), +USB Drive (index 11)' + $nl + 'bool mscActive(); void ccActUsbMsc(); // usbmsc_renphone.ino')
# 2) label
Rep ([ref]$s) '"BT","Tangkap"};' '"BT","Tangkap","USB"};'
# 3) aksi
Rep ([ref]$s) 'ccActBluetooth, ccActScreenshot };' 'ccActBluetooth, ccActScreenshot, ccActUsbMsc };'
# 4) status ON/OFF tile
$a4 = '    case 9: return btCcEnabled(); // Bluetooth nyala = tombol terisi'
Rep ([ref]$s) $a4 ($a4 + $nl + '    case 11: return mscActive(); // USB Drive aktif = tombol terisi')
# 5) ikon kartu SD
$a5 = '    case 8: { // Mode Senyap: lonceng'
$icon = @(
'    case 11: { // USB Drive: ikon kartu SD (sudut kanan-atas terpotong) + 3 pin',
'      for(int t=0;t<2;t++){',
'        s.drawLine(cx-6+t,cy-9,cx+2,cy-9,ic);   s.drawLine(cx+2,cy-9,cx+6,cy-5,ic);',
'        s.drawFastVLine(cx+6-t,cy-5,15,ic);     s.drawFastHLine(cx-6,cy+9-t,13,ic);',
'        s.drawFastVLine(cx-6+t,cy-9,19,ic);',
'      }',
'      s.drawFastVLine(cx-3,cy-7,4,ic); s.drawFastVLine(cx-1,cy-7,4,ic); s.drawFastVLine(cx+1,cy-7,4,ic);',
'      break;',
'    }'
) -join $nl
Rep ([ref]$s) $a5 ($icon + $nl + $a5)

[IO.File]::WriteAllText((Resolve-Path $f), $s, (New-Object Text.UTF8Encoding($false)))
Write-Host 'OK: tile USB ditambahkan ke Control Center (12 tile).'
