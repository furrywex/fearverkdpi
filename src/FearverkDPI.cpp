#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <csignal>
#include <algorithm>
#include <deque>
#include <map>
#include <cstdint>

#include "windivert.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "WinDivert.lib")

// ============================================================================
// 1. ANSI COLOR CODES & UI GLYPHS
// ============================================================================
namespace UI {
    const char* RESET       = "\033[0m";
    const char* BOLD        = "\033[1m";
    const char* CLEAR_ALL   = "\033[2J\033[H";
    const char* CURSOR_HOME = "\033[H";
    const char* CURSOR_HIDE = "\033[?25l";
    const char* CURSOR_SHOW = "\033[?25h";

    const char* C_RED       = "\033[38;2;255;85;85m";
    const char* C_GREEN     = "\033[38;2;80;250;123m";
    const char* C_YELLOW    = "\033[38;2;241;250;140m";
    const char* C_BLUE      = "\033[38;2;98;114;164m";
    const char* C_MAGENTA   = "\033[38;2;255;121;198m";
    const char* C_CYAN      = "\033[38;2;139;233;253m";
    const char* C_WHITE     = "\033[38;2;248;248;242m";
    const char* C_GRAY      = "\033[38;2;98;114;164m";
    const char* C_DARKGRAY  = "\033[38;2;68;71;90m";
    const char* C_ORANGE    = "\033[38;2;255;184;108m";
}

// ============================================================================
// 2. DATA STRUCTURES & CONFIGURATION
// ============================================================================
struct GatewayNode {
    std::string id;
    std::string countryName;
    std::string countryCode;
    std::string testIp;
    int pingMs;
    bool isAlive;
};

struct PacketLogEntry {
    std::string timestamp;
    std::string protocol;
    std::string targetDomain;
    std::string srcIpPort;
    std::string dstIpPort;
    std::string action;
    uint32_t packetSize;
};

struct DomainHitStats {
    std::string domain;
    uint64_t requestCount;
    std::string lastSeenTime;
    std::string category;
};

struct AppConfig {
    std::string targetCountry  = "AUTO";
    bool blockQuic             = true;
    bool fakeTlsEnabled        = true;
    uint8_t fakeTlsTtl         = 3;
    bool splitSniEnabled       = true;
    uint32_t fallbackSplit     = 2;
    bool httpHostMix           = true;
    int refreshRateMs          = 150;
};

struct EngineMetrics {
    std::atomic<uint64_t> totalInspected{0};
    std::atomic<uint64_t> quicDropped{0};
    std::atomic<uint64_t> tlsFragmented{0};
    std::atomic<uint64_t> fakeInjections{0};
    std::atomic<uint64_t> httpModified{0};
    std::atomic<uint64_t> totalBytes{0};
};

// ============================================================================
// 3. GLOBAL ENGINE STATE
// ============================================================================
std::atomic<bool> g_Running(true);
HANDLE g_DivertHandle = INVALID_HANDLE_VALUE;
AppConfig g_Config;
EngineMetrics g_Metrics;
GatewayNode g_ActiveGateway;

std::vector<GatewayNode> g_Gateways = {
    { "JP-01", "Japan (Tokyo Gateway)",           "JP", "1.1.1.1",        999, false },
    { "US-01", "United States (Cloudflare Any)",  "US", "1.0.0.1",        999, false },
    { "DE-01", "Germany (Frankfurt Core)",        "DE", "8.8.8.8",        999, false },
    { "NL-01", "Netherlands (Amsterdam IX)",      "NL", "9.9.9.9",        999, false },
    { "SG-01", "Singapore (Asia Gateway)",        "SG", "208.67.222.222", 999, false },
    { "KR-01", "Korea (Seoul Edge)",              "KR", "8.8.4.4",        999, false }
};

std::mutex g_LogMutex;
std::deque<PacketLogEntry> g_PacketLogs;
const size_t MAX_LOG_ENTRIES = 8;

