# aml-nand-ecc-bch
Расчет ECC для микросхем NAND в устройствах Amlogic для тех у кого нет UFPI.  
В UFPI этот алгоритм называется AMLOGIC_P4K_SP256_CW8_D2L14.  

Сборка:  
gcc main.c bch.c -o ecc_fix  

Использование: ./ecc_fix [-v] [-skip N] <command> <input_file> [output_file]  
Команды: check, fixdata, fixecc  

-v - verbose режим, выдает на экран адреса блоков с ошибками.  
-skip N - пропустить первые N блоков (блок==4352 байта) от начала файла (в выходной файл они будут записаны без изменений). Полезно если правите только в конце дампа и боитесь что утилита что-то поломает в стартовых секторах.  

check - проверить данные в файле по их ECC  
fixdata - исправить ошибки в файле на основании имеющихся в нем ECC  
fixecc - пересчитать ECC на основании данных файла  

Вывод на экран:  
Uncorrectable error at offset 0x0 (page 1, block 0)  
...  
Correctable 1 bit errors at offset 0x1fb78430 (page 122271, block 3)  
Number of erroneous blocks: 3287  
Number of erroneous bits: 1750  
Number of uncorrectable blocks: 1543  

Программа заточена под TC58NVG2S0HTA00. Для других микросхем правьте:   
#define PAGE_SIZE 4352        - размер страницы NAND  
#define BLOCK_SIZE 528        - размер одного блока данных (юзерданные+OOB+ECC, то есть DATA_SIZE+ECC_SIZE)  
#define DATA_SIZE 514         - размер данных (512 байт юзерданных + 2 байта OOB)  
#define ECC_SIZE 14           - размер ECC байт  
#define SPARE_SIZE 128        - оставшееся свободное место в странице NAND (считать как PAGE_SIZE-BLOCK_SIZE*BLOCKS_PER_PAGE, цифра отличается от PageSpare в UFPI!)  
#define BLOCKS_PER_PAGE 8     - количество страниц BLOCK_SIZE в PAGE_SIZE  

Корректируются только исправимые ошибки (до 8 бит на страницу).  
Uncorrectable error обязательно будут в первой странице (это "волшебный" блок, имеющий свой формат), также ожидайте их в UBIFS секторах, которые ждут когда страницу отформатируют.  
Неисправимые блоки, OOB данные и оставшиеся данные в SPARE_SIZE в выходной файл записываются как были в оригинальном файле.  

Если исправляете данные в файле, обязательно забейте все 14 байтов ECC этого блока нулями - это сигнал для утилиты, что данный блок требуется принудительно пересчитать!  
Если не обнулить, то ECC пересчитано не будет!  

Рекомендация по использованию:  
1. Считать дамп программатором  
2. Поправить в дампе ошибки, корректируемые ECC: ./ecc_fix -v dump.bin dump_fixed.bin  
3. Внести свои изменения в dump_fixed.bin, не забываем обнулить байты ECC для измененного блока!   
4. Пересчитать ECC: ./ecc_fix -v dump_fixed.bin dump_to_write.bin  
5. Записать dump_to_write.bin программатором  
