#pragma once

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace Zeri::Link {

#ifdef _WIN32

    class ScopedHandle final {
    public:
        ScopedHandle() = default;
        explicit ScopedHandle(HANDLE h) noexcept : m_handle(h) {}

        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;

        ScopedHandle(ScopedHandle&& other) noexcept : m_handle(other.m_handle) {
            other.m_handle = nullptr;
        }

        ScopedHandle& operator=(ScopedHandle&& other) noexcept {
            if (this != &other) {
                Reset();
                m_handle = other.m_handle;
                other.m_handle = nullptr;
            }
            return *this;
        }

        ~ScopedHandle() { Reset(); }

        [[nodiscard]] HANDLE Get() const noexcept { return m_handle; }

        [[nodiscard]] HANDLE Release() noexcept {
            HANDLE tmp = m_handle;
            m_handle = nullptr;
            return tmp;
        }

        void Reset(HANDLE h = nullptr) noexcept {
            if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE) {
                CloseHandle(m_handle);
            }
            m_handle = h;
        }

        [[nodiscard]] bool IsValid() const noexcept {
            return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
        }

    private:
        HANDLE m_handle{ nullptr };
    };

#else

    class ScopedFd final {
    public:
        ScopedFd() = default;
        explicit ScopedFd(int fd) noexcept : m_fd(fd) {}

        ScopedFd(const ScopedFd&) = delete;
        ScopedFd& operator=(const ScopedFd&) = delete;

        ScopedFd(ScopedFd&& other) noexcept : m_fd(other.m_fd) {
            other.m_fd = -1;
        }

        ScopedFd& operator=(ScopedFd&& other) noexcept {
            if (this != &other) {
                Reset();
                m_fd = other.m_fd;
                other.m_fd = -1;
            }
            return *this;
        }

        ~ScopedFd() { Reset(); }

        [[nodiscard]] int Get() const noexcept { return m_fd; }

        [[nodiscard]] int Release() noexcept {
            const int tmp = m_fd;
            m_fd = -1;
            return tmp;
        }

        void Reset(int fd = -1) noexcept {
            if (m_fd != -1) {
                ::close(m_fd);
            }
            m_fd = fd;
        }

        [[nodiscard]] bool IsValid() const noexcept {
            return m_fd != -1;
        }

    private:
        int m_fd{ -1 };
    };

#endif

}

/*
ScopedHandle.h — RAII wrappers for OS-level handles (Zeri-Link)

Provides deterministic resource cleanup for platform-specific process I/O handles.
On Windows, ScopedHandle wraps a Win32 HANDLE and calls CloseHandle on destruction.
On POSIX, ScopedFd wraps an int file descriptor and calls close() on destruction.

Both classes share the same API surface:
- Get(): Returns the raw handle/fd without releasing ownership.
- Release(): Transfers ownership to the caller; the wrapper becomes invalid.
- Reset(h): Closes the current resource (if valid) and takes ownership of h.
- IsValid(): Returns true if the wrapped resource is usable.

Move semantics are supported; copy is deleted. This prevents double-close bugs
when handles are transferred between scopes (e.g., after CreatePipe or pipe2).

Windows-specific: Reset() guards against both nullptr and INVALID_HANDLE_VALUE,
since CreatePipe returns nullptr on failure while CreateFile returns INVALID_HANDLE_VALUE.
This dual check makes the wrapper safe for any Win32 handle source.

Used by WindowsProcessHost and PosixProcessHost to manage pipe endpoints,
process handles, and Job Object handles throughout the child process lifecycle.
*/