std::mutex g_DomainMutex;
std::map<std::string, DomainHitStats> g_DomainStats;

// ============================================================================
// 4. UTILITIES & TIMESTAMP
// ============================================================================
std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    struct tm buf;
    localtime_s(&buf, &in_time_t);
    ss << std::put_time(&buf, "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void PushPacketLog(const PacketLogEntry& entry) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    g_PacketLogs.push_front(entry);
    if (g_PacketLogs.size() > MAX_LOG_ENTRIES) {
        g_PacketLogs.pop_back();
    }
}

void RegisterDomainHit(const std::string& domain, const std::string& category) {
    if (domain.empty()) return;
    std::lock_guard<std::mutex> lock(g_DomainMutex);
    auto& item = g_DomainStats[domain];
    item.domain = domain;
    item.requestCount++;
    item.lastSeenTime = GetCurrentTimestamp();
    item.category = category;
}

std::string ClassifyDomain(const std::string& host) {
    std::string lower = host;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("googlevideo") != std::string::npos || lower.find("youtube") != std::string::npos || lower.find("ytimg") != std::string::npos)
        return "YouTube Media";
    if (lower.find("discord") != std::string::npos || lower.find("discordapp") != std::string::npos)
        return "Discord Voice/RTC";
    if (lower.find("roblox") != std::string::npos || lower.find("rbxcdn") != std::string::npos)
        return "Roblox Studio/Game";
    if (lower.find("twitch") != std::string::npos || lower.find("ttvnw") != std::string::npos)
        return "Twitch Stream";
    if (lower.find("spotify") != std::string::npos)
        return "Spotify Audio";
    return "HTTPS Web";
}

// ============================================================================
// 5. ICMP PROBER & NODE SELECTION
// ============================================================================
int ProbeLatency(const std::string& ipStr, int timeoutMs = 700) {
    HANDLE hIcmp = IcmpCreateFile();
    if (hIcmp == INVALID_HANDLE_VALUE) return 999;

    unsigned long ip = inet_addr(ipStr.c_str());
    if (ip == INADDR_NONE) {
        IcmpCloseHandle(hIcmp);
        return 999;
    }

    char sendBuf[32] = "FearverkProbeData";
    DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendBuf) + 8;
    std::vector<char> replyBuf(replySize);

    DWORD replies = IcmpSendEcho(hIcmp, ip, sendBuf, sizeof(sendBuf), NULL, replyBuf.data(), replySize, timeoutMs);
    int latency = 999;

    if (replies > 0) {
        PICMP_ECHO_REPLY reply = reinterpret_cast<PICMP_ECHO_REPLY>(replyBuf.data());
        if (reply->Status == IP_SUCCESS) {
            latency = static_cast<int>(reply->RoundTripTime);
            if (latency == 0) latency = 1;
        }
    }

    IcmpCloseHandle(hIcmp);
    return latency;
}

void ProbeAllGateways() {
    for (auto& gw : g_Gateways) {
        gw.pingMs = ProbeLatency(gw.testIp, 600);
        gw.isAlive = (gw.pingMs < 900);
    }
}

void SelectActiveGateway() {
    ProbeAllGateways();

    if (g_Config.targetCountry != "AUTO") {
        for (const auto& gw : g_Gateways) {
            if (_stricmp(gw.countryCode.c_str(), g_Config.targetCountry.c_str()) == 0) {
                g_ActiveGateway = gw;
                return;
            }
        }
    }

    // Default to minimum ping node
    GatewayNode bestNode = g_Gateways[0];
    for (const auto& gw : g_Gateways) {
        if (gw.pingMs < bestNode.pingMs) {
            bestNode = gw;
        }
    }
    g_ActiveGateway = bestNode;
}

