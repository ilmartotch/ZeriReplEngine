#include "../Include/BugSnapshot.h"
#include "../Include/AppPaths.h"
#include "../Include/HelpCatalog.h"
#include "../Include/UserPaths.h"
#include "../../Engines/Include/Interface/IContext.h"

#include "version.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #include <process.h>
#else
    #include <unistd.h>
#endif

namespace Zeri::Core {

    namespace {
        [[nodiscard]] std::string ToLower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        [[nodiscard]] std::string FormatTimeUtc(std::chrono::system_clock::time_point tp) {
            const std::time_t tt = std::chrono::system_clock::to_time_t(tp);
            std::tm tm{};
#if defined(_WIN32)
            gmtime_s(&tm, &tt);
#else
            gmtime_r(&tt, &tm);
#endif
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
            return oss.str();
        }

        [[nodiscard]] std::string CurrentDateLocalYYYYMMDD() {
            const std::time_t tt = std::time(nullptr);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y%m%d");
            return oss.str();
        }

        [[nodiscard]] std::string BuildSnapshotFileName(const std::string& date, std::size_t id) {
            std::ostringstream oss;
            oss << "snapshotBug_" << date << "_" << std::setw(4) << std::setfill('0') << id << ".json";
            return oss.str();
        }

        [[nodiscard]] std::expected<std::filesystem::path, std::string> ReserveSnapshotPath(const std::filesystem::path& dir) {
            const std::string date = CurrentDateLocalYYYYMMDD();
            for (std::size_t id = 1; id <= 9999; ++id) {
                const auto candidate = dir / BuildSnapshotFileName(date, id);
                std::error_code ec;
                const bool exists = std::filesystem::exists(candidate, ec);
                if (ec) {
                    return std::unexpected("Failed to inspect snapshot destination: " + ec.message());
                }
                if (!exists) {
                    return candidate;
                }
            }
            return std::unexpected("Unable to reserve a unique snapshot file name for date " + date);
        }

        [[nodiscard]] std::uint64_t HashFileFNV1a64(
            const std::filesystem::path& path,
            std::size_t maxBytes,
            std::string& warning
        ) {
            static constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
            static constexpr std::uint64_t kPrime = 1099511628211ULL;

            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                warning = "unreadable";
                return 0;
            }

            std::uint64_t hash = kOffsetBasis;
            std::size_t totalRead = 0;
            std::array<char, 4096> buffer{};
            while (file.good() && totalRead < maxBytes) {
                const std::size_t remaining = maxBytes - totalRead;
                const std::size_t chunk = std::min<std::size_t>(buffer.size(), remaining);
                file.read(buffer.data(), static_cast<std::streamsize>(chunk));
                const auto readCount = static_cast<std::size_t>(file.gcount());
                if (readCount == 0) {
                    break;
                }
                for (std::size_t idx = 0; idx < readCount; ++idx) {
                    const auto byte = static_cast<std::uint8_t>(buffer[idx]);
                    hash ^= byte;
                    hash *= kPrime;
                }
                totalRead += readCount;
            }

            if (totalRead >= maxBytes) {
                warning = "hash_truncated";
            }
            return hash;
        }

        [[nodiscard]] bool ShouldSkipDirectoryName(std::string_view name) {
            static const std::set<std::string> kExcluded = {
                ".git",
                ".github",
                ".idea",
                ".vs",
                ".zeri",
                ".vscode",
                "cmake",
                "node_modules",
                "out",
                "vcpkg",
                "build",
                "build-debug",
                "build-release",
                "dist"
            };
            return kExcluded.contains(ToLower(std::string(name)));
        }

