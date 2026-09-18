#include <iostream>
#include <stdlib.h>  // getenv() 需要
#include <string>

int main() {
    // 从环境变量读取 API Key
    const char* api_key_cstr = std::getenv("LLM_API_KEY");

    if (!api_key_cstr) {
        std::cerr << "错误：没有找到环境变量 LLM_API_KEY" << std::endl;
        return 1;
    }

    std::string api_key = api_key_cstr;
    std::cout << "API Key 读取成功！" << std::endl;
    std::cout << "Key 前8位：" << api_key.substr(0, 8) << "..." << std::endl;

    return 0;
}