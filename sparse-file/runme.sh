#!/bin/bash

echo "Build"
make

echo "Create fileA"
./create_test_file_a.sh

echo "fileA to fileB"
./sparse fileA fileB

echo "gzip fileA, fileB"
gzip -c fileA > fileA.gz
gzip -c fileB > fileB.gz

echo "fileB.gz to fileC"
gzip -cd fileB.gz | ./sparse fileC

echo "fileA to fileD with -b 100"
./sparse -b 100 fileA fileD

{
    echo "======================================"

    for file in fileA fileA.gz fileB fileB.gz fileC fileD; do
        SIZE=$(stat -f "%z" $file)
        BLOCKS=$(stat -f "%b" $file)
        echo "Файл: $file | Размер: $SIZE байт | Физический размер на диске: $BLOCKS блоков"
    done

} | tee result.txt

echo "Completed"