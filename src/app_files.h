#pragma once
#include "rules.h"
namespace home {
struct FileIdentity {ApplicationIdentity identity;std::wstring error;};
FileIdentity resolveApplicationFile(const std::wstring& file);
}