        [[nodiscard]] bool IsKeyFilePath(const std::filesystem::path& relativePath) {
            const std::string rel = ToLower(relativePath.string());
            static const std::array<std::string_view, 8> kInterestingPrefixes = {
                "src\\", "ui\\", "runtime\\", "help\\", "engine\\", "include\\", "modules\\", ".github\\"
            };
            static const std::set<std::string> kInterestingFileNames = {
                "cmakelists.txt", "build.ps1", "install.ps1", "go.mod", "go.sum"
            };
            static const std::set<std::string> kInterestingExtensions = {
                ".cpp", ".cxx", ".cc", ".c", ".h", ".hpp",
                ".go", ".mod", ".sum",
                ".json", ".toml", ".yaml", ".yml", ".ini", ".txt", ".md",
                ".ps1", ".cmake", ".js", ".ts", ".lua", ".py", ".rb"
            };

            for (const auto prefix : kInterestingPrefixes) {
                if (rel.starts_with(prefix)) {
                    return true;
                }
            }
            const std::string fileName = ToLower(relativePath.filename().string());
            if (kInterestingFileNames.contains(fileName)) {
                return true;
            }
            const std::string extension = ToLower(relativePath.extension().string());
            return kInterestingExtensions.contains(extension);
        }

        [[nodiscard]] std::string ReadEnvVar(const char* key) {
            if (key == nullptr || *key == '\0') {
                return {};
            }
#if defined(_WIN32)
            char* raw = nullptr;
            std::size_t len = 0;
            if (_dupenv_s(&raw, &len, key) != 0 || raw == nullptr) {
                return {};
            }
            std::string value(raw);
            std::free(raw);
            return value;
#else
            if (const char* raw = std::getenv(key); raw != nullptr) {
                return std::string(raw);
            }
            return {};
#endif
        }

        [[nodiscard]] std::string NormalizeSlashes(std::string pathText) {
            std::ranges::replace(pathText, '\\', '/');
            return pathText;
        }

        [[nodiscard]] bool StartsWithPath(std::string_view value, std::string_view prefix) {
            if (prefix.empty() || value.size() < prefix.size()) {
                return false;
            }
#if defined(_WIN32)
            for (std::size_t index = 0; index < prefix.size(); ++index) {
                const auto l = static_cast<char>(std::tolower(static_cast<unsigned char>(value[index])));
                const auto r = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[index])));
                if (l != r) {
                    return false;
                }
            }
            return true;
#else
            return value.substr(0, prefix.size()) == prefix;