void GatewayKeeperThread() {
    while (g_Running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!g_Running) break;
        int currentPing = ProbeLatency(g_ActiveGateway.testIp, 800);
        if (currentPing < 900) {
            g_ActiveGateway.pingMs = currentPing;
            g_ActiveGateway.isAlive = true;
        } else {
            g_ActiveGateway.isAlive = false;
        }
    }
}

// ============================================================================
// 6. PROTOCOL PARSERS (TLS 1.2 / 1.3 SNI EXTRACTOR & HTTP HOST)
// ============================================================================
std::string ExtractTlsSni(const uint8_t* payload, uint32_t payloadLen, int* outSplitOffset) {
    if (outSplitOffset) *outSplitOffset = -1;
    if (payloadLen < 43) return "";
    if (payload[0] != 0x16 || payload[1] != 0x03) return ""; // Not TLS Handshake
    if (payload[5] != 0x01) return "";                       // Not ClientHello

    uint32_t offset = 43; // Skip Header, Version, Random
    if (offset >= payloadLen) return "";

    uint8_t sessionIdLen = payload[offset];
    offset += 1 + sessionIdLen;

    if (offset + 2 > payloadLen) return "";
    uint16_t cipherSuitesLen = (payload[offset] << 8) | payload[offset + 1];
    offset += 2 + cipherSuitesLen;

    if (offset + 1 > payloadLen) return "";
    uint8_t compMethodsLen = payload[offset];
    offset += 1 + compMethodsLen;

    if (offset + 2 > payloadLen) return "";
    uint16_t extensionsLen = (payload[offset] << 8) | payload[offset + 1];
    offset += 2;

    uint32_t extEnd = offset + extensionsLen;
    if (extEnd > payloadLen) extEnd = payloadLen;

    while (offset + 4 <= extEnd) {
        uint16_t extType = (payload[offset] << 8) | payload[offset + 1];
        uint16_t extLen  = (payload[offset + 2] << 8) | payload[offset + 3];
        offset += 4;

        if (extType == 0x0000) { // Server Name Indication
            if (offset + 5 <= extEnd) {
                uint8_t nameType = payload[offset + 2];
                if (nameType == 0x00) {
                    uint16_t nameLen = (payload[offset + 3] << 8) | payload[offset + 4];
                    uint32_t nameStart = offset + 5;
                    if (nameStart + nameLen <= payloadLen) {
                        std::string sni(reinterpret_cast<const char*>(payload + nameStart), nameLen);
                        if (outSplitOffset) {
                            *outSplitOffset = static_cast<int>(nameStart + (nameLen / 2));
                        }
                        return sni;
                    }
                }
            }
            return "";
        }
        offset += extLen;
    }
    return "";
}

std::string ExtractHttpHost(const uint8_t* payload, uint32_t payloadLen, int* outSplitOffset) {
    if (outSplitOffset) *outSplitOffset = -1;
    if (payloadLen < 16) return "";

    std::string text(reinterpret_cast<const char*>(payload), payloadLen);
    std::string target = "Host: ";
    
    auto it = std::search(text.begin(), text.end(), target.begin(), target.end(), [](char a, char b) {
        return std::toupper(a) == std::toupper(b);
    });

    if (it != text.end()) {
        size_t startPos = std::distance(text.begin(), it) + target.length();
        size_t endPos = text.find("\r\n", startPos);
        if (endPos != std::string::npos) {
            if (outSplitOffset) {
                *outSplitOffset = static_cast<int>(startPos);
            }
            return text.substr(startPos, endPos - startPos);
        }
    }
    return "";
}

