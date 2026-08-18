// Chuniboard-Chunithm-Server
// Receives Brokenithm-Android UDP/TCP input and:
//  1. Writes to BROKENITHM_SHARED_BUFFER shared memory (for chuniio.dll / segatools path32=)
//  2. Sends Windows SendInput keyboard events (Yuancon layout, for simulators)

#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <inttypes.h>

#define _CRT_SECURE_NO_WARNINGS

#include "socket.h"
#include "defer.h"
#include "version.h"
#include "struct.h"
#include <windows.h>

// ── Windows shims for POSIX APIs ─────────────────────────────────────────────

// gettimeofday() – winsock2.h (via socket.h) already defines struct timeval
static inline int gettimeofday(struct timeval *tv, void *)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    t -= 116444736000000000ULL;
    tv->tv_sec  = (long)(t / 10000000ULL);
    tv->tv_usec = (long)((t % 10000000ULL) / 10ULL);
    return 0;
}

// getopt() – minimal POSIX-compatible implementation for Windows
static int   optind = 1;
static char *optarg = nullptr;
static int getopt(int argc, char *argv[], const char *optstring)
{
    if (optind >= argc || argv[optind][0] != '-') return -1;
    char opt = argv[optind][1];
    if (opt == '\0') return -1;
    ++optind;
    const char *p = strchr(optstring, opt);
    if (!p) return '?';
    if (p[1] == ':') {
        if (optind >= argc) return '?';
        optarg = argv[optind++];
    }
    return (unsigned char)opt;
}
// ─────────────────────────────────────────────────────────────────────────────

// ── Yuancon keyboard layout (32 slider + 6 air) ──────────────────────────────
// Matches brokenithm-kb KeyboardSimulator YUANCON_BTN_MAP.
// Slider cells 0-31 (right to left), then air sensors 32-37 (low to high).
static const WORD YUANCON_KEY_MAP[38] = {
    '6','5','4','3','2','1','Z','Y',
    'X','W','V','U','T','S','R','Q',
    'P','O','N','M','L','K','J','I',
    'H','G','F','E','D','C','B','A',
    VK_OEM_MINUS, VK_OEM_PLUS, VK_OEM_4,
    VK_OEM_6,     VK_OEM_5,    VK_OEM_1
};

// SendInput function pointer (resolved at runtime to avoid linker issues)
using SendInputFn = UINT(WINAPI *)(UINT, LPINPUT, int);
static SendInputFn g_SendInput = reinterpret_cast<SendInputFn>(
    GetProcAddress(GetModuleHandleW(L"user32"), "SendInput"));

// Sends key-down / key-up events for changed bits between prev and cur states.
// state bits: bits 0-31 = slider cells, bits 32-37 = air sensors.
static void send_keyboard(uint64_t prev, uint64_t cur)
{
    static INPUT buf[38];
    int n = 0;
    uint64_t changed = prev ^ cur;
    for (int i = 0; i < 38 && changed; i++, changed >>= 1)
    {
        if (!(changed & 1)) continue;
        buf[n].type          = INPUT_KEYBOARD;
        buf[n].ki.wVk        = YUANCON_KEY_MAP[i];
        buf[n].ki.wScan      = 0;
        buf[n].ki.dwFlags    = (cur & ((uint64_t)1 << i)) ? 0 : KEYEVENTF_KEYUP;
        buf[n].ki.time       = 0;
        buf[n].ki.dwExtraInfo= 0;
        n++;
    }
    if (n && g_SendInput)
        g_SendInput(n, buf, sizeof(INPUT));
}
// ─────────────────────────────────────────────────────────────────────────────

std::string remote_address;
uint16_t remote_port = 52468;
uint16_t server_port = 52468;
bool tcp_mode = false;
bool kb_mode  = false;   // also emit keyboard events

size_t tcp_buffer_size      = 96;
size_t tcp_receive_threshold= 48;

std::atomic_bool EXIT_FLAG{false}, CONNECTED{false};

// ── Shared memory helpers ─────────────────────────────────────────────────────
static HANDLE           g_hMapFile = NULL;
static IPCMemoryInfo   *g_memory   = nullptr;