#endif
        }

        [[nodiscard]] std::string RedactHomeSegment(std::string normalizedPath) {
            std::size_t start = normalizedPath.find("/Users/");
            std::size_t prefixLength = 7U;
            if (start == std::string::npos) {
                start = normalizedPath.find("/home/");
                prefixLength = 6U;
            }
            if (start == std::string::npos) {
                return normalizedPath;
            }
            const std::size_t userStart = start + prefixLength;
            const std::size_t userEnd = normalizedPath.find('/', userStart);
            if (userEnd == std::string::npos) {
                return normalizedPath.substr(0, userStart) + "<user>";
            }
            return normalizedPath.substr(0, userStart) + "<user>" + normalizedPath.substr(userEnd);
        }

        [[nodiscard]] std::string RedactPath(
            const std::filesystem::path& value,
            const std::filesystem::path& userDataDir,
            const std::filesystem::path& sessionTempDir
        ) {
            std::string normalized = NormalizeSlashes(value.string());
            const std::string userData = NormalizeSlashes(userDataDir.string());
            const std::string sessionTemp = NormalizeSlashes(sessionTempDir.string());
            if (!userData.empty() && StartsWithPath(normalized, userData)) {
                std::string suffix = normalized.substr(userData.size());
                if (!suffix.empty() && suffix.front() == '/') {
                    suffix.erase(suffix.begin());
                }
                return suffix.empty() ? "<user_data>" : "<user_data>/" + suffix;
            }
            if (!sessionTemp.empty() && StartsWithPath(normalized, sessionTemp)) {
                std::string suffix = normalized.substr(sessionTemp.size());
                if (!suffix.empty() && suffix.front() == '/') {
                    suffix.erase(suffix.begin());
                }
                return suffix.empty() ? "<session_temp>" : "<session_temp>/" + suffix;
            }
            return RedactHomeSegment(normalized);
        }

        [[nodiscard]] std::vector<std::string> ReadTailLines(
            const std::filesystem::path& path,
            std::size_t maxLines,
            std::size_t maxBytes
        ) {
            std::vector<std::string> lines;
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return lines;
            }

            file.seekg(0, std::ios::end);
            const std::streamoff size = file.tellg();
            if (size <= 0) {
                return lines;
            }

            const std::streamoff readFrom = std::max<std::streamoff>(0, size - static_cast<std::streamoff>(maxBytes));
            file.seekg(readFrom, std::ios::beg);
            if (readFrom > 0) {
                std::string ignored;
                std::getline(file, ignored);
            }

            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
                if (lines.size() > maxLines) {
                    lines.erase(lines.begin());
                }
            }
            return lines;
        }

        [[nodiscard]] std::string PlatformName() {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "macos";
#elif defined(__linux__)
            return "linux";
#else
            return "unknown";
#endif
        }

        [[nodiscard]] std::uint32_t ProcessId() {
#if defined(_WIN32)
            return static_cast<std::uint32_t>(::_getpid());
#else
            return static_cast<std::uint32_t>(::getpid());
#endif
        }

        struct LogRoot {
            std::filesystem::path path;
            std::string label;
        };

        struct LogCandidate {
            std::filesystem::path path;
            std::filesystem::path root;
            std::string rootLabel;
            std::string redactedPath;
            std::string lastWriteUtc;
            std::uint64_t sizeBytes{ 0 };
            int priority{ 0 };
        };

        [[nodiscard]] bool IsLogFile(const std::filesystem::path& path) {
            const std::string ext = ToLower(path.extension().string());
            return ext == ".log" || ext == ".txt" || ext == ".jsonl" || ext == ".ndjson";
        }

        [[nodiscard]] int ComputeLogPriority(const std::filesystem::path& path) {
            const std::string lower = ToLower(NormalizeSlashes(path.filename().string()) + " " + NormalizeSlashes(path.string()));
            int priority = 0;
            if (lower.find("zeri") != std::string::npos ||
                lower.find("engine") != std::string::npos ||
                lower.find("yuumi") != std::string::npos ||
                lower.find("bridge") != std::string::npos) {
                priority += 3;
            }
            if (lower.find("startup") != std::string::npos ||
                lower.find("stderr") != std::string::npos ||
                lower.find("stdout") != std::string::npos ||
                lower.find("panic") != std::string::npos ||
                lower.find("fatal") != std::string::npos ||
                lower.find("error") != std::string::npos ||
                lower.find("crash") != std::string::npos) {
                priority += 4;
            }
            if (lower.find("session") != std::string::npos || lower.find("runtime") != std::string::npos) {
                priority += 1;
            }
            return priority;
        }

        [[nodiscard]] bool IsErrorLikeLine(std::string_view line) {
            const std::string lower = ToLower(std::string(line));
            return lower.find("error") != std::string::npos ||
                lower.find("fatal") != std::string::npos ||
                lower.find("panic") != std::string::npos ||
                lower.find("exception") != std::string::npos ||
                lower.find("crash") != std::string::npos;
        }

        [[nodiscard]] bool IsWarningLikeLine(std::string_view line) {
            const std::string lower = ToLower(std::string(line));
            return lower.find("warn") != std::string::npos ||
                lower.find("deprecated") != std::string::npos ||
                lower.find("retry") != std::string::npos;
        }
    }

    std::expected<std::filesystem::path, std::string> CreateBugSnapshot(
        const RuntimeState& runtimeState,
        const StartupDiagnosticsReport& startupDiagnostics,
        const std::filesystem::path& projectRoot,
        const BugSnapshotMetadata& metadata,
        const BugSnapshotLimits& limits
    ) {
        try {
            const auto snapshotDir = ResolveUserDataDir() / "bug-reports";
            std::filesystem::create_directories(snapshotDir);
            const auto userDataDir = ResolveUserDataDir();
            const std::filesystem::path sessionTempDir = ReadEnvVar("ZERI_SESSION_TEMP_DIR");

            const auto reservedPath = ReserveSnapshotPath(snapshotDir);
            if (!reservedPath.has_value()) {
                return std::unexpected(reservedPath.error());
            }
            const auto snapshotPath = reservedPath.value();

            nlohmann::json root = nlohmann::json::object();
            root["generated_at_utc"] = FormatTimeUtc(std::chrono::system_clock::now());

            nlohmann::json app = nlohmann::json::object();
            app["name"] = "ZeriEngine";
            app["version"] = ZERI_VERSION_STRING;
            root["app"] = std::move(app);

            const auto* currentCtx = runtimeState.GetCurrentContext();
            const auto localVars = runtimeState.GetCurrentLocalVariables();
            const auto localFuncs = runtimeState.GetCurrentLocalFunctions();
            const auto& helpCatalog = HelpCatalog::Instance();

            nlohmann::json runtime = nlohmann::json::object();
            runtime["context"] = currentCtx ? currentCtx->GetName() : "global";
            runtime["local_variable_count"] = localVars.size();
            runtime["local_function_count"] = localFuncs.size();
            runtime["function_registry_revision"] = runtimeState.GetFunctionRegistryRevision();
            runtime["session_corrupted"] = runtimeState.WasSessionCorrupted();
            runtime["help_catalog_loaded"] = helpCatalog.IsLoaded();
            runtime["help_catalog_source"] = RedactPath(helpCatalog.SourcePath(), userDataDir, sessionTempDir);
            runtime["help_catalog_error"] = helpCatalog.LastError();
            root["runtime"] = std::move(runtime);

            nlohmann::json startup = nlohmann::json::object();
            startup["executable_dir"] = RedactPath(startupDiagnostics.executableDir, userDataDir, sessionTempDir);
            startup["issues"] = nlohmann::json::array();
            for (const auto& issue : startupDiagnostics.issues) {
                startup["issues"].push_back({
                    {"code", issue.code},
                    {"message", issue.message},
                    {"hint", issue.hint}
                });
            }
            root["startup"] = std::move(startup);

            std::error_code cwdEc;
            const auto cwd = std::filesystem::current_path(cwdEc);

            nlohmann::json host = nlohmann::json::object();
            host["platform"] = PlatformName();
            host["architecture_bits"] = sizeof(void*) * 8;
            host["process_id"] = ProcessId();
            host["cwd"] = cwdEc ? std::string("<unavailable>") : RedactPath(cwd, userDataDir, sessionTempDir);
            host["executable_dir"] = RedactPath(ResolveExecutableDir(), userDataDir, sessionTempDir);
            host["user_data_dir"] = "<user_data>";
            host["hardware_threads"] = std::thread::hardware_concurrency();
            root["host"] = std::move(host);

            nlohmann::json interaction = nlohmann::json::object();
            interaction["trigger_command"] = metadata.triggerCommand.empty() ? "/bug snapshot" : metadata.triggerCommand;
            interaction["recent_commands"] = nlohmann::json::array();
            for (const auto& record : metadata.commandHistory) {
                interaction["recent_commands"].push_back({
                    {"command", record.command},
                    {"kind", record.kind},
                    {"response_code", record.responseCode},
                    {"success", record.success}
                });
            }
            root["interaction"] = std::move(interaction);

            const auto scanStart = std::chrono::steady_clock::now();
            nlohmann::json project = nlohmann::json::object();
            project["root"] = RedactPath(projectRoot, userDataDir, sessionTempDir);
            project["limits"] = {
                {"max_files", limits.maxFiles},
                {"max_key_files", limits.maxKeyFiles},
                {"max_scan_seconds", limits.maxScanSeconds},
                {"max_hashed_bytes_per_file", limits.maxHashedBytesPerFile}
            };
            project["files"] = nlohmann::json::array();
            project["excluded_directories"] = nlohmann::json::array({".git", ".github", ".idea", ".vs", ".zeri", ".vscode", "cmake", "node_modules", "out", "vcpkg", "build", "build-debug", "build-release", "dist"});

            std::size_t scannedFiles = 0;
            std::size_t includedFiles = 0;
            std::size_t droppedNonKeyFiles = 0;
            std::size_t droppedByKeyFileLimit = 0;
            std::size_t skippedEntries = 0;
            bool timedOut = false;
            std::uint64_t includedTotalBytes = 0;

            std::error_code iterEc;
            std::filesystem::recursive_directory_iterator it(
                projectRoot,
                std::filesystem::directory_options::skip_permission_denied,
                iterEc
            );
            if (iterEc) {
                project["scan_error"] = iterEc.message();
            } else {
                for (auto end = std::filesystem::recursive_directory_iterator(); it != end; it.increment(iterEc)) {
                    if (iterEc) {
                        iterEc.clear();
                        ++skippedEntries;
                        continue;
                    }

                    const auto now = std::chrono::steady_clock::now();
                    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - scanStart).count();
                    if (elapsed > static_cast<long long>(limits.maxScanSeconds)) {
                        timedOut = true;
                        break;
                    }

                    const auto& entry = *it;
                    const auto name = entry.path().filename().string();
                    if (entry.is_directory(iterEc)) {
                        if (ShouldSkipDirectoryName(name)) {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }
                    if (iterEc) {
                        iterEc.clear();
                        ++skippedEntries;
                        continue;
                    }
                    if (!entry.is_regular_file(iterEc)) {
                        if (iterEc) {
                            iterEc.clear();
                        }
                        ++skippedEntries;
                        continue;
                    }
                    if (iterEc) {
                        iterEc.clear();
                        ++skippedEntries;
                        continue;
                    }

                    ++scannedFiles;
                    if (scannedFiles > limits.maxFiles) {
                        timedOut = true;
                        break;
                    }

                    std::error_code relEc;
                    auto relative = std::filesystem::relative(entry.path(), projectRoot, relEc);
                    if (relEc) {
                        relative = entry.path().filename();
                    }
                    if (!IsKeyFilePath(relative)) {
                        ++droppedNonKeyFiles;
                        continue;
                    }
                    if (includedFiles >= limits.maxKeyFiles) {
                        ++droppedByKeyFileLimit;
                        continue;
                    }

                    std::error_code sizeEc;
                    const auto size = std::filesystem::file_size(entry.path(), sizeEc);

                    std::error_code writeEc;
                    const auto writeTime = entry.last_write_time(writeEc);
                    std::string writeTimeText = "<unavailable>";
                    if (!writeEc) {
                        const auto sctp = std::chrono::system_clock::now() +
                            std::chrono::duration_cast<std::chrono::system_clock::duration>(writeTime - std::filesystem::file_time_type::clock::now());
                        writeTimeText = FormatTimeUtc(sctp);
                    }

                    std::string hashWarning;
                    const std::uint64_t hash = HashFileFNV1a64(entry.path(), limits.maxHashedBytesPerFile, hashWarning);
                    std::ostringstream hashOss;
                    hashOss << std::hex << std::setw(16) << std::setfill('0') << hash;

                    nlohmann::json fileItem = {
                        {"path", NormalizeSlashes(relative.string())},
                        {"size_bytes", sizeEc ? 0 : static_cast<std::uint64_t>(size)},
                        {"last_write_utc", writeTimeText},
                        {"fnv1a64", hashOss.str()}
                    };
                    if (!hashWarning.empty()) {
                        fileItem["hash_note"] = hashWarning;
                    }
                    project["files"].push_back(std::move(fileItem));
                    ++includedFiles;
                    if (!sizeEc) {
                        includedTotalBytes += static_cast<std::uint64_t>(size);
                    }
                }
            }

            project["scanned_file_count"] = scannedFiles;
            project["included_file_count"] = includedFiles;
            project["included_total_bytes"] = includedTotalBytes;
            project["dropped_non_key_files"] = droppedNonKeyFiles;
            project["dropped_by_key_file_limit"] = droppedByKeyFileLimit;
            project["skipped_entry_count"] = skippedEntries;
            project["timed_out"] = timedOut;
            root["project_snapshot"] = std::move(project);

            nlohmann::json logs = nlohmann::json::object();
            logs["sources"] = nlohmann::json::array();
            std::size_t logsTotalLines = 0;
            std::size_t logsErrorLikeLines = 0;
            std::size_t logsWarningLikeLines = 0;

            std::vector<std::string> syntheticLines;
            syntheticLines.push_back("snapshot_generated_at_utc=" + root["generated_at_utc"].get<std::string>());
            syntheticLines.push_back("trigger_command=" + (metadata.triggerCommand.empty() ? std::string("/bug snapshot") : metadata.triggerCommand));
            syntheticLines.push_back("runtime_context=" + std::string(currentCtx ? currentCtx->GetName() : "global"));
            syntheticLines.push_back("function_registry_revision=" + std::to_string(runtimeState.GetFunctionRegistryRevision()));
            syntheticLines.push_back("session_corrupted=" + std::string(runtimeState.WasSessionCorrupted() ? "true" : "false"));
            syntheticLines.push_back("help_catalog_loaded=" + std::string(helpCatalog.IsLoaded() ? "true" : "false"));
            if (!helpCatalog.LastError().empty()) {
                syntheticLines.push_back("help_catalog_error=" + helpCatalog.LastError());
            }
            syntheticLines.push_back("startup_issue_count=" + std::to_string(startupDiagnostics.issues.size()));
            for (const auto& issue : startupDiagnostics.issues) {
                syntheticLines.push_back("startup_issue[" + issue.code + "] " + issue.message + " | hint=" + issue.hint);
            }
            logs["sources"].push_back({
                {"path", "<generated>/runtime_diagnostics.log"},
                {"source", "synthetic_diagnostics"},
                {"priority", 100},
                {"size_bytes", 0},
                {"last_write_utc", root["generated_at_utc"]},
                {"tail_lines", syntheticLines}
            });
            logsTotalLines += syntheticLines.size();
            for (const auto& line : syntheticLines) {
                if (IsErrorLikeLine(line)) {
                    ++logsErrorLikeLines;
                } else if (IsWarningLikeLine(line)) {
                    ++logsWarningLikeLines;
                }
            }

            std::vector<LogCandidate> logCandidates;
            std::set<std::string> seenLogs;
            std::vector<LogRoot> logRoots;
            logRoots.push_back({ userDataDir / "logs", "user_data_logs" });
            logRoots.push_back({ userDataDir, "user_data" });
            if (!sessionTempDir.empty()) {
                logRoots.push_back({ sessionTempDir / "logs", "session_temp_logs" });
            }
            const auto executableDir = ResolveExecutableDir();
            if (!executableDir.empty()) {
                logRoots.push_back({ executableDir / "logs", "executable_logs" });
            }
            if (!projectRoot.empty()) {
                logRoots.push_back({ projectRoot / "logs", "project_logs" });
            }
            for (const auto& logRoot : logRoots) {
                std::error_code rootEc;
                const auto& rootDir = logRoot.path;
                if (!std::filesystem::exists(rootDir, rootEc) || rootEc) {
                    continue;
                }
                std::error_code logsEc;
                for (std::filesystem::recursive_directory_iterator itLogs(rootDir, std::filesystem::directory_options::skip_permission_denied, logsEc), end; itLogs != end; itLogs.increment(logsEc)) {
                    if (logsEc) {
                        logsEc.clear();
                        continue;
                    }
                    if (!itLogs->is_regular_file(logsEc)) {
                        if (logsEc) {
                            logsEc.clear();
                        }
                        continue;
                    }
                    if (!IsLogFile(itLogs->path())) {
                        continue;
                    }
                    const std::string dedupKey = ToLower(NormalizeSlashes(itLogs->path().string()));
                    if (seenLogs.contains(dedupKey)) {
                        continue;
                    }
                    seenLogs.insert(dedupKey);

                    std::error_code relEc;
                    const auto rel = std::filesystem::relative(itLogs->path(), rootDir, relEc);
                    const std::string pathText = relEc
                        ? RedactPath(itLogs->path(), userDataDir, sessionTempDir)
                        : NormalizeSlashes(rel.string());

                    std::error_code sizeEc;
                    const auto fileSize = std::filesystem::file_size(itLogs->path(), sizeEc);

                    std::error_code writeEc;
                    const auto writeTime = itLogs->last_write_time(writeEc);
                    std::string writeTimeText = "<unavailable>";
                    if (!writeEc) {
                        const auto sctp = std::chrono::system_clock::now() +
                            std::chrono::duration_cast<std::chrono::system_clock::duration>(writeTime - std::filesystem::file_time_type::clock::now());
                        writeTimeText = FormatTimeUtc(sctp);
                    }

                    LogCandidate candidate;
                    candidate.path = itLogs->path();
                    candidate.root = rootDir;
                    candidate.rootLabel = logRoot.label;
                    candidate.redactedPath = pathText;
                    candidate.sizeBytes = sizeEc ? 0 : static_cast<std::uint64_t>(fileSize);
                    candidate.lastWriteUtc = writeTimeText;
                    candidate.priority = ComputeLogPriority(itLogs->path());
                    logCandidates.push_back(std::move(candidate));
                }
            }

            std::sort(logCandidates.begin(), logCandidates.end(), [](const auto& a, const auto& b) {
                if (a.priority != b.priority) {
                    return a.priority > b.priority;
                }
                return a.lastWriteUtc > b.lastWriteUtc;
            });
            if (logCandidates.size() > limits.maxLogFiles) {
                logCandidates.resize(limits.maxLogFiles);
            }

            for (const auto& candidate : logCandidates) {
                const auto lines = ReadTailLines(candidate.path, limits.maxLogLinesPerFile, limits.maxLogBytesPerFile);
                logs["sources"].push_back({
                    {"path", candidate.redactedPath},
                    {"source", candidate.rootLabel},
                    {"priority", candidate.priority},
                    {"size_bytes", candidate.sizeBytes},
                    {"last_write_utc", candidate.lastWriteUtc},
                    {"tail_lines", lines}
                });
                logsTotalLines += lines.size();
                for (const auto& line : lines) {
                    if (IsErrorLikeLine(line)) {
                        ++logsErrorLikeLines;
                    } else if (IsWarningLikeLine(line)) {
                        ++logsWarningLikeLines;
                    }
                }
            }
            root["logs"] = std::move(logs);

            nlohmann::json health = nlohmann::json::object();
            health["complete"] = true;
            health["partial_reasons"] = nlohmann::json::array();
            if (timedOut) {
                health["complete"] = false;
                health["partial_reasons"].push_back("project_scan_timed_out");
            }
            if (includedFiles == 0U) {
                health["complete"] = false;
                health["partial_reasons"].push_back("no_key_files_collected");
            }
            if (logCandidates.empty()) {
                health["partial_reasons"].push_back("no_logs_found");
            }
            root["snapshot_health"] = std::move(health);

            std::size_t commandFailures = 0;
            std::size_t commandBySlash = 0;
            std::size_t commandByContextSwitch = 0;
            std::size_t commandByExpression = 0;
            for (const auto& record : metadata.commandHistory) {
                if (!record.success) {
                    ++commandFailures;
                }
                if (record.kind == "slash_command") {
                    ++commandBySlash;
                } else if (record.kind == "context_switch") {
                    ++commandByContextSwitch;
                } else if (record.kind == "expression") {
                    ++commandByExpression;
                }
            }

            nlohmann::json diagnosticsSummary = nlohmann::json::object();
            diagnosticsSummary["schema_version"] = 2;
            diagnosticsSummary["project"] = {
                {"scanned_file_count", scannedFiles},
                {"included_file_count", includedFiles},
                {"dropped_non_key_files", droppedNonKeyFiles},
                {"dropped_by_key_file_limit", droppedByKeyFileLimit},
                {"timed_out", timedOut}
            };
            diagnosticsSummary["interaction"] = {
                {"recent_commands_count", metadata.commandHistory.size()},
                {"failed_commands_count", commandFailures},
                {"slash_commands_count", commandBySlash},
                {"context_switch_commands_count", commandByContextSwitch},
                {"expression_commands_count", commandByExpression}
            };
            diagnosticsSummary["logs"] = {
                {"sources_count", static_cast<std::size_t>(1 + logCandidates.size())},
                {"total_tail_lines", logsTotalLines},
                {"error_like_lines", logsErrorLikeLines},
                {"warning_like_lines", logsWarningLikeLines}
            };
            diagnosticsSummary["startup"] = {
                {"issues_count", startupDiagnostics.issues.size()}
            };
            root["diagnostics_summary"] = std::move(diagnosticsSummary);

            const std::string serialized = root.dump(2);
            std::ofstream out(snapshotPath, std::ios::trunc);
            if (!out.is_open()) {
                return std::unexpected("Failed to open snapshot file for writing: " + snapshotPath.string());
            }
            out << serialized;
            if (!out.good()) {
                return std::unexpected("Failed to write snapshot file: " + snapshotPath.string());
            }

            return snapshotPath;
        } catch (const std::exception& ex) {
            return std::unexpected(std::string("Snapshot creation failed: ") + ex.what());
        }
    }

}

/*
BugSnapshot.cpp
Creates bounded, privacy-safe bug snapshots under user data bug-reports directory.
The snapshot captures runtime diagnostics, startup checks, bounded project metadata hashes,
and bounded recent app log tails without including source file contents or environment secrets.
*/