// ============================================================================
// 7. DPI BYPASS INJECTION ENGINE
// ============================================================================
void InjectFakePacket(HANDLE hDivert, const uint8_t* packet, uint32_t packetLen, PWINDIVERT_ADDRESS addr, uint8_t ttl) {
    uint8_t fakeBuf[0xFFFF];
    memcpy(fakeBuf, packet, packetLen);

    PWINDIVERT_IPHDR ipHdr = nullptr;
    PWINDIVERT_TCPHDR tcpHdr = nullptr;
    PVOID payload = nullptr;
    UINT payloadLen = 0;

    WinDivertHelperParsePacket(
        fakeBuf, packetLen,
        &ipHdr, nullptr, nullptr, nullptr, nullptr,
        &tcpHdr, nullptr,
        &payload, &payloadLen,
        nullptr, nullptr
    );

    if (ipHdr && tcpHdr && payload && payloadLen > 5) {
        ipHdr->TTL = ttl;
        uint8_t* p = reinterpret_cast<uint8_t*>(payload);
        if (payloadLen > 30) {
            p[payloadLen - 1] ^= 0xEE;
            p[payloadLen - 2] ^= 0x77;
        }

        WinDivertHelperCalcChecksums(fakeBuf, packetLen, addr, 0);
        WinDivertSend(hDivert, fakeBuf, packetLen, nullptr, addr);
        g_Metrics.fakeInjections++;
    }
}

void ExecuteTcpSplit(HANDLE hDivert, const uint8_t* packet, uint32_t packetLen, PWINDIVERT_ADDRESS addr, uint32_t splitPos) {
    PWINDIVERT_IPHDR ipHdr = nullptr;
    PWINDIVERT_TCPHDR tcpHdr = nullptr;
    PVOID payload = nullptr;
    UINT payloadLen = 0;

    WinDivertHelperParsePacket(
        const_cast<uint8_t*>(packet), packetLen,
        &ipHdr, nullptr, nullptr, nullptr, nullptr,
        &tcpHdr, nullptr,
        &payload, &payloadLen,
        nullptr, nullptr
    );

    if (!ipHdr || !tcpHdr || !payload || payloadLen <= splitPos) {
        WinDivertSend(hDivert, packet, packetLen, nullptr, addr);
        return;
    }

    uint32_t hdrLen = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(payload) - packet);
    uint8_t* pData = reinterpret_cast<uint8_t*>(payload);

    // PART 1
    uint8_t p1[0xFFFF];
    uint32_t p1Len = hdrLen + splitPos;
    memcpy(p1, packet, p1Len);

    PWINDIVERT_IPHDR ip1 = reinterpret_cast<PWINDIVERT_IPHDR>(p1);
    PWINDIVERT_TCPHDR tcp1 = reinterpret_cast<PWINDIVERT_TCPHDR>(p1 + (ipHdr->HdrLength * 4));
    ip1->Length = htons(static_cast<uint16_t>(p1Len));

    WinDivertHelperCalcChecksums(p1, p1Len, addr, 0);
    WinDivertSend(hDivert, p1, p1Len, nullptr, addr);

    // PART 2
    uint8_t p2[0xFFFF];
    uint32_t p2DataLen = payloadLen - splitPos;
    uint32_t p2Len = hdrLen + p2DataLen;

    memcpy(p2, packet, hdrLen);
    memcpy(p2 + hdrLen, pData + splitPos, p2DataLen);

    PWINDIVERT_IPHDR ip2 = reinterpret_cast<PWINDIVERT_IPHDR>(p2);
    PWINDIVERT_TCPHDR tcp2 = reinterpret_cast<PWINDIVERT_TCPHDR>(p2 + (ipHdr->HdrLength * 4));
    ip2->Length = htons(static_cast<uint16_t>(p2Len));
    tcp2->SeqNum = htonl(ntohl(tcpHdr->SeqNum) + splitPos);

    WinDivertHelperCalcChecksums(p2, p2Len, addr, 0);
    WinDivertSend(hDivert, p2, p2Len, nullptr, addr);

    g_Metrics.tlsFragmented++;
}

