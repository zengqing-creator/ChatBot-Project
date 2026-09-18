#pragma once

// Windows 版本宏定义
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#define _WIN32_WINNT 0x0A00
#endif
#if !defined(WINVER) || WINVER < 0x0A00
#define WINVER 0x0A00
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

// Windows API
#include <windows.h>

// 标准库
#include <iostream>
#include <string>
#include <cstdlib>
#include <iomanip>
#include <ctime>
#include <functional>
#include <map>
#include <vector>
#include <memory>

// 第三方库
#include "httplib.h"
#include "json.hpp"
#include "sqlite3.h"

using json = nlohmann::json;
