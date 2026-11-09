#!/bin/bash
# Temporary: reports what the LibXenon toolchain offers (removed before merge).
set -x
echo "DEVKITXENON=$DEVKITXENON"; ls $DEVKITXENON $DEVKITXENON/usr/lib $DEVKITXENON/usr/include | head -80
which xenon-g++ cmake make; xenon-g++ --version
cd "$(dirname "$0")"
for h in mutex thread filesystem atomic condition_variable chrono optional string_view; do
  printf '#include <%s>\nint main(){return 0;}\n' $h > h.cpp
  xenon-g++ -std=c++17 -DXENON -m32 -maltivec -fno-pic -mpowerpc64 -mhard-float -c h.cpp -o h.o 2>&1 | head -3 && echo "HEADER $h ok"
done
cat > fs.cpp <<'C'
#include <filesystem>
#include <mutex>
#include <cstdio>
std::mutex m;
int main(){ std::lock_guard<std::mutex> g(m); std::error_code ec;
  for (auto& e : std::filesystem::directory_iterator("uda0:/", ec)) std::printf("%s\n", e.path().c_str());
  return std::filesystem::exists("uda0:/x", ec); }
C
M="-DXENON -m32 -maltivec -fno-pic -mpowerpc64 -mhard-float"
xenon-g++ -std=c++17 $M fs.cpp -o fs.elf -L$DEVKITXENON/xenon/lib/32 -L$DEVKITXENON/usr/lib -n -T $DEVKITXENON/app.lds -lfat -lxenon -lm 2>&1 | tail -20; echo "LINK rc=$?"
ls -la fs.elf
find $DEVKITXENON -name "libstdc++*" | head; find $DEVKITXENON -iname "*fat*" | head; find $DEVKITXENON -name "*.cmake" | head
grep -rn "thread\|gthr" $DEVKITXENON/xenon/include/c++/*/powerpc-elf/bits/c++config.h 2>/dev/null | grep -i "define _GLIBCXX_HAS_GTHREADS\|_GLIBCXX_USE_DIRENT" | head
find $DEVKITXENON -name c++config.h | head -2 | xargs grep -n "GTHREADS\|DIRENT\|FILESYSTEM" | head -20
ls $DEVKITXENON/usr/include/libfat* $DEVKITXENON/usr/include/fat* 2>/dev/null; ls $DEVKITXENON/usr/include