// ============================================================================
// 8. ADVANCED TEXT USER INTERFACE (TUI DASHBOARD)
// ============================================================================
void RenderTuiDashboard(auto startTime) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - startTime
    ).count();

    int hrs  = static_cast<int>(elapsed / 3600);
    int mins = static_cast<int>((elapsed % 3600) / 60);
    int secs = static_cast<int>(elapsed % 60);

    double totalMB = static_cast<double>(g_Metrics.totalBytes.load()) / (1024.0 * 1024.0);

    std::stringstream out;
    out << UI::CURSOR_HOME;

    // Header Title Bar
    out << UI::C_CYAN << UI::BOLD;
    out << "  ╔═══════════════════════════════════════════════════════════════════════════════════════════╗\n";
    out << "  ║                fearverk dpi - by furrywex on github! good luck with using!                ║\n";
    out << "  ╚═══════════════════════════════════════════════════════════════════════════════════════════╝\n" << UI::RESET;

    // Section 1: Active Gateway & Health Status
    std::string pingDisplay = (g_ActiveGateway.isAlive) 
        ? (std::to_string(g_ActiveGateway.pingMs) + " ms") 
        : "UNREACHABLE";
    std::string statusBadge = (g_ActiveGateway.isAlive) 
        ? std::string(UI::C_GREEN) + "● RUNNING (SECURE)" + UI::RESET 
        : std::string(UI::C_RED) + "○ DEGRADED" + UI::RESET;

    out << "  " << UI::C_DARKGRAY << "┌─[ ACTIVE PROFILE & NODE ]────────────────────────────────────────────────────────────────┐\n" << UI::RESET;
    out << "  " << UI::C_DARKGRAY << "│" << UI::RESET
        << "  Target Node : " << UI::C_WHITE << UI::BOLD << std::setw(28) << std::left << (g_ActiveGateway.countryName) << UI::RESET
        << "  │ Profile Code : " << UI::C_YELLOW << std::setw(8) << g_Config.targetCountry << UI::RESET
        << "  │ Status: " << statusBadge << "\n";
    out << "  " << UI::C_DARKGRAY << "│" << UI::RESET
        << "  Edge Address: " << UI::C_CYAN << std::setw(28) << std::left << g_ActiveGateway.testIp << UI::RESET
        << "  │ Live Latency : ";
    
    if (g_ActiveGateway.pingMs < 100) out << UI::C_GREEN;
    else if (g_ActiveGateway.pingMs < 250) out << UI::C_YELLOW;
    else out << UI::C_RED;

    out << std::setw(8) << std::left << pingDisplay << UI::RESET
        << "  │ Uptime: " << UI::C_WHITE << std::setw(2) << std::setfill('0') << hrs << ":"
        << std::setw(2) << std::setfill('0') << mins << ":"
        << std::setw(2) << std::setfill('0') << secs << UI::RESET << "\n";
    out << "  " << UI::C_DARKGRAY << "└──────────────────────────────────────────────────────────────────────────────────────────┘\n" << UI::RESET;

    // Section 2: Real-time Telemetry Counters
    out << "  " << UI::C_DARKGRAY << "┌─[ LIVE TRAFFIC METRICS ]─────────────────────────────────────────────────────────────────┐\n" << UI::RESET;
    out << "  " << UI::C_DARKGRAY << "│" << UI::RESET
        << "  Packets Scanned: " << UI::C_WHITE << std::setw(12) << std::left << g_Metrics.totalInspected.load() << UI::RESET
        << " │ SNI Fragmented : " << UI::C_YELLOW << std::setw(10) << g_Metrics.tlsFragmented.load() << UI::RESET
        << " │ QUIC Blocked  : " << UI::C_CYAN << std::setw(10) << g_Metrics.quicDropped.load() << UI::RESET << "\n";
    out << "  " << UI::C_DARKGRAY << "│" << UI::RESET
        << "  Data Processed : " << UI::C_WHITE << std::setw(9) << std::left << std::fixed << std::setprecision(2) << totalMB << " MB" << UI::RESET
        << " │ Fake Injected  : " << UI::C_MAGENTA << std::setw(10) << g_Metrics.fakeInjections.load() << UI::RESET
        << " │ HTTP Desynced : " << UI::C_GREEN << std::setw(10) << g_Metrics.httpModified.load() << UI::RESET << "\n";
    out << "  " << UI::C_DARKGRAY << "└──────────────────────────────────────────────────────────────────────────────────────────┘\n" << UI::RESET;

    // Section 3: Intercepted Services & Hostnames
    out << "  " << UI::C_DARKGRAY << "┌─[ INTERCEPTED DOMAINS & CLOUD ENDPOINTS ]────────────────────────────────────────────────┐\n" << UI::RESET;
    out << "  " << UI::C_DARKGRAY << "│ " << UI::C_WHITE << UI::BOLD
        << std::setw(34) << std::left << "DOMAIN / HOSTNAME" 
        << std::setw(22) << "SERVICE CLASS" 
        << std::setw(14) << "HITS" 
        << std::setw(14) << "LAST SEEN" << UI::C_DARKGRAY << "│\n" << UI::RESET;
    out << "  " << UI::C_DARKGRAY << "├──────────────────────────────────────────────────────────────────────────────────────────┤\n" << UI::RESET;

    {
        std::lock_guard<std::mutex> lock(g_DomainMutex);
        int rows = 0;
        for (auto it = g_DomainStats.rbegin(); it != g_DomainStats.rend() && rows < 5; ++it, ++rows) {
            const auto& item = it->second;
            out << "  " << UI::C_DARKGRAY << "│ " << UI::C_CYAN
                << std::setw(34) << std::left << (item.domain.length() > 32 ? item.domain.substr(0, 29) + "..." : item.domain)
                << UI::C_YELLOW << std::setw(22) << item.category
                << UI::C_WHITE  << std::setw(14) << item.requestCount
                << UI::C_GRAY   << std::setw(14) << item.lastSeenTime
                << UI::C_DARKGRAY << "│\n" << UI::RESET;
        }
        for (; rows < 5; ++rows) {
            out << "  " << UI::C_DARKGRAY << "│ " << std::setw(84) << " " << "│\n" << UI::RESET;
        }
    }
    out << "  " << UI::C_DARKGRAY << "└──────────────────────────────────────────────────────────────────────────────────────────┘\n" << UI::RESET;

    // Section 4: Deformed Packet Stream Log
    out << "  " << UI::C_DARKGRAY << "┌─[ REAL-TIME DEFORMED PACKET STREAM ]─────────────────────────────────────────────────────┐\n" << UI::RESET;
    out << "  " << UI::C_DARKGRAY << "│ " << UI::C_WHITE << UI::BOLD
        << std::setw(12) << std::left << "TIME"
        << std::setw(8)  << "PROTO"
        << std::setw(26) << "TARGET HOST"
        << std::setw(18) << "ENGINE ACTION"
        << std::setw(10) << "PAYLOAD"
        << std::setw(10) << "STATUS" << UI::C_DARKGRAY << "│\n" << UI::RESET;
    out << "  " << UI::C_DARKGRAY << "├──────────────────────────────────────────────────────────────────────────────────────────┤\n" << UI::RESET;

    {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        size_t count = 0;
        for (const auto& log : g_PacketLogs) {
            if (count >= 6) break;
            out << "  " << UI::C_DARKGRAY << "│ " << UI::C_GRAY << std::setw(12) << std::left << log.timestamp
                << UI::C_WHITE << std::setw(8) << log.protocol
                << UI::C_CYAN  << std::setw(26) << (log.targetDomain.length() > 24 ? log.targetDomain.substr(0, 22) + ".." : log.targetDomain)
                << UI::C_YELLOW << std::setw(18) << log.action
                << UI::C_WHITE  << std::setw(10) << (std::to_string(log.packetSize) + " B")
                << UI::C_GREEN  << std::setw(10) << "BYPASSED" << UI::C_DARKGRAY << "│\n" << UI::RESET;
            count++;
        }
        for (; count < 6; ++count) {
            out << "  " << UI::C_DARKGRAY << "│ " << std::setw(84) << " " << "│\n" << UI::RESET;
        }
    }
    out << "  " << UI::C_DARKGRAY << "└──────────────────────────────────────────────────────────────────────────────────────────┘\n" << UI::RESET;
    out << "  " << UI::C_ORANGE << "  [ESC / CTRL+C] Shutdown Engine   │   [DPI Filter: ACTIVE]   │   [Architecture: x64 WinDivert]" << UI::RESET << "\n";

    std::cout << out.str() << std::flush;
}