static bool shm_open_or_create()
{
    const char *name = "Local\\BROKENITHM_SHARED_BUFFER";
    g_hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!g_hMapFile)
        g_hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                        PAGE_READWRITE, 0,
                                        sizeof(IPCMemoryInfo), name);
    if (!g_hMapFile) return false;
    g_memory = reinterpret_cast<IPCMemoryInfo *>(
        MapViewOfFileEx(g_hMapFile, FILE_MAP_ALL_ACCESS,
                        0, 0, sizeof(IPCMemoryInfo), nullptr));
    if (!g_memory) return false;
    memset(g_memory, 0, sizeof(IPCMemoryInfo));
    return true;
}
// ─────────────────────────────────────────────────────────────────────────────

void socketSetTimeout(SOCKET sHost, int timeout)
{
    setsockopt(sHost, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(int));
    setsockopt(sHost, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(int));
}

int socketBind(SOCKET sHost, long addr, uint16_t port)
{
    sockaddr_in src = {};
    src.sin_family      = AF_INET;
    src.sin_addr.s_addr = addr;
    src.sin_port        = htons(port);
    return bind(sHost, reinterpret_cast<sockaddr *>(&src), sizeof(src));
}

sockaddr_in makeIPv4Addr(const std::string &host, uint16_t port)
{
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, host.data(), &addr.sin_addr.s_addr);
    addr.sin_port = htons(port);
    return addr;
}

