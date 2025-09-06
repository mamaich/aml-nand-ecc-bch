# aml-nand-ecc-bch
Расчет ECC для микросхем NAND в устройствах Amlogic для тех у кого нет UFPI.  
В UFPI этот алгоритм называется AMLOGIC_P4K_SP256_CW8_D2L14.  

Сборка:  
gcc main.c bch.c -o ecc_fix  

Использование: ./ecc_fix [-v] [-skip N] <command> <input_file> [output_file]  
Commands: check, fixdata, fixecc  

-v - verbose режим, выдает на экран адреса блоков с ошибками.  
-skip N - пропустить первые N блоков (блок==4352 байта) от начала файла (в выходной файл они будут записаны без изменений). Полезно если правите только в конце дампа и боитесь что утилита что-то поломает в стартовых секторах.  

check - проверить ECC  
fixdata - поправить данные на основании ECC  
fixecc - пересчитать ECC на основании данных  

Вывод на экран:  
Uncorrectable error at offset 0x0 (page 1, block 0)  
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
Неисправимые блоки в выходной файл записываются as is.  
