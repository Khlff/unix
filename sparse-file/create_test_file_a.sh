dd if=/dev/zero of=fileA bs=1 count=$((4*1024*1024 + 1)) 2>/dev/null

printf '\1' | dd of=fileA bs=1 count=1 conv=notrunc 2>/dev/null
printf '\1' | dd of=fileA bs=1 count=1 seek=10000 conv=notrunc 2>/dev/null
printf '\1' | dd of=fileA bs=1 count=1 seek=$((4*1024*1024)) conv=notrunc 2>/dev/null