

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Standard Library — I/O and String Handling
//----------------------------------------------------------------------------------
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

//----------------------------------------------------------------------------------
// Standard Library — Containers
//----------------------------------------------------------------------------------
#include <algorithm>
#include <array>
#include <bitset>
#include <deque>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

//----------------------------------------------------------------------------------
// Standard Library — Numerics and Math
//----------------------------------------------------------------------------------
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <random>

//----------------------------------------------------------------------------------
// Standard Library — Memory and Characters
//----------------------------------------------------------------------------------
#include <cctype>
#include <cstdio>
#include <cstring>
#include <locale>

//----------------------------------------------------------------------------------
// Standard Library — Multithreading
//----------------------------------------------------------------------------------
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <thread>

//----------------------------------------------------------------------------------
// Standard Library — Time
//----------------------------------------------------------------------------------
#include <chrono>

//----------------------------------------------------------------------------------
// Standard Library — Exceptions and Utilities
//----------------------------------------------------------------------------------
#include <stdexcept>

//----------------------------------------------------------------------------------
// Standard Library — File System
//----------------------------------------------------------------------------------
#include <filesystem>

#include <optional>

//----------------------------------------------------------------------------------
// Windows
//----------------------------------------------------------------------------------
#ifdef _WIN32
#include <windows.h>
#endif

//----------------------------------------------------------------------------------
// Type Aliases
//----------------------------------------------------------------------------------
using Bytes = std::vector<uint8_t>;

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Version
//----------------------------------------------------------------------------------

#define CLIENT_VERSION "v0.1.0"

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Windows Specific Utilities
//----------------------------------------------------------------------------------

class WindowsUtilities {
  public:
    void showCursor() {
#ifdef _WIN32
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_CURSOR_INFO info;
        info.dwSize   = 100;
        info.bVisible = TRUE;
        SetConsoleCursorInfo(hConsole, &info);
#endif
    }

    static inline int getConsoleWidth() {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO csbi;

        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {

            return csbi.srWindow.Right - csbi.srWindow.Left + 1;
        }

        return 80; // fallback width
#else
        return 80;
#endif
    }

    void maximizeConsoleWindow() {
#ifdef _WIN32
        HWND consoleWindow = GetConsoleWindow();

        if (consoleWindow != nullptr) {
            ShowWindow(consoleWindow, SW_MAXIMIZE);
        }
#endif
    }

    void hideCursor() {
#ifdef _WIN32
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_CURSOR_INFO info;
        info.dwSize   = 100;
        info.bVisible = FALSE;
        SetConsoleCursorInfo(hConsole, &info);
#endif
    }

    bool enableAnsiEscapes() {
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode  = 0;

        if (!GetConsoleMode(hOut, &mode))
            return false;

        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        return SetConsoleMode(hOut, mode) != 0;
#else
        return true;
#endif
    }

    void fitBufferToWindow() {
#ifdef _WIN32
        // Remove horizontal scrollbar only — don't touch buffer height
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hOut, &csbi);

        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;

        // Only shrink buffer width to window width, leave height alone
        COORD bufferSize = {(SHORT)w, csbi.dwSize.Y};
        SetConsoleScreenBufferSize(hOut, bufferSize);
#endif
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Global Enums
//----------------------------------------------------------------------------------

enum class Align { LEFT, CENTER, RIGHT };

enum class Page { HOME, COMPRESSOR };

enum class InputMode { COMMAND };

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Global Structs
//----------------------------------------------------------------------------------

struct Line {
    std::string text;
    Align alignment = Align::LEFT;
};

struct SymbolEntry {
    uint16_t wordId;
    std::vector<uint16_t> positions;
};

struct ByteEntry {
    uint8_t byteValue;
    std::vector<uint16_t> positions;
};

struct BlockMeta {
    char state;
    uint32_t symbolPosition;
};

struct Layer {
    size_t index      = 0;
    size_t inputBits  = 0;
    size_t outputBits = 0;
};

struct EncodingMeta {
    int wordIdBytes;
    int positionBytes;
};

struct EncodingResult {
    std::vector<SymbolEntry> symbols;
    std::vector<uint8_t> bytes;
    EncodingMeta meta;
    std::string binary;
};

struct BinaryEncodingResult {
    std::vector<ByteEntry> entries;
    std::vector<uint8_t> bytes;
    std::string binary;
};

struct HuffmanResults {
    std::vector<uint8_t> inputBytes;
    std::string finalBinaryPackage;   // ASCII '0'/'1' debug form -- keep for display only
    std::vector<uint8_t> packedBytes; // actual on-disk/serializable form
    uint8_t paddingBits = 0;          // trailing pad bits in packedBytes.back()
    std::string selectedCodec;
    size_t inputBits  = 0;
    size_t outputBits = 0;
    std::unordered_map<uint8_t, std::string> huffmanCode;
};

struct RLEEntry {
    uint8_t value;
    uint8_t count;
};

struct RLEResult {
    std::vector<RLEEntry> entries;
    std::vector<uint8_t> bytes;
};

struct TransitionEntry {
    uint8_t toPair;
    uint32_t gap;
};

struct TransitionStream {
    uint8_t startPair;
    std::vector<TransitionEntry> transitions;
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Layered Compression
//----------------------------------------------------------------------------------

struct CompressionState {
    enum class Stage { MENU, AWAITING_TEXT, DECODE_PROMPT };

    Stage stage = Stage::MENU;
    std::vector<std::string> statusLines;
    static constexpr size_t maxStatusLines = 6;

    void addStatus(const std::string &line) {
        statusLines.push_back(line);
        if (statusLines.size() > maxStatusLines)
            statusLines.erase(statusLines.begin());
    }

    void reset() {
        stage = Stage::MENU;
        statusLines.clear();
    }

    // BlockData output
    std::vector<BlockMeta> blocks;
    std::string blockStates;   // one char per block, e.g. "010" for 3 blocks
    std::string blockPadPairs; // two chars per block, e.g. "000110" for 3 blocks ("00","01","10")
    std::vector<uint32_t> hashPositions;
    std::string bitPairs;

    // OrderHeader output
    std::vector<uint32_t> headerPositions;
    std::vector<uint8_t> headerGaps;
    HuffmanResults headerGapCanonical;
    std::string headerPacked;

    // OrderFooter output
    std::vector<uint8_t> hashGaps;
    std::vector<uint8_t> sumValue;
    std::string hashGapBits;
    std::vector<uint8_t> run;
    HuffmanResults hashGapCanonical;
    HuffmanResults hashGapCanonicalDouble;
    std::string footerPacked;
    std::string footerCodec;

    // OrderBody output
    HuffmanResults bitPairsHuffman;
    std::string bodyPacked;

    // GENERIC HUFFMAN OUTPUT
    std::string selectedCodec;
    std::string outputBits;
    size_t outputBitsSize = 0;
    std::unordered_map<uint8_t, std::string> huffmanCode;

    // GENERAL HUFFMAN
    HuffmanResults huffman;
    std::vector<RLEEntry> rleEntries;

    // ENCODING RESULTS
    std::vector<SymbolEntry> encoded;
    std::vector<uint8_t> bytes;
    std::vector<ByteEntry> byteEntries;
    bool isText             = true;
    size_t originalBits     = 0;
    size_t afterRleBits     = 0;
    size_t afterBinBits     = 0;
    size_t afterHuffmanBits = 0;
    double compressionRatio = 0.0;

    // ENCODED LAYERS
    std::string eBits;
    std::string dBits;
    std::string header;
    std::string body;
    size_t p0 = 0;
    size_t p1 = 0;
    size_t p2 = 0;
    size_t p3 = 0;

    // FINALIZATION
    std::vector<Layer> layers;
    size_t targetBits  = 0;
    size_t zeroPadding = 0;
    bool fitsTarget    = false;
    std::string finalizedBits;

    // RESET BY BLOCKDATA
    void resetBlockAnalysis() {
        blockStates.clear();
        bitPairs.clear();
        blocks.clear();
        hashPositions.clear();
        hashGaps.clear();
        hashGapBits.clear();
        hashGapCanonical = {};
        bitPairsHuffman  = {};
        headerPositions.clear();
        headerGaps.clear();
        headerGapCanonical = {};
    }

