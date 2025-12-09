#include <stdio.h>

// argc: 参数个数 (包括程序名自己)
// argv: 参数字符串数组
int main(int argc, char *argv[]) {
    // 1. 检查参数够不够
    // 我们需要: ./txt2bin <输入.txt> <输出.dsm>
    // 所以 argc 应该是 3 (程序名 + 输入 + 输出)
    if (argc != 3) {
        printf("用法错误！\n");
        printf("正确用法: %s <input_file.txt> <output_file.dsm>\n", argv[0]);
        return 1;
    }

    // 2. 获取文件名 (从命令行参数里拿)
    char *input_path = argv[1];
    char *output_path = argv[2];

    printf("正在转换: %s -> %s ...\n", input_path, output_path);

    // 3. 打开文件
    FILE *txt = fopen(input_path, "r");
    if (!txt) {
        perror("无法打开输入文件 (txt)");
        return 1;
    }

    FILE *bin = fopen(output_path, "wb"); // 记得是 wb (write binary)
    if (!bin) {
        perror("无法创建输出文件 (dsm)");
        fclose(txt);
        return 1;
    }

    // 4. 转换逻辑 (保持不变)
    int num;
    int count = 0;
    while (fscanf(txt, "%d", &num) != EOF) {
        fwrite(&num, sizeof(int), 1, bin);
        count++;
    }

    // 5. 收尾
    fclose(txt);
    fclose(bin);
    printf("转换完成！共写入 %d 个整数。\n", count);
    
    return 0;
}