int socketSendTo(SOCKET sHost, const sockaddr_in &addr, const std::string &data)
{
    return sendto(sHost, data.data(), (int)data.size(), 0,
                  reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
}

std::string getTime(int type)
{
    time_t lt;
    char tmpbuf[32], cMillis[7];
    std::string format;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    snprintf(cMillis, sizeof(cMillis), "%06ld", (long)tv.tv_usec);
    lt = time(nullptr);
    struct tm *local = localtime(&lt);
    switch (type) {
    case 1: format = "%Y%m%d-%H%M%S"; break;
    case 2: format = "%Y/%m/%d %a %H:%M:%S." + std::string(cMillis); break;
    default:format = "%Y-%m-%d %H:%M:%S"; break;
    }
    strftime(tmpbuf, sizeof(tmpbuf), format.data(), local);
    return std::string(tmpbuf);
}

template<typename... Args>
void printErr(const char *fmt, Args... args)
{
    std::string t = "[" + getTime(2) + "] ";
    fprintf(stderr, t.data());
    fprintf(stderr, fmt, args...);
}

// ── Debug verbose logging (compiled out in NDEBUG/Release builds) ─────────────
// Build with -DCMAKE_BUILD_TYPE=Debug to enable. Output goes to stderr.
#ifndef NDEBUG
#  define VERBOSE(fmt, ...) printErr("[DBG] " fmt, ##__VA_ARGS__)
#else
#  define VERBOSE(fmt, ...) ((void)0)
#endif
// ─────────────────────────────────────────────────────────────────────────────

// ── LED broadcast thread ──────────────────────────────────────────────────────
void threadLEDBroadcast(SOCKET sHost, const IPCMemoryInfo *memory)
{
    static std::string prev_status;
    static int skip = 0;
    static std::string head = "\x63LED";
    auto addr = makeIPv4Addr(remote_address, remote_port);
    while (!EXIT_FLAG) {
        if (!CONNECTED) { Sleep(50); continue; }
        std::string cur;
        cur.assign(reinterpret_cast<const char *>(memory->ledRgbData),
                   sizeof(memory->ledRgbData));
        bool same = !prev_status.empty() &&
                    (memcmp(prev_status.data(), cur.data(), prev_status.size()) == 0);
        prev_status = cur;
        if (!same || ++skip > 50) {
            cur.insert(0, head);
            socketSendTo(sHost, addr, cur);
            skip = 0;
        }
        Sleep(10);
    }
}

// ── Input receive thread ──────────────────────────────────────────────────────
enum { FUNCTION_COIN = 1, FUNCTION_CARD };
enum { CARD_AIME, CARD_FELICA };

uint32_t last_input_packet_id = 0;

void updatePacketId(uint32_t newId)
{
    // Only advance monotonically: ignore stale out-of-order UDP packets.
    // If we set last_id backwards, every subsequent in-order packet would
    // falsely appear as a huge drop, flooding the log with noise.
    if (newId <= last_input_packet_id) return;

    if (newId > last_input_packet_id + 1 && last_input_packet_id != 0)
        printErr("[WARN] Dropped %u packet(s) before #%u\n",
                 newId - last_input_packet_id - 1, newId);
    last_input_packet_id = newId;
}

void getSocksAddress(const PacketConnect *pkt,
                     std::string &address, uint16_t &port)
{
    char cAddr[128] = {};
    port = ntohs(pkt->port);
    switch (pkt->addrType) {
    case 1: inet_ntop(AF_INET,  pkt->addr.addr4.addr, cAddr, 127); break;
    case 2: inet_ntop(AF_INET6, pkt->addr.addr6,      cAddr, 127); break;
    }
    address.assign(cAddr);
}

// Build a 64-bit keyboard bitmask from slider + air state in shared memory.
// Bits 0-31 = slider cells (from memory->sliderIoStatus, threshold 20)
// Bits 32-37 = air sensors (from memory->airIoStatus)
static uint64_t make_kb_state(const IPCMemoryInfo *mem)
{
    uint64_t state = 0;
    for (int i = 0; i < 32; i++)
        if (mem->sliderIoStatus[i] >= 20)
            state |= (uint64_t)1 << i;
    for (int i = 0; i < 6; i++)
        if (mem->airIoStatus[i])
            state |= (uint64_t)1 << (32 + i);
    return state;
}

void threadInputReceive(SOCKET sHost, IPCMemoryInfo *memory)
{
    std::vector<char> recv_buffer(tcp_buffer_size);
    char buffer[BUFSIZ];
    std::string remains;
    uint64_t kb_prev = 0;
    auto addr = makeIPv4Addr(remote_address, remote_port);

    while (!EXIT_FLAG) {
        int recv_len, real_len;
        size_t packet_len;
        uint32_t current_packet_id;

        if (!tcp_mode) {
            if ((recv_len = recvfrom(sHost, buffer, BUFSIZ - 1, 0, nullptr, nullptr)) == -1)
                continue;
            real_len   = buffer[0];
            if (real_len > recv_len) continue;
            packet_len = real_len + 1;
        } else {
            if (remains.size() < tcp_receive_threshold) {
                if ((recv_len = recv(sHost, recv_buffer.data(),
                                     (int)(tcp_buffer_size - 1), 0)) == -1)
                    continue;
                remains.append(recv_buffer.data(), recv_len);
            }
            int data_left = (int)remains.size();
            real_len      = remains[0];
            if (real_len > data_left) continue;
            packet_len    = real_len + 1;
            memcpy(buffer, remains.data(), packet_len);
            remains.erase(0, packet_len);
        }

        // Input (with air)
        if (packet_len >= sizeof(PacketInput) &&
            buffer[1]=='I' && buffer[2]=='N' && buffer[3]=='P')
        {
            auto *pkt = reinterpret_cast<PacketInput *>(buffer);
            memcpy(memory->airIoStatus,    pkt->airIoStatus,    sizeof(pkt->airIoStatus));
            memcpy(memory->sliderIoStatus, pkt->sliderIoStatus, sizeof(pkt->sliderIoStatus));
            memory->testBtn    = pkt->testBtn;
            memory->serviceBtn = pkt->serviceBtn;
            current_packet_id  = ntohl(pkt->packetId);
            updatePacketId(current_packet_id);
#ifndef NDEBUG
            {
                static uint8_t dbg_air[6]  = {};
                static uint8_t dbg_sld[32] = {};
                static uint8_t dbg_test    = 0, dbg_svc = 0;
                bool ac = memcmp(dbg_air, pkt->airIoStatus,    6)  != 0;
                bool sc = memcmp(dbg_sld, pkt->sliderIoStatus, 32) != 0;
                bool bc = (dbg_test != pkt->testBtn) || (dbg_svc != pkt->serviceBtn);
                if (ac || sc || bc) {
                    memcpy(dbg_air, pkt->airIoStatus,    6);
                    memcpy(dbg_sld, pkt->sliderIoStatus, 32);
                    dbg_test = pkt->testBtn; dbg_svc = pkt->serviceBtn;
                    uint8_t airBits = 0;
                    for (int _i = 0; _i < 6;  _i++) if (pkt->airIoStatus[_i])           airBits |= (uint8_t)(1u << _i);
                    int sldCnt = 0;
                    for (int _i = 0; _i < 32; _i++) if (pkt->sliderIoStatus[_i] >= 20)  sldCnt++;
                    VERBOSE("INP #%-5u  air=0x%02X  slider=%d/32  test=%d svc=%d\n",
                            current_packet_id, airBits, sldCnt,
                            pkt->testBtn, pkt->serviceBtn);
                }
            }
#endif
        }
        // Input (without air)
        else if (packet_len >= sizeof(PacketInputNoAir) &&
                 buffer[1]=='I' && buffer[2]=='P' && buffer[3]=='T')
        {
            auto *pkt = reinterpret_cast<PacketInputNoAir *>(buffer);
            memcpy(memory->sliderIoStatus, pkt->sliderIoStatus, sizeof(pkt->sliderIoStatus));
            memory->testBtn    = pkt->testBtn;
            memory->serviceBtn = pkt->serviceBtn;
            current_packet_id  = ntohl(pkt->packetId);
            updatePacketId(current_packet_id);
#ifndef NDEBUG
            {
                static uint8_t dbg_sld[32] = {};
                static uint8_t dbg_test    = 0, dbg_svc = 0;
                bool sc = memcmp(dbg_sld, pkt->sliderIoStatus, 32) != 0;
                bool bc = (dbg_test != pkt->testBtn) || (dbg_svc != pkt->serviceBtn);
                if (sc || bc) {
                    memcpy(dbg_sld, pkt->sliderIoStatus, 32);
                    dbg_test = pkt->testBtn; dbg_svc = pkt->serviceBtn;
                    int sldCnt = 0;
                    for (int _i = 0; _i < 32; _i++) if (pkt->sliderIoStatus[_i] >= 20) sldCnt++;
                    VERBOSE("IPT #%-5u  slider=%d/32  test=%d svc=%d\n",
                            current_packet_id, sldCnt, pkt->testBtn, pkt->serviceBtn);
                }
            }
#endif
        }
        // Function button (coin / card)
        else if (packet_len >= sizeof(PacketFunction) &&
                 buffer[1]=='F' && buffer[2]=='N' && buffer[3]=='C')
        {
            auto *pkt = reinterpret_cast<PacketFunction *>(buffer);
            if (pkt->funcBtn == FUNCTION_COIN) memory->coinInsertion = 1;
            if (pkt->funcBtn == FUNCTION_CARD) memory->cardRead      = 1;
            VERBOSE("FNC  button=%s\n",
                    pkt->funcBtn == FUNCTION_COIN ? "COIN" :
                    pkt->funcBtn == FUNCTION_CARD ? "CARD" : "?");
        }
        // Connect
        else if (packet_len >= sizeof(PacketConnect) &&
                 buffer[1]=='C' && buffer[2]=='O' && buffer[3]=='N')
        {
            last_input_packet_id = 0;
            auto *pkt = reinterpret_cast<PacketConnect *>(buffer);
            getSocksAddress(pkt, remote_address, remote_port);
            printErr("[INFO] Device %s:%d connected.\n",
                     remote_address.data(), remote_port);
            CONNECTED = true;
        }
        // Disconnect
        else if (packet_len >= 4 &&
                 buffer[1]=='D' && buffer[2]=='I' && buffer[3]=='S')
        {
            CONNECTED = false;
            if (tcp_mode) { EXIT_FLAG = true; break; }
            if (!remote_address.empty()) {
                printErr("[INFO] Device %s:%d disconnected.\n",
                         remote_address.data(), remote_port);
                remote_address.clear();
            }
        }
        // Ping
        else if (packet_len >= sizeof(PacketPing) &&
                 buffer[1]=='P' && buffer[2]=='I' && buffer[3]=='N')
        {
            if (!CONNECTED) continue;
            std::string resp;
            resp.assign(buffer, 12);
            resp.replace(2, 1, "O");
            socketSendTo(sHost, addr, resp);
        }
        // Remote card
        else if (packet_len >= sizeof(PacketCard) &&
                 buffer[1]=='C' && buffer[2]=='R' && buffer[3]=='D')
        {
            auto *pkt = reinterpret_cast<PacketCard *>(buffer);
            memory->remoteCardRead = pkt->remoteCardRead;
            memory->remoteCardType = pkt->remoteCardType;
            memcpy(memory->remoteCardId, pkt->remoteCardId, 10);
            {
                char _hex[21] = {};
                for (int _i = 0; _i < 10; _i++)
                    snprintf(_hex + _i*2, 3, "%02X", (unsigned char)pkt->remoteCardId[_i]);
                VERBOSE("CRD  type=%-6s  id=%s\n",
                        pkt->remoteCardType == CARD_FELICA ? "FeliCa" : "Aime", _hex);
            }
        }

        // Emit keyboard events if --kb flag is set
        if (kb_mode && CONNECTED) {
            uint64_t kb_cur = make_kb_state(memory);
            if (kb_cur != kb_prev) {
                VERBOSE("KB   air=0x%02X  sliders=%d/32\n",
                        (unsigned)((kb_cur >> 32) & 0x3F),
                        /* popcount lo32 */ [&]{ int n=0; for(int _i=0;_i<32;_i++) n+=(int)((kb_cur>>_i)&1); return n; }());
                send_keyboard(kb_prev, kb_cur);
                kb_prev = kb_cur;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void printInfo()
{
    printf("=================================================\n");
    printf("=       Chuniboard-Chunithm-Server              =\n");
    printf("=     Brokenithm Android -> segatools/KB        =\n");
    printf("=               " VERSION "                      =\n");
    printf("=================================================\n\n");
    printf("Modes:\n");
    printf("  (default)  write to BROKENITHM_SHARED_BUFFER\n");
    printf("             (use segatools.ini [chuniio] path32=chunihook.dll)\n");
    printf("  --kb       also emit Yuancon keyboard events\n\n");
#ifndef NDEBUG
    printf("[DEBUG BUILD \xe2\x80\x94 verbose console logging enabled]\n\n");
#endif
}

void checkArgs(int argc, char *argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "p:Tr:k")) != -1) {
        switch (opt) {
        case 'p': server_port = (uint16_t)atoi(optarg); break;
        case 'T': tcp_mode    = true; break;
        case 'r':
            tcp_receive_threshold = (size_t)atoi(optarg);
            tcp_buffer_size       = tcp_receive_threshold * 2;
            break;
        case 'k': kb_mode = true; break;
        }
    }
}

int main(int argc, char *argv[])
{
    checkArgs(argc, argv);
    SetConsoleTitle("Chuniboard-Chunithm-Server");
    printInfo();

    WSAData wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printErr("[ERROR] WSA startup failed!\n");
        return -1;
    }

    if (!shm_open_or_create()) {
        printErr("[ERROR] Cannot open/create shared memory! error: %lu\n",
                 GetLastError());
        WSACleanup();
        return -1;
    }
    defer(UnmapViewOfFile(g_memory))
    defer(CloseHandle(g_hMapFile))

    printErr("[INFO] Shared memory ready (BROKENITHM_SHARED_BUFFER)\n");
    if (kb_mode)
        printErr("[INFO] Keyboard (Yuancon layout) mode enabled\n");

    if (!tcp_mode) {
        printErr("[INFO] Mode: UDP\n");
        SOCKET sHost = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        defer(closesocket(sHost))
        socketSetTimeout(sHost, 2000);
        socketBind(sHost, htonl(INADDR_ANY), server_port);
        printErr("[INFO] Waiting for device on port %d...\n", server_port);
        auto LEDThread   = std::thread(threadLEDBroadcast, sHost, g_memory);
        auto InputThread = std::thread(threadInputReceive, sHost, g_memory);
        while (_getwch() != L'q');
        EXIT_FLAG = true;
        LEDThread.join();
        InputThread.join();
    } else {
        printErr("[INFO] Mode: TCP\n");
        SOCKET sHost = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        defer(closesocket(sHost));
        socketSetTimeout(sHost, 50);
        socketBind(sHost, htonl(INADDR_ANY), server_port);
        listen(sHost, 10);
        while (true) {
            printErr("[INFO] Waiting for device on port %d...\n", server_port);
            sockaddr_in user_socket = {};
            socklen_t   sock_size   = sizeof(user_socket);
            SOCKET acc_socket = accept(sHost, (sockaddr *)&user_socket, &sock_size);
            defer(closesocket(acc_socket));
            char buf[20] = {};
            const char *ua = inet_ntop(AF_INET, &user_socket.sin_addr, buf, 20);
            if (ua) printErr("[INFO] Device %s:%d connected.\n", ua, user_socket.sin_port);
            CONNECTED = true;
            EXIT_FLAG = false;
            auto LEDThread   = std::thread(threadLEDBroadcast, acc_socket, g_memory);
            auto InputThread = std::thread(threadInputReceive, acc_socket, g_memory);
            LEDThread.join();
            InputThread.join();
            EXIT_FLAG = true;
            CONNECTED = false;
        }
    }
    WSACleanup();
    return 0;
}