    // RESET BY LAYEREDCOMPRESSION
    void resetForRun(const std::string &eBits_, size_t targetBits_) {
        resetBlockAnalysis();
        dBits.clear();
        header.clear();
        body.clear();
        finalizedBits.clear();
        layers.clear();
        p0 = p1 = p2 = p3 = 0;
        zeroPadding       = 0;
        fitsTarget        = false;
        eBits             = eBits_;
        targetBits        = targetBits_;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// File System
//----------------------------------------------------------------------------------

namespace fs = std::filesystem;

class FileSystem {
  public:
    static fs::path getLogPath() {
        fs::path dir = "logs";

        if (!fs::exists(dir))
            fs::create_directories(dir);

        return dir / "debug.log";
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Functions
//----------------------------------------------------------------------------------

class Render {
  public:
    void pushLine(const std::string &txt, Align a = Align::LEFT) { bufferedLines.push_back({txt, a}); }

    void flushToColumn(std::vector<Line> &column) {
        column.insert(column.end(), bufferedLines.begin(), bufferedLines.end());
        bufferedLines.clear();
    }

    void clearBuffer() { bufferedLines.clear(); }

    void addEmptyLines(std::vector<Line> &col, int n) {
        for (int i = 0; i < n; ++i)
            col.push_back({"", Align::CENTER});
    }

    std::string makeLine(char c = '=') const { return std::string(consoleWidth(), c); }

    std::string printColumns(const std::vector<std::vector<Line>> &columns, int spacing = 1, int padding = 0, int widthOverride = 0) const {
        const int cw           = widthOverride > 0 ? widthOverride : consoleWidth();
        const int numCols      = (int)std::max<size_t>(1, columns.size());
        const int totalSpacing = spacing * (numCols - 1);
        const int usableWidth  = cw - (padding * 2) - totalSpacing;
        const int colWidth     = std::max(1, usableWidth / numCols);
        const std::string leftPad(std::max(0, (cw - (colWidth * numCols + totalSpacing + padding * 2)) / 2), ' ');

        std::vector<std::vector<std::string>> wrappedText(numCols);
        std::vector<std::vector<Align>> wrappedAlign(numCols);

        for (int c = 0; c < numCols; ++c) {
            for (const auto &ln : columns[c]) {
                auto &wt = wrappedText[c];
                auto &wa = wrappedAlign[c];

                if (ln.text.find_first_not_of(" \t\r\n") == std::string::npos) {
                    wt.push_back("");
                    wa.push_back(ln.alignment);
                    continue;
                }

                if ((int)ln.text.size() <= colWidth) {
                    wt.push_back(ln.text);
                    wa.push_back(ln.alignment);
                    continue;
                }

                std::istringstream iss(ln.text);
                std::string word, current;

                while (iss >> word) {
                    if (current.empty()) {
                        current = word;
                    } else if ((int)(current.size() + 1 + word.size()) <= colWidth) {
                        current += ' ' + word;
                    } else {
                        wt.push_back(current);
                        wa.push_back(ln.alignment);
                        current = word;
                    }
                }

                if (!current.empty()) {
                    wt.push_back(current);
                    wa.push_back(ln.alignment);
                }
            }
        }

        size_t maxLines = 0;
        for (const auto &col : wrappedText)
            maxLines = std::max(maxLines, col.size());

        std::ostringstream oss;
        for (size_t i = 0; i < maxLines; ++i) {
            oss << leftPad;
            for (int c = 0; c < numCols; ++c) {
                const std::string &text = i < wrappedText[c].size() ? wrappedText[c][i] : "";
                const Align a           = i < wrappedAlign[c].size() ? wrappedAlign[c][i] : Align::LEFT;
                oss << alignFragment(text, a, colWidth);
                if (c < numCols - 1)
                    oss << std::string(spacing, ' ');
            }
            oss << '\n';
        }

        return oss.str();
    }

    // clang-format off
        std::string printHeaderColumns   (const std::vector<std::vector<Line>> &cols) const { return printColumns(cols, 0, 0); }
        std::string printMenuColumns     (const std::vector<std::vector<Line>> &cols) const { return printColumns(cols, 1, 0); }
        std::string printBodyColumns     (const std::vector<std::vector<Line>> &cols) const { return printColumns(cols, 2, 0); }
        //std::string printIndicatorColumns(const std::vector<std::vector<Line>> &cols) const { return printColumns(cols, 2, 0); }
        std::string printFooterColumns   (const std::vector<std::vector<Line>> &cols) const { return printColumns(cols, 0, 0); }
        std::string printCalendar        (const std::vector<std::vector<Line>> &cols) const { return printColumns(cols, 1, 0); }
    // clang-format on

    int consoleWidth() const {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        return std::max(1, csbi.srWindow.Right - csbi.srWindow.Left);
    }

    std::string
    printColumnsPercent(const std::vector<std::vector<Line>> &columns, const std::vector<double> &percents, int spacing = 1, int padding = 0, int widthOverride = 0) const {
        const int cw = widthOverride > 0 ? widthOverride : consoleWidth();

        const int numCols = (int)columns.size();

        if ((int)percents.size() != numCols || numCols == 0)
            return "";

        const int totalSpacing = spacing * (numCols - 1);
        const int usableWidth  = cw - (padding * 2) - totalSpacing;

        // column widths
        std::vector<int> colWidths(numCols);
        int used = 0;

        for (int i = 0; i < numCols; ++i) {
            colWidths[i] = (int)(usableWidth * (percents[i] / 100.0));
            used += colWidths[i];
        }

        colWidths.back() += usableWidth - used;

        // flatten rows per column WITHOUT word wrapping
        std::vector<std::vector<std::string>> text(numCols);
        std::vector<std::vector<Align>> align(numCols);

        for (int c = 0; c < numCols; ++c) {
            for (const auto &ln : columns[c]) {
                std::string t = ln.text;

                // HARD RULE: no reflow, only cut
                if ((int)t.size() > colWidths[c])
                    t = t.substr(0, colWidths[c]);

                text[c].push_back(t);
                align[c].push_back(ln.alignment);
            }
        }

        size_t maxRows = 0;
        for (const auto &c : text)
            maxRows = std::max(maxRows, c.size());

        std::ostringstream oss;

        for (size_t r = 0; r < maxRows; ++r) {
            for (int c = 0; c < numCols; ++c) {
                std::string t = (r < text[c].size()) ? text[c][r] : "";

                Align a = (r < align[c].size()) ? align[c][r] : Align::LEFT;

                oss << alignFragment(t, a, colWidths[c]);

                if (c < numCols - 1)
                    oss << std::string(spacing, ' ');
            }

            oss << '\n';
        }

        return oss.str();
    }

  private:
    std::vector<Line> bufferedLines;

    std::string alignFragment(const std::string &txt, Align a, int width) const {
        const int len   = (int)txt.size();
        const int space = width - len;

        if (len >= width)
            return txt.substr(0, width);

        switch (a) {
        case Align::LEFT:
            return txt + std::string(space, ' ');
        case Align::RIGHT:
            return std::string(space, ' ') + txt;
        case Align::CENTER: {
            const int l = space / 2;
            return std::string(l, ' ') + txt + std::string(space - l, ' ');
        }
        }
        return txt;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// File System -- CLASS 1
//----------------------------------------------------------------------------------

class Functions {
  public:
    // =========================================================
    // NUMERIC / HASH HELPERS
    // =========================================================

    static uint64_t hexToUint64(const std::string &hexStr) {
        uint64_t value = 0;
        std::stringstream ss(hexStr);
        ss >> std::hex >> value;
        return value;
    }

    // Takes first 15 hex chars to fit in 60 bits
    static uint64_t hashToUint60(const std::string &hexHash) { return hexToUint64(hexHash.substr(0, std::min<size_t>(15, hexHash.size()))); }

    static uint64_t messageToUint60(const std::string &message) {
        uint64_t value = 0;
        for (unsigned char c : message)
            value = ((value << 4) | (c & 0xF)) & ((1ULL << 60) - 1);
        return value;
    }

    // Extracts a timestamp embedded in the first 15 chars of a txid
    static uint64_t extractTimestamp(const std::string &txid) {
        std::string tsStr;
        for (size_t i = 0; i < 15 && i < txid.size(); ++i) {
            if (txid[i] != '-')
                tsStr += txid[i];
        }
        if (tsStr.empty())
            return 0;
        return std::stoull(tsStr);
    }

    // =========================================================
    // FORMATTERS
    // =========================================================

    // All integral types (int, long, long long, uint64_t, etc.)
    template <typename T> static std::enable_if_t<std::is_integral_v<T>, std::string> format(T value) { return addCommas(std::to_string(value)); }

    // Default: 2 decimal places
    static std::string format(double value) { return formatFixed(value, 2); }

    // Inserts thousand separators into a numeric string
    static std::string addCommas(std::string s) {
        size_t dotPos = s.find('.');
        if (dotPos == std::string::npos)
            dotPos = s.size();
        int pos = static_cast<int>(dotPos) - 3;
        while (pos > 0) {
            s.insert(pos, ",");
            pos -= 3;
        }
        return s;
    }

    static std::string formatFixed(double value, int precision) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(precision) << value;
        return addCommas(ss.str());
    }

    static std::string formatDouble(double value) { return formatFixed(value, 2); }
    static std::string formatExchangeRateDouble(double value) { return formatFixed(value, 5); }
    static std::string formatDoubleUSD(double value) { return formatFixed(value, 2); }
    static std::string formatDoubleBTC(double value) { return formatFixed(value, 8); }

    // Integer satoshi/cent representations → human-readable

    static std::string formatWithCommas(long long value) { return addCommas(std::to_string(value)); }
    static std::string formatNumber(long long value) { return formatWithCommas(value); }

    // =========================================================
    // BINARY / HEX
    // =========================================================

    static std::string generateByteTable() {
        std::string output;
        for (int i = 0; i <= 50; ++i)
            output += std::bitset<8>(i).to_string();
        return output;
    }

    static std::string stringToBinaryASCII(const std::string &input, bool padToPowerOfTwo = true) {
        std::string binary;
        binary.reserve(input.size() * 8);
        for (unsigned char c : input)
            binary += std::bitset<8>(c).to_string();
        if (padToPowerOfTwo) {
            while (!binary.empty() && (binary.size() & (binary.size() - 1)) != 0)
                binary.push_back('0');
        }
        return binary;
    }

    static std::string binaryASCIIToString(const std::string &binary) {
        if (binary.size() % 8 != 0)
            throw std::runtime_error("Binary length must be multiple of 8");
        std::string output;
        output.reserve(binary.size() / 8);
        for (size_t i = 0; i < binary.size(); i += 8) {
            std::bitset<8> bits(binary.substr(i, 8));
            output.push_back(static_cast<char>(bits.to_ulong()));
        }
        return output;
    }

    // Binary string → uppercase hex (pads to nearest nibble)
    static std::string binaryToHex(const std::string &binary) {
        if (binary.empty())
            throw std::runtime_error("Empty binary string");
        for (char c : binary) {
            if (c != '0' && c != '1')
                throw std::runtime_error("Invalid binary character");
        }
        std::string padded = binary;
        int pad            = (4 - static_cast<int>(padded.size() % 4)) % 4;
        padded             = std::string(pad, '0') + padded;
        std::string hex;
        hex.reserve(padded.size() / 4);
        for (size_t i = 0; i < padded.size(); i += 4) {
            int value = 0;
            for (int j = 0; j < 4; ++j)
                value = (value << 1) + (padded[i + j] - '0');
            hex.push_back(value < 10 ? char('0' + value) : char('A' + value - 10));
        }
        return hex;
    }

    static std::string bytesToBinary(const std::vector<uint8_t> &bytes) {
        std::string binary;
        binary.reserve(bytes.size() * 8);
        for (uint8_t b : bytes)
            binary += std::bitset<8>(b).to_string();
        return binary;
    }

    static std::vector<uint8_t> binaryToBytes(const std::string &binary) {
        if (binary.size() % 8 != 0)
            throw std::runtime_error("Binary size must be multiple of 8");
        std::vector<uint8_t> bytes;
        bytes.reserve(binary.size() / 8);
        for (size_t i = 0; i < binary.size(); i += 8) {
            std::bitset<8> bits(binary.substr(i, 8));
            bytes.push_back(static_cast<uint8_t>(bits.to_ulong()));
        }
        return bytes;
    }

    static std::string toBinary(const std::string &text) { return stringToBinaryASCII(text, false); }

    static std::string generateByteBlock(uint8_t value, uint32_t count) {
        std::string data;
        data.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            data.push_back(static_cast<char>(value));
        }
        return data;
    }

    static std::string makeProgressBar(int current, int total, int width = 50) {
        if (total <= 0)
            total = 1;
        double progress = std::clamp(static_cast<double>(current) / static_cast<double>(total), 0.0, 1.0);
        int filled      = static_cast<int>(std::round(progress * width));
        std::ostringstream oss;
        oss << "[";
        for (int i = 0; i < filled; ++i)
            oss << "#";
        for (int i = filled; i < width; ++i)
            oss << " ";
        oss << "] " << std::setw(3) << static_cast<int>(progress * 100) << "%";
        return oss.str();
    }

    // Prints in-place using \r — call repeatedly to animate
    static void printProgressBar(int current, int total, int width = 50) {
        std::cout << '\r' << makeProgressBar(current, total, width);
        std::cout.flush();
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// System Clock
//----------------------------------------------------------------------------------

class SystemClock {
  public:
    inline long long getMilliseconds() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

    inline long long getNanoseconds() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    }

    // =========================================================
    // FORMATTED TIME STRINGS
    // =========================================================

    // Current local time as "YYYY-MM-DD HH:MM:SS.mmm"
    std::string getCurrentTime() {
        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::ostringstream ss;
        ss << std::put_time(std::localtime(&tt), "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Logger
//----------------------------------------------------------------------------------

class Logger {
  public:
    enum Level { LOG_INFO, LOG_WARNING, LOG_ERROR };

    Logger() = default;

    ~Logger() {
        if (logFile.is_open()) {
            logFile << getCurrentTime() << " [INFO]    === Logger shutdown ===\n";
            logFile.close();
        }
    }

    void log(const std::string &message, Level level = LOG_INFO, const char *className = "", const char *funcName = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureLogFileOpen();

        std::ostringstream ss;
        ss << getCurrentTime() << " ";

        switch (level) {
        case LOG_INFO:
            ss << "[INFO]    ";
            break;
        case LOG_WARNING:
            ss << "[WARNING] ";
            break;
        case LOG_ERROR:
            ss << "[ERROR]   ";
            break;
        }

        if (className[0] != '\0')
            ss << "[" << className << "::" << funcName << "] ";

        ss << message << "\n";

        logFile << ss.str();
        logFile.flush();
    }

    void info(const std::string &msg) { log(msg, LOG_INFO); }
    void warning(const std::string &msg) { log(msg, LOG_WARNING); }
    void error(const std::string &msg) { log(msg, LOG_ERROR); }

    void setPath(const fs::path &path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (logFile.is_open())
            logFile.close();
        logFileName = path.string();
    }

  private:
    static constexpr const char *CLASS_NAME = "Logger";
    SystemClock systemClock;
    std::ofstream logFile;
    std::mutex mutex_;
    std::string logFileName = "file_debug";

    void ensureLogFileOpen() {
        if (!logFile.is_open()) {
            logFile.open(logFileName, std::ios::out | std::ios::app);

            if (logFile.is_open()) {
                logFile << getCurrentTime() << " [INFO] === Logger startup ===\n";
            } else {
                throw std::runtime_error("FATAL: Could not open log file");
            }
        }
    }

    std::string getCurrentTime() { return std::to_string(systemClock.getNanoseconds()); }
};

// Global Instance
inline Logger logger;

// Macros
#define LOG_INFO(msg) logger.log(msg, Logger::LOG_INFO, CLASS_NAME, __func__)
#define LOG_WARNING(msg) logger.log(msg, Logger::LOG_WARNING, CLASS_NAME, __func__)
#define LOG_ERROR(msg) logger.log(msg, Logger::LOG_ERROR, CLASS_NAME, __func__)

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// namespace CRYPTO {SHA256}
//----------------------------------------------------------------------------------

namespace CRYPTO {
class SHA256 {
  public:
    SHA256() { reset(); }

    // clang format off
    // ------------------------------------------------------------
    // Input: {0x48, 0x65, 0x6C, 0x6C, 0x6F}
    // Output: {0x2C, 0xF2, 0x4D, 0xBA, ...}
    // Useful for: HMAC, key derivation, checksums, binary protocols
    // ------------------------------------------------------------
    inline Bytes hashBytes(const Bytes &data) {
        update(data.data(), data.size());
        return digestBytes();
    }

    // ------------------------------------------------------------
    // Input: "hello"
    // Output: {0x2C, 0xF2, 0x4D, 0xBA, ...}
    // Useful when: You need the hash in binary form for further processing
    // ------------------------------------------------------------
    inline Bytes hashString(const std::string &data) {
        update(reinterpret_cast<const uint8_t *>(data.data()), data.size());
        return digestBytes();
    }

    // ------------------------------------------------------------
    // Input: "hello"
    // Output: "00101100111100100100110110111010..."
    // Useful for: Entropy pools, mnemonic generation, bit manipulation, debugging
    // ------------------------------------------------------------
    inline std::string hashBinary(const std::string &data) {
        update(reinterpret_cast<const uint8_t *>(data.data()), data.size());
        return digestBinary();
    }

    // ------------------------------------------------------------
    // Input: "hello"
    // Output: "1b161e5c1fa7425e73043362938b9824"
    // Useful for: Transaction IDs, fingerprints, certificates, wallet identifiers, logging and display
    // ------------------------------------------------------------
    inline std::string hashHex(const std::string &data) {
        update(reinterpret_cast<const uint8_t *>(data.data()), data.size());
        return digest();
    }

    inline void update(const uint8_t *data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buffer[bufferLen++] = data[i];
            if (bufferLen == 64) {
                transform(buffer);
                bitlen += 512;
                bufferLen = 0;
            }
        }
    }
    // clang format on

    inline std::string digest() {
        uint64_t totalBits = bitlen + bufferLen * 8;

        buffer[bufferLen++] = 0x80;
        if (bufferLen > 56) {
            while (bufferLen < 64)
                buffer[bufferLen++] = 0x00;
            transform(buffer);
            bufferLen = 0;
        }

        while (bufferLen < 56)
            buffer[bufferLen++] = 0x00;

        for (int i = 7; i >= 0; --i)
            buffer[bufferLen++] = (totalBits >> (i * 8)) & 0xFF;

        transform(buffer);

        std::ostringstream oss;
        for (int i = 0; i < 8; ++i)
            oss << std::hex << std::setw(8) << std::setfill('0') << h[i];

        reset(); // reset internal state after digest
        return oss.str();
    }

    inline Bytes digestBytes() {
        std::string hex = digest();
        Bytes out;
        out.reserve(32);
        for (size_t i = 0; i < hex.size(); i += 2)
            out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        return out;
    }

    inline std::string digestBinary() {
        std::string hex = digest();
        std::string binary;
        for (char c : hex) {
            uint8_t val = (c <= '9') ? c - '0' : 10 + (std::tolower(c) - 'a');
            for (int i = 3; i >= 0; --i)
                binary += ((val >> i) & 1) ? '1' : '0';
        }
        return binary;
    }

    inline void reset() {
        h[0]      = 0x6a09e667;
        h[1]      = 0xbb67ae85;
        h[2]      = 0x3c6ef372;
        h[3]      = 0xa54ff53a;
        h[4]      = 0x510e527f;
        h[5]      = 0x9b05688c;
        h[6]      = 0x1f83d9ab;
        h[7]      = 0x5be0cd19;
        bitlen    = 0;
        bufferLen = 0;
    }

  private:
    static constexpr const char *CLASS_NAME = "SHA256";
    uint32_t h[8];
    uint64_t bitlen;
    uint8_t buffer[64];
    size_t bufferLen;

    inline void transform(const uint8_t block[64]) {
        uint32_t w[64];

        for (int i = 0; i < 16; ++i) {
            w[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16) | (block[i * 4 + 2] << 8) | (block[i * 4 + 3]);
        }

        for (int i = 16; i < 64; ++i) {
            w[i] = theta1(w[i - 2]) + w[i - 7] + theta0(w[i - 15]) + w[i - 16];
        }

        uint32_t a     = h[0];
        uint32_t b     = h[1];
        uint32_t c     = h[2];
        uint32_t d     = h[3];
        uint32_t e     = h[4];
        uint32_t f     = h[5];
        uint32_t g     = h[6];
        uint32_t h_val = h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t temp1 = h_val + sig1(e) + choose(e, f, g) + K[i] + w[i];
            uint32_t temp2 = sig0(a) + majority(a, b, c);
            h_val          = g;
            g              = f;
            f              = e;
            e              = d + temp1;
            d              = c;
            c              = b;
            b              = a;
            a              = temp1 + temp2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += h_val;
    }

    inline static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
    inline static uint32_t choose(uint32_t e, uint32_t f, uint32_t g) { return (e & f) ^ (~e & g); }
    inline static uint32_t majority(uint32_t a, uint32_t b, uint32_t c) { return (a & b) ^ (a & c) ^ (b & c); }
    inline static uint32_t sig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
    inline static uint32_t sig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
    inline static uint32_t theta0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
    inline static uint32_t theta1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

    inline static constexpr uint32_t K[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
                                              0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
                                              0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
                                              0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
                                              0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
                                              0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
};
} // namespace CRYPTO

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Random Number Generator -- CLASS 6
//----------------------------------------------------------------------------------

// wrapper around platform-specific "pin memory, prevent swap" calls.
// used anywhere sensitive data (entropy pools, key material) needs to stay
// out of swap/pagefile for the lifetime of the object holding it.
class SecureMemory {
  public:
    static inline bool lock(void *ptr, size_t bytes) {
#ifdef _WIN32
        return VirtualLock(ptr, bytes) != 0;
#else
        return mlock(ptr, bytes) == 0;
#endif
    }

    static inline bool unlock(void *ptr, size_t bytes) {
#ifdef _WIN32
        return VirtualUnlock(ptr, bytes) != 0;
#else
        return munlock(ptr, bytes) == 0;
#endif
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Random Number Generator -- CLASS 6
//----------------------------------------------------------------------------------

class RandomNumberGenerator {
  public:
    inline std::string run() {
        // first, reserve enough space for the expected output size to avoid repeated reallocations during string concatenation.
        std::string result;
        result.reserve(expectedBits); // capacity hint only — see note on expectedBits below, actual output can be slightly longer
        // one must always reset the state before each run() to ensure that the ring buffer and running averages are fresh for this invocation.
        resetState();

        for (int i = 0; i < totalIterations; ++i) {
            // apply the most imprtant function - see bellow in private section for more details
            long long duration = countdown();
            // increment the count of samples, add the duration to the running sum, and compute the new running average.
            ++count;
            globalSum += duration;
            globalAvg = globalSum / count; // running average, used as the threshold for this iteration's bit extraction

            // extract 1 bit of raw entropy: was this timing sample above or below the running average?
            // this is the "jitter" bit — noisy, biased, low-quality on its own, which is why it gets pooled and hashed below
            int bit = duration < globalAvg ? 0 : 1;

            // ring buffer write (push newest bit)
            localBits[tail] = bit;
            tail            = (tail + 1) % localBufferSize;

            // assuming 4 values - 0 equals the point at which the series wraps around, marking the end of the local buffer size.
            // pushed  1 | ... | head=0 tail=1
            // pushed  2 | ... | head=0 tail=2
            // pushed  3 | ... | head=0 tail=3
            // pushed  4 | ... | head=0 tail=0   <- wrapped! tail went 3 -> (3+1) % 4 = 0
            // pushed  5 | ... | head=1 tail=1
            //
            // how it works, the array is faster to compute, hashLocalBits() later orders the bits for SHA256 to hash
            // [1, None, None, None]
            // [1, 2, None, None]
            // [1, 2, 3, None]
            // [1, 2, 3, 4]
            // [5, 2, 3, 4]   <- only slot 0 changed: 1 -> 5
            // [5, 6, 3, 4]   <- only slot 1 changed: 2 -> 6
            // [5, 6, 7, 4]   <- only slot 2 changed: 3 -> 7
            // [5, 6, 7, 8]   <- only slot 3 changed: 4 -> 8
            //
            // then we are back to zero, completeing the loop in the fastest time possible
            // the oldest bit is now at head=0, the newest bit is at tail=0, and the buffer is full.
            if (!filled) {
                // buffer isn't full yet — we're still in the initial fill-up phase.
                if (tail == 0)
                    filled = true;
            } else {
                head = (head + 1) % localBufferSize;
            }

            // Warm-up runs from i = 0 .. warmupIterations-1: buffer fills (by i = 511)
            // and globalAvg keeps converging, but nothing is hashed yet.
            // Once i >= warmupIterations, every remaining iteration hashes —
            // the buffer is already guaranteed full by then...
            // Only start emitting digests once we're past the warm-up period.
            // once full, every iteration represents one full 512-bit sliding window (oldest -> newest),
            // 512 bits = 2^512 = 64 bytes, which is exactly what SHA-256 expects as input.
            // this is an astronimcally large number of combinations, so the output is effectively "whitened" and conditioned.
            // in scientific notation, 2⁵¹² ≈ 1.34 × 10¹⁵⁴ which is a 155 digit long number so large that it is effectively impossible to brute-force or predict.
            // we now hash it every time we add a bit to the local buffer, remove the first, add the last etc,.
            // this keeps the output stream continuously fed with fresh digests due to the avalanche effect of SHA256.
            if (i >= warmupIterations) {
                result += hashLocalBits();
            }
        }

        return result;
    }

  private:
    CRYPTO::SHA256 sha;
    SystemClock systemClock;

    static constexpr int warmupBytes           = 1250;            // warm-up period in bytes, 10,000 bits pass through local buffer
    static constexpr int warmupIterations      = warmupBytes * 8; // 10,000 bits
    static constexpr int localBufferSize       = 512;             // ring buffer capacity — holds the last 512 raw entropy bits, hashed together to whiten/condition the output
    std::array<int, localBufferSize> localBits = {};              // the ring buffer itself — one int (0/1) per bit; array chosen over vector for fixed size + speed
    static constexpr int total                 = 256;             // SHA-256 digest size in bits — size of each unit of output this class produces
    static constexpr int byte64                = 64;              // 512 bits (localBufferSize) expressed in bytes — what actually gets fed into SHA256::update()
    static constexpr int producingIterations   = 512;             // number of hashes emitted after warm-up ends -> 512 * 256 = 131,072 bits in the pool
    static constexpr int totalIterations       = warmupIterations + producingIterations; // 10,512 — full run length, warm-up + production
    static constexpr int expectedBits          = producingIterations * total;            // 131,072 bits in bit pool

    size_t head         = 0;     // index of the OLDEST live bit in the ring buffer (next to be evicted on write, once full)
    size_t tail         = 0;     // index of the NEXT WRITE position (where the newest bit goes)
    bool filled         = false; // latches true once the ring buffer has been fully populated at least once
    long long globalSum = 0;     // running sum of all timing samples seen so far this run
    long long globalAvg = 0;     // running average of timing samples — used as the live threshold for bit extraction
    long long count     = 0;     // number of timing samples taken so far this run (denominator for globalAvg)

    // busy-wait a fixed, tiny amount of work and measure how long it actually took in nanoseconds.
    // for faster computers, this will be a smaller number; for slower computers, it will be larger.
    // therefore: the volitile value x = 10 can be changed for x = 100 etc,.
    // the actual duration is noisy due to CPU/OS scheduling jitter, cache state, thermal throttling, etc —
    // that jitter is the raw entropy source this whole class is built on.
    inline long long countdown() {
        volatile int x = 10;
        auto start     = systemClock.getNanoseconds();
        while (x > 0) {
            int tmp = x;
            x       = tmp - 1;
        }
        return systemClock.getNanoseconds() - start;
    }

    // packs the current 512-bit ring buffer window into 64 bytes (oldest bit first, MSB-first within each byte),
    // then runs it through SHA-256 to condition/whiten the raw jitter bits into a uniform-looking digest.
    inline std::string hashLocalBits() {
        uint8_t bytes[64] = {0};

        for (size_t i = 0; i < localBufferSize; ++i) {
            // walk the ring starting at head (oldest) and wrapping forward to tail (newest) —
            // this only reads the correct chronological order because head is now actually maintained above.
            size_t idx = (head + i) % localBufferSize;
            if (localBits[idx]) {
                bytes[i / 8] |= (1 << (7 - (i % 8)));
            }
        }

        sha.update(bytes, byte64);

        return sha.digestBinary();
    }

    // resets all run-scoped state so run() can be called repeatedly and produce independent output each time.
    // note: localBits itself is intentionally NOT cleared here — it gets fully overwritten during the
    // fill-up phase of the next run() before filled ever becomes true again, so stale bits never get hashed.
    void resetState() {
        head      = 0;
        tail      = 0;
        count     = 0;
        filled    = false;
        globalSum = 0;
        globalAvg = 0;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Random Number Generator -- CLASS 6
//----------------------------------------------------------------------------------

class BinaryEntropyPool {
  public:
    BinaryEntropyPool() {
        bitPool.reserve(POOL_RESERVED);                    // pre-allocate 200% of one refill's worth upfront
        SecureMemory::lock(bitPool.data(), POOL_RESERVED); // pin the reserved region so it can't be swapped to disk
        // refill(); // intentionally left disabled — pool starts empty, first get() call triggers the initial fill
    }

    ~BinaryEntropyPool() {
        drain();                                             // zero out any remaining bits before releasing memory
        SecureMemory::unlock(bitPool.data(), POOL_RESERVED); // must match the size used in lock() above
    }

    // Fetches bitsNeeded bits, automatically choosing the right strategy:
    // - <= POOL_CAPACITY (one refill's worth): served directly via get(), fastest path, no chunking overhead.
    // - >  POOL_CAPACITY: routed to getLarge(), which pulls it in POOL_CAPACITY-sized chunks.
    // This is the method callers should use by default — get() or getLarge() remain available directly
    // if you specifically know which strategy you want (e.g. a tight loop issuing many small requests).
    inline std::string get(size_t bitsNeeded) {
        if (bitsNeeded <= POOL_CAPACITY)
            return requestSmall(bitsNeeded);
        return requestLarge(bitsNeeded);
    }

    // Current number of unconsumed bits sitting in the pool. Mainly for diagnostics/monitoring.
    inline size_t available() const {
        std::lock_guard<std::mutex> lock(poolMutex);
        return bitPool.size();
    }

    // Securely wipes the entire pool and re-reserves capacity for future refills.
    // Called on destruction, and available to call manually if you need to force-discard
    // the current pool contents (e.g. suspected compromise, or before a sensitive operation).
    inline void drain() {
        std::lock_guard<std::mutex> lock(poolMutex);
        secureClear(bitPool);
        bitPool.reserve(POOL_CAPACITY);
    }

  private:
    RandomNumberGenerator rng;
    Functions functions;

    static constexpr size_t POOL_CAPACITY = 512 * 256;         // intended: 131,072 bits — one rng.run()
    static constexpr size_t POOL_RESERVED = POOL_CAPACITY * 2; // 262,144 bits — 200% headroom over one refill
    static constexpr size_t LOW_WATERMARK = 512 * 128;         // refill proactively once below half of POOL_CAPACITY

    std::string bitPool;          // the pool itself — a flat string of '0'/'1' characters acting as a bit queue
    mutable std::mutex poolMutex; // guards all reads/writes to bitPool, since get()/available()/drain() can be called from multiple threads

    // Returns exactly bitsNeeded bits, refilling from the RNG first if the pool is running low
    // or doesn't have enough to satisfy the request. Consumed bits are securely erased from the
    // pool immediately after being copied out, so they can't linger in memory post-use.
    inline std::string requestSmall(size_t bitsNeeded) {
        std::lock_guard<std::mutex> lock(poolMutex); // pool is shared across threads — serialize all access

        if (bitsNeeded > POOL_RESERVED) {
            throw std::invalid_argument("get(): requested bits exceed pool's maximum single-request capacity — use request() or getLarge() instead");
        }

        if (bitPool.size() < LOW_WATERMARK)
            refill(); // proactive top-up once we drop below the halfway mark, before we're actually starved
        while (bitPool.size() < bitsNeeded)
            refill(); // reactive top-up — guarantees enough bits exist to satisfy this specific request

        std::string result = bitPool.substr(0, bitsNeeded); // copy out the requested prefix
        secureErase(bitsNeeded);                            // wipe + remove those bits from the pool so they're one-time-use

        return result;
    }

    // Same contract as get(), but for requests larger than a single refill's worth (POOL_CAPACITY).
    // Pulls bits in POOL_CAPACITY-sized chunks via repeated get() calls and concatenates them.
    inline std::string requestLarge(size_t bitsNeeded) {
        std::string result;
        result.reserve(bitsNeeded);

        size_t remaining = bitsNeeded;
        size_t completed = 0;
        std::cout << '\n';

        while (remaining > 0) {
            size_t chunkSize = std::min(remaining, POOL_CAPACITY);
            result += get(chunkSize);
            completed += chunkSize;
            remaining -= chunkSize;

            functions.printProgressBar(completed, bitsNeeded);
        }

        std::cout << '\n';

        return result;
    }

    // Unused — getLarge() currently reimplements this chunking loop inline instead of calling this.
    // Either wire this in or remove it so there's only one chunking implementation to maintain.
    inline std::vector<std::string> getChunked(size_t bitsNeeded) {
        std::vector<std::string> chunks;

        size_t remaining = bitsNeeded;

        while (remaining > 0) {
            size_t chunkSize = std::min(remaining, POOL_CAPACITY);
            chunks.push_back(get(chunkSize));
            remaining -= chunkSize;
        }

        return chunks;
    }

    inline void refill() {
        if (bitPool.size() >= POOL_RESERVED)
            return; // already at max reserved capacity — adding more would force a reallocation, invalidating the memory lock

        std::string fresh = rng.run();
        size_t room       = POOL_RESERVED - bitPool.size();

        if (fresh.size() > room)
            fresh.resize(room); // trim to whatever room remains — wastes some freshly-generated entropy bits, but that's a far cheaper cost than an unlocked buffer

        bitPool += fresh;
    }

    // Overwrites every byte of the given string with 0 before clearing it, so freed/reused
    // memory doesn't retain the old bit pattern. volatile prevents the compiler from optimizing
    // this "pointless-looking" write-then-discard away — a plain loop without volatile could
    // legally be eliminated entirely by the optimizer since the values are never read afterward.
    inline void secureClear(std::string &s) {
        volatile char *p = s.data();
        for (size_t i = 0; i < s.size(); ++i)
            p[i] = 0;
        s.clear();
    }

    // Same secure-wipe technique as secureClear(), but only for the first n bits/chars —
    // used by get() to destroy just the bits that were handed out, leaving the rest of the pool intact.
    inline void secureErase(size_t n) {
        volatile char *p = bitPool.data();
        for (size_t i = 0; i < n; ++i)
            p[i] = 0;
        bitPool.erase(0, n);
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Meta Data
//----------------------------------------------------------------------------------

class MetaData {
  public:
    inline std::string binaryToBase62WithPadding(const std::string &binaryStr) {
        const size_t chunkCount = binaryStr.size() / 6;

        result.clear();
        paddedBits.clear();
        result.reserve(chunkCount);
        paddedBits.reserve(chunkCount * 8);

        for (size_t i = 0; i + 6 <= binaryStr.size(); i += 6) {
            // manual 6-bit binary to int, faster than stoi
            int value = ((binaryStr[i] - '0') << 5) | ((binaryStr[i + 1] - '0') << 4) | ((binaryStr[i + 2] - '0') << 3) | ((binaryStr[i + 3] - '0') << 2)
                        | ((binaryStr[i + 4] - '0') << 1) | ((binaryStr[i + 5] - '0'));

            if (value < 62) {
                result += BASE62[value];
                paddedBits += "00";
                paddedBits += binaryStr.substr(i, 6);
            }
        }

        return result;
    }

    inline const std::string &getPaddedBinary() const { return paddedBits; }

  private:
    static constexpr const char *CLASS_NAME = "MetaData";
    static constexpr char BASE62[]          = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string paddedBits;
    std::string result;
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Generate UUID
//----------------------------------------------------------------------------------

class GenerateUUID {
  public:
    GenerateUUID() {
        // pre-allocate all working buffers once
        uuidBuffer.reserve(UUID_LENGTH + 4); // 4 hyphens
        fullString.reserve(UUID_LENGTH);
        randomPart.reserve(RANDOM_CHARS);
    }

    inline std::string generateUUID() {
        // STEP 1: timestamp
        long long ms          = systemClock.getMilliseconds();
        std::string timestamp = std::to_string(ms);

        // STEP 2: entropy
        std::string binary = bep.get(NUM_BITS);

        // STEP 3: base62 encode
        randomPart = metaData.binaryToBase62WithPadding(binary);

        // STEP 4: concatenate
        fullString = timestamp + randomPart;

        // STEP 5: format
        return formatUUID(fullString);
    }

    inline std::string formatUUID(const std::string &s) {
        uuidBuffer.clear();

        // format: 8-4-4-4-12
        uuidBuffer.append(s, 0, 8);
        uuidBuffer += '-';
        uuidBuffer.append(s, 8, 4);
        uuidBuffer += '-';
        uuidBuffer.append(s, 12, 4);
        uuidBuffer += '-';
        uuidBuffer.append(s, 16, 4);
        uuidBuffer += '-';
        uuidBuffer.append(s, 20, 12);

        return uuidBuffer;
    }

    inline std::string getUUIDDate(const std::string &uuid) {
        long long ts = extractTimestamp(uuid);
        return timestampToDate(ts);
    }

    inline std::string removeHyphens(const std::string &uuid) {
        std::string result;
        result.reserve(uuid.size());

        for (char c : uuid) {
            if (c != '-')
                result += c;
        }

        return result;
    }

    // generate a batch of UUIDs in one call
    inline std::vector<std::string> generateBatch(size_t count) {
        std::vector<std::string> uuids;
        uuids.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            uuids.push_back(generateUUID());
        }

        return uuids;
    }

  private:
    static constexpr const char *CLASS_NAME = "GenerateUUID";
    RandomNumberGenerator rng;
    BinaryEntropyPool bep;
    MetaData metaData;
    SystemClock systemClock;

    static constexpr size_t NUM_BITS      = 152; // bits of entropy
    static constexpr size_t TIMESTAMP_LEN = 13;  // ms timestamp digits
    static constexpr size_t RANDOM_CHARS  = 19;  // base62 chars from 152 bits
    static constexpr size_t UUID_LENGTH   = 32;  // total chars before hyphens

    // pre-allocated buffers
    std::string uuidBuffer;
    std::string fullString;
    std::string randomPart;

    inline long long extractTimestamp(const std::string &uuid) {
        std::string clean = removeHyphens(uuid);
        return std::stoll(clean.substr(0, TIMESTAMP_LEN));
    }

    inline std::string timestampToDate(long long ms) {
        std::time_t seconds = ms / 1000;
        std::tm *timeinfo   = std::localtime(&seconds);

        std::stringstream ss;
        ss << std::put_time(timeinfo, "%Y-%m-%d %H:%M:%S");

        return ss.str();
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Entropy Analyzer
//----------------------------------------------------------------------------------

class EntropyAnalyzer {
  public:
    static constexpr size_t BYTE_RANGE = 256;

    std::array<uint64_t, BYTE_RANGE> freq{};
    uint64_t totalBytes = 0;

    void reset() {
        freq.fill(0);
        totalBytes = 0;
    }

    void feedBits(const std::string &bits) {
        for (size_t i = 0; i + 8 <= bits.size(); i += 8) {
            uint8_t byte = 0;
            for (int b = 0; b < 8; b++) {
                byte <<= 1;
                byte |= (bits[i + b] == '1') ? 1 : 0;
            }
            freq[byte]++;
            totalBytes++;
        }
    }

    void print() const {
        if (totalBytes == 0) {
            std::cout << "No data.\n";
            return;
        }

        uint64_t maxFreq = *std::max_element(freq.begin(), freq.end());

        std::cout << "\nBYTE FREQUENCY DISTRIBUTION\n";
        std::cout << "Total bytes: " << totalBytes << "\n";
        std::cout << "Expected per byte (uniform): " << std::fixed << std::setprecision(2) << (100.0 / 256.0) << "%\n";
        std::cout << std::string(60, '-') << "\n";
        std::cout << "Each block = 10% of max observed frequency\n";
        std::cout << std::string(60, '-') << "\n\n";

        for (int i = 0; i < 256; i++) {
            double pct   = (static_cast<double>(freq[i]) / totalBytes) * 100.0;
            double ofMax = (static_cast<double>(freq[i]) / maxFreq) * 100.0;
            int blocks   = static_cast<int>(ofMax / 10.0);
            if (blocks > 10)
                blocks = 10;

            std::cout << std::dec << std::setw(3) << i << " (0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << i << std::dec << std::setfill(' ') << ")"
                      << " (";
            for (int b = 7; b >= 0; b--)
                std::cout << ((i >> b) & 1);
            std::cout << ")"
                      << " | ";

            for (int b = 0; b < 10; b++)
                std::cout << (b < blocks ? "█" : "·");

            std::cout << "  " << std::fixed << std::setprecision(3) << pct << "%"
                      << "  (" << freq[i] << ")\n";
        }

        double mean     = static_cast<double>(totalBytes) / 256.0;
        double variance = 0.0;
        for (int i = 0; i < 256; i++) {
            double diff = static_cast<double>(freq[i]) - mean;
            variance += diff * diff;
        }
        variance /= 256.0;
        double stddev = std::sqrt(variance);

        uint64_t minFreq = *std::min_element(freq.begin(), freq.end());

        std::cout << "\n" << std::string(60, '-') << "\n";
        std::cout << "Min frequency: " << minFreq << "  (" << std::fixed << std::setprecision(3) << (static_cast<double>(minFreq) / totalBytes * 100.0) << "%)\n";
        std::cout << "Max frequency: " << maxFreq << "  (" << std::fixed << std::setprecision(3) << (static_cast<double>(maxFreq) / totalBytes * 100.0) << "%)\n";
        std::cout << "Std deviation: " << std::fixed << std::setprecision(2) << stddev << " bytes\n";
        std::cout << "Ideal uniform: " << std::fixed << std::setprecision(2) << mean << " bytes per value\n";
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Database
//----------------------------------------------------------------------------------

class Database {
  public:
    struct Entry {
        std::string uuid;
        std::string name;
        std::string layers;
        std::string originalSize;
        std::string key;
        std::string xored;
    };

    void
    programRecord(const std::string &uuid, const std::string &name, const std::string &layers, const std::string &originalSize, const std::string &key, const std::string &xored) {
        ensureHeader(keysFile, keysHeader);
        ensureHeader(xorFile, xorHeader);

        appendToFile(keysFile, uuid + "|" + key + "\n");
        appendToFile(xorFile, uuid + "|" + name + "|" + layers + "|" + originalSize + "|" + xored + "\n");
    }

    std::vector<Entry> loadAll() const {
        auto keyMap  = parseKeysFile();
        auto entries = parseXorFile();

        for (auto &e : entries) {
            auto it = keyMap.find(e.uuid);
            if (it != keyMap.end())
                e.key = it->second;
        }

        return entries;
    }

    Entry selectRecord() const {
        auto entries = loadAll();

        if (entries.empty()) {
            std::cout << "No records found.\n";
            return {};
        }

        printTable(entries);

        int choice = -1;
        while (choice < 1 || choice > (int)entries.size()) {
            std::cout << "Select record number: ";
            std::string line;
            std::getline(std::cin, line);
            try {
                choice = std::stoi(line);
            } catch (...) {}
        }

        return entries[choice - 1];
    }

    std::string getKeyBits(const Entry &e) const { return hexToBits(e.key); }
    std::string getXoredBits(const Entry &e) const { return hexToBits(e.xored); }

    std::string getDocumentName() {
        std::string name;
        std::cout << "========================================================================================================================\n";
        std::cout << "                  DOCUMENT COMPRESSION INTERFACE                 \n";
        std::cout << "========================================================================================================================\n";
        std::cout << "Please enter the name of the document you would like to compress.\n";
        std::cout << "This name will be used as an identifier in the database and will\n";
        std::cout << "be associated with the compressed output for future retrieval.\n\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "Examples:\n";
        std::cout << "  - research_notes\n";
        std::cout << "  - project_alpha_data\n";
        std::cout << "  - encrypted_message_01\n\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        name = promptInput("Document Name: ");
        std::cout << "========================================================================================================================\n";
        if (name.empty()) {
            std::cout << "Warning: No name entered. Using default: 'untitled_doc'\n";
            name = "untitled_doc";
        }
        return name;
    }

    std::string getDirectoryName() {
        std::string dir;
        std::cout << "========================================================================================================================\n";
        std::cout << "Please enter a directory/category name for this document (max 64 characters).\n";
        std::cout << "========================================================================================================================\n";
        dir = promptInput("Directory: ");
        if (dir.empty()) {
            std::cout << "Warning: No directory entered. Using default: 'uncategorized'\n";
            dir = "uncategorized";
        }
        return dir;
    }

  private:
    const std::string keysFile   = "keys.db";
    const std::string xorFile    = "xor.db";
    const std::string keysHeader = "UUID|Key";
    const std::string xorHeader  = "UUID|Name|Layers|OrigSize|XOR";

    std::string promptInput(const std::string &message) {
        std::string input;
        while (true) {
            std::cout << message;
            std::getline(std::cin, input);
            if (input.size() <= 64)
                break;
            std::cout << "Error: max 64 characters allowed.\n";
        }
        return input;
    }

    void ensureHeader(const std::string &filename, const std::string &header) const {
        std::ifstream check(filename);
        bool needsHeader = !check.good() || check.peek() == std::ifstream::traits_type::eof();
        check.close();
        if (needsHeader)
            appendToFile(filename, header + "\n");
    }

    void appendToFile(const std::string &filename, const std::string &line) const {
        std::ofstream out(filename, std::ios::app);
        if (!out)
            throw std::runtime_error("Cannot open: " + filename);
        out << line;
    }

    std::unordered_map<std::string, std::string> parseKeysFile() const {
        std::unordered_map<std::string, std::string> keyMap;
        std::ifstream in(keysFile);
        if (!in)
            return keyMap;

        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first) {
                first = false;
                continue;
            }
            auto parts = split(line, '|');
            if (parts.size() < 2)
                continue;
            keyMap[parts[0]] = parts[1];
        }
        return keyMap;
    }

    std::vector<Entry> parseXorFile() const {
        std::vector<Entry> entries;
        std::ifstream in(xorFile);
        if (!in)
            return entries;

        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first) {
                first = false;
                continue;
            }
            auto parts = split(line, '|');
            if (parts.size() < 5)
                continue;

            Entry e;
            e.uuid         = parts[0];
            e.name         = parts[1];
            e.layers       = parts[2];
            e.originalSize = parts[3];
            e.xored        = parts[4];
            entries.push_back(e);
        }
        return entries;
    }

    void printTable(const std::vector<Entry> &entries) const {
        const int W_NUM      = 4;
        const int W_UUID     = 38;
        const int W_NAME     = 20;
        const int W_LAYERS   = 8;
        const int W_ORIGSIZE = 12;
        const int W_XOR      = 20;
        const int W_KEY      = 20;

        std::cout << "\n";

        std::cout << std::left << std::setw(W_NUM) << "#" << std::setw(W_UUID) << "UUID" << std::setw(W_NAME) << "Name" << std::setw(W_LAYERS) << "Layers" << std::setw(W_ORIGSIZE)
                  << "OrigSize" << std::setw(W_XOR) << "XOR" << std::setw(W_KEY) << "Key" << "\n";

        std::cout << std::string(W_NUM + W_UUID + W_NAME + W_LAYERS + W_ORIGSIZE + W_XOR + W_KEY, '-') << "\n";

        for (size_t i = 0; i < entries.size(); ++i) {
            const auto &e = entries[i];

            std::cout << std::left << std::setw(W_NUM) << (i + 1) << std::setw(W_UUID) << e.uuid << std::setw(W_NAME) << e.name << std::setw(W_LAYERS) << e.layers
                      << std::setw(W_ORIGSIZE) << e.originalSize << std::setw(W_XOR) << e.xored << std::setw(W_KEY) << e.key << "\n";
        }

        std::cout << "\n";
    }

    std::string hexToBits(const std::string &hex) const {
        std::string bits;
        bits.reserve(hex.size() * 4);

        for (char c : hex) {
            uint8_t nibble = 0;

            if (c >= '0' && c <= '9')
                nibble = c - '0';
            else if (c >= 'A' && c <= 'F')
                nibble = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f')
                nibble = c - 'a' + 10;

            for (int i = 3; i >= 0; --i)
                bits.push_back(((nibble >> i) & 1) ? '1' : '0');
        }
        return bits;
    }

    std::vector<std::string> split(const std::string &s, char delim) const {
        std::vector<std::string> parts;
        std::istringstream ss(s);
        std::string token;
        while (std::getline(ss, token, delim))
            parts.push_back(token);
        return parts;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// XOR Cypher
//----------------------------------------------------------------------------------

class XORCypher {
  public:
    struct Result {
        std::string K; // original A (stored key stream)
        std::string X; // XOR result
    };

    struct Decompressed {
        std::string A;
        std::string B;
    };

    // XOR ENCODE
    inline Result compress(const std::string &A, const std::string &B) {
        if (A.size() != B.size()) {
            throw std::runtime_error("A and B must be same size");
        }

        Result r;
        r.K.reserve(A.size());
        r.X.reserve(A.size());

        for (size_t i = 0; i < A.size(); i++) {

            char a = A[i];
            char b = B[i];

            if (!isBit(a) || !isBit(b)) {
                throw std::runtime_error("Invalid bit detected in XORCypher::compress");
            }

            r.K.push_back(a);
            r.X.push_back(xorBit(a, b));
        }

        return r;
    }

    // XOR DECODE
    inline Decompressed decompress(const std::string &K, const std::string &X) {
        if (K.empty() || X.empty()) {
            throw std::runtime_error("K or X is empty");
        }

        if (K.size() != X.size()) {
            throw std::runtime_error("Size mismatch");
        }

        Decompressed d;
        d.A.reserve(K.size());
        d.B.reserve(K.size());

        for (size_t i = 0; i < K.size(); i++) {

            char k = K[i];
            char x = X[i];

            if (!isBit(k) || !isBit(x)) {
                throw std::runtime_error("Invalid bit detected in XORCypher::decompress");
            }

            char A = k;
            char B = xorBit(k, x);

            d.A.push_back(A);
            d.B.push_back(B);
        }

        return d;
    }

  private:
    static constexpr const char *CLASS_NAME = "XORCypher";

    // HELPERS
    static inline bool isBit(char c) { return (c == '0' || c == '1'); }

    static inline char xorBit(char a, char b) { return (a == b) ? '0' : '1'; }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Text Encoding
//----------------------------------------------------------------------------------

class TextEncoding {
  public:
    EncodingResult encode(const std::string &text) {
        LOG_INFO("running text encoder");
        EncodingResult result;
        std::unordered_map<uint16_t, std::string> localIdToWord;
        result.symbols = encodeWords(text, localIdToWord);
        result.meta    = computeMeta(result.symbols);
        result.bytes   = encodeToBytes(result.symbols, result.meta, localIdToWord);
        result.binary  = functions.bytesToBinary(result.bytes);
        return result;
    }

    std::string decode(const std::string &binary) {
        auto bytes = functions.binaryToBytes(binary);
        std::unordered_map<uint16_t, std::string> localIdToWord;
        auto symbols = decodeFromBytes(bytes, localIdToWord);
        return decodeToTextExact(symbols, localIdToWord);
    }

  private:
    static constexpr const char *CLASS_NAME = "TextEncoding";
    Functions functions;

    std::vector<SymbolEntry> encodeWords(const std::string &text, std::unordered_map<uint16_t, std::string> &idToWord) {
        idToWord.clear();

        std::unordered_map<std::string, uint16_t> wordToId;
        std::unordered_map<std::string, std::vector<uint16_t>> positions;

        uint16_t nextId = 1;
        uint16_t pos    = 0;
        std::vector<std::string> tokens;
        std::string current;

        for (char c : text) {
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                tokens.push_back(std::string(1, c));
            } else {
                current.push_back(c);
            }
        }

        if (!current.empty())
            tokens.push_back(current);

        for (const std::string &w : tokens) {
            pos++;
            if (!wordToId.count(w)) {
                wordToId[w]      = nextId;
                idToWord[nextId] = w;
                nextId++;
            }
            positions[w].push_back(pos);
        }

        std::vector<SymbolEntry> output(wordToId.size());
        for (auto &[w, id] : wordToId) {
            output[id - 1] = {id, positions[w]};
        }

        return output;
    }

    std::string decodeToTextExact(const std::vector<SymbolEntry> &data, const std::unordered_map<uint16_t, std::string> &idToWord) {
        std::vector<std::pair<uint16_t, std::string>> ordered;

        for (const auto &e : data) {
            auto it          = idToWord.find(e.wordId);
            std::string word = (it != idToWord.end()) ? it->second : std::string();
            for (auto p : e.positions) {
                ordered.push_back({p, word});
            }
        }

        std::sort(ordered.begin(), ordered.end(), [](auto &a, auto &b) { return a.first < b.first; });

        std::string result;
        for (auto &x : ordered)
            result += x.second;

        return result;
    }

    std::vector<uint8_t> encodeToBytes(const std::vector<SymbolEntry> &data, EncodingMeta meta, const std::unordered_map<uint16_t, std::string> &idToWord) {
        std::vector<uint8_t> out;
        auto pushInt = [&](uint16_t v, int bytes) {
            for (int i = 0; i < bytes; i++)
                out.push_back((v >> (8 * i)) & 0xFF);
        };

        out.push_back(meta.wordIdBytes);
        out.push_back(meta.positionBytes);
        pushInt((uint16_t)data.size(), 2);

        for (const auto &e : data) {
            pushInt(e.wordId, meta.wordIdBytes);

            auto it                 = idToWord.find(e.wordId);
            const std::string &word = it->second; // guaranteed present: encodeWords just built this map
            pushInt((uint16_t)word.size(), 2);
            for (char c : word)
                out.push_back(static_cast<uint8_t>(c));

            pushInt((uint16_t)e.positions.size(), meta.positionBytes);
            for (auto p : e.positions)
                pushInt(p, meta.positionBytes);
        }
        return out;
    }

    std::vector<SymbolEntry> decodeFromBytes(const std::vector<uint8_t> &data, std::unordered_map<uint16_t, std::string> &idToWord) {
        if (data.size() < 3)
            throw std::runtime_error("Corrupt byte stream");

        size_t idx = 0;
        EncodingMeta meta;
        meta.wordIdBytes   = data[idx++];
        meta.positionBytes = data[idx++];

        auto readInt = [&](int bytes) -> uint16_t {
            uint16_t value = 0;
            for (int i = 0; i < bytes; i++) {
                if (idx >= data.size())
                    throw std::runtime_error("Unexpected EOF");
                value |= (data[idx++] << (8 * i));
            }
            return value;
        };

        uint16_t entryCount = readInt(2);
        std::vector<SymbolEntry> output;
        output.reserve(entryCount);

        idToWord.clear(); // rebuild fresh from THIS stream, not leftover state

        for (uint16_t i = 0; i < entryCount; i++) {
            SymbolEntry e;
            e.wordId = readInt(meta.wordIdBytes);

            uint16_t wordLen = readInt(2);
            std::string word;
            word.reserve(wordLen);
            for (uint16_t c = 0; c < wordLen; c++) {
                if (idx >= data.size())
                    throw std::runtime_error("Unexpected EOF");
                word.push_back(static_cast<char>(data[idx++]));
            }
            idToWord[e.wordId] = word;

            uint16_t posCount = readInt(meta.positionBytes);
            e.positions.reserve(posCount);
            for (uint16_t j = 0; j < posCount; j++)
                e.positions.push_back(readInt(meta.positionBytes));

            output.push_back(std::move(e));
        }
        return output;
    }

    EncodingMeta computeMeta(const std::vector<SymbolEntry> &data) {
        uint16_t maxId = 0, maxPos = 0;

        for (const auto &e : data) {
            maxId = std::max(maxId, e.wordId);
            for (auto p : e.positions)
                maxPos = std::max(maxPos, p);
        }

        return {bytesNeeded(maxId), bytesNeeded(maxPos)};
    }

    int bytesNeeded(uint32_t v) {
        if (v <= 0xFF)
            return 1;
        if (v <= 0xFFFF)
            return 2;
        return 4;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Run Length Encoding
//----------------------------------------------------------------------------------

class RunLengthEncoding {
  public:
    RLEResult encode(const std::vector<uint8_t> &input) {
        RLEResult result;

        if (input.empty())
            return result;

        uint8_t current    = input[0];
        uint16_t runLength = 1; // internal accumulator can still be bigger

        auto flushRun = [&](uint8_t value, uint16_t length) {
            while (length > 0) {
                uint8_t chunk = (length > 255) ? 255 : static_cast<uint8_t>(length);

                result.entries.push_back({value, chunk});
                length -= chunk;
            }
        };

        for (size_t i = 1; i < input.size(); i++) {
            if (input[i] == current) {
                runLength++;
            } else {
                flushRun(current, runLength);
                current   = input[i];
                runLength = 1;
            }
        }

        flushRun(current, runLength);

        result.bytes = encodeToBytes(result.entries);
        return result;
    }

    std::vector<uint8_t> decode(const std::vector<uint8_t> &data) {
        auto entries = decodeFromBytes(data);

        std::vector<uint8_t> output;
        output.reserve(entries.size() * 2);

        for (const auto &e : entries) {
            output.insert(output.end(), e.count, e.value);
        }

        return output;
    }

  private:
    static constexpr const char *CLASS_NAME = "RunLengthEncoding";
    //----------------------------------------------------
    // SERIALISATION
    //----------------------------------------------------

    std::vector<uint8_t> encodeToBytes(const std::vector<RLEEntry> &entries) {
        std::vector<uint8_t> out;

        uint16_t count = static_cast<uint16_t>(entries.size());

        out.push_back((count >> 8) & 0xFF);
        out.push_back(count & 0xFF);

        for (const auto &e : entries) {
            out.push_back(e.value);
            out.push_back(e.count);
        }

        return out;
    }

    std::vector<RLEEntry> decodeFromBytes(const std::vector<uint8_t> &data) {
        if (data.size() < 2)
            throw std::runtime_error("Corrupt RLE stream");

        size_t idx = 0;

        uint16_t numEntries = (data[idx] << 8) | data[idx + 1];
        idx += 2;

        std::vector<RLEEntry> entries;
        entries.reserve(numEntries);

        for (uint16_t i = 0; i < numEntries; i++) {
            if (idx + 1 >= data.size())
                throw std::runtime_error("Unexpected EOF");

            RLEEntry e;
            e.value = data[idx++];
            e.count = data[idx++];

            entries.push_back(e);
        }

        return entries;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Binary Encoding
//----------------------------------------------------------------------------------

class BinaryEncoding {
  public:
    // encode binary string → run length encoded binary string
    BinaryEncodingResult encode(const std::string &bits) {
        BinaryEncodingResult result;

        uint8_t padCount   = (8 - (bits.size() % 8)) % 8;
        std::string padded = bits + std::string(padCount, '0');

        result.entries = buildEntries(padded);
        result.bytes   = encodeToBytes(result.entries, padCount);
        result.binary  = functions.bytesToBinary(result.bytes);

        return result;
    }

    // decode binary string → original binary string
    std::string decode(const std::string &bits) {
        auto bytes = functions.binaryToBytes(bits);

        if (bytes.empty())
            throw std::runtime_error("Corrupt byte stream");

        uint8_t padCount = bytes[0];
        std::vector<uint8_t> rest(bytes.begin() + 1, bytes.end());

        auto entries       = decodeFromBytes(rest);
        std::string result = reconstruct(entries);

        if (padCount > 0)
            result.resize(result.size() - padCount);

        return result;
    }

  private:
    static constexpr const char *CLASS_NAME = "BinaryEncoding";
    Functions functions;

    // chunk bitstream into bytes, record position of each byte value
    // NOTE: caller must ensure bits.size() is a multiple of 8 (i.e. pre-padded)
    std::vector<ByteEntry> buildEntries(const std::string &bits) {
        std::map<uint8_t, std::vector<uint16_t>> posMap;

        uint16_t pos = 0;
        for (size_t i = 0; i + 8 <= bits.size(); i += 8) {
            uint8_t value = 0;
            for (size_t b = 0; b < 8; b++) {
                if (bits[i + b] == '1')
                    value |= (1 << (7 - b));
            }
            posMap[value].push_back(pos++);
        }

        std::vector<ByteEntry> entries;
        entries.reserve(posMap.size());

        for (auto &[val, positions] : posMap) {
            entries.push_back({val, positions});
        }

        return entries;
    }

    // reconstruct padded bitstream from entries (caller strips padding)
    std::string reconstruct(const std::vector<ByteEntry> &entries) {
        std::vector<std::pair<uint16_t, uint8_t>> ordered;

        for (const auto &e : entries) {
            for (auto p : e.positions) {
                ordered.push_back({p, e.byteValue});
            }
        }

        std::sort(ordered.begin(), ordered.end(), [](auto &a, auto &b) { return a.first < b.first; });

        std::string result;
        result.reserve(ordered.size() * 8);

        for (auto &[pos, val] : ordered) {
            for (int b = 7; b >= 0; b--) {
                result += ((val >> b) & 1) ? '1' : '0';
            }
        }

        return result;
    }

    std::vector<uint8_t> encodeToBytes(const std::vector<ByteEntry> &entries, uint8_t padCount) {
        std::vector<uint8_t> out;

        // prefix: pad count (0-7)
        out.push_back(padCount);

        // header: number of unique byte values
        uint16_t count = (uint16_t)entries.size();
        out.push_back((count >> 8) & 0xFF);
        out.push_back(count & 0xFF);

        for (const auto &e : entries) {
            // byte value
            out.push_back(e.byteValue);

            // position count
            uint16_t posCount = (uint16_t)e.positions.size();
            out.push_back((posCount >> 8) & 0xFF);
            out.push_back(posCount & 0xFF);

            // positions
            for (auto p : e.positions) {
                out.push_back((p >> 8) & 0xFF);
                out.push_back(p & 0xFF);
            }
        }

        return out;
    }

    std::vector<ByteEntry> decodeFromBytes(const std::vector<uint8_t> &data) {
        if (data.size() < 2)
            throw std::runtime_error("Corrupt byte stream");

        size_t idx     = 0;
        uint16_t count = (data[idx] << 8) | data[idx + 1];
        idx += 2;

        std::vector<ByteEntry> entries;
        entries.reserve(count);

        for (uint16_t i = 0; i < count; i++) {
            if (idx >= data.size())
                throw std::runtime_error("Unexpected EOF");

            ByteEntry e;
            e.byteValue = data[idx++];

            uint16_t posCount = (data[idx] << 8) | data[idx + 1];
            idx += 2;

            e.positions.reserve(posCount);
            for (uint16_t j = 0; j < posCount; j++) {
                uint16_t pos = (data[idx] << 8) | data[idx + 1];
                idx += 2;
                e.positions.push_back(pos);
            }

            entries.push_back(std::move(e));
        }

        return entries;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Huffman Encoding
//----------------------------------------------------------------------------------

class HuffmanEncoding {
  public:
    HuffmanResults runFromBits(const std::string &bits) {
        HuffmanResults results;

        results.inputBits = bits.size();

        std::vector<uint8_t> bytes = toBytes(bits);

        results.huffmanCode = buildHuffmanCode(bytes);

        std::string tableBits = encodeTable(results.huffmanCode);
        std::string dataBits  = encodeFromCode(bytes, results.huffmanCode);

        results.finalBinaryPackage = tableBits + dataBits;
        results.outputBits         = results.finalBinaryPackage.size();
        results.packedBytes        = packBitsToBytes(results.finalBinaryPackage, results.paddingBits); // add this

        return results;
    }

    HuffmanResults runFromBytes(const std::vector<uint8_t> &bytes) {
        HuffmanResults results;
        results.inputBits = bytes.size() * 8;

        results.huffmanCode = buildHuffmanCode(bytes);

        std::string tableBits = encodeTable(results.huffmanCode);
        std::string dataBits  = encodeFromCode(bytes, results.huffmanCode);

        results.finalBinaryPackage = tableBits + dataBits;
        results.outputBits         = results.finalBinaryPackage.size();
        results.packedBytes        = packBitsToBytes(results.finalBinaryPackage, results.paddingBits);

        return results;
    }

    inline static constexpr std::array<std::string_view, 20> STATIC_HUFFMAN_CODES = {
        "0",          "10",         "110",         "1110",        "11110",       "111110",      "1111110",      "1111111000",   "1111111001",   "1111111010",
        "1111111011", "1111111100", "11111111010", "11111111011", "11111111100", "11111111101", "111111111100", "111111111101", "111111111110", "111111111111"};

    static uint8_t bitsNeeded(uint32_t value) {
        uint8_t bits = 1;
        while ((1u << bits) <= value)
            bits++;
        return bits;
    }

    HuffmanResults runCanonical(const std::vector<uint8_t> &bytes) {
        HuffmanResults results;
        results.inputBits  = bytes.size() * 8;
        results.inputBytes = bytes;

        if (bytes.empty()) {
            results.finalBinaryPackage = "";
            results.outputBits         = 0;
            results.packedBytes        = {};
            return results;
        }

        std::array<size_t, 256> freq{};
        for (uint8_t b : bytes)
            freq[b]++;

        std::vector<uint8_t> symbolsByFreq;
        for (int v = 0; v < 256; v++) {
            if (freq[v] > 0)
                symbolsByFreq.push_back((uint8_t)v);
        }

        std::sort(symbolsByFreq.begin(), symbolsByFreq.end(), [&](uint8_t a, uint8_t b) { return freq[a] > freq[b]; });

        if (symbolsByFreq.size() > STATIC_HUFFMAN_CODES.size()) {}

        std::unordered_map<uint8_t, std::string> canonicalCodes;
        for (size_t rank = 0; rank < symbolsByFreq.size() && rank < STATIC_HUFFMAN_CODES.size(); rank++) {
            canonicalCodes[symbolsByFreq[rank]] = std::string(STATIC_HUFFMAN_CODES[rank]);
        }

        results.huffmanCode = canonicalCodes;

        uint8_t symbolCount = static_cast<uint8_t>(symbolsByFreq.size());
        uint8_t minSymbol   = symbolsByFreq.empty() ? 0 : *std::min_element(symbolsByFreq.begin(), symbolsByFreq.end());
        uint8_t maxSymbol   = symbolsByFreq.empty() ? 0 : *std::max_element(symbolsByFreq.begin(), symbolsByFreq.end());
        uint8_t symbolRange = static_cast<uint8_t>(maxSymbol - minSymbol);
        uint8_t symbolBits  = bitsNeeded(symbolRange);

        std::string tableBits;
        tableBits += toBits(symbolCount, 5);
        tableBits += toBits(minSymbol, 8);
        tableBits += toBits(symbolBits, 4);

        for (uint8_t symbol : symbolsByFreq) {
            tableBits += toBits(static_cast<uint32_t>(symbol - minSymbol), symbolBits);
        }

        std::string dataBits = encodeFromCode(bytes, canonicalCodes);

        results.finalBinaryPackage = tableBits + dataBits;
        results.outputBits         = results.finalBinaryPackage.size();
        results.packedBytes        = packBitsToBytes(results.finalBinaryPackage, results.paddingBits);

        return results;
    }

    // Encode using an externally-supplied fixed code (e.g. the {0,10,110,111} standard bitpairs table).
    // symbolOrder is the rank order (most-frequent first) used to reconstruct fixedCode on decode,
    // since we don't serialize a full table — just which symbol got which fixed-length slot.
    HuffmanResults runWithFixedCode(const std::vector<uint8_t> &bytes, const std::unordered_map<uint8_t, std::string> &fixedCode, const std::vector<uint8_t> &symbolOrder) {
        HuffmanResults results;
        results.inputBits   = bytes.size() * 8;
        results.inputBytes  = bytes;
        results.huffmanCode = fixedCode;

        // Minimal table: just the rank permutation, 2 bits per symbol (4 symbols -> 8 bits total)
        std::string tableBits;
        for (uint8_t sym : symbolOrder)
            tableBits += toBits(sym, 2);

        std::string dataBits = encodeFromCode(bytes, fixedCode);

        results.finalBinaryPackage = tableBits + dataBits;
        results.outputBits         = results.finalBinaryPackage.size();
        results.packedBytes        = packBitsToBytes(results.finalBinaryPackage, results.paddingBits);

        return results;
    }

    std::vector<uint8_t> decodeWithFixedCode(const std::string &bits) {
        static const std::array<std::string, 4> codeShape = {"0", "10", "110", "111"};

        size_t idx = 0;
        std::vector<uint8_t> symbolOrder;
        for (int i = 0; i < 4; i++)
            symbolOrder.push_back(static_cast<uint8_t>(fromBits(bits, idx, 2)));

        std::unordered_map<std::string, uint8_t> reverse;
        for (int i = 0; i < 4; i++)
            reverse[codeShape[i]] = symbolOrder[i];

        std::vector<uint8_t> result;
        std::string buffer;
        for (size_t i = idx; i < bits.size(); i++) {
            buffer += bits[i];
            if (reverse.count(buffer)) {
                result.push_back(reverse.at(buffer));
                buffer.clear();
            }
        }

        return result;
    }

    std::vector<uint8_t> packBitsToBytes(const std::string &bits, uint8_t &paddingOut) {
        std::vector<uint8_t> bytes;
        bytes.reserve((bits.size() + 7) / 8);

        size_t i = 0;
        for (; i + 8 <= bits.size(); i += 8) {
            uint8_t byte = 0;
            for (int b = 0; b < 8; ++b)
                byte = (byte << 1) | (bits[i + b] == '1' ? 1 : 0);
            bytes.push_back(byte);
        }

        size_t remaining = bits.size() - i;
        paddingOut       = 0;
        if (remaining > 0) {
            uint8_t byte = 0;
            for (int b = 0; b < 8; ++b) {
                bool bit = (b < (int)remaining) ? (bits[i + b] == '1') : false;
                byte     = (byte << 1) | (bit ? 1 : 0);
            }
            bytes.push_back(byte);
            paddingOut = static_cast<uint8_t>(8 - remaining);
        }
        return bytes;
    }

    HuffmanResults runCanonicalFromBits(const std::string &bits) {
        std::vector<uint8_t> bytes = toBytes(bits);
        return runCanonical(bytes);
    }

    std::string decode(const std::string &bits) {
        auto [code, dataStart]     = decodeTable(bits);
        std::vector<uint8_t> bytes = decodeFromCode(bits.substr(dataStart), code);
        return bytesToBits(bytes);
    }

    std::vector<uint8_t> decodeCanonical(const std::string &bits) {
        size_t idx   = 0;
        size_t count = fromBits(bits, idx, 8);

        // read symbol/length pairs and reconstruct canonical codes
        std::vector<std::pair<uint8_t, uint8_t>> symbolLengths;
        for (size_t i = 0; i < count; i++) {
            uint8_t symbol = static_cast<uint8_t>(fromBits(bits, idx, 8));
            uint8_t len    = static_cast<uint8_t>(fromBits(bits, idx, 8));
            symbolLengths.push_back({symbol, len});
        }

        // symbolLengths already stored in canonical order so reconstruct directly
        std::unordered_map<std::string, uint8_t> reverse;
        uint32_t code  = 0;
        uint8_t length = symbolLengths[0].second;

        for (const auto &[symbol, len] : symbolLengths) {
            if (len > length) {
                code <<= (len - length);
                length = len;
            }
            std::string codeBits;
            for (int i = length - 1; i >= 0; --i) {
                codeBits += ((code >> i) & 1) ? '1' : '0';
            }
            reverse[codeBits] = symbol;
            code++;
        }

        // decode data
        std::vector<uint8_t> result;
        std::string buffer;
        for (size_t i = idx; i < bits.size(); i++) {
            buffer += bits[i];
            if (reverse.count(buffer)) {
                result.push_back(reverse.at(buffer));
                buffer.clear();
            }
        }

        return result;
    }

  private:
    static constexpr const char *CLASS_NAME = "HuffmanEncoding";

    std::vector<uint8_t> toBytes(const std::string &bits) {
        std::vector<uint8_t> bytes;
        bytes.reserve(bits.size() / 8);

        for (size_t i = 0; i + 8 <= bits.size(); i += 8) {
            uint8_t value = 0;
            for (size_t b = 0; b < 8; b++) {
                if (bits[i + b] == '1')
                    value |= (1 << (7 - b));
            }
            bytes.push_back(value);
        }

        return bytes;
    }

    std::string bytesToBits(const std::vector<uint8_t> &bytes) {
        std::string bits;
        bits.reserve(bytes.size() * 8);

        for (uint8_t b : bytes) {
            for (int i = 7; i >= 0; i--) {
                bits += ((b >> i) & 1) ? '1' : '0';
            }
        }

        return bits;
    }

    struct HuffmanNode {
        uint8_t value = 0;
        size_t freq   = 0;
        std::shared_ptr<HuffmanNode> left;
        std::shared_ptr<HuffmanNode> right;
        bool isLeaf() const { return !left && !right; }
    };

    struct HuffmanCompare {
        bool operator()(const std::shared_ptr<HuffmanNode> &a, const std::shared_ptr<HuffmanNode> &b) const { return a->freq > b->freq; }
    };

    std::unordered_map<uint8_t, std::string> buildHuffmanCode(const std::vector<uint8_t> &bytes) {
        std::unordered_map<uint8_t, size_t> freq;
        for (uint8_t b : bytes)
            freq[b]++;

        std::priority_queue<std::shared_ptr<HuffmanNode>, std::vector<std::shared_ptr<HuffmanNode>>, HuffmanCompare> pq;

        for (const auto &[val, count] : freq) {
            auto node   = std::make_shared<HuffmanNode>();
            node->value = val;
            node->freq  = count;
            pq.push(node);
        }

        if (pq.empty())
            return {};

        if (pq.size() == 1) {
            std::unordered_map<uint8_t, std::string> single;
            single[pq.top()->value] = "0";
            return single;
        }

        while (pq.size() > 1) {
            auto left = pq.top();
            pq.pop();
            auto right = pq.top();
            pq.pop();
            auto parent   = std::make_shared<HuffmanNode>();
            parent->freq  = left->freq + right->freq;
            parent->left  = left;
            parent->right = right;
            pq.push(parent);
        }

        std::unordered_map<uint8_t, std::string> code;
        buildCodeRecursive(pq.top(), "", code);

        return code;
    }

    void buildCodeRecursive(const std::shared_ptr<HuffmanNode> &node, const std::string &prefix, std::unordered_map<uint8_t, std::string> &code) {
        if (node->isLeaf()) {
            code[node->value] = prefix.empty() ? "0" : prefix;
            return;
        }

        buildCodeRecursive(node->left, prefix + "0", code);
        buildCodeRecursive(node->right, prefix + "1", code);
    }

    std::string encodeFromCode(const std::vector<uint8_t> &bytes, const std::unordered_map<uint8_t, std::string> &code) {
        std::string bits;
        for (uint8_t b : bytes)
            bits += code.at(b);
        return bits;
    }

    std::string encodeTable(const std::unordered_map<uint8_t, std::string> &code) {
        std::string bits;

        bits += toBits(code.size(), 16);

        for (const auto &[symbol, huff] : code) {
            bits += toBits(symbol, 8);
            bits += toBits(huff.size(), 8);
            bits += huff;
        }

        return bits;
    }

    std::pair<std::unordered_map<uint8_t, std::string>, size_t> decodeTable(const std::string &bits) {
        size_t idx   = 0;
        size_t count = fromBits(bits, idx, 16);

        std::unordered_map<uint8_t, std::string> code;

        for (size_t i = 0; i < count; i++) {
            uint8_t symbol   = static_cast<uint8_t>(fromBits(bits, idx, 8));
            size_t len       = fromBits(bits, idx, 8);
            std::string huff = bits.substr(idx, len);
            idx += len;
            code[symbol] = huff;
        }

        return {code, idx};
    }

    std::vector<uint8_t> decodeFromCode(const std::string &bits, const std::unordered_map<uint8_t, std::string> &code) {
        std::unordered_map<std::string, uint8_t> inverted;
        for (const auto &[sym, huff] : code)
            inverted[huff] = sym;

        std::vector<uint8_t> result;
        std::string current;

        for (char c : bits) {
            current += c;
            if (inverted.count(current)) {
                result.push_back(inverted[current]);
                current.clear();
            }
        }

        return result;
    }

    std::string toBits(uint64_t value, size_t bitCount) {
        std::string bits;
        bits.reserve(bitCount);
        for (int i = bitCount - 1; i >= 0; i--) {
            bits += ((value >> i) & 1) ? '1' : '0';
        }
        return bits;
    }

    size_t fromBits(const std::string &bits, size_t &idx, size_t count) {
        size_t value = 0;
        for (size_t i = 0; i < count; i++) {
            value = (value << 1) | (bits[idx++] == '1' ? 1 : 0);
        }
        return value;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Encoding Router
//----------------------------------------------------------------------------------

class EncodingRouter {
  public:
    struct TextAnalysis {
        bool isASCII      = true;
        size_t byteSize   = 0;
        size_t asciiBytes = 0;
        size_t utf8Bytes  = 0;
        size_t codepoints = 0;
        double utf8Ratio  = 0.0;
    };

    std::string encode(const std::string &input, bool printC, CompressionState &state) {
        std::string binary;
        TextAnalysis analysis = analyzeUtf8(input);

        state.isText = !isBinary(input);
        LOG_INFO("Is Text? Yes = " + std::to_string(state.isText));

        state.originalBits = input.size() * 8;
        LOG_INFO("Original Bits: " + std::to_string(state.originalBits));

        if (state.isText) {
            LOG_INFO("Applying Text Encoding...");
            auto dataResult = textEncoding.encode(input);
            state.encoded   = dataResult.symbols;
            state.bytes     = dataResult.bytes;
            binary          = dataResult.binary;

            if (printC) {
                std::cout << "TEXT detected\n";
                std::cout << "Codepoints:    " << analysis.codepoints << "\n";
                std::cout << "ASCII bytes:   " << analysis.asciiBytes << "\n";
                std::cout << "UTF-8 bytes:   " << analysis.utf8Bytes << "\n";
                std::cout << "Original bits: " << state.originalBits << "\n";
                std::cout << "Total bytes:   " << analysis.byteSize << "\n";
                std::cout << "UTF-8 ratio:   " << analysis.utf8Ratio << "\n";
                std::cout << "Encoded bits:  " << binary.size() << "\n";
                std::cout << "Encoded bytes: " << binary.size() / 8 << "\n";

                std::cout << "\nEncoded binary:\n";
                std::cout << binary << '\n';

                std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
            }

            LOG_INFO("Codepoints:    " + std::to_string(analysis.codepoints));
            LOG_INFO("ASCII bytes:   " + std::to_string(analysis.asciiBytes));
            LOG_INFO("UTF-8 bytes:   " + std::to_string(analysis.utf8Bytes));
            LOG_INFO("Original bits: " + std::to_string(state.originalBits));
            LOG_INFO("Total bytes:   " + std::to_string(analysis.byteSize));
            LOG_INFO("UTF-8 ratio:   " + std::to_string(analysis.utf8Ratio));
            LOG_INFO("Encoded bits:  " + std::to_string(binary.size()));
            LOG_INFO("Encoded bytes: " + std::to_string(binary.size() / 8));
        } else {
            binary = input;
        }

        const std::string baseline = binary;

        LOG_INFO("Baseline Binary:");
        LOG_INFO(baseline);

        LOG_INFO("Applying Run Length Encoding...");
        bool rleApplied = applyRunLengthEncoding(printC, state, binary);

        LOG_INFO("Applying Binary Encoding...");
        bool binApplied = applyBinaryEncoding(printC, state, binary);

        LOG_INFO("Applying Huffman Encoding...");
        bool huffApplied = applyHuffmanEncoding(printC, state, binary);

        // Roll back if compression didn't help.
        if (binary.size() >= baseline.size()) {
            if (printC)
                std::cout << "No compression gain     — returning original\n";

            LOG_INFO("No compression gain     — returning original");

            binary = baseline;

            rleApplied  = false;
            binApplied  = false;
            huffApplied = false;

            state.afterRleBits     = baseline.size();
            state.afterBinBits     = baseline.size();
            state.afterHuffmanBits = baseline.size();
        }

        uint8_t header = 0;

        if (state.isText) {
            header |= (1 << 3);
        } else {
            // No text encoding was applied.
            state.encoded.clear();
            state.bytes.clear();
        }

        if (rleApplied) {
            header |= (1 << 2);
        } else {
            // RLE not used.
            state.rleEntries.clear();
        }

        if (binApplied) {
            header |= (1 << 1);
        } else {
            // Binary encoding not used.
            state.byteEntries.clear();
        }

        if (huffApplied) {
            header |= (1 << 0);
        } else {
            // Huffman not used.
            state.huffman = HuffmanResults{};
        }

        // Convert header byte to 8 bits.
        std::string headerBits;
        headerBits.reserve(8);

        for (int i = 7; i >= 0; --i)
            headerBits.push_back((header & (1 << i)) ? '1' : '0');

        LOG_INFO("Header: " + headerBits);

        // Prefix the header.
        std::string finalBits = headerBits + binary;

        state.compressionRatio = 100.0 * (1.0 - (double)state.afterHuffmanBits / (double)state.originalBits);

        LOG_INFO("Compression Ratio: " + std::to_string(state.compressionRatio) + "%");

        LOG_INFO("RETURNING: Final bits");

        return finalBits;
    }

  private:
    static constexpr const char *CLASS_NAME = "EncodingRouter";

    Functions functions;
    TextEncoding textEncoding;
    RunLengthEncoding runLengthEncoding;
    BinaryEncoding binaryEncoding;
    HuffmanEncoding huffmanEncoding;

    bool applyRunLengthEncoding(bool printC, CompressionState &state, std::string &binary) {
        LOG_INFO("Converting Binary to bytes for RLE...");
        std::vector<uint8_t> inputBytes = functions.binaryToBytes(binary);

        RLEResult rleResult = runLengthEncoding.encode(inputBytes);
        state.rleEntries    = rleResult.entries;

        std::string rleBinary = functions.bytesToBinary(rleResult.bytes);

        if (!rleBinary.empty() && rleBinary.size() < binary.size()) {
            if (printC)
                std::cout << "Byte RLE accepted (" << binary.size() << " -> " << rleBinary.size() << " bits)\n";
            LOG_INFO("Byte RLE accepted (" + std::to_string(binary.size()) + " -> " + std::to_string(rleBinary.size()) + " bits)");
            binary             = rleBinary;
            state.afterRleBits = binary.size();
            return true;
        }

        if (printC)
            std::cout << "Byte RLE rejected       — no gain\n";
        LOG_INFO("Byte RLE rejected       — no gain\n");
        state.afterRleBits = binary.size();
        return false;
    }

    bool applyBinaryEncoding(bool printC, CompressionState &state, std::string &binary) {
        auto binResult    = binaryEncoding.encode(binary);
        state.byteEntries = binResult.entries;

        if (binResult.binary.size() < binary.size()) {
            if (printC)
                std::cout << "BinaryEncoding accepted (" << binary.size() << " -> " << binResult.binary.size() << " bits)\n";
            LOG_INFO("BinaryEncoding accepted (" + std::to_string(binary.size()) + " -> " + std::to_string(binResult.binary.size()) + " bits)");
            binary             = binResult.binary;
            state.afterBinBits = binary.size();
            return true;
        }

        if (printC)
            std::cout << "BinaryEncoding rejected — no gain\n";
        LOG_INFO("BinaryEncoding rejected — no gain");
        state.afterBinBits = binary.size();
        return false;
    }

    bool applyHuffmanEncoding(bool printC, CompressionState &state, std::string &binary) {
        auto huffResult = huffmanEncoding.runFromBits(binary);
        state.huffman   = huffResult;

        if (huffResult.finalBinaryPackage.size() < binary.size()) {
            if (printC)
                std::cout << "Huffman accepted (" << binary.size() << " -> " << huffResult.finalBinaryPackage.size() << " bits)\n";
            LOG_INFO("Huffman accepted (" + std::to_string(binary.size()) + " -> " + std::to_string(huffResult.finalBinaryPackage.size()) + " bits)");
            binary                 = huffResult.finalBinaryPackage;
            state.afterHuffmanBits = binary.size();
            return true;
        }

        if (printC)
            std::cout << "Huffman rejected        — no gain\n";
        LOG_INFO("Huffman rejected        — no gain");
        return false;
    }

    // decode: pure pipeline, no state needed
    std::string decode(const std::string &input) {
        bool isText      = (input[4] == '1');
        bool rleApplied  = (input[5] == '1');
        bool binApplied  = (input[6] == '1');
        bool huffApplied = (input[7] == '1');

        std::string binary = input.substr(8);

        if (huffApplied)
            binary = huffmanEncoding.decode(binary);

        if (binApplied)
            binary = binaryEncoding.decode(binary);

        if (rleApplied) {
            auto bytes        = functions.binaryToBytes(binary);
            auto decodedBytes = runLengthEncoding.decode(bytes);
            binary            = functions.bytesToBinary(decodedBytes);
        }

        if (isText)
            return textEncoding.decode(binary);

        return binary;
    }

    bool isBinary(const std::string &input) {
        for (char c : input)
            if (c != '0' && c != '1')
                return false;
        return true;
    }

    TextAnalysis analyzeUtf8(const std::string &input) {
        TextAnalysis stats;
        stats.byteSize = input.size();

        size_t i = 0;
        while (i < input.size()) {
            unsigned char c = static_cast<unsigned char>(input[i]);

            if (c <= 0x7F) {
                stats.asciiBytes++;
                stats.codepoints++;
                i += 1;
            } else if ((c & 0xE0) == 0xC0 && i + 1 < input.size()) {
                stats.utf8Bytes += 2;
                stats.codepoints++;
                stats.isASCII = false;
                i += 2;
            } else if ((c & 0xF0) == 0xE0 && i + 2 < input.size()) {
                stats.utf8Bytes += 3;
                stats.codepoints++;
                stats.isASCII = false;
                i += 3;
            } else if ((c & 0xF8) == 0xF0 && i + 3 < input.size()) {
                stats.utf8Bytes += 4;
                stats.codepoints++;
                stats.isASCII = false;
                i += 4;
            } else {
                // invalid byte → treat as 1 byte fallback
                stats.utf8Bytes++;
                stats.codepoints++;
                stats.isASCII = false;
                i += 1;
            }
        }

        stats.utf8Ratio = stats.byteSize > 0 ? (double)stats.utf8Bytes / (double)stats.byteSize : 0.0;

        return stats;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Operations Result
//----------------------------------------------------------------------------------

struct OperationsResult {
    std::string encodedValue;
    std::string decodedValue;

    void reset() {
        encodedValue.clear();
        decodedValue.clear();
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Binary Entropy Pool
//----------------------------------------------------------------------------------

class Operations {
  public:
    inline std::string runC(std::string eBits) {
        for (size_t offset = 0; offset < eBits.size(); offset += CHUNK_SIZE) {
            std::string chunk = eBits.substr(offset, CHUNK_SIZE);
            OP_C(chunk);
        }

        return opResults.encodedValue;
    }

    inline std::string runD(std::string dBits) {
        opResults.decodedValue.clear();

        auto blocks = splitEncoded(dBits);
        for (const auto &b : blocks)
            processBlock(b);

        return opResults.decodedValue;
    }

    inline void printVerification(const std::string &original) const {
        bool sizeMatch    = original.size() == opResults.decodedValue.size();
        bool contentMatch = original == opResults.decodedValue;

        std::cout << "\n";
        std::cout << "========================================\n";
        std::cout << "VERIFICATION\n";
        std::cout << "========================================\n";
        std::cout << "Original size      : " << original.size() << "\n";
        std::cout << "Decoded size       : " << opResults.decodedValue.size() << "\n";
        std::cout << "Size match         : " << (sizeMatch ? "YES" : "NO") << "\n";
        std::cout << "Content match      : " << (contentMatch ? "YES" : "NO") << "\n";

        if (!contentMatch) {
            size_t minLen = std::min(original.size(), opResults.decodedValue.size());

            for (size_t i = 0; i < minLen; ++i) {
                if (original[i] != opResults.decodedValue[i]) {
                    std::cout << "First mismatch at bit " << i << "\n";
                    std::cout << "Expected: " << original[i] << "\n";
                    std::cout << "Actual: " << opResults.decodedValue[i] << "\n";

                    break;
                }
            }
        }

        std::cout << "========================================\n";
    }

  private:
    static constexpr const char *CLASS_NAME = "Operations";
    static constexpr size_t CHUNK_SIZE      = 2049; // 2+0+4+9 = 15, divisible by 3
    OperationsResult opResults;

    // ENCODER
    inline void OP_C(std::string &eBits) {
        char currentState = eBits[0];
        uint8_t padBits   = static_cast<uint8_t>((3 - (eBits.size() % 3)) % 3);

        opResults.encodedValue += currentState;
        opResults.encodedValue += ':';
        opResults.encodedValue += padBitsToPair(padBits); // "00", "01", or "10"

        for (uint8_t i = 0; i < padBits; i++) {
            eBits.push_back('0');
        }

        while (eBits.size() >= 3) {
            char a = eBits[0];
            char b = eBits[1];
            char c = eBits[2];

            if (a != currentState) {
                opResults.encodedValue += "#";
                currentState = a;
            }

            applyOpEncode(currentState, eBits, a, b, c);
        }
    }

    void opCode(char &p, char &q, bool one) {
        char x = one ? '0' : '1'; // x = opposite of state
        char y = one ? '1' : '0'; // y = same as state

        if (p == x && q == x) {
            p = y;
            q = y;
            flip1(); // 11
        } else if (p == x && q == y) {
            p = y;
            flip2(); // 01
        } else if (p == y && q == x) {
            q = y;
            flip3(); // 10
        } else if (p == y && q == y) {
            flip4(); // 00
        }
    }

    inline void applyOpEncode(char state, std::string &eBits, char &a, char &b, char &c) {
        if (state == '0') {
            opCode(b, c, false);
        } else if (state == '1') {
            opCode(b, c, true);
        }
        operate3(eBits);
    }

    inline void operate3(std::string &eBits) { eBits.erase(0, 3); }

    inline void flip1() {
        opResults.encodedValue.push_back('1');
        opResults.encodedValue.push_back('1');
    }

    inline void flip2() {
        opResults.encodedValue.push_back('0');
        opResults.encodedValue.push_back('1');
    }

    inline void flip3() {
        opResults.encodedValue.push_back('1');
        opResults.encodedValue.push_back('0');
    }

    inline void flip4() {
        opResults.encodedValue.push_back('0');
        opResults.encodedValue.push_back('0');
    }

    inline void applyOpDecode(char &state, const std::string &op) {
        if (op == "#") {
            state = (state == '0') ? '1' : '0';
            return; // no bits to emit — this is a state-change marker, not data
        }

        char x = (state == '0') ? '1' : '0'; // x = opposite of state (matches opCode)
        char y = state;                      // y = same as state (matches opCode)

        char b, c;
        if (op == "11") {
            b = x;
            c = x;
        } else if (op == "01") {
            b = x;
            c = y;
        } else if (op == "10") {
            b = y;
            c = x;
        } else /* "00" */ {
            b = y;
            c = y;
        }

        opResults.decodedValue.push_back(state); // a = state
        opResults.decodedValue.push_back(b);
        opResults.decodedValue.push_back(c);
    }

    inline void processBlock(const std::string &block) {
        char state          = block[0];
        std::string padPair = block.substr(2, 2);
        uint8_t padBits     = pairToPadBits(padPair);
        std::string data    = block.substr(4); // header is now 4 chars: state, ':', pad-pair

        size_t startLen = opResults.decodedValue.size();

        for (size_t i = 0; i < data.size();) {
            std::string op;
            if (data[i] == '#') {
                op = "#";
                i += 1;
            } else if (i + 1 < data.size()) {
                op += data[i];
                op += data[i + 1];
                i += 2;
            } else {
                break;
            }
            applyOpDecode(state, op);
        }

        (void)startLen; // no longer needed for length math, kept in case you want it for logging
        if (padBits > 0) {
            opResults.decodedValue.resize(opResults.decodedValue.size() - padBits);
        }
    }

    inline std::vector<std::string> splitEncoded(const std::string &encoded) {
        std::vector<std::string> blocks;
        std::string current;

        for (size_t i = 0; i < encoded.size(); i++) {
            if (i + 1 < encoded.size() && (encoded[i] == '0' || encoded[i] == '1') && encoded[i + 1] == ':') {
                if (!current.empty()) {
                    blocks.push_back(current);
                    current.clear();
                }
                current += encoded[i];
                current += ':';
                i++;
            } else {
                current += encoded[i];
            }
        }

        if (!current.empty())
            blocks.push_back(current);

        return blocks;
    }

    inline std::string padBitsToPair(uint8_t padBits) {
        switch (padBits) {
        case 0:
            return "00";
        case 1:
            return "01";
        case 2:
            return "10";
        default:
            return "00"; // shouldn't happen, (3 - n%3)%3 is always 0/1/2
        }
    }

    inline uint8_t pairToPadBits(const std::string &pair) {
        if (pair == "00")
            return 0;
        if (pair == "01")
            return 1;
        if (pair == "10")
            return 2;
        return 0;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Printer
//----------------------------------------------------------------------------------

class Printer {
  public:
    Printer(const CompressionState &state)
        : state(state) {}

    // clang-format off
    enum Method {
        METHOD_1,  // Word ID index
        METHOD_2,  // Byte stream
        METHOD_3,  // RLE index
        METHOD_4,  // Huffman
        METHOD_5,  // Router
        METHOD_6,  // eBits
        METHOD_7,  // dBits
        METHOD_8,  // Block headers
        METHOD_9,  // No hashes
        METHOD_10, // No headers
        METHOD_11, // Hash positions
        METHOD_12, // Hash gaps
        METHOD_13, // Hash gap bits
        METHOD_14, // adaptive bits
        METHOD_15, // Run Length Encoding
        METHOD_16,  // Gap Huffman
        METHOD_17,  // Gap Huffman
        METHOD_18  // BitPairs Huffman Canonical
    };

    void print(Method method, bool printP) {
        switch (method) {
        case METHOD_1:  std::cout << printMethod_1(printP);  break;
        case METHOD_2:  std::cout << printMethod_2(printP);  break;
        case METHOD_3:  std::cout << printMethod_3(printP);  break;
        case METHOD_4:  std::cout << printMethod_4(printP);  break;
        case METHOD_5:  std::cout << printMethod_5(printP);  break;
        case METHOD_6:  std::cout << printMethod_6(printP);  break;
        case METHOD_7:  std::cout << printMethod_7(printP);  break;
        case METHOD_8:  std::cout << printMethod_8(printP);  break;
        case METHOD_9:  std::cout << printMethod_9(printP);  break;
        case METHOD_10: std::cout << printMethod_10(printP); break;
        case METHOD_11: std::cout << printMethod_11(printP); break;
        case METHOD_12: std::cout << printMethod_12(printP); break;
        case METHOD_13: std::cout << printMethod_13(printP); break;
        case METHOD_14: std::cout << printMethod_14(printP); break;
        case METHOD_15: std::cout << printMethod_15(printP); break;
        case METHOD_16: std::cout << printMethod_16(printP); break;
        case METHOD_17: std::cout << printMethod_17(printP); break;
        case METHOD_18: std::cout << printMethod_18(printP); break;
        }
    }
    // clang-format on

  private:
    static constexpr const char *CLASS_NAME = "Printer";
    const CompressionState &state;

    // =========================================================
    // PRINT METHODS
    // =========================================================

    inline std::string printMethod_1(bool printP) {
        size_t totalPositions = 0;
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "WORD ID INDEX\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Size: " << state.encoded.size() << "\n";
        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            for (const auto &entry : state.encoded) {
                totalPositions += entry.positions.size();

                out << "WordID: " << entry.wordId;
                out << " | Occ: " << entry.positions.size();
                out << " | Positions: ";

                for (size_t i = 0; i < entry.positions.size(); i++) {
                    out << entry.positions[i];
                    if (i + 1 < entry.positions.size())
                        out << ",";
                }

                out << "\n";
            }
        }

        out << "Total positions: " << totalPositions << "\n";
        return out.str();
    }

    inline std::string printMethod_2(bool printP) {
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "WORD ID BYTE STREAM\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Size: " << state.bytes.size() << "\n";
        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            for (size_t i = 0; i < state.bytes.size(); i++) {

                // Start a new line every 8 bytes
                if (i % 8 == 0) {
                    if (i != 0)
                        out << "\n";

                    out << "[" << (i / 8) << "] ";
                }

                out << std::bitset<8>(state.bytes[i]) << " ";
            }
        }

        return out.str();
    }

    inline std::string printMethod_3(bool printP) {
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "BINARY RLE STREAM\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Size: " << state.byteEntries.size() << "\n";
        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            for (const auto &e : state.byteEntries) {
                out << "Byte: " << std::setw(3) << static_cast<int>(e.byteValue) << " | Occ: " << std::setw(4) << e.positions.size() << " | Positions: ";

                for (size_t i = 0; i < e.positions.size(); i++) {
                    out << e.positions[i];
                    if (i + 1 < e.positions.size())
                        out << ",";
                }

                out << "\n";
            }
        }

        return out.str();
    }

    inline std::string printMethod_4(bool printP) {
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "HUFFMAN ENCODING BYTE STREAM\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Input size:  " << state.huffman.inputBits << " bits\n";
        out << "Output size: " << state.huffman.outputBits << " bits\n";
        out << "Codec:       " << state.huffman.selectedCodec << "\n";
        out << "Huffman table:\n";
        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            auto ordered = std::vector<std::pair<uint8_t, std::string>>(state.huffman.huffmanCode.begin(), state.huffman.huffmanCode.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

            for (const auto &[byte, huff] : ordered) {
                out << "  " << std::setw(3) << static_cast<int>(byte) << " -> " << huff << "\n";
            }
        }

        return out.str();
    }

    inline std::string printMethod_5(bool printP) {
        std::ostringstream out;
        out << "========================================================================================================================\n";
        out << "ENCODING ROUTER SUMMERY\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Input type:    " << (state.isText ? "TEXT" : "BINARY") << "\n";
        out << "Original size: " << state.originalBits << " bits\n";
        out << "After RLE:     " << state.afterRleBits << " bits\n";
        out << "After Huffman: " << state.afterHuffmanBits << " bits\n";
        out << "Compression:   " << std::fixed << std::setprecision(2) << state.compressionRatio << "%\n";
        return out.str();
    }

    inline std::string printMethod_6(bool printP) {
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "SURCE EBITS\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Size: " << state.eBits.size() << " bits\n";
        out << "Size: " << state.eBits.size() / 8 << " Bytes\n";
        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            out << state.eBits << "\n";
        }

        return out.str();
    }

    inline std::string printMethod_7(bool printP) {
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "DBITS\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Size: " << state.dBits.size() << " Bytes\n";

        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";

            for (size_t i = 0; i < state.dBits.size(); ++i) {
                if (i > 0 && (state.dBits[i] == '0' || state.dBits[i] == '1') && i + 1 < state.dBits.size() && state.dBits[i + 1] == ':') {
                    out << "\n\n";
                }

                out << state.dBits[i];
            }

            out << '\n';
        }

        return out.str();
    }

    inline std::string printMethod_8(bool printP) {
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "BLOCK HEADERS\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Count: " << state.blocks.size() << "\n";
        out << "States: " << state.blockStates << "\n";
        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            for (size_t i = 0; i < state.blocks.size(); i++) {
                out << "Block " << (i + 1) << " state: " << state.blocks[i].state << " pos: " << state.blocks[i].symbolPosition << "\n";
            }
        }

        return out.str();
    }

    inline std::string printMethod_9(bool printP) {
        std::ostringstream out;

        size_t count00 = 0;
        size_t count01 = 0;
        size_t count10 = 0;
        size_t count11 = 0;

        //--------------------------------------------------
        // Count pairs
        //--------------------------------------------------
        for (size_t i = 0; i + 1 < state.bitPairs.size(); i += 2) {
            char a = state.bitPairs[i];
            char b = state.bitPairs[i + 1];

            if (a == '0' && b == '0')
                ++count00;
            else if (a == '0' && b == '1')
                ++count01;
            else if (a == '1' && b == '0')
                ++count10;
            else if (a == '1' && b == '1')
                ++count11;
        }

        size_t totalPairs = count00 + count01 + count10 + count11;

        auto percent = [&](size_t count) {
            if (totalPairs == 0)
                return 0.0;
            return (100.0 * count) / totalPairs;
        };

        auto bits = [](size_t count) {
            return count * 2;
        };

        auto bytes = [&](size_t count) {
            return bits(count) / 8.0;
        };

        //--------------------------------------------------
        // Output
        //--------------------------------------------------
        out << "========================================================================================================================\n";
        out << "BIT PAIRS\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Total Pairs : " << totalPairs << "\n";
        out << "Total Bits  : " << state.bitPairs.size() << "\n";
        out << "Total Bytes : " << (state.bitPairs.size() + 7) / 8 << "\n";
        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            out << state.bitPairs << "\n";
        }

        return out.str();
    }

    inline std::string printMethod_10(bool printP) {
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "BIT PAIRS (HUFFMAN CANONICAL ENCODED)\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Size: " << state.bitPairs.size() / 2 << " symbols\n";
        out << "Size: " << state.bitPairsHuffman.packedBytes.size() << " Bytes\n";

        auto printHuffman = [&](const std::string &label, const HuffmanResults &r, size_t inputBits) {
            if (inputBits == 0)
                return;

            out << "[" << label << "]  ";
            out << "Input: " << inputBits << " bits  ";
            out << "Output: " << r.packedBytes.size() << " bytes  ";

            double pct = 100.0 * (1.0 - (double)r.outputBits / (double)inputBits);
            out << "Reduction: " << std::fixed << std::setprecision(2) << pct << "%\n";
        };

        auto bitPairLabel = [](uint8_t symbol) -> std::string {
            return std::string(1, (symbol & 0b10) ? '1' : '0') + ((symbol & 0b01) ? '1' : '0');
        };

        auto printTable = [&](const HuffmanResults &r) {
            out << "Huffman table:\n";

            // Count occurrences of each symbol from the original values fed into encoding
            size_t freq[4] = {0, 0, 0, 0};
            for (uint8_t b : r.inputBytes)
                freq[b]++;

            size_t total = r.inputBytes.size();

            std::vector<std::pair<uint8_t, std::string>> ordered(r.huffmanCode.begin(), r.huffmanCode.end());
            std::sort(ordered.begin(), ordered.end(), [&](const auto &a, const auto &b) {
                return freq[a.first] > freq[b.first]; // rank by frequency, most common first
            });

            int rank = 1;
            for (const auto &[symbol, code] : ordered) {
                double pct = total ? (100.0 * freq[symbol] / total) : 0.0;
                out << "  #" << rank << "  " << bitPairLabel(symbol) << "  count: " << std::setw(6) << freq[symbol] << "  (" << std::fixed << std::setprecision(2) << std::setw(5)
                    << pct << "%)"
                    << "  -> " << code << "\n";
                rank++;
            }
        };

        out << "------------------------------------------------------------------------------------------------------------------------\n";
        printHuffman("BitPairs Huffman Canonical", state.bitPairsHuffman, state.bitPairs.size());
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        printTable(state.bitPairsHuffman);

        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            out << "Encoded bits (" << state.bitPairsHuffman.finalBinaryPackage.size() << " bits):\n";
            out << state.bitPairsHuffman.finalBinaryPackage << "\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            out << "Standard bitPairs:  " << (state.bitPairs.size() / 2) << " symbols -> " << state.bitPairsHuffman.packedBytes.size() << " bytes\n";
        }

        return out.str();
    }

    inline std::string printMethod_11(bool printP) {
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "PRINT METHOD 11\n";

        return out.str();
    }

    inline std::string printMethod_12(bool printP) {
        std::ostringstream out;

        size_t count = state.hashPositions.size();

        out << "========================================================================================================================\n";
        out << "HASH COUNT\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Hashes: " << count << "\n";
        out << "Bytes:  " << count << "\n";
        out << "Bits:   " << count * 8 << "\n";

        return out.str();
    }

    inline std::string printMethod_13(bool printP) {
        std::ostringstream out;
        size_t count = state.hashPositions.size();

        out << "========================================================================================================================\n";
        out << "HASH POSITIONS\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Count: " << count << "\n";
        out << "Bytes: " << count * 4 << "\n";
        out << "Bits:  " << count * 32 << "\n";

        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            for (size_t i = 0; i < state.hashPositions.size(); ++i) {
                out << state.hashPositions[i];
                if (i + 1 < state.hashPositions.size())
                    out << ", ";
            }

            out << "\n";
        }

        return out.str();
    }

    inline std::string printMethod_14(bool printP) {
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "HASH GAPS\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        size_t count = state.hashGaps.size();
        out << "Count: " << count << "\n";
        out << "Bytes: " << count << "\n";
        out << "Bits:  " << count * 8 << "\n";

        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            for (size_t i = 0; i < state.hashGaps.size(); ++i) {
                out << static_cast<int>(state.hashGaps[i]);
                if (i + 1 < state.hashGaps.size())
                    out << ", ";
            }

            out << "\n";
        }

        return out.str();
    }

    inline std::string printMethod_15(bool printP) {
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "FINAL GAP STREAM\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Size: " << state.hashGapBits.size() << " bits\n";
        out << "Size: " << state.hashGapBits.size() / 8 << " Bytes\n";

        if (printP) {
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            out << state.hashGapBits << "\n";
        }

        return out.str();
    }

    inline std::string printMethod_16(bool printP) {
        std::ostringstream out;

        const auto &c = state.hashGapCanonical;

        size_t rawBits = state.hashGaps.size() * 8;

        out << "========================================================================================================================\n";
        out << "HASH GAP STREAM (HUFFMAN ENCODED)\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Input:  " << rawBits << " bits  (" << state.hashGaps.size() << " bytes)\n";
        out << "Output: " << c.outputBits << " bits  (" << c.outputBits / 8 << " bytes)\n";

        if (rawBits > 0) {
            double pct = 100.0 * (1.0 - (double)c.outputBits / (double)rawBits);
            out << "Reduction: " << std::fixed << std::setprecision(2) << pct << "%\n";
        }

        out << "------------------------------------------------------------------------------------------------------------------------\n";
        out << "Huffman table:\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";

        std::vector<std::pair<uint8_t, std::string>> ordered(c.huffmanCode.begin(), c.huffmanCode.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

        for (const auto &[byte, code] : ordered) {
            out << "  " << std::setw(3) << (int)byte << " -> " << code << "\n";
        }

        if (printP) {
            out << "\nEncoded bitstring:\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            out << c.finalBinaryPackage << "\n";
            out << "Bitstring size: " << c.finalBinaryPackage.size() << " bits  (" << c.finalBinaryPackage.size() / 8 << " bytes)\n";
        }

        return out.str();
    }

    inline std::string printMethod_17(bool printP) {
        std::ostringstream out;

        out << "========================================================================================================================\n";
        out << "PRINT METHOD 17 \n";

        return out.str();
    }

    inline std::string printMethod_18(bool printP) {
        std::ostringstream out;

        const auto &c = state.bitPairsHuffman;

        size_t rawBits = state.bitPairs.size(); // already 2 bits/symbol, no ×8 needed

        out << "========================================================================================================================\n";
        out << "BITPAIRS STREAM (HUFFMAN CANONICAL ENCODED)\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";

        out << "Input:  " << rawBits << " bits  (" << rawBits / 8 << " bytes, " << (rawBits / 2) << " symbols)\n";
        out << "Output: " << c.outputBits << " bits  (" << c.outputBits / 8 << " bytes)\n";

        if (rawBits > 0) {
            double pct = 100.0 * (1.0 - (double)c.outputBits / (double)rawBits);
            out << "Reduction: " << std::fixed << std::setprecision(2) << pct << "%\n";
        }

        if (printP) {
            out << "\nHuffman table:\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";

            std::vector<std::pair<uint8_t, std::string>> ordered(c.huffmanCode.begin(), c.huffmanCode.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

            for (const auto &[byte, code] : ordered) {
                out << "  " << std::setw(3) << (int)byte << " -> " << code << "\n";
            }

            out << "\nEncoded bitstring:\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            out << c.finalBinaryPackage << "\n";
            out << "\nBitstring size: " << c.finalBinaryPackage.size() << " bits  (" << c.finalBinaryPackage.size() / 8 << " bytes)\n";
        }

        return out.str();
    }

    inline void printNumberList(const std::vector<uint16_t> &v) {
        for (size_t i = 0; i < v.size(); i++) {
            std::cout << v[i];

            if (i + 1 < v.size()) {
                std::cout << ",";
            }
        }

        std::cout << "\n";
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Layered Compression
//----------------------------------------------------------------------------------

class OrderFunctions {
  public:
    // Encodes: [first position as a raw byte][gap, gap, gap, ...]
    // The first element is NOT a gap — it's the absolute starting position,
    // needed because gaps alone only encode relative distances.
    std::vector<uint8_t> positionsToGaps(const std::vector<uint32_t> &positions) {
        std::vector<uint8_t> gaps;
        if (positions.empty())
            return gaps;

        gaps.reserve(positions.size());
        gaps.push_back(static_cast<uint8_t>(positions[0]));

        uint32_t prev = positions[0];
        for (size_t i = 1; i < positions.size(); i++) {
            uint32_t gap = positions[i] - prev;
            gaps.push_back(static_cast<uint8_t>(gap));
            prev = positions[i];
        }
        return gaps;
    }

    // Inverse of positionsToGaps: first element is the absolute starting point
    // every element after is a gap from the previous position.
    std::vector<uint32_t> gapsToPositions(const std::vector<uint8_t> &gaps) {
        std::vector<uint32_t> positions;
        if (gaps.empty())
            return positions;

        positions.reserve(gaps.size());
        positions.push_back(gaps[0]);

        uint32_t prev = gaps[0];
        for (size_t i = 1; i < gaps.size(); i++) {
            uint32_t next = prev + gaps[i];
            positions.push_back(next);
            prev = next;
        }
        return positions;
    }

    std::string bytesToBits(const std::vector<uint8_t> &data) {
        std::string bits;
        bits.reserve(data.size() * 8);
        for (uint8_t b : data) {
            for (int i = 7; i >= 0; --i) {
                bits.push_back((b & (1 << i)) ? '1' : '0');
            }
        }
        return bits;
    }

    // Inverse of bitPairsToValues: turns each 2-bit value back into its
    // original "00"/"01"/"10"/"11" pair.
    std::string valuesToBitPairs(const std::vector<uint8_t> &values) {
        std::string bits;
        bits.reserve(values.size() * 2);
        for (uint8_t v : values) {
            bits += (v & 0b10) ? '1' : '0';
            bits += (v & 0b01) ? '1' : '0';
        }
        return bits;
    }

    std::vector<uint8_t> bitPairsToValues(const std::string &bitPairs) {
        std::vector<uint8_t> values;
        values.reserve(bitPairs.size() / 2);

        for (size_t i = 0; i + 1 < bitPairs.size(); i += 2) {
            uint8_t v = 0;
            v |= (bitPairs[i] == '1') ? 0b10 : 0;
            v |= (bitPairs[i + 1] == '1') ? 0b01 : 0;
            values.push_back(v);
        }

        return values;
    }

    inline std::string toBinary(size_t value, size_t bits) {
        std::string out(bits, '0');
        for (size_t i = 0; i < bits; i++)
            if (value & (1ULL << (bits - 1 - i)))
                out[i] = '1';
        return out;
    }

    inline size_t fromBinary(const std::string &bits) {
        size_t value = 0;
        for (char c : bits) {
            value <<= 1;
            if (c == '1')
                value |= 1;
        }
        return value;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Layered Compression
//----------------------------------------------------------------------------------

class BlockData {
  public:
    // clang-format off
    void runEncodeState(const std::string &dBits, CompressionState &state) {
        encodeState(dBits, state);
    }

    std::string runDecodeState(CompressionState &state) {
        return decodeState(state);
    }
    // clang-format on

  private:
    static constexpr const char *CLASS_NAME = "BlockData";
    // Tied to BlockData's own CHUNK_SIZE (2049) so there's one source of truth.
    // Every 683 symbols (body pairs + footer hashes combined), a new header appears.
    static constexpr size_t SYMBOLS_PER_BLOCK = 683;

    void encodeState(const std::string &dBits, CompressionState &state) {
        state.blockStates.clear();
        state.blockPadPairs.clear();
        state.bitPairs.clear();
        state.hashPositions.clear();

        uint32_t symbolPosition = 0;

        for (size_t i = 0; i < dBits.size();) {
            if (isBlockHeader(dBits, i)) {
                state.blockStates += dBits[i];
                state.blockPadPairs += dBits.substr(i + 2, 2); // the "00"/"01"/"10" pad pair
                i += 4;
                continue;
            } else if (dBits[i] == '#') {
                state.hashPositions.push_back(symbolPosition);
                symbolPosition++;
                i++;
                continue;
            } else if (i + 1 < dBits.size()) {
                std::string pair;
                pair += dBits[i];
                pair += dBits[i + 1];
                state.bitPairs += pair;
                symbolPosition++;
                i += 2;
                continue;
            } else {
                i++;
            }
        }
    }

    // Inverse of encodeState. Header placement is regenerated from
    // SYMBOLS_PER_BLOCK instead of looked up from stored positions.
    std::string decodeState(CompressionState &state) {
        size_t totalSymbols = state.hashPositions.size() + state.bitPairs.size() / 2;
        std::unordered_set<uint32_t> hashPosSet(state.hashPositions.begin(), state.hashPositions.end());

        std::string dBits;
        dBits.reserve(state.bitPairs.size() + state.hashPositions.size() + state.blockStates.size() * 4);

        size_t bitPairIdx    = 0;
        size_t blockStateIdx = 0;

        auto emitHeader = [&]() {
            dBits += state.blockStates[blockStateIdx];
            dBits += ':';
            dBits += state.blockPadPairs.substr(blockStateIdx * 2, 2);
            blockStateIdx++;
        };

        for (uint32_t pos = 0; pos < totalSymbols; pos++) {
            if (pos % SYMBOLS_PER_BLOCK == 0 && blockStateIdx < state.blockStates.size()) {
                emitHeader();
            }

            if (hashPosSet.count(pos)) {
                dBits += '#';
            } else {
                dBits += state.bitPairs.substr(bitPairIdx, 2);
                bitPairIdx += 2;
            }
        }

        // Edge case: a trailing block whose body ended up empty (final chunk
        // landed exactly on a SYMBOLS_PER_BLOCK boundary) still needs its header emitted.
        if (blockStateIdx < state.blockStates.size()) {
            emitHeader();
        }

        return dBits;
    }

    inline bool isBlockHeader(const std::string &text, size_t i) { return i + 3 < text.size() && (text[i] == '0' || text[i] == '1') && text[i + 1] == ':'; }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Layered Compression
//----------------------------------------------------------------------------------

class VariableWidthHeader {
  public:
    enum class WidthTag : uint8_t { W8 = 0, W16 = 1, W32 = 2, W64 = 3 };

    static WidthTag selectWidth(uint64_t value) {
        if (value <= std::numeric_limits<uint8_t>::max())
            return WidthTag::W8;
        if (value <= std::numeric_limits<uint16_t>::max())
            return WidthTag::W16;
        if (value <= std::numeric_limits<uint32_t>::max())
            return WidthTag::W32;
        return WidthTag::W64;
    }

    static uint8_t widthBits(WidthTag tag) {
        switch (tag) {
        case WidthTag::W8:
            return 8;
        case WidthTag::W16:
            return 16;
        case WidthTag::W32:
            return 32;
        case WidthTag::W64:
            return 64;
        }
        return 64;
    }

    static std::string tagToBits(WidthTag tag) {
        switch (tag) {
        case WidthTag::W8:
            return "00";
        case WidthTag::W16:
            return "01";
        case WidthTag::W32:
            return "10";
        case WidthTag::W64:
            return "11";
        }
        return "00";
    }

    static WidthTag bitsToTag(const std::string &pair) {
        if (pair == "00")
            return WidthTag::W8;
        if (pair == "01")
            return WidthTag::W16;
        if (pair == "10")
            return WidthTag::W32;
        return WidthTag::W64;
    }

    static std::string encode(uint64_t value) {
        WidthTag tag    = selectWidth(value);
        std::string out = tagToBits(tag);
        uint8_t bits    = widthBits(tag);
        for (int i = bits - 1; i >= 0; i--)
            out += ((value >> i) & 1) ? '1' : '0';
        return out;
    }

    // Bounds-checked. Returns false (leaves pos/outValue untouched) instead of
    // reading past the end of `bits` on truncated/corrupted input.
    static bool decode(const std::string &bits, size_t &pos, uint64_t &outValue) {
        if (pos + 2 > bits.size())
            return false;

        WidthTag tag  = bitsToTag(bits.substr(pos, 2));
        uint8_t width = widthBits(tag);

        if (pos + 2 + width > bits.size())
            return false;

        size_t valuePos = pos + 2;
        uint64_t value  = 0;
        for (uint8_t i = 0; i < width; i++)
            value = (value << 1) | (bits[valuePos + i] - '0');

        pos      = valuePos + width;
        outValue = value;
        return true;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Layered Compression
//----------------------------------------------------------------------------------

class OrderHeader {
  public:
    void runHeaderEncoding(CompressionState &state) { packHeader(state); }

    void runHeaderDecoding(CompressionState &state, const std::string &packedHeader) { unpackHeader(packedHeader, state.blockStates); }

  private:
    // headerPacked *is* blockStates now — nothing to length-prefix,
    // nothing to strip on the way back out.
    void packHeader(CompressionState &state) { state.headerPacked = state.blockStates; }

    void unpackHeader(const std::string &packed, std::string &blockStatesOut) { blockStatesOut = packed; }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Layered Compression
//----------------------------------------------------------------------------------

class OrderBody {
  public:
    // Picks the smaller of RAW / HUFFMAN_FIXED. No RUNC here — that's a
    // whole-layer decision made by RecursiveEncoding/LayeredCompression,
    // not something OrderBody should trigger on every call.
    void runBodyEncoding(CompressionState &state) {
        std::string rawCandidate = "00" + state.bitPairs;

        HuffmanResults huffResult = buildHuffmanFixed(state);
        std::string huffCandidate = "01" + huffResult.finalBinaryPackage;

        bool huffmanWins = huffCandidate.size() < rawCandidate.size();

        LOG_INFO(std::string("OrderBody: selected ") + (huffmanWins ? "HUFFMAN_FIXED" : "RAW") + " (" + std::to_string(huffmanWins ? huffCandidate.size() : rawCandidate.size())
                 + " bits, raw=" + std::to_string(rawCandidate.size()) + " huffman=" + std::to_string(huffCandidate.size()) + ")");

        huffResult.finalBinaryPackage = huffCandidate;
        huffResult.outputBits         = huffCandidate.size();
        state.bitPairsHuffman         = huffResult;

        state.bodyPacked = huffmanWins ? huffCandidate : rawCandidate;
    }

    void runBodyDecoding(CompressionState &state, const std::string &encodedBody) {
        if (encodedBody.size() < 2) {
            state.bitPairs = "";
            return;
        }

        std::string tag     = encodedBody.substr(0, 2);
        std::string payload = encodedBody.substr(2);

        if (tag == "00") {
            state.bitPairs = payload;
        } else if (tag == "01") {
            std::vector<uint8_t> values = huffmanEncoding.decodeWithFixedCode(payload);
            state.bitPairs              = orderFunctions.valuesToBitPairs(values);
        } else {
            LOG_INFO("OrderBody::runBodyDecoding — unknown tag, cannot decode");
            state.bitPairs = "";
        }
    }

  private:
    static constexpr const char *CLASS_NAME = "OrderBody";
    OrderFunctions orderFunctions;
    HuffmanEncoding huffmanEncoding;

    HuffmanResults buildHuffmanFixed(CompressionState &state) {
        std::vector<uint8_t> values = orderFunctions.bitPairsToValues(state.bitPairs);

        size_t freq[4] = {0, 0, 0, 0};
        for (uint8_t v : values)
            freq[v]++;

        std::vector<std::pair<uint8_t, size_t>> ranked = {{0b00, freq[0b00]}, {0b01, freq[0b01]}, {0b10, freq[0b10]}, {0b11, freq[0b11]}};
        std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

        static const std::array<std::string, 4> codeShape = {"0", "10", "110", "111"};
        std::unordered_map<uint8_t, std::string> fixedCode;
        std::vector<uint8_t> symbolOrder;
        for (size_t i = 0; i < 4; i++) {
            fixedCode[ranked[i].first] = codeShape[i];
            symbolOrder.push_back(ranked[i].first);
        }

        HuffmanResults result = huffmanEncoding.runWithFixedCode(values, fixedCode, symbolOrder);
        result.selectedCodec  = "BITPAIRS_HUFFMAN_FIXED";
        result.inputBits      = state.bitPairs.size();
        result.outputBits     = result.finalBinaryPackage.size();
        return result;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Layered Compression
//----------------------------------------------------------------------------------

// Footer:    (8152 bits):

class OrderFooter {
  public:
    void runFooterEncoding(CompressionState &state) {
        createHashGaps(state);
        hashGapsHuffman(state);
    }

    void runFooterDecoding(CompressionState &state, const std::string &footerBits) {
        if (footerBits.empty()) {
            return;
        }

        state.hashGaps      = huffmanEncoding.decodeCanonical(footerBits);
        state.hashPositions = orderFunctions.gapsToPositions(state.hashGaps);
    }

  private:
    static constexpr const char *CLASS_NAME = "OrderFooter";

    OrderFunctions orderFunctions;
    HuffmanEncoding huffmanEncoding;

    // HASH GAPS
    void createHashGaps(CompressionState &state) {
        state.hashGaps    = orderFunctions.positionsToGaps(state.hashPositions);
        state.hashGapBits = orderFunctions.bytesToBits(state.hashGaps);
    }

    // state.run = encodeRuns(state.hashGapBits);

    // HUFFMAN
    void hashGapsHuffman(CompressionState &state) {
        HuffmanResults canonical = huffmanEncoding.runCanonical(state.hashGaps);
        canonical.selectedCodec  = "HASH_HUFFMAN";

        canonical.inputBits  = state.hashGaps.size() * 8;
        canonical.outputBits = canonical.finalBinaryPackage.size();

        state.hashGapCanonical = canonical;
        state.footerPacked     = canonical.finalBinaryPackage;

        printCanonicalStats(canonical, "HASH GAPS (HUFFMAN CANONICAL ENCODED)");
    }

    void printCanonicalStats(const HuffmanResults &c, const std::string &label) {
        std::cout << "========================================================================================================================\n";
        std::cout << label << "\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "Input   : " << c.inputBits << " bits (" << c.inputBits / 8 << " bytes)\n";
        std::cout << "Output  : " << c.outputBits << " bits (" << c.outputBits / 8 << " bytes)\n";

        if (c.inputBits != 0) {
            double reduction = 100.0 * (1.0 - (double)c.outputBits / c.inputBits);
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "Reduction: " << reduction << "%\n";
        }

        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "\nHuffman Code Table\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << std::left << std::setw(8) << "Value" << std::setw(12) << "Code Len" << "Code\n";

        for (const auto &[value, code] : c.huffmanCode) {
            std::cout << std::left << std::setw(8) << (int)value << std::setw(12) << code.size() << code << "\n";
        }

        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "\nFinal bitstream:\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << c.finalBinaryPackage << "\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Layered Compression
//----------------------------------------------------------------------------------

struct LayerSections {
    std::string header;
    std::string body;
    std::string footer;
    bool ok = false;
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Layered Compression
//----------------------------------------------------------------------------------

class RecursiveEncoding {
  public:
    std::string encode(const std::string &dBits, CompressionState &state, size_t inputBits) {
        blockData.runEncodeState(dBits, state);

        orderHeader.runHeaderEncoding(state);
        orderBody.runBodyEncoding(state);
        orderFooter.runFooterEncoding(state);

        static constexpr size_t BODY_META_BITS = 10; // 2-bit tag + 8-bit rank table

        std::string bodyMeta = state.bodyPacked.substr(0, BODY_META_BITS);
        std::string bodyData = state.bodyPacked.substr(BODY_META_BITS);
        LayerSections sections;
        sections.header = state.headerPacked;
        sections.body   = bodyMeta + "0" + bodyData; // "0" = not nested
        sections.footer = state.footerPacked;
        sections.ok     = true;

        std::string result = serializeSections(sections, state);

        if (result.size() >= inputBits) {
            std::string nested = tryNestedBodyEncode(bodyMeta, bodyData, sections, state);
            if (!nested.empty()) {
                result = nested;
                std::cout << "\nNested Encoding applied!\n";
            }
        }

        return result;
    }

    std::string decode(const std::string &encodedBits, CompressionState &state) {
        if (encodedBits.empty()) {
            LOG_INFO("RecursiveEncoding::decode — empty input, aborting");
            return "";
        }

        LayerSections outerSections = deserializeSections(encodedBits);
        if (!outerSections.ok) {
            LOG_INFO("RecursiveEncoding::decode — failed to parse outer sections, aborting");
            return "";
        }

        orderHeader.runHeaderDecoding(state, outerSections.header);
        orderFooter.runFooterDecoding(state, outerSections.footer);

        if (outerSections.body.size() < BODY_META_BITS + 1) {
            LOG_INFO("RecursiveEncoding::decode — body too small for meta+flag, aborting");
            return "";
        }

        std::string bodyMeta = outerSections.body.substr(0, BODY_META_BITS);
        char nestedFlag      = outerSections.body[BODY_META_BITS];
        std::string payload  = outerSections.body.substr(BODY_META_BITS + 1);

        std::string bodyData;
        if (nestedFlag == '0') {
            bodyData = payload; // not nested — this already is the real body data
        } else {
            CompressionState innerState;
            std::string innerDBits = decode(payload, innerState); // recursive — same function, one layer deeper
            if (innerDBits.empty()) {
                LOG_INFO("RecursiveEncoding::decode — nested layer decode failed, aborting");
                return "";
            }
            bodyData = operations.runD(innerDBits); // reverse the OP_C pass that ran before nesting
        }

        orderBody.runBodyDecoding(state, bodyMeta + bodyData);

        return blockData.runDecodeState(state);
    }

    LayerSections deserializeSections(const std::string &encodedBits) {
        LayerSections sections;

        size_t pos = 0;
        uint64_t p0, p1;

        if (!VariableWidthHeader::decode(encodedBits, pos, p0)) {
            LOG_INFO("RecursiveEncoding::deserializeSections — failed to decode p0 prefix");
            return sections; // ok stays false
        }
        if (!VariableWidthHeader::decode(encodedBits, pos, p1)) {
            LOG_INFO("RecursiveEncoding::deserializeSections — failed to decode p1 prefix");
            return sections; // ok stays false
        }

        std::string rest = encodedBits.substr(pos);

        if (p0 > rest.size() || p1 > rest.size() || p0 > p1) {
            return sections; // ok stays false
        }

        sections.header = rest.substr(0, p0);
        sections.body   = rest.substr(p0, p1 - p0);
        sections.footer = rest.substr(p1);
        sections.ok     = true;

        return sections;
    }

  private:
    static constexpr const char *CLASS_NAME = "RecursiveEncoding";
    static constexpr size_t BODY_META_BITS  = 10;

    BlockData blockData;
    OrderHeader orderHeader;
    OrderBody orderBody;
    OrderFooter orderFooter;
    OrderFunctions orderFunctions;
    HuffmanEncoding huffmanEncoding;
    Operations operations;

    std::string serializeSections(const LayerSections &s, CompressionState &state) {
        size_t p0 = s.header.size();
        size_t p1 = p0 + s.body.size();

        std::string p0Prefix = VariableWidthHeader::encode(p0);
        std::string p1Prefix = VariableWidthHeader::encode(p1);

        state.p0 = p0;
        state.p1 = p1;
        state.p2 = p0Prefix.size() + p1Prefix.size(); // actual bits spent on the two length prefixes

        std::string out;
        out.reserve(state.p2 + p1 + s.footer.size());
        out += p0Prefix;
        out += p1Prefix;
        out += s.header;
        out += s.body;
        out += s.footer;
        return out;
    }

    std::string tryNestedBodyEncode(const std::string &bodyMeta, const std::string &bodyData, const LayerSections &outerSections, CompressionState &state) {
        std::string bodyDBits = operations.runC(bodyData); // pure data only — no meta re-entering OP_C

        CompressionState innerState;
        blockData.runEncodeState(bodyDBits, innerState);

        orderHeader.runHeaderEncoding(innerState);
        orderBody.runBodyEncoding(innerState);
        orderFooter.runFooterEncoding(innerState);

        LayerSections innerSections;
        innerSections.header = innerState.headerPacked;
        innerSections.body   = innerState.bodyPacked;
        innerSections.footer = innerState.footerPacked;
        innerSections.ok     = true;

        std::string nestedBody = serializeSections(innerSections, innerState);

        if (nestedBody.size() + 1 + BODY_META_BITS >= outerSections.body.size()) {
            std::cout << "\nNesting was not applied!\n";
            std::cout << "Header:             " << innerSections.header.size() << "\n";
            std::cout << "Body:               " << innerSections.body.size() << "\n";
            std::cout << "Footer:             " << innerSections.footer.size() << "\n";
            std::cout << "Total Nesting size: " << nestedBody.size() << "\n";
            std::cout << "Original size:      " << outerSections.body.size() << "\n\n";
            std::cout << "Difference:         " << outerSections.body.size() - nestedBody.size() << "\n\n";
            return ""; // +1 accounts for the tag bit we're about to add — no real gain
        }

        std::cout << "\nNesting applied!\n";
        std::cout << "Header:             " << innerSections.header.size() << "\n";
        std::cout << "Body:               " << innerSections.body.size() << "\n";
        std::cout << "Footer:             " << innerSections.footer.size() << "\n";
        std::cout << "Total Nesting size: " << nestedBody.size() << "\n";
        std::cout << "Original size:      " << outerSections.body.size() << "\n\n";
        std::cout << "Difference:         " << outerSections.body.size() - nestedBody.size() << "\n\n";

        LayerSections finalSections;
        finalSections.header = outerSections.header;
        finalSections.body   = bodyMeta + "1" + nestedBody; // "1" = nested
        finalSections.footer = outerSections.footer;
        finalSections.ok     = true;

        return serializeSections(finalSections, state);
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Layered Compression
//----------------------------------------------------------------------------------

class LayeredCompression {
  public:
    explicit LayeredCompression(CompressionState &state)
        : state(state) {}

    // LayeredCompression.h
    void runByteEncoding(const std::string &eBits, size_t targetBits, uint8_t byteVal, size_t maxLayers, bool printC) {
        state.resetForRun(eBits, targetBits);
        byteValue = byteVal;

        // The pad-count prefix lives inside the targetBits budget, not on top of it —
        // so the real content budget is targetBits minus the prefix width.
        size_t contentBudget = targetBits - PAD_PREFIX_BITS;

        std::string current = eBits;
        size_t layerIndex   = 1;

        while (current.size() > contentBudget && (maxLayers == 0 || layerIndex <= maxLayers)) {
            std::string dBits       = operations.runC(current);
            std::string encodedBits = recursiveEncoding.encode(dBits, state, current.size());

            Layer layer;
            layer.index      = layerIndex++;
            layer.inputBits  = current.size();
            layer.outputBits = encodedBits.size();

            if (printC) {
                printLayer(layer, static_cast<int>(layer.index), encodedBits, printC);
                std::cerr << "-- press Enter for next layer --";
                std::cin.get();
            }

            if (layer.outputBits >= layer.inputBits) {
                break; // no gain this layer — stop, don't keep it
            }

            state.layers.push_back(layer);
            current = encodedBits;
        }

        state.fitsTarget = current.size() <= contentBudget;
        if (state.fitsTarget) {
            state.zeroPadding = contentBudget - current.size();

            if (state.zeroPadding > MAX_PAD_VALUE) {
                // Shouldn't happen in practice — contentBudget and current.size() are
                // both far smaller than this in real use — but an 8-bit prefix simply
                // cannot represent more than 255, so guard against silent corruption.
                LOG_INFO("runEncoding — zeroPadding (" + std::to_string(state.zeroPadding) + ") exceeds 8-bit prefix capacity, aborting fit");
                state.fitsTarget    = false;
                state.finalizedBits = current;
            } else {
                state.finalizedBits = encodePadCount(state.zeroPadding) + current + std::string(state.zeroPadding, '0');
            }
        } else {
            state.finalizedBits = current;
        }

        if (printC) {
            printSummary();
        }
    }

    void runEncoding(const std::string &eBits, size_t targetBits, size_t maxLayers, bool printC) {
        state.resetForRun(eBits, targetBits);

        // The pad-count prefix lives inside the targetBits budget, not on top of it —
        // so the real content budget is targetBits minus the prefix width.
        size_t contentBudget = targetBits - PAD_PREFIX_BITS;

        std::string current = eBits;
        size_t layerIndex   = 1;

        while (current.size() > contentBudget && state.layers.size() < maxLayers) {
            std::string dBits       = operations.runC(current);
            std::string encodedBits = recursiveEncoding.encode(dBits, state, current.size());

            Layer layer;
            layer.index      = layerIndex++;
            layer.inputBits  = current.size();
            layer.outputBits = encodedBits.size();

            if (printC) {
                printLayer(layer, static_cast<int>(layer.index), encodedBits, printC);
            }

            if (layer.outputBits >= layer.inputBits) {
                break; // no gain this layer — stop, don't keep it
            }

            state.layers.push_back(layer);
            current = encodedBits;
        }

        state.fitsTarget = current.size() <= contentBudget;
        if (state.fitsTarget) {
            state.zeroPadding = contentBudget - current.size();

            if (state.zeroPadding > MAX_PAD_VALUE) {
                // Shouldn't happen in practice — contentBudget and current.size() are
                // both far smaller than this in real use — but an 8-bit prefix simply
                // cannot represent more than 255, so guard against silent corruption.
                LOG_INFO("runEncoding — zeroPadding (" + std::to_string(state.zeroPadding) + ") exceeds 8-bit prefix capacity, aborting fit");
                state.fitsTarget    = false;
                state.finalizedBits = current;
            } else {
                state.finalizedBits = encodePadCount(state.zeroPadding) + current + std::string(state.zeroPadding, '0');
            }
        } else {
            state.finalizedBits = current;
        }

        if (printC) {
            printSummary();
        }
    }

    std::string runDecoding(const std::string &payloadBits, size_t layerCount) {
        std::string current = stripPadding(payloadBits);
        if (current.empty() && !payloadBits.empty()) {
            LOG_INFO("runDecoding — padding strip failed, aborting");
            return "";
        }

        for (size_t i = 0; i < layerCount; ++i) {
            std::string dBits = recursiveEncoding.decode(current, state);
            if (dBits.empty()) {
                LOG_INFO("runDecoding — layer " + std::to_string(layerCount - i) + " failed, aborting");
                return "";
            }
            current = operations.runD(dBits);
        }

        return current;
    }

  private:
    static constexpr const char *CLASS_NAME = "LayeredCompression";
    static constexpr size_t PAD_PREFIX_BITS = 8;   // width of the leading pad-count prefix in finalizedBits
    static constexpr size_t MAX_PAD_VALUE   = 255; // (1 << PAD_PREFIX_BITS) - 1

    CompressionState &state;
    Operations operations;
    RecursiveEncoding recursiveEncoding;
    OrderFunctions orderFunctions;
    uint8_t byteValue = 0;

    std::string encodePadCount(size_t value) {
        std::string bits(PAD_PREFIX_BITS, '0');
        for (int i = static_cast<int>(PAD_PREFIX_BITS) - 1; i >= 0; --i) {
            bits[i] = (value & 1) ? '1' : '0';
            value >>= 1;
        }
        return bits;
    }

    size_t decodePadCount(const std::string &prefixBits) {
        size_t value = 0;
        for (char c : prefixBits)
            value = (value << 1) | (c == '1' ? 1 : 0);
        return value;
    }

    // Inverse of the finalizedBits assembly in runEncoding: strips the leading
    // 8-bit pad-count prefix, then strips that many trailing zero bits.
    std::string stripPadding(const std::string &finalizedBits) {
        if (finalizedBits.size() < PAD_PREFIX_BITS) {
            LOG_INFO("stripPadding — payload smaller than pad-count prefix, aborting");
            return "";
        }

        size_t zeroPadding = decodePadCount(finalizedBits.substr(0, PAD_PREFIX_BITS));
        std::string body   = finalizedBits.substr(PAD_PREFIX_BITS);

        if (zeroPadding > body.size()) {
            LOG_INFO("stripPadding — zeroPadding exceeds payload size, aborting");
            return "";
        }

        return body.substr(0, body.size() - zeroPadding);
    }

    double calculateScore(double bitsPerHashActual, double bitsPerHash) { return bitsPerHashActual - bitsPerHash; }

    // PRINT LAYER
    void printLayer(const Layer &layer, int layerIndex, const std::string &encodedBits, bool printP) {
        // ── printer methods ───────────────────────────────────
        Printer printer(state);
        if (layerIndex < 2) {
            std::cout << "========================================================================================================================\n";
            std::cout << "ENCODING FIRST LAYER\n";
            printer.print(Printer::METHOD_1, printP);
            printer.print(Printer::METHOD_2, printP);
            printer.print(Printer::METHOD_3, printP);
            printer.print(Printer::METHOD_4, printP);
            printer.print(Printer::METHOD_5, printP);
        }
        std::cout << "========================================================================================================================\n";
        std::cout << "ROUND " << layerIndex << "\n";

        printer.print(Printer::METHOD_6, printP);
        printer.print(Printer::METHOD_7, printP);
        printer.print(Printer::METHOD_8, printP);
        printer.print(Printer::METHOD_9, printP);
        printer.print(Printer::METHOD_10, printP);
        printer.print(Printer::METHOD_11, printP);
        printer.print(Printer::METHOD_12, printP);
        printer.print(Printer::METHOD_13, printP);
        printer.print(Printer::METHOD_14, printP);
        printer.print(Printer::METHOD_15, printP);
        printer.print(Printer::METHOD_16, printP);
        printer.print(Printer::METHOD_17, printP);
        printer.print(Printer::METHOD_18, printP);

        // ── section breakdown (live data from this layer's encode pass) ──────
        std::cout << "========================================================================================================================\n";
        std::cout << "RECURSIVE ENCODING OUTPUT\n";
        std::cout << "========================================================================================================================\n";
        std::cout << "p0 (header end offset): " << state.p0 << "\n";
        std::cout << "p1 (body end offset):   " << state.p1 << "\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "Header: (" << state.headerPacked.size() << " bits):\n";
        std::cout << "Block states: " << state.blocks.size() << "\n";
        std::cout << "  " << state.blockStates << "\n";
        if (printP) {
            std::cout << state.headerPacked << "\n";
        }
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "Body: (" << state.bodyPacked.size() << " bits):\n";
        if (printP) {
            std::cout << state.bodyPacked << "\n";
        }
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "Footer:    (" << state.footerPacked.size() << " bits):\n";
        if (printP) {
            std::cout << state.footerPacked << "\n";
        }
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        size_t totalBits    = state.headerPacked.size() + state.bodyPacked.size() + state.footerPacked.size();
        size_t totalBytes   = totalBits / 8;
        auto tag0           = VariableWidthHeader::selectWidth(state.p0);
        auto tag1           = VariableWidthHeader::selectWidth(state.p1);
        size_t hashCount    = state.hashPositions.size();
        size_t expectedBits = hashCount * 8;
        size_t actualBits   = state.footerPacked.size();

        std::vector<uint8_t> values = orderFunctions.bitPairsToValues(state.bitPairs);
        size_t freq[4]              = {0, 0, 0, 0};
        for (uint8_t v : values) {
            freq[v]++;
        }

        size_t total = values.size();
        std::cout << "Bit Pairs:\n";
        static const char *labels[4] = {"00", "01", "10", "11"};
        for (int i = 0; i < 4; i++) {
            double pct = total ? (100.0 * freq[i] / total) : 0.0;
            std::cout << labels[i] << ": " << freq[i] << " (" << pct << "%)\n";
        }

        long diff                = static_cast<long>(actualBits) - static_cast<long>(expectedBits);
        double pct               = expectedBits ? (100.0 * diff / expectedBits) : 0.0;
        double bitsPerHashActual = hashCount ? (static_cast<double>(actualBits) / hashCount) : 0.0;
        double bitsPerHash       = hashCount ? (static_cast<double>(state.bodyPacked.size() + state.blockStates.size()) / hashCount) : 0.0;
        double score             = calculateScore(bitsPerHashActual, bitsPerHash);
        size_t lengthPrefix      = state.p2;
        size_t blockStateBits    = state.blockStates.size();
        size_t metadataBits      = lengthPrefix + blockStateBits;
        long compressionOverhead = static_cast<long>(layer.inputBits) - static_cast<long>(layer.outputBits) - static_cast<long>(metadataBits);

        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "Hashes: " << hashCount << " (" << expectedBits << " expected bits)\n";
        std::cout << "Actual: " << actualBits << " bits (" << (diff >= 0 ? "+" : "") << diff << ", " << pct << "%)\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "Ratio of Actual bits to hashes: " << bitsPerHashActual << "\n";
        std::cout << "Ratio of Hashes to Bit Pairs:   " << bitsPerHash << "\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        if (score < 0.0) {
            std::cout << "PASS\n";
        } else if (score == 0.0) {
            std::cout << "BORDERLINE\n";
        } else {
            std::cout << "FAIL\n";
        }
        std::cout << "Score = " << score << '\n';
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "Metadata bits:        " << metadataBits << '\n';
        std::cout << "Compression overhead: " << compressionOverhead << '\n';
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "p0 prefix width: " << (2 + VariableWidthHeader::widthBits(tag0)) << " bits (2 tag + " << VariableWidthHeader::widthBits(tag0) << " value)\n";
        std::cout << "p1 prefix width: " << (2 + VariableWidthHeader::widthBits(tag1)) << " bits (2 tag + " << VariableWidthHeader::widthBits(tag1) << " value)\n";
        std::cout << "Length-prefix overhead: " << state.p2 << " bits\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "Total: " << totalBits << " bits / " << totalBytes << " bytes\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";

        if (layer.outputBits >= layer.inputBits) {
            size_t diff = layer.outputBits - layer.inputBits;
            std::cout << "Layer " << layer.index << "REJECTED! (no compression gain) — " << diff << " bits over input\n";
            std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        } else {
            size_t diff = layer.inputBits - layer.outputBits;
            std::cout << "Layer " << layer.index << " PASSED! — " << diff << " bits under input\n";
            std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        }
    }

    void printSummary() {
        if (state.layers.empty())
            return;

        const Layer &last         = state.layers.back();
        double compressionPercent = last.inputBits != 0 ? (1.0 - static_cast<double>(last.outputBits) / static_cast<double>(last.inputBits)) * 100.0 : 0.0;

        std::cout << "========================================================================================================================\n";
        std::cout << "ENCODING SCHEME DEBUG\n";
        std::cout << "========================================================================================================================\n";
        std::cout << "Original size:   " << last.inputBits << " bits\n";
        std::cout << "Compressed size: " << last.outputBits << " bits\n";
        std::cout << "Compression:     " << std::fixed << std::setprecision(2) << compressionPercent << "%\n";
        std::cout << "Layers kept:     " << state.layers.size() << "\n";
        std::cout << "========================================================================================================================\n";
        std::cout << "LAYER OPERATIONS\n";
        std::cout << "========================================================================================================================\n";

        for (const auto &layer : state.layers) {
            double pct = layer.inputBits != 0 ? (1.0 - static_cast<double>(layer.outputBits) / static_cast<double>(layer.inputBits)) * 100.0 : 0.0;
            std::cout << "Layer " << layer.index << ": " << layer.inputBits << " -> " << layer.outputBits << " bits (" << std::fixed << std::setprecision(2) << pct << "%)\n";
        }

        std::cout << "========================================================================================================================\n";
        std::cout << "FINALIZATION\n";
        std::cout << "========================================================================================================================\n";
        std::cout << "Target bits:  " << state.targetBits << "\n";

        if (state.fitsTarget) {
            std::cout << "Fits target:  YES\n";
            std::cout << "Zero padding: " << state.zeroPadding << "\n";
            std::cout << "========================================================================================================================\n";
            std::cout << "COMPRESSION COMPLETE: BYTE " + std::to_string(static_cast<uint32_t>(byteValue)) + " COMPRESSED TO BELOW 512 BITS\n";
            std::cout << "========================================================================================================================\n";
        } else {
            std::cout << "Fits target:  NO\n";
            std::cout << "Final size:   " << state.finalizedBits.size() << " bits\n";
            std::cout << "========================================================================================================================\n";
            std::cout << "COMPRESSION FAILED: BYTE " + std::to_string(static_cast<uint32_t>(byteValue)) + " DID NOT COMPRESS!\n";
            std::cout << "========================================================================================================================\n";
        }
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Services Compression
//----------------------------------------------------------------------------------

struct ServicesCompression {
    CompressionState &state;
    Functions &functions;
    BinaryEntropyPool &bep;
    Operations &operations;
    LayeredCompression &layeredCompression;
    TextEncoding &textEncoding;
    Database &database;
    XORCypher &xorCypher;
    GenerateUUID &uuidGen;
    EncodingRouter &router;
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Compression
//----------------------------------------------------------------------------------

class Compression {
  public:
    explicit Compression(ServicesCompression &services)
        : services(services) {}

    struct PadResult {
        bool ok;
        std::string A;
        std::string B;
    };

    // clang-format off
    void encodeBytes(const std::string &input, uint8_t byteValue, size_t maxLayers, bool printC, bool printP) { 
        compressBytes(input, byteValue, maxLayers, printC, printP); 
    }

    void encode(const std::string &input, bool printC, bool printP) { 
        compress(input, printC, printP); 
    }
    // clang-format on

    void decode() { decompress(); }

  private:
    static constexpr const char *CLASS_NAME = "Compression";

    SystemClock systemClock;

    // Field        bytes  bits
    // UUID         36     288
    // Directory    64     512
    // Name         64     512
    // Layers       8      64
    // OriginalSize 8      64
    // Metadata total size 1440 bits

    static constexpr size_t TOTAL_BITS    = 2048;
    static constexpr size_t HEADER_BITS   = 16;
    static constexpr size_t METADATA      = 1440;                                // 288 + 512 + 512 + 64 + 64 = 1440 bits
    static constexpr size_t TARGET_BITS   = TOTAL_BITS - HEADER_BITS - METADATA; // 592 — compressed payload budget
    static constexpr size_t HEX_LENGTH    = 256;                                 // 1024 bits per half == 256 hex chars
    static constexpr size_t REQUIRED_BITS = HEX_LENGTH * 4;                      // 1024
    static constexpr size_t UUID_CHARS    = 36;
    static constexpr size_t DIR_CHARS     = 64;
    static constexpr size_t NAME_CHARS    = 64;
    static constexpr size_t LAYERS_CHARS  = 8;

    ServicesCompression &services;

    bool format = true;
    bool hashes = true;
    bool printD = true;
    bool printC = true;

    struct DecodedMetadata {
        std::string uuid;
        std::string directory;
        std::string name;
        std::string layers;
        uint64_t originalSize;
    };

    // ── encoder ──────────────────────────────────────────────────────────
    void compressBytes(const std::string &input, uint8_t byteValue, size_t maxLayers, bool printC, bool printP) {
        // Compression::compressBytes
        services.layeredCompression.runByteEncoding(input, TARGET_BITS, byteValue, maxLayers, printC);

        std::string name      = byteToHexName(byteValue);
        std::string directory = "sweep";
        std::string uuid      = services.uuidGen.generateUUID();
        uint64_t originalSize = input.size();
        size_t layers         = services.state.layers.size();

        std::string metaBits     = buildMetadataBits(uuid, directory, name, layers, originalSize);
        std::string combinedBits = metaBits + services.state.finalizedBits;

        auto pad = padAndSplit(combinedBits);

        if (!pad.ok) {
            LOG_INFO(name + " — SKIPPED: combined bits (" + std::to_string(combinedBits.size()) + ") too large to pad to " + std::to_string(TOTAL_BITS) + " bits");
            services.database.programRecord(uuid, name, std::to_string(layers), std::to_string(originalSize), "skipped", "skipped");
            return;
        }

        auto compressed = services.xorCypher.compress(pad.A, pad.B);

        bool kOk = compressed.K.size() == REQUIRED_BITS;
        bool xOk = compressed.X.size() == REQUIRED_BITS;

        std::string kField;
        std::string xField;

        if (kOk && xOk) {
            kField = bitsToHex(compressed.K);
            xField = bitsToHex(compressed.X);
            LOG_INFO(name + " — compression OK, K/X reduced to " + std::to_string(HEX_LENGTH) + " hex chars each");
        } else {
            kField = "skipped";
            xField = "skipped";
            LOG_INFO(name + " — SKIPPED: K=" + std::to_string(compressed.K.size()) + " bits, X=" + std::to_string(compressed.X.size()) + " bits (expected "
                     + std::to_string(REQUIRED_BITS) + " each)");
        }

        services.database.programRecord(uuid, name, std::to_string(layers), std::to_string(originalSize), kField, xField);
    }

    void compress(const std::string &input, bool printC, bool printP) {
        std::string eBits    = services.router.encode(input, printC, services.state);
        services.state.eBits = eBits;

        EntropyAnalyzer analyzer;
        analyzer.feedBits(eBits);
        analyzer.print();

        long long start = systemClock.getNanoseconds();

        services.layeredCompression.runEncoding(eBits, TARGET_BITS, 1, printC);

        long long end = systemClock.getNanoseconds();

        long long elapsedNs = end - start;

        // Convert input size to MB
        double mb = eBits.size() / (1024.0 * 1024.0);

        // Convert nanoseconds to seconds
        double seconds = elapsedNs / 1'000'000'000.0;

        // MB per second
        double speed = mb / seconds;

        std::cout << "Input Size : " << mb << " MB\n";
        std::cout << "Time       : " << seconds << " s\n";
        std::cout << "Speed      : " << speed << " MB/s\n";

        std::string name         = services.database.getDocumentName();
        std::string directory    = services.database.getDirectoryName();
        std::string uuid         = services.uuidGen.generateUUID();
        uint64_t originalSize    = input.size();
        size_t layers            = services.state.layers.size();
        std::string metaBits     = buildMetadataBits(uuid, directory, name, layers, originalSize);
        std::string combinedBits = metaBits + services.state.finalizedBits;

        auto pad = padAndSplit(combinedBits);

        /*
                if (!pad.ok) {
                    LOG_INFO(name + " — SKIPPED: combined bits (" + std::to_string(combinedBits.size()) + ") too large to pad to " + std::to_string(TOTAL_BITS) + " bits");
                    services.database.programRecord(uuid, name, std::to_string(layers), std::to_string(originalSize), "skipped", "skipped");
                    return;
                }
        */

        auto compressed = services.xorCypher.compress(pad.A, pad.B);

        // bool kOk = compressed.K.size() == REQUIRED_BITS;
        // bool xOk = compressed.X.size() == REQUIRED_BITS;

        std::string kField;
        std::string xField;

        kField = bitsToHex(compressed.K);
        xField = bitsToHex(compressed.X);

        /*
                if (kOk && xOk) {
                    kField = bitsToHex(compressed.K);
                    xField = bitsToHex(compressed.X);
                    LOG_INFO(name + " — compression OK, K/X reduced to " + std::to_string(HEX_LENGTH) + " hex chars each");
                } else {
                    kField = "skipped";
                    xField = "skipped";
                    LOG_INFO(name + " — SKIPPED: K=" + std::to_string(compressed.K.size()) + " bits, X=" + std::to_string(compressed.X.size()) + " bits (expected "
                             + std::to_string(REQUIRED_BITS) + " each)");
                }
        */

        services.database.programRecord(uuid, name, std::to_string(layers), std::to_string(originalSize), kField, xField);
    }

    // ── decoder ──────────────────────────────────────────────────────────
    void decompress() {
        Database::Entry record = services.database.selectRecord();

        if (record.uuid.empty())
            return;

        if (record.key == "skipped" || record.xored == "skipped") {
            std::cout << "Record " << record.uuid << " was skipped during compression — nothing to decode.\n";
            return;
        }

        std::string K = services.database.getKeyBits(record);
        std::string X = services.database.getXoredBits(record);

        /*

                if (K.size() != REQUIRED_BITS || X.size() != REQUIRED_BITS) {
                    std::cout << "Record " << record.uuid << " — size mismatch before decompress: K=" << K.size() << " bits, X=" << X.size() << " bits (expected " <<
           REQUIRED_BITS
                              << " each). Key lookup likely failed — check keys.db/xor.db for a UUID mismatch.\n";
                    return;
                }
        */

        auto decompressed    = services.xorCypher.decompress(K, X); // 1. reverse XOR
        std::string rejoined = decompressed.A + decompressed.B;     // 2. recombine A+B → back to `padded`

        std::string header   = rejoined.substr(0, HEADER_BITS);         // 3. read the pad-count prefix
        std::string metaBits = rejoined.substr(HEADER_BITS, METADATA);  // 4. fixed-size metadata
        std::string payload  = rejoined.substr(HEADER_BITS + METADATA); // 5. payload — still has padding at its tail

        size_t padCount = 0;
        for (char c : header)
            padCount = (padCount << 1) | (c == '1' ? 1 : 0);

        payload.resize(payload.size() - padCount); // 6. strip padding LAST, after everything else

        DecodedMetadata meta = decodeMetadataBits(metaBits);
        std::cout << "decodeMetadataBits returned\n" << std::flush;

        std::cout << "========================================================================================================================\n";
        std::cout << "METADATA (decoded from XOR-recombined bits)\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "UUID:          " << meta.uuid << "\n";
        std::cout << "Directory:     " << meta.directory << "\n";
        std::cout << "Name:          " << meta.name << "\n";
        std::cout << "Layers:        " << meta.layers << "\n";
        std::cout << "Original Size: " << meta.originalSize << " bytes\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "Record " << record.uuid << " (" << record.name << ") — payload recovered: " << payload.size() << " bits.\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << "PAYLOAD BITS (" << payload.size() << " bits, layers still applied — not yet reversed)\n";
        std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
        std::cout << payload << "\n";
        std::cout << "========================================================================================================================\n";

        size_t layerCount = 0;
        try {
            layerCount = std::stoul(meta.layers);
        } catch (const std::exception &ex) {
            std::cout << "Record " << record.uuid << " — could not parse layer count from metadata (\"" << meta.layers << "\"): " << ex.what() << "\n";
            return;
        }

        std::string decoded = services.layeredCompression.runDecoding(payload, layerCount);

        std::cout << "\nFinished...\n";
        std::cin.get();
    }

    // ── utilities ────────────────────────────────────────────────────────
    inline std::string byteToHexName(uint8_t value) {
        std::ostringstream oss;
        oss << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(value);
        return oss.str();
    }

    // Converts a '0'/'1' bitstring into hex characters, 4 bits per hex digit.
    // Assumes bits.size() is a multiple of 4 — caller is responsible for
    // checking length (e.g. REQUIRED_BITS) before calling this.
    inline std::string bitsToHex(const std::string &bits) {
        std::string hex;
        hex.reserve(bits.size() / 4);

        for (size_t i = 0; i + 4 <= bits.size(); i += 4) {
            int nibble = 0;
            for (int j = 0; j < 4; ++j) {
                nibble = (nibble << 1) | (bits[i + j] == '1' ? 1 : 0);
            }
            hex.push_back("0123456789ABCDEF"[nibble]);
        }

        return hex;
    }

    PadResult padAndSplit(const std::string &bits) {
        size_t contentBits = HEADER_BITS + bits.size();

        if (contentBits > TOTAL_BITS) {
            return {false, "", ""}; // don't silently grow past the fixed 2048-bit budget
        }

        size_t padCount = TOTAL_BITS - contentBits;
        std::string header(HEADER_BITS, '0');
        size_t value = padCount;
        for (int i = static_cast<int>(HEADER_BITS) - 1; i >= 0; --i) {
            header[i] = (value & 1) ? '1' : '0';
            value >>= 1;
        }

        std::string padded = header + bits + std::string(padCount, '0');
        size_t half        = padded.size() / 2;
        return {true, padded.substr(0, half), padded.substr(half)};
    }
    /*

        // ── padding / split (over combined metadata + compressed bits) ───────
        PadResult padAndSplit(const std::string &bits) {
            size_t contentBits = HEADER_BITS + bits.size();
            size_t padCount;

            if (contentBits <= TOTAL_BITS) {
                padCount = TOTAL_BITS - contentBits;
            } else {
                // Oversized input: round the total up to the next multiple of 16 bits,
                // so each half (total/2) is a whole number of bytes. This guarantees
                // bitsToHex never has to silently drop trailing bits that don't form
                // a complete nibble.
                size_t remainder = contentBits % 16;
                padCount         = (remainder == 0) ? 0 : (16 - remainder);
            }

            std::string header(HEADER_BITS, '0');
            size_t value = padCount;
            for (int i = static_cast<int>(HEADER_BITS) - 1; i >= 0; --i) {
                header[i] = (value & 1) ? '1' : '0';
                value >>= 1;
            }

            std::string padded = header + bits + std::string(padCount, '0');
            size_t half        = padded.size() / 2;
            return {true, padded.substr(0, half), padded.substr(half)};
        }

            PadResult padAndSplit(const std::string &bits) {
                if (bits.size() + HEADER_BITS > TOTAL_BITS)
                    return {false, "", ""};

                size_t padCount = TOTAL_BITS - HEADER_BITS - bits.size();

                std::string header(HEADER_BITS, '0');
                size_t value = padCount;
                for (int i = static_cast<int>(HEADER_BITS) - 1; i >= 0; --i) {
                    header[i] = (value & 1) ? '1' : '0';
                    value >>= 1;
                }

                std::string padded = header + bits + std::string(padCount, '0');
                size_t half        = padded.size() / 2;

                return {true, padded.substr(0, half), padded.substr(half)};
            }
        */
    // ── metadata packing ────────────────────────────────────────────────
    std::string stringToFixedBits(const std::string &str, size_t charCount) {
        std::string fixed = str;
        if (fixed.size() > charCount)
            fixed = fixed.substr(0, charCount);
        else if (fixed.size() < charCount)
            fixed.append(charCount - fixed.size(), ' ');

        std::string bits;
        bits.reserve(charCount * 8);
        for (unsigned char c : fixed)
            for (int i = 7; i >= 0; --i)
                bits.push_back(((c >> i) & 1) ? '1' : '0');

        return bits;
    }

    // Encodes: [1-byte length prefix][content bytes][zero padding] == totalBytes always
    std::string encodePrefixedField(const std::string &value, size_t totalBytes) {
        size_t maxContentBytes = totalBytes - 1;

        std::string content = value;
        if (content.size() > maxContentBytes)
            content = content.substr(0, maxContentBytes);

        uint8_t len = static_cast<uint8_t>(content.size());

        std::string bits;
        bits.reserve(totalBytes * 8);

        for (int i = 7; i >= 0; --i)
            bits.push_back(((len >> i) & 1) ? '1' : '0');

        for (unsigned char c : content)
            for (int i = 7; i >= 0; --i)
                bits.push_back(((c >> i) & 1) ? '1' : '0');

        size_t paddingBytes = maxContentBytes - content.size();
        bits.append(paddingBytes * 8, '0');

        return bits;
    }

    std::string uint64ToBits(uint64_t value) {
        std::string bits(64, '0');
        for (int i = 63; i >= 0; --i) {
            bits[i] = (value & 1) ? '1' : '0';
            value >>= 1;
        }
        return bits;
    }

    std::string buildMetadataBits(const std::string &uuid, const std::string &directory, const std::string &name, size_t layers, uint64_t originalSize) {
        std::string bits;
        bits += stringToFixedBits(uuid, UUID_CHARS);                       // 288 bits, constant width, no prefix needed
        bits += encodePrefixedField(directory, DIR_CHARS);                 // 512 bits
        bits += encodePrefixedField(name, NAME_CHARS);                     // 512 bits
        bits += encodePrefixedField(std::to_string(layers), LAYERS_CHARS); // 64 bits
        bits += uint64ToBits(originalSize);                                // 64 bits
        return bits;
    }

    // ── metadata unpacking (inverse of buildMetadataBits) ──────────────────
    std::string bitsToChars(const std::string &bits) {
        std::string out;
        out.reserve(bits.size() / 8);
        for (size_t i = 0; i + 8 <= bits.size(); i += 8) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; ++j)
                byte = (byte << 1) | (bits[i + j] == '1' ? 1 : 0);
            out.push_back(static_cast<char>(byte));
        }
        return out;
    }

    // Inverse of stringToFixedBits: fixed width, space-padded, no length prefix.
    std::string decodeFixedBits(const std::string &bits, size_t charCount) {
        std::string raw = bitsToChars(bits.substr(0, charCount * 8));
        size_t end      = raw.find_last_not_of(' ');
        return (end == std::string::npos) ? "" : raw.substr(0, end + 1);
    }

    // Inverse of encodePrefixedField: [1-byte length][content][zero padding].
    std::string decodePrefixedField(const std::string &bits, size_t totalBytes) {
        std::string allBytes = bitsToChars(bits.substr(0, totalBytes * 8));
        uint8_t len          = static_cast<uint8_t>(allBytes[0]);
        len                  = std::min<uint8_t>(len, static_cast<uint8_t>(totalBytes - 1));
        return allBytes.substr(1, len);
    }

    uint64_t bitsToUint64(const std::string &bits) {
        uint64_t value = 0;
        for (size_t i = 0; i < 64 && i < bits.size(); ++i)
            value = (value << 1) | (bits[i] == '1' ? 1 : 0);
        return value;
    }

    DecodedMetadata decodeMetadataBits(const std::string &metaBits) {
        size_t offset = 0;

        std::string uuid = decodeFixedBits(metaBits.substr(offset), UUID_CHARS);
        offset += UUID_CHARS * 8;

        std::string directory = decodePrefixedField(metaBits.substr(offset), DIR_CHARS);
        offset += DIR_CHARS * 8;

        std::string name = decodePrefixedField(metaBits.substr(offset), NAME_CHARS);
        offset += NAME_CHARS * 8;

        std::string layers = decodePrefixedField(metaBits.substr(offset), LAYERS_CHARS);
        offset += LAYERS_CHARS * 8;

        uint64_t originalSize = bitsToUint64(metaBits.substr(offset));

        return {uuid, directory, name, layers, originalSize};
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Prompt Input -- CLASS 1
//----------------------------------------------------------------------------------

class PromptInput {
  public:
    struct InputRules {
        size_t minLength = 0;
        size_t maxLength = 64;

        bool allowEmpty       = false;
        bool asciiOnly        = false;
        bool numbersOnly      = false;
        bool noWhitespace     = false;
        bool allowSymbols     = true;
        bool alphanumericOnly = false;
        bool decimalOnly      = false;
    };

    static bool validate(const std::string &input, const InputRules &rules, std::string &outError) {
        if (!rules.allowEmpty && input.empty()) {
            outError = "Input cannot be empty";
            return false;
        }
        if (input.size() < rules.minLength) {
            outError = "Minimum length: " + std::to_string(rules.minLength);
            return false;
        }
        if (input.size() > rules.maxLength) {
            outError = "Maximum length: " + std::to_string(rules.maxLength);
            return false;
        }

        if (rules.asciiOnly)
            for (unsigned char c : input)
                if (c > 127) {
                    outError = "ASCII characters only";
                    return false;
                }

        if (rules.numbersOnly)
            for (unsigned char c : input)
                if (!std::isdigit(c)) {
                    outError = "Numbers only";
                    return false;
                }

        if (rules.alphanumericOnly)
            for (unsigned char c : input)
                if (!std::isalnum(c)) {
                    outError = "Letters and numbers only";
                    return false;
                }

        if (rules.noWhitespace)
            for (unsigned char c : input)
                if (std::isspace(c)) {
                    outError = "No whitespace allowed";
                    return false;
                }

        if (!rules.allowSymbols)
            for (unsigned char c : input)
                if (!std::isalnum(c) && !std::isspace(c)) {
                    outError = "Symbols not allowed";
                    return false;
                }

        if (rules.decimalOnly) {
            bool hasDot = false;
            for (int i = 0; i < (int)input.size(); i++) {
                char c = input[i];
                if (c == '-' && i == 0)
                    continue; // allow leading minus
                if (c == '.' && !hasDot) {
                    hasDot = true;
                    continue;
                } // allow one dot
                if (!std::isdigit((unsigned char)c)) {
                    outError = "Numbers only (decimal allowed)";
                    return false;
                }
            }
        }

        return true;
    }

    // =========================================================
    // RULES
    // =========================================================

    static InputRules rules_password() {
        InputRules r;
        r.minLength    = 8;
        r.maxLength    = 64;
        r.allowEmpty   = false;
        r.noWhitespace = true;
        r.allowSymbols = true;
        return r;
    }

    static InputRules rules_username() {
        InputRules r;
        r.minLength        = 3;
        r.maxLength        = 32;
        r.allowEmpty       = false;
        r.noWhitespace     = true;
        r.alphanumericOnly = true;
        return r;
    }

    // call after rules_password passes
    static bool passwordStrength(const std::string &input, std::string &outError) {
        bool hasLetter = false, hasDigit = false, hasSymbol = false;
        for (unsigned char c : input) {
            if (std::isalpha(c))
                hasLetter = true;
            if (std::isdigit(c))
                hasDigit = true;
            if (!std::isalnum(c) && !std::isspace(c))
                hasSymbol = true;
        }
        if (!hasLetter) {
            outError = "Password must contain a letter";
            return false;
        }
        if (!hasDigit) {
            outError = "Password must contain a number";
            return false;
        }
        if (!hasSymbol) {
            outError = "Password must contain a symbol";
            return false;
        }
        return true;
    }

    static InputRules rules_command() {
        InputRules r;
        r.maxLength    = 64;
        r.allowEmpty   = true;
        r.allowSymbols = true;
        return r;
    }

    static InputRules rules_amount() {
        InputRules r;
        r.maxLength    = 16;
        r.allowEmpty   = false;
        r.noWhitespace = true;
        r.decimalOnly  = true;
        return r;
    }

    static InputRules rules_filename() {
        InputRules r;
        r.maxLength        = 32;
        r.noWhitespace     = true;
        r.alphanumericOnly = true;
        return r;
    }

    static InputRules rules_label() {
        InputRules r;
        r.maxLength    = 48;
        r.allowEmpty   = false;
        r.allowSymbols = true;
        return r;
    }

    static InputRules rules_selection() {
        InputRules r;
        r.maxLength   = 4;
        r.allowEmpty  = false;
        r.numbersOnly = true;
        return r;
    }

    static InputRules rules_month() {
        InputRules r;
        r.maxLength        = 12;
        r.allowEmpty       = false;
        r.alphanumericOnly = true;
        return r;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// File System -- CLASS 1
//----------------------------------------------------------------------------------

class CompressionCommands {
  public:
    CompressionCommands(CompressionState &state, ServicesCompression &services, Compression &compression)
        : state(state)
        , services(services)
        , compression(compression) {}

    bool handle(const std::string &input, std::vector<std::string> &cmdLog) {
        switch (state.stage) {
        case CompressionState::Stage::MENU:
            return handleMenu(input, cmdLog);
        case CompressionState::Stage::AWAITING_TEXT:
            handleTextInput(input, cmdLog);
            return true;
        case CompressionState::Stage::DECODE_PROMPT:
            handleDecodePrompt(input, cmdLog);
            return true;
        }
        return true;
    }

  private:
    static constexpr const char *CLASS_NAME = "CompressionCommands";

    CompressionState &state;
    ServicesCompression &services;
    Compression &compression;

    static constexpr uint32_t sweepCountPerByte = 1'000;
    static constexpr int sweepMaxLayers         = 20;
    static constexpr bool printCompressionSteps = true;
    static constexpr bool printPipelineSteps    = true;

    bool handleMenu(const std::string &input, std::vector<std::string> &cmdLog) {
        if (input == "1") {
            state.stage = CompressionState::Stage::AWAITING_TEXT;
            state.addStatus("Enter text to compress, then press Enter.");
            cmdLog.push_back("Compression: awaiting manual text input.");
        } else if (input == "2") {
            runFileEncode("poem.txt", cmdLog);
        } else if (input == "3") {
            runFileEncode("KingJamesBible.txt", cmdLog);
        } else if (input == "4") {
            runSweep(cmdLog);
        } else if (input == "0") {
            return false;
        } else {
            state.addStatus("Invalid choice — pick 1-4, or 0 to go back.");
        }

        return true;
    }

    void handleTextInput(const std::string &input, std::vector<std::string> &cmdLog) {
        if (input.empty()) {
            state.addStatus("No text entered — type something and press Enter.");
            return;
        }

        runEncode(input, cmdLog);
    }

    void handleDecodePrompt(const std::string &input, std::vector<std::string> &cmdLog) {
        if (!input.empty() && (input[0] == 'y' || input[0] == 'Y')) {
            state.addStatus("Select a record to decode...");
            cmdLog.push_back("Compression: decode started.");

            compression.decode();

            state.addStatus("Decode complete.");
            cmdLog.push_back("Compression: decode complete.");
        } else {
            state.addStatus("Skipped decode.");
        }

        state.stage = CompressionState::Stage::MENU;
    }

    void runFileEncode(const std::string &filename, std::vector<std::string> &cmdLog) {
        std::ifstream file(filename);

        if (!file.is_open()) {
            state.addStatus("Failed to open " + filename);
            cmdLog.push_back("Compression: failed to open " + filename);
            return;
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        state.addStatus("Loaded " + filename + " (" + services.functions.format(static_cast<long long>(content.size())) + " bytes)");
        runEncode(content, cmdLog);
    }

    void runEncode(const std::string &input, std::vector<std::string> &cmdLog) {
        state.addStatus("Encoding " + services.functions.format(static_cast<long long>(input.size())) + " bytes...");
        cmdLog.push_back("Compression: encode started (" + std::to_string(input.size()) + " bytes).");

        compression.encode(input, printCompressionSteps, printPipelineSteps);

        state.addStatus("Encode complete.");
        cmdLog.push_back("Compression: encode complete.");

        state.stage = CompressionState::Stage::DECODE_PROMPT;
    }

    void runSweep(std::vector<std::string> &cmdLog) {
        state.addStatus("Running full 256-byte sweep — every byte value, one at a time...");
        cmdLog.push_back("Compression: 256-byte sweep started.");

        for (uint32_t val = 0; val < 256; ++val) {
            std::string rawBytes = services.functions.generateByteBlock(static_cast<uint8_t>(val), sweepCountPerByte);
            std::vector<uint8_t> rawBytesVec(rawBytes.begin(), rawBytes.end());
            std::string sweepInput = services.functions.bytesToBinary(rawBytesVec);

            LOG_INFO("========================================================================================================================");
            LOG_INFO("STARTING COMPRESSION OF BYTE: " + std::to_string(val));
            LOG_INFO("========================================================================================================================");

            compression.encodeBytes(sweepInput, static_cast<uint8_t>(val), sweepMaxLayers, printCompressionSteps, printPipelineSteps);

            LOG_INFO("========================================================================================================================");
            LOG_INFO("COMPRESSION COMPLETE");
            LOG_INFO("========================================================================================================================");
        }

        state.addStatus("Sweep complete — all 256 byte values processed.");
        cmdLog.push_back("Compression: 256-byte sweep complete.");
        state.stage = CompressionState::Stage::MENU;
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// File System -- CLASS 1
//----------------------------------------------------------------------------------

struct UIState {
    Page currentPage  = Page::HOME;
    Page previousPage = Page::HOME;
    InputMode mode    = InputMode::COMMAND;

    // std::string activeLogDate;
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// File System -- CLASS 1
//----------------------------------------------------------------------------------

class Commands {
  public:
    Commands(UIState &uiState)
        : uiState(uiState) {}

    void navigate(Page page, std::vector<std::string> &cmdLog, const std::string &arg = "") {
        if (page != uiState.currentPage)
            uiState.previousPage = uiState.currentPage;
        uiState.currentPage = page;
        cmdLog.push_back("[>] -> " + pageName());

        switch (page) {
        case Page::COMPRESSOR:
            break;
        default:
            break;
        }
    }

    std::string pageName() const {
        switch (uiState.currentPage) {
        case Page::COMPRESSOR:
            return "Compressor";
        default:
            return "Unknown";
        }
    }

    bool handle(const std::string &input, std::vector<std::string> &cmdLog, std::vector<std::string> &body) {
        // ── Compression has its own numeric menu, not slash commands ───
        if (uiState.currentPage == Page::COMPRESSOR)
            return compressionCommands.handle(input, cmdLog);

        // ── General input validation ───────────────────────────────────
        if (uiState.mode == InputMode::COMMAND) {
            std::string err;
            if (!input.empty() && !PromptInput::validate(input, PromptInput::rules_command(), err)) {
                cmdLog.push_back("[!] " + err);
                return true;
            }
        }

        if (input.empty())
            return false;

        if (input[0] != '/') {
            cmdLog.push_back("[!] Not a command: " + input);
            return true;
        }

        // ── Parse command and first arg ───────────────────────────────
        std::istringstream iss(input.substr(1));
        std::string cmd;
        iss >> cmd;
        for (char &c : cmd)
            c = static_cast<char>(std::tolower((unsigned char)c));

        std::string arg;
        iss >> arg;

        // ── Global navigation ─────────────────────────────────────────
        if (cmd == "compressor" || cmd == "comp") {
            navigate(Page::COMPRESSOR, cmdLog);
            return true;
        }

        if (cmd == "back") {
            if (uiState.previousPage == uiState.currentPage)
                cmdLog.push_back("[!] Already here");
            else {
                uiState.currentPage = uiState.previousPage;
                cmdLog.push_back("[>] Back to " + pageName());
            }
            return true;
        }

        // ── Page-specific commands ────────────────────────────────────
        switch (uiState.currentPage) {
        case Page::COMPRESSOR:
            return compressionCommands.handle(input, cmdLog);

        default:
            cmdLog.push_back("[!] Unknown command: /" + cmd);
            return true;
        }
    }

  private:
    CompressionCommands &compressionCommands;
    UIState &uiState;

    void printCommands(std::vector<std::string> &cmdLog) {
        cmdLog.push_back("--------------- Commands --------------");
        cmdLog.push_back("  /compression          (h)");
        cmdLog.push_back("---------------------------------------");
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Main -- CLASS 18
//----------------------------------------------------------------------------------

class CompressionPage {
  public:
    void init(CompressionState &state, std::vector<std::string> &cmdLog) {
        state.reset();
        cmdLog.push_back("Compression page opened.");
    }

    std::vector<std::string> buildUI(const CompressionState &state, int w, int h) const {
        const int boxW    = 90;
        const int padLeft = std::max(0, (w - boxW) / 2);

        const std::string indent(padLeft, ' ');
        const std::string border(boxW, '=');

        auto center = [&](const std::string &text) {
            const int pad = std::max(0, (w - (int)text.size()) / 2);
            return std::string(pad, ' ') + text;
        };

        std::vector<std::string> content;

        content.emplace_back(indent + border);
        content.emplace_back(center("COMPRESSION TERMINAL"));
        content.emplace_back(indent + border);
        content.emplace_back("");

        if (state.statusLines.empty()) {
            content.emplace_back(indent + "  (no activity yet)");
        } else {
            for (const auto &line : state.statusLines)
                content.emplace_back(indent + "  " + line);
        }

        content.emplace_back("");
        content.emplace_back(indent + border);

        switch (state.stage) {
        case CompressionState::Stage::MENU:
            content.emplace_back(indent + "  1. Enter text manually");
            content.emplace_back(indent + "  2. Load poem.txt");
            content.emplace_back(indent + "  3. Load KingJamesBible.txt");
            content.emplace_back(indent + "  4. Run full 256-byte sweep");
            content.emplace_back(indent + "  0. Back");
            break;
        case CompressionState::Stage::AWAITING_TEXT:
            content.emplace_back(indent + "  Type the text you want to compress, then press Enter.");
            break;
        case CompressionState::Stage::DECODE_PROMPT:
            content.emplace_back(indent + "  Decode a record now? (y/n)");
            break;
        }

        content.emplace_back(indent + border);

        std::vector<std::string> lines;
        const int topPad = std::max(0, (h - (int)content.size()) / 3);

        for (int i = 0; i < topPad; ++i)
            lines.emplace_back("");

        for (const auto &line : content)
            lines.emplace_back(line);

        return lines;
    }

  private:
    static constexpr const char *CLASS_NAME = "CompressionPage";
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Binary Entropy Pool -- CLASS 7
//----------------------------------------------------------------------------------

class Output {
  public:
    explicit Output(Commands &cmds, TradingLoop &tradingLoop)
        : commands(cmds)
        , tradingLoop(tradingLoop) {}

    std::string buildBar(int w = 0) {
        if (w <= 0)
            w = render.consoleWidth();
        return std::string(w, '=') + "\033[K\n";
    }

    std::string buildHeader(int iterationCount, double fps) {
        std::vector<Line> row1a, row1b, row1c;
        std::vector<Line> row2a, row2b, row2c;

        row1a.emplace_back("BTC" /*services.enumFuncs.buildCurrencyPair(market.key.pair)*/, Align::CENTER);
        row1b.emplace_back("--- " + commands.pageName() + " ---", Align::CENTER);
        row1c.emplace_back(/*services.enumFuncs.exchangeToString(market.key.exchange)*/ " EXCHANGE", Align::CENTER);

        row2a.emplace_back("Iteration: " + std::to_string(iterationCount) + "  FPS: " + std::to_string((int)fps), Align::CENTER);
        row2b.emplace_back("Price: $" + formatPrice(currentPrice), Align::CENTER);
        row2c.emplace_back("Date: " + systemClock.getCurrentTime(), Align::CENTER);

        std::ostringstream out;
        out << buildBar();
        out << render.printHeaderColumns({row1a, row1b, row1c});
        out << buildBar();
        out << render.printHeaderColumns({row2a, row2b, row2c});
        out << buildBar();
        return out.str();
    }

    std::string buildCommandLine(const std::string &input, int w) {
        std::ostringstream out;
        out << std::string(w, '=') << "\033[K\n";
        out << "[>] Command Line: " << input << "_\033[K\n";
        return out.str();
    }

    std::string buildFooter(const std::string &userName, const std::string &loginTime, const std::string &lastLoginTime) {
        // Side side = tradingLoop.getTradeDecision();
        std::vector<Line> f1, f2, f3, f4, f5;

        f1.emplace_back("User: " + userName, Align::CENTER);
        f2.emplace_back("Login time: " + loginTime, Align::CENTER);
        f3.emplace_back("Last login: " + lastLoginTime, Align::CENTER);
        f4.emplace_back("Direction: " /*+ enumFuncs.tradeTypeToString(side)*/, Align::CENTER);
        f5.emplace_back(std::string("Version: ") + CLIENT_VERSION, Align::CENTER);

        std::ostringstream out;
        out << buildBar();
        out << render.printFooterColumns({f1, f2, f3, f4, f5});
        out << buildBar();
        return out.str();
    }

    std::string buildPageHome(int w) {
        std::ostringstream out;

        auto center = [&](const std::string &txt) {
            if (txt.empty())
                return std::string("\n");
            int pad = std::max(0, (w - (int)txt.size()) / 2);
            return std::string(pad, ' ') + txt + "\n";
        };

        out << center("=== TRADE TERMINAL ===");
        out << center("");
        out << center("PAGES");
        out << center("");
        out << center("/compression");
        out << center("");
        out << center("COMMANDS");
        out << center("");
        out << center("/back");

        out << center("");
        out << center("Type any command starting with / to navigate.");

        return out.str();
    }

  private:
    Commands &commands;

    static std::string formatPrice(uint64_t cents) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(cents) / 100.0);
        return ss.str();
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Main -- CLASS 18
//----------------------------------------------------------------------------------

struct ServicesUI {
    // Compression algorithms
    CompressionState compressionState{};
    Operations operations{};
    TextEncoding textEncoding{};
    Database compressionDatabase{};
    XORCypher xorCypher{};
    EncodingRouter encodingRouter{};
    LayeredCompression layeredCompression;
    ServicesCompression servicesCompression;
    Compression compression;

    // UI command handlers
    // DatabaseCommands databaseCommands;
    CompressionCommands compressionCommands;
    Commands commands;
    Output output;

    // Pages
    CompressionPage compressionPage;

    ServicesUI()
        : databaseCommands(fileSystem)
        , commands(services, compressionCommands)
        , compressionState()
        , compressionPage()
        , compressionCommands(compressionState, servicesCompression, compression)
        , databasePage(fileSystem) {}
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Main -- CLASS 18
//----------------------------------------------------------------------------------

class UserInterface {
  public:
    explicit UserInterface(ServicesUI &services)
        : services(services) {}

    ~UserInterface() {
        services.uiRunning.store(false, std::memory_order_release);
        join();
    }

    void startOnThread() { uiThread = std::thread(&UserInterface::run, this); }

    void join() {
        if (uiThread.joinable())
            uiThread.join();
    }

    void run() {
        windowsUtilities.enableAnsiEscapes();
        windowsUtilities.fitBufferToWindow();
        windowsUtilities.hideCursor();

        std::ios::sync_with_stdio(false);
        std::cin.tie(nullptr);

        constexpr int FPS     = 30;
        const auto FRAME_TIME = std::chrono::milliseconds(1000 / FPS);
        int iterationCount    = 0;
        double fps            = 0.0;
        using Clock           = std::chrono::high_resolution_clock;
        auto lastFrameTime    = Clock::now();
        std::string commandInput;

        std::cout << "\033[3J\033[2J\033[H";
        std::cout.flush();

        while (services.uiRunning.load(std::memory_order_acquire)) {
            ++iterationCount;

            auto frameStart = Clock::now();
            double elapsed  = std::chrono::duration<double>(frameStart - lastFrameTime).count();
            fps             = elapsed > 0.0 ? 1.0 / elapsed : 0.0;
            lastFrameTime   = frameStart;

            // ── Input ─────────────────────────────────────────────────────────
            while (_kbhit()) {
                char c = _getch();
                if (c == '\r') {

                    if (!commandInput.empty()) {
                        if (commandInput == "/exit" || commandInput == "/quit") {
                            cmdLog.push_back("[<] Shutting down...");
                            services.uiRunning.store(false, std::memory_order_release);
                            services.tradingLoop.setTrading(false);
                            services.encryptedFileSystem.lock();
                            std::this_thread::sleep_for(std::chrono::seconds(2));
                            commandInput.clear();
                            continue;
                        }
                        bool handled = services.commands.handle(commandInput, cmdLog, body);
                        if (!handled)
                            body.push_back("> " + commandInput);
                        commandInput.clear();
                    }

                } else if (c == '\b') {
                    if (!commandInput.empty())
                        commandInput.pop_back();
                } else {
                    commandInput += c;
                }
            }

            // ── Layout ────────────────────────────────────────────────────────
            auto [W, H] = getConsoleSize();

            constexpr int TOP_EMPTY = 1;
            constexpr int HEADER    = 5;
            constexpr int CMD       = 2;
            constexpr int FOOTER    = 3;

            const int BODY_SIZE = std::max(0, H - (TOP_EMPTY + HEADER + CMD + FOOTER));
            const int SIDEBAR_W = 40;
            const int MAIN_W    = W - SIDEBAR_W - 1;
            const int CHART_H   = std::max(3, BODY_SIZE);

            while ((int)cmdLog.size() > BODY_SIZE)
                cmdLog.erase(cmdLog.begin());

            // ── Build frame ───────────────────────────────────────────────────
            std::ostringstream frame;
            frame << "\033[K\n";
            frame << services.output.buildHeader(iterationCount, fps);

            bool renderMain = true;

            // ── Main app pages ────────────────────────────────────────────────
            if (renderMain) {
                switch (services.uiState.currentPage) {

                case Page::COMPRESSOR:
                    frame << buildBody(cmdLog, services.compressionPage.buildUI(services.compressionState, MAIN_W, BODY_SIZE), BODY_SIZE, W, SIDEBAR_W, true);
                    break;
                }

                frame << services.output.buildCommandLine(commandInput, W);
                frame << services.output.buildFooter(services.authPage.pendingUsername, services.authPage.loginTime, services.authPage.lastLoginTime);

                std::string frameText = frame.str();
                if (!frameText.empty() && frameText.back() == '\n')
                    frameText.pop_back();

                std::cout << "\033[H" << frameText;
                std::cout.flush();

                // ── Frame-rate cap ────────────────────────────────────────────────
                auto renderElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - frameStart);
                if (renderElapsed < FRAME_TIME) {
                    std::this_thread::sleep_for(FRAME_TIME - renderElapsed);
                }
            }

            windowsUtilities.showCursor();
            std::cout << "\033[0m\033[2J\033[H";
            std::cout << "[<] Exiting the program...\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout.flush();
        }
    }

  private:
    ServicesUI &services;
    WindowsUtilities windowsUtilities;
    std::thread uiThread;

    std::vector<std::string> body;
    std::vector<std::string> cmdLog;

    std::vector<std::string> expandLines(const std::vector<std::string> &entries, int colW) {
        std::vector<std::string> lines;
        for (const auto &entry : entries) {
            std::istringstream iss(entry);
            std::string seg;
            while (std::getline(iss, seg)) {
                if ((int)seg.size() <= colW)
                    lines.push_back(seg);
                else
                    for (int pos = 0; pos < (int)seg.size(); pos += colW)
                        lines.push_back(seg.substr(pos, colW));
            }
        }
        return lines;
    }

    std::string buildBody(const std::vector<std::string> &sidebar,
                          const std::vector<std::string> &main,
                          int BODY_SIZE,
                          int w,
                          int sidebarW,
                          bool rawMain                                      = false,
                          const std::vector<std::vector<Line>> *mainColumns = nullptr,
                          const std::vector<double> *percents               = nullptr,
                          bool usePercentColumns                            = false) {

        const int dividerW = sidebarW > 0 ? 1 : 0;
        const int mainW    = w - sidebarW - dividerW;

        std::vector<std::string> sideLines = expandLines(sidebar, sidebarW);
        while ((int)sideLines.size() > BODY_SIZE)
            sideLines.erase(sideLines.begin());

        std::vector<std::string> mainLines;

        if (mainColumns && usePercentColumns && percents) {
            std::string rendered = services.render.printColumnsPercent(*mainColumns, *percents, 2, 0, mainW);
            mainLines            = services.accountingPage.splitLines(rendered);
        } else if (mainColumns) {
            std::string rendered = services.render.printColumns(*mainColumns, 2, 0, mainW);
            mainLines            = services.accountingPage.splitLines(rendered);
        } else if (rawMain) {
            mainLines = main;
        } else {
            mainLines = expandLines(main, mainW);
        }

        while ((int)mainLines.size() > BODY_SIZE)
            mainLines.erase(mainLines.begin());

        std::ostringstream out;
        for (int i = 0; i < BODY_SIZE; ++i) {
            const std::string &s = i < (int)sideLines.size() ? sideLines[i] : "";
            const std::string &m = i < (int)mainLines.size() ? mainLines[i] : "";

            if (sidebarW > 0) {
                out << s << std::string(std::max(0, sidebarW - (int)s.size()), ' ');
                out << '|';
            }

            if (rawMain || mainColumns)
                out << ((int)m.size() > mainW ? m.substr(0, mainW) : m + std::string(std::max(0, mainW - (int)m.size()), ' '));
            else
                out << m << std::string(std::max(0, mainW - (int)m.size()), ' ');

            out << "\033[K\n";
        }
        return out.str();
    }
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Main -- CLASS 18
//----------------------------------------------------------------------------------

int main() {
    WindowsUtilities windowsUtilities;
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    windowsUtilities.maximizeConsoleWindow();
    SetConsoleTitle(TEXT("Compression Algorithm"));
#endif

    ServicesUI services;
    services.fileSystem.bootstrapGlobal();

    // startup
    UserInterface ui(services);
    ui.startOnThread();
    ui.join();

    // shutdown
    services.tradingLoop.stop();

    return 0;
}

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Main
//----------------------------------------------------------------------------------
