import sys
p = "phone.ino"
t = open(p, encoding="utf-8", newline="").read()
start = "// NYX-EXTRAS-V1: prototipe fungsi nyxextras_renphone.ino & banner telepon (call_renphone.ino)\n"
end = "void nyxSkeleton(LGFX_Sprite&,int);\n"
i = t.find(start)
if i < 0: sys.exit("blok prototipe tidak ketemu (patch belum terpasang?)")
j = t.find(end, i)
if j < 0: sys.exit("akhir blok prototipe tidak ketemu")
j += len(end)
block = t[i:j]
t = t[:i] + t[j:]
anchor = "void showToast(const char* msg, int ms=1400);\n"
if t.count(anchor) != 1: sys.exit("anchor showToast tidak unik")
t = t.replace(anchor, anchor + block)
open(p, "w", encoding="utf-8", newline="").write(t)
print("OK: blok prototipe dipindah ke setelah showToast()")
