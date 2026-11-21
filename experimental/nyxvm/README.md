# NyxVM (eksperimental, DIPARKIR)

Jalur alternatif yang ditulis lebih dulu: interpreter bytecode + MMU software sungguhan (page table,
soft-TLB, demand paging, swap SD, hibernate). Memberi isolasi nyata dan hibernate, tapi lebih lambat dari native.

**Status jujur:** `nyxvm_core.h` dan `nyxsys.h` hanya **lolos kompilasi** (`-Wall -Wextra -Wpedantic`).
**Tidak ada tes, tidak ada assembler/compiler, tidak pernah dijalankan.** Jangan dianggap siap pakai.
Dipindah keluar dari folder sketch agar tidak ikut build. Diputuskan memakai jalur native
(`docs/features/native-apps.md`) karena kecepatan; jalur ini disimpan bila nanti butuh sandbox sungguhan.
