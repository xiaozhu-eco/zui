#include "log/log.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#if defined(ZUI_WITH_SPDLOG)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif
#endif

namespace {

#if defined(ZUI_WITH_SPDLOG) && defined(_WIN32)

std::string read_available(HANDLE read_end) {
    std::string data;
    for (int attempt = 0; attempt < 50; ++attempt) {
        DWORD available = 0;
        if (!PeekNamedPipe(read_end, nullptr, 0, nullptr, &available, nullptr)) {
            break;
        }
        if (available == 0) {
            continue;
        }
        const std::size_t at = data.size();
        data.resize(at + available);
        DWORD got = 0;
        if (!ReadFile(read_end, data.data() + at, available, &got, nullptr)) {
            data.resize(at);
            break;
        }
        data.resize(at + got);
        if (data.find("zui-log-marker 42") != std::string::npos) {
            break;
        }
    }
    return data;
}

#endif

}  // namespace

TEST(InternalLog, Marker) {
#if defined(ZUI_WITH_SPDLOG) && defined(_WIN32)
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    ASSERT_TRUE(CreatePipe(&read_end, &write_end, &sa, 0));
    HANDLE saved = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE for_sink = nullptr;
    ASSERT_TRUE(DuplicateHandle(GetCurrentProcess(), write_end, GetCurrentProcess(), &for_sink, 0,
                                FALSE, DUPLICATE_SAME_ACCESS));
    ASSERT_TRUE(SetStdHandle(STD_ERROR_HANDLE, for_sink));

    ZUI_INFO("zui-log-marker {}", 42);
    zui::log::flush();

    const std::string data = read_available(read_end);
    SetStdHandle(STD_ERROR_HANDLE, saved);
    CloseHandle(write_end);
    CloseHandle(read_end);
    EXPECT_NE(data.find("zui-log-marker 42"), std::string::npos);
    EXPECT_NE(data.find("[zui]"), std::string::npos);
#elif defined(ZUI_WITH_SPDLOG)
    int fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(fds), 0);
    const int saved = ::dup(STDERR_FILENO);
    ASSERT_NE(saved, -1);
    ASSERT_NE(::dup2(fds[1], STDERR_FILENO), -1);
    ZUI_INFO("zui-log-marker {}", 42);
    zui::log::flush();
    std::fflush(stderr);
    ASSERT_NE(::dup2(saved, STDERR_FILENO), -1);
    ::close(saved);
    ::close(fds[1]);
    const int flags = ::fcntl(fds[0], F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
    }
    std::string data;
    char buf[256];
    for (int attempt = 0; attempt < 50; ++attempt) {
        const ssize_t n = ::read(fds[0], buf, sizeof(buf));
        if (n > 0) {
            data.append(buf, buf + n);
        }
        if (data.find("zui-log-marker 42") != std::string::npos) {
            break;
        }
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            break;
        }
    }
    ::close(fds[0]);
    EXPECT_NE(data.find("zui-log-marker 42"), std::string::npos);
    EXPECT_NE(data.find("[zui]"), std::string::npos);
#else
    ZUI_TRACE("compiled out {}", 1);
    ZUI_DEBUG("compiled out {}", 1);
    ZUI_INFO("zui-log-marker {}", 42);
    ZUI_WARN("compiled out");
    ZUI_ERROR("compiled out");
    ZUI_CRITICAL("compiled out");
    SUCCEED();
#endif
}

TEST(InternalLog, FileSink) {
#if defined(ZUI_WITH_SPDLOG)
#if defined(_WIN32)
    char dir[MAX_PATH];
    ASSERT_GT(GetTempPathA(MAX_PATH, dir), 0u);
    const std::string path = std::string(dir) + "zui-log-file-marker.txt";
    ASSERT_EQ(_putenv_s("ZUI_LOG_FILE", path.c_str()), 0);
#else
    const std::string path = "/tmp/zui-log-file-marker.txt";
    ASSERT_EQ(setenv("ZUI_LOG_FILE", path.c_str(), 1), 0);
#endif
    std::remove(path.c_str());
    ZUI_FILE_INFO("zui-file-marker {}", 7);
    zui::log::flush();
    std::ifstream in(path);
    ASSERT_TRUE(in.good());
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(data.find("zui-file-marker 7"), std::string::npos);
    EXPECT_NE(data.find("[zui]"), std::string::npos);
    EXPECT_EQ(data.find("\033"), std::string::npos);
    in.close();
    std::remove(path.c_str());
#else
    ZUI_FILE_INFO("compiled out {}", 1);
    SUCCEED();
#endif
}
