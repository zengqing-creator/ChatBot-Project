#include <iostream>
#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

int main() {
    // 测试 HTTP 客户端
    httplib::Client client("www.baidu.com");
    auto res = client.Get("/");
    std::cout << "HTTP test: " << (res && res->status == 200 ? "OK" : "FAIL") << std::endl;
    
    // 测试 JSON 解析
    json j = {{"test", "success"}};
    std::cout << "JSON test: " << j.dump() << std::endl;
    
    return 0;
}