void TuiRenderWorker() {
    auto startTime = std::chrono::steady_clock::now();
    std::cout << UI::CLEAR_ALL;

    while (g_Running) {
        RenderTuiDashboard(startTime);
        std::this_thread::sleep_for(std::chrono::milliseconds(g_Config.refreshRateMs));
    }
}

// ============================================================================
// 9. COMMAND LINE PARSER
// ============================================================================
void ParseCommandLineArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--country" || arg == "-c") && i + 1 < argc) {
            g_Config.targetCountry = argv[++i];
        } else if (arg == "--no-quic") {
            g_Config.blockQuic = false;
        } else if (arg == "--no-fake") {
            g_Config.fakeTlsEnabled = false;
        } else if (arg == "--ttl" && i + 1 < argc) {
            g_Config.fakeTlsTtl = static_cast<uint8_t>(std::stoi(argv[++i]));
        }
    }
}

void SignalHandler(int) {
    g_Running = false;
    if (g_DivertHandle != INVALID_HANDLE_VALUE) {
        WinDivertClose(g_DivertHandle);
        g_DivertHandle = INVALID_HANDLE_VALUE;
    }
}

// ============================================================================
// 10. ENTRY POINT & CORE PIPELINE
// ============================================================================
int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    std::cout << UI::CURSOR_HIDE;

    ParseCommandLineArgs(argc, argv);
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    SelectActiveGateway();

    // Outbound IPv4 HTTPS/HTTP TCP with payload + QUIC UDP 443
    const char* filter = 
        "outbound && ip && "
        "((tcp && (tcp.DstPort == 443 || tcp.DstPort == 80) && tcp.PayloadLength > 0) || "
        "(udp && udp.DstPort == 443))";

    g_DivertHandle = WinDivertOpen(filter, WINDIVERT_LAYER_NETWORK, 0, 0);

    if (g_DivertHandle == INVALID_HANDLE_VALUE) {
        std::cout << UI::CURSOR_SHOW << UI::C_RED << "\n[!] FATAL ERROR: Unable to load WinDivert kernel driver.\n";
        std::cout << "    -> Ensure you are running this executable as ADMINISTRATOR.\n";
        std::cout << "    -> Ensure WinDivert.dll and WinDivert64.sys exist in runtime path.\n\n" << UI::RESET;
        system("pause");
        return 1;
    }

    std::thread tuiThread(TuiRenderWorker);
    std::thread keeperThread(GatewayKeeperThread);

    uint8_t packet[0xFFFF];
    UINT packetLen = 0;
    WINDIVERT_ADDRESS addr;
    PWINDIVERT_IPHDR ipHdr = nullptr;
    PWINDIVERT_TCPHDR tcpHdr = nullptr;
    PWINDIVERT_UDPHDR udpHdr = nullptr;
    PVOID payload = nullptr;
    UINT payloadLen = 0;

    char srcIp[INET_ADDRSTRLEN], dstIp[INET_ADDRSTRLEN];

    while (g_Running) {
        if (!WinDivertRecv(g_DivertHandle, packet, sizeof(packet), &packetLen, &addr)) {
            if (!g_Running) break;
            continue;
        }

        g_Metrics.totalInspected++;
        g_Metrics.totalBytes += packetLen;

        WinDivertHelperParsePacket(
            packet, packetLen,
            &ipHdr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            &tcpHdr,
            &udpHdr,
            &payload,
            &payloadLen,
            nullptr,
            nullptr
        );

        if (ipHdr) {
            inet_ntop(AF_INET, &ipHdr->SrcAddr, srcIp, INET_ADDRSTRLEN);
            inet_ntop(AF_INET, &ipHdr->DstAddr, dstIp, INET_ADDRSTRLEN);
        }

        // 1. QUIC (HTTP/3 UDP) DROP
        if (udpHdr != nullptr && g_Config.blockQuic) {
            g_Metrics.quicDropped++;
            PacketLogEntry entry;
            entry.timestamp = GetCurrentTimestamp();
            entry.protocol = "UDP/443";
            entry.targetDomain = "QUIC Media Handshake";
            entry.srcIpPort = std::string(srcIp) + ":" + std::to_string(ntohs(udpHdr->SrcPort));
            entry.dstIpPort = std::string(dstIp) + ":" + std::to_string(ntohs(udpHdr->DstPort));
            entry.action = "QUIC-DROP";
            entry.packetSize = packetLen;
            PushPacketLog(entry);
            continue;
        }

        // 2. HTTPS TCP 443 BYPASS
        if (tcpHdr != nullptr && payloadLen > 0) {
            uint8_t* pData = reinterpret_cast<uint8_t*>(payload);

            if (payloadLen > 5 && pData[0] == 0x16 && pData[1] == 0x03) {
                int splitOffset = -1;
                std::string sni = ExtractTlsSni(pData, payloadLen, &splitOffset);

                if (!sni.empty()) {
                    std::string category = ClassifyDomain(sni);
                    RegisterDomainHit(sni, category);

                    PacketLogEntry logEntry;
                    logEntry.timestamp = GetCurrentTimestamp();
                    logEntry.protocol = "TLS/443";
                    logEntry.targetDomain = sni;
                    logEntry.srcIpPort = std::string(srcIp) + ":" + std::to_string(ntohs(tcpHdr->SrcPort));
                    logEntry.dstIpPort = std::string(dstIp) + ":" + std::to_string(ntohs(tcpHdr->DstPort));
                    logEntry.action = "SNI-SPLIT+FAKE";
                    logEntry.packetSize = packetLen;
                    PushPacketLog(logEntry);
                }

                uint32_t splitPos = (splitOffset > 0) ? static_cast<uint32_t>(splitOffset) : g_Config.fallbackSplit;

                if (g_Config.fakeTlsEnabled) {
                    InjectFakePacket(g_DivertHandle, packet, packetLen, &addr, g_Config.fakeTlsTtl);
                }

                ExecuteTcpSplit(g_DivertHandle, packet, packetLen, &addr, splitPos);
                continue;
            }

            // 3. HTTP TCP 80 BYPASS
            if (g_Config.httpHostMix && tcpHdr->DstPort == htons(80)) {
                int hostOffset = -1;
                std::string host = ExtractHttpHost(pData, payloadLen, &hostOffset);
                if (hostOffset > 0) {
                    RegisterDomainHit(host, "HTTP Plain");
                    ExecuteTcpSplit(g_DivertHandle, packet, packetLen, &addr, static_cast<uint32_t>(hostOffset));
                    g_Metrics.httpModified++;
                    continue;
                }
            }
        }

        WinDivertSend(g_DivertHandle, packet, packetLen, nullptr, &addr);
    }

    if (tuiThread.joinable()) tuiThread.join();
    if (keeperThread.joinable()) keeperThread.join();

    std::cout << UI::CURSOR_SHOW << UI::C_GREEN << "\n[+] Fearverk DPI cleanly stopped. Driver unloaded.\n" << UI::RESET;
    return 0;
}