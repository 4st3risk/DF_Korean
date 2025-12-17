# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <Windows.h>
# include "MinHook.h"
# include <SDL.h>
# include <ft2build.h>
# include FT_FREETYPE_H

# include <string>
# include <vector>
# include <regex>
# include <map>
# include <fstream>
# include <sstream>
# include <algorithm>
# include <iostream>
# include <unordered_map>

using namespace std;

# define LINE_HEIGHT 12
# define PARA_GAP    20

// =========================================================
// 1. Global Values
// =========================================================

void GetDFColor(int index, Uint8* r, Uint8* g, Uint8* b) {
    if (index >= 0 && index <= 15) {
        switch (index) {
        case 0: *r = 0;   *g = 0;   *b = 0;   break;   // Black
        case 1: *r = 80;  *g = 80;  *b = 180; break;   // Blue
        case 2: *r = 60;  *g = 160; *b = 60;  break;   // Green
        case 3: *r = 60;  *g = 160; *b = 160; break;   // Cyan
        case 4: *r = 180; *g = 60;  *b = 60;  break;   // Red
        case 5: *r = 19; *g = 255;  *b = 101; break;   // Magenta
        case 6: *r = 215; *g = 155; *b = 45;  break;   // Brown
        case 7: *r = 210; *g = 210; *b = 210; break;   // Light Gray
        case 8: *r = 128; *g = 128; *b = 128; break;   // Dark Gray
        case 9: *r = 100; *g = 100; *b = 255; break;   // LBlue
        case 10:*r = 100; *g = 255; *b = 100; break;   // LGreen
        case 11:*r = 100; *g = 255; *b = 255; break;   // LCyan
        case 12:*r = 255; *g = 100; *b = 100; break;   // LRed
        case 13:*r = 255; *g = 255; *b = 255; break;   // LMagenta
        case 14:*r = 255; *g = 255; *b = 80;  break;   // Yellow
        case 15:*r = 255; *g = 255; *b = 255; break;   // White
        }
        return;
    }
    switch (index) {
        case 66: *r = 19; *g =255; *b = 101; return; //Green
        case 67: *r = 167; *g = 60; *b = 213; return; // purple
        case 70: *r = 167; *g = 60; *b = 213; return;
        case 71: *r = 18; *g = 254; *b = 207; return; //cyan
    }
    int fallback_index = index % 16;
    GetDFColor(fallback_index, r, g, b);
}


struct MSVC_String {
    union {
        char* ptr;
        char buf[16];
    } data;
    size_t size;
    size_t capacity;

    const char* c_str() const {
        return (capacity >= 16) ? data.ptr : data.buf;
    }
};

extern FT_Face g_ft_face;
int GetNextUTF8Char(const char* ptr, uint32_t* out_char);

static int ClampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static int CalcKoreanFontPixelHeight(int tile_h) {
    if (tile_h <= 0) return 12;
    int font_px = (tile_h * 3) / 4; // 16px tile -> 12px font
    return ClampInt(font_px, 8, tile_h);
}

static int MeasureKoreanTextureWidthPx(const char* text, int tile_h) {
    if (!g_ft_face || !text || !*text) return 0;

    int font_px = CalcKoreanFontPixelHeight(tile_h);
    FT_Set_Pixel_Sizes(g_ft_face, 0, font_px);

    int pen_x = 0;
    const char* p = text;
    while (*p) {
        uint32_t unicode_char = 0;
        int step = GetNextUTF8Char(p, &unicode_char);
        if (!FT_Load_Char(g_ft_face, unicode_char, FT_LOAD_DEFAULT)) {
            pen_x += (g_ft_face->glyph->advance.x >> 6);
        }
        p += step;
    }

    int pad_px = max(2, tile_h / 4);
    return pen_x + pad_px;
}

static MSVC_String MakeMSVCStringFromBuffer(char* buffer, size_t len) {
    MSVC_String out{};
    if (len < 16) {
        memset(out.data.buf, 0, sizeof(out.data.buf));
        memcpy(out.data.buf, buffer, len);
        out.data.buf[len] = '\0';
        out.size = len;
        out.capacity = 15;
        return out;
    }

    out.data.ptr = buffer;
    out.size = len;
    out.capacity = len;
    return out;
}

// =========================================================
// 2. Function Pointers
// =========================================================

string SmartTranslate(string original);
int SDLCALL Detour_SDL_RenderCopy(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect);
SDL_Texture* SDLCALL Detour_SDL_CreateTextureFromSurface(SDL_Renderer* renderer, SDL_Surface* surface);
void SDLCALL Detour_SDL_DestroyTexture(SDL_Texture* texture);
void SDLCALL Detour_SDL_RenderPresent(SDL_Renderer* renderer);
SDL_Texture* GetKoreanTexture(SDL_Renderer* renderer, const char* text, int tile_h, int* out_w, int* out_h);
bool IsValidString(const char* str, size_t max_len = 1024);

typedef SDL_Texture* (SDLCALL* SDL_CreateTextureFromSurface_t)(SDL_Renderer* renderer, SDL_Surface* surface);
typedef void (SDLCALL* SDL_DestroyTexture_t)(SDL_Texture* texture);
typedef int (SDLCALL* SDL_RenderCopy_t)(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect);
typedef void (SDLCALL* SDL_RenderPresent_t)(SDL_Renderer* renderer);


SDL_CreateTextureFromSurface_t fpSDL_CreateTextureFromSurface = NULL;
SDL_DestroyTexture_t fpSDL_DestroyTexture = NULL;
SDL_RenderCopy_t fpSDL_RenderCopy = NULL;
SDL_RenderPresent_t fpSDL_RenderPresent = NULL;

FT_Library g_ft_lib = NULL;
FT_Face g_ft_face = NULL;

typedef void (__fastcall* tAddst)(void* gps, MSVC_String* src, uint8_t justify, uint32_t space);
tAddst fpAddst = nullptr;
typedef void(__fastcall* tPtrst)(void* gps, void* src, size_t a3, size_t a4);
tPtrst fpPtrst = nullptr;

//const uintptr_t OFFSET_ADDST = 0xAB2410;
//const uintptr_t OFFSET_HIDDEN_ADDST = 0xAB2240;
uintptr_t g_OFFSET_PTRST = 0;
uintptr_t g_OFFSET_ADDST = 0;

map<string, string> translation_map;
SDL_Renderer* g_renderer = nullptr;


// =========================================================
// 3. Data Structures
// =========================================================

typedef struct { unsigned long hash; char character; } HashEntry;
HashEntry global_hash_map[5000];
int map_count = 0;

#define MAX_PTR_CACHE 10000
typedef struct { SDL_Texture* tex_ptr; char character; Uint8 r, g, b; } PtrCache;
PtrCache ptr_cache[MAX_PTR_CACHE];
int ptr_count = 0;

typedef struct { char original[128]; char translated[128]; } TransEntry;
TransEntry trans_dict[2000];
int trans_count = 0;

typedef struct { char text[128]; SDL_Texture* texture; int w, h; } TextureCache;
TextureCache kr_tex_cache[2000];
int kr_cache_count = 0;

unsigned long dumped_hashes[5000];
int dumped_count = 0;

struct TextRenderCommand {
    string text;
    int x, y;
    uint8_t r, g, b;
    int life;
};

struct RenderItem {
    SDL_Texture* texture;
    int x, y;
    int w, h;
    Uint32 last_update_tick;
};

typedef struct {
    SDL_Texture* tex;
    char c;
    SDL_Rect dst_rect;
    SDL_Rect src_rect;
    int has_src;
    Uint8 r, g, b, a;
} RenderOp;

struct PatternEntry {
    regex pattern;
    string replacement;
};

struct FlatNode {
    std::vector<std::pair<unsigned char, int>> children;
    int replacementIdx = -1;

    FlatNode() {
        children.reserve(2);
    }
    int FindChild(unsigned char c) {
        for (auto& pair : children) {
            if (pair.first == c) return pair.second;
        }
        return -1;
    }
};

vector<PatternEntry> pattern_list;

typedef pair<string, string> GlossaryEntry;
vector<GlossaryEntry> glossary_list;

std::map<string, RenderItem> g_renderCache;
vector<TextRenderCommand> g_renderQueue;
std::unordered_map<string, string> g_smartCache;

RenderOp frame_ops[4096];
int op_count = 0;

const int TILE_WIDTH = 8;
const int TILE_HEIGHT = 12;

const int OFFSET_GPS_X = 456;
const int OFFSET_GPS_Y = 456 + 4;

Uint32 g_lastReloadTick = 0;
Uint32 g_reloadMessageTick = 0;

bool g_showReloadMessage = false;
bool g_seenColors[256] = { false };
bool IsPotentialTextChar(char c);

static DWORD last_load_time = 0;

FILE* log_file = NULL;
void log_msg(const char* format, ...) {
    if (!log_file) fopen_s(&log_file, "dfkr_log.txt", "w");
    if (log_file) {
        va_list args;
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        fflush(log_file);
    }
}



class GlossaryTrie {
public:
    std::vector<FlatNode> nodes;
    std::vector<std::string> replacements;

    GlossaryTrie() { Clear(); }

    void Clear() {
        nodes.clear();
        replacements.clear();
        nodes.emplace_back();
        nodes.reserve(200000);
    }

    void Insert(std::string key, const std::string& val) {
        if (key.empty()) return;

        int currIdx = 0;
        for (unsigned char c : key) {
            unsigned char lowerC = (unsigned char)tolower(c);

            int nextIdx = nodes[currIdx].FindChild(lowerC);
            if (nextIdx == -1) {
                nextIdx = (int)nodes.size();
                nodes[currIdx].children.push_back({ lowerC, nextIdx });
                nodes.emplace_back();
            }
            currIdx = nextIdx;
        }
        nodes[currIdx].replacementIdx = (int)replacements.size();
        replacements.push_back(val);
    }

    const std::string* Find(const char* str, size_t len, size_t& outMatchLen) {
        int currIdx = 0;
        int lastMatchIdx = -1;
        size_t lastMatchLen = 0;

        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)tolower(str[i]);

            int nextIdx = nodes[currIdx].FindChild(c);
            if (nextIdx == -1) break;

            currIdx = nextIdx;

            if (nodes[currIdx].replacementIdx != -1) {
                lastMatchIdx = nodes[currIdx].replacementIdx;
                lastMatchLen = i + 1;
            }
        }

        outMatchLen = lastMatchLen;
        if (lastMatchIdx != -1) {
            return &replacements[lastMatchIdx];
        }
        return nullptr;
    }
};

GlossaryTrie g_glossaryTrie;
// =========================================================
// 4. Hooking Functions
// =========================================================


void __fastcall DetourPtrst(void* gps, void* arg2, size_t a3, size_t a4) {
    enum class PtrstTextType {
        None,
        CStr,
        MSVCString,
    };

    PtrstTextType detected_type = PtrstTextType::None;
    const char* detected_text = nullptr;
    size_t detected_len = 0;

    if (arg2 != nullptr && !IsBadReadPtr(arg2, sizeof(MSVC_String))) {
        MSVC_String* strObj = (MSVC_String*)arg2;
        if (strObj->size > 0 && strObj->size < 2000 && strObj->capacity < 2000 && strObj->size <= strObj->capacity) {
            const char* strContent = strObj->c_str();
            size_t max_len = min((size_t)1024, strObj->size + 1);
            if (IsValidString(strContent, max_len)) {
                detected_text = strContent;
                detected_len = strObj->size;
                detected_type = PtrstTextType::MSVCString;
            }
        }
    }

    if (detected_type == PtrstTextType::None) {
        const char* rawPtr = (const char*)arg2;
        if (IsValidString(rawPtr)) {
            detected_text = rawPtr;
            detected_len = strnlen(rawPtr, 1024);
            detected_type = PtrstTextType::CStr;
        }
    }

    if (detected_type == PtrstTextType::None || detected_text == nullptr) {
        fpPtrst(gps, arg2, a3, a4);
        return;
    }

    Uint8 r, g, b;
    GetDFColor(15, &r, &g, &b);

    if (a3 != 0 && !IsBadReadPtr((void*)a3, 1)) {
        unsigned char* colorArray = (unsigned char*)a3;
        unsigned char colorIndex = colorArray[0];

        if (colorIndex > 15 && !g_seenColors[colorIndex]) {
            g_seenColors[colorIndex] = true;
            // log_msg("[NEW PALETTE] Found Color number: %d\n", colorIndex);
        }
    }

    string key = detected_text;
    string korText = SmartTranslate(key);

    if (korText.empty()) {
        fpPtrst(gps, arg2, a3, a4);
        return;
    }

    if (a3 != 0 && !IsBadReadPtr((void*)a3, 1)) {
        unsigned char* colorArray = (unsigned char*)a3;
        size_t len = key.length();

        unsigned char bestColor = 7;
        int maxPriority = -1;

        for (size_t i = 0; i < len; i++) {
            if (IsBadReadPtr(colorArray + i, 1)) break;

            unsigned char col = colorArray[i];

            int priority = 0;
            if (col == 7 || col == 8 || col == 0) priority = 0;
            else if (col == 15 || col == 14 || col == 12 || col == 10 || col == 66 || col == 67 || col == 70 || col == 71) priority = 100;
            else priority = 50;

            if (priority > maxPriority) {
                maxPriority = priority;
                bestColor = col;
            }
        }
        GetDFColor(bestColor, &r, &g, &b);
    }

    int grid_x = *(int*)((char*)gps + 0x84); // Address of x
    int grid_y = *(int*)((char*)gps + 0x88); // Address of y
    int tile_w = *(int*)((char*)gps + 0x1D8);
    int tile_h = *(int*)((char*)gps + 0x1DC);
    if (tile_w == 0) tile_w = 12;
    if (tile_h == 0) tile_h = 16;

    int final_pixel_x = 0;
    int final_pixel_y = grid_y * tile_h;

    if (g_renderer) {
        int justify = 0;
        if (a3 != 0 && !IsBadReadPtr((void*)a3, sizeof(int))) {
            justify = *(int*)a3;
        }

        if (justify == 1) {
            int screen_grid_w = *(int*)((char*)gps + 0x28C);
            if (screen_grid_w == 0) screen_grid_w = 149;
            int screen_pixel_w = screen_grid_w * tile_w;

            int text_pixel_w = MeasureKoreanTextureWidthPx(korText.c_str(), tile_h);
            if (text_pixel_w <= 0) text_pixel_w = ((int)korText.length() / 3) * tile_w;

            final_pixel_x = (screen_pixel_w / 2) - (text_pixel_w / 2);
        }
        else if (justify == 2) {
            int screen_grid_w = *(int*)((char*)gps + 0x28C);

            int text_pixel_w = MeasureKoreanTextureWidthPx(korText.c_str(), tile_h);
            if (text_pixel_w <= 0) text_pixel_w = ((int)korText.length() / 3) * tile_w;

            final_pixel_x = (screen_grid_w * tile_w) - text_pixel_w;
        }
        else {
            final_pixel_x = grid_x * tile_w;
        }

        char uniqueKey[256];
        sprintf_s(uniqueKey, "%s_%d_%d_%d", key.c_str(), final_pixel_x, final_pixel_y, tile_h);

        Uint32 current_tick = SDL_GetTicks();

        if (g_renderCache.count(uniqueKey)) {
            g_renderCache[uniqueKey].last_update_tick = current_tick;

            g_renderCache[uniqueKey].x = final_pixel_x;
            g_renderCache[uniqueKey].y = final_pixel_y;
        }
        else {
            RenderItem item;
            item.x = final_pixel_x;
            item.y = final_pixel_y;
            item.last_update_tick = current_tick;

            item.texture = GetKoreanTexture(g_renderer, korText.c_str(), tile_h, &item.w, &item.h);

            if (item.texture) {
                SDL_SetTextureColorMod(item.texture, r, g, b);
                g_renderCache[uniqueKey] = item;
            }
        }
    }

    std::vector<char> placeholderBuf(detected_len + 1, ' ');
    for (size_t i = 0; i < detected_len; i++) {
        char c = detected_text[i];
        placeholderBuf[i] = (c == '\n' || c == '\r' || c == '\t') ? c : ' ';
    }
    placeholderBuf[detected_len] = '\0';

    if (detected_type == PtrstTextType::CStr) {
        fpPtrst(gps, placeholderBuf.data(), a3, a4);
        return;
    }

    MSVC_String placeholderStr = MakeMSVCStringFromBuffer(placeholderBuf.data(), detected_len);
    fpPtrst(gps, &placeholderStr, a3, a4);
}

void __fastcall DetourAddst(void* gps, MSVC_String* src, uint8_t justify, uint32_t space) {
    const char* orgText = src->c_str();
    Uint8 r, g, b;
    GetDFColor(15, &r, &g, &b);

    // printf("[MAIN] GPS: addr: 0x%p\n", gps);
    string key = orgText;
    string korText = SmartTranslate(key);

    if (!korText.empty()) {
        int grid_x = *(int*)((char*)gps + 0x84);
        int grid_y = *(int*)((char*)gps + 0x88); // +4 offset
        
        int tile_w = *(int*)((char*)gps + 0x1D8);
        int tile_h = *(int*)((char*)gps + 0x1DC); // +4 offset

        if (tile_w == 0) tile_w = 12;
        if (tile_h == 0) tile_h = 16;

        int final_pixel_x = 0;
        int final_pixel_y = grid_y * tile_h;

        if (justify == 1) {
            int screen_grid_w = *(int*)((char*)gps + 0x28C);
            if (screen_grid_w == 0) screen_grid_w = 149;

            int screen_pixel_w = screen_grid_w * tile_w;
            int text_pixel_w = MeasureKoreanTextureWidthPx(korText.c_str(), tile_h);
            if (text_pixel_w <= 0) text_pixel_w = ((int)korText.length() / 3) * tile_w;
            final_pixel_x = (screen_pixel_w / 2) - (text_pixel_w / 2);
        }
        else if (justify == 2) {
            int screen_grid_w = *(int*)((char*)gps + 0x28C);
            int text_pixel_w = MeasureKoreanTextureWidthPx(korText.c_str(), tile_h);
            if (text_pixel_w <= 0) text_pixel_w = ((int)korText.length() / 3) * tile_w;
            final_pixel_x = (screen_grid_w * tile_w) - text_pixel_w;
        }
        else {
            final_pixel_x = grid_x * tile_w;
        }

        TextRenderCommand cmd;
        cmd.text = korText;
        cmd.x = final_pixel_x;
        cmd.y = final_pixel_y;

        cmd.r = r;
        cmd.g = g;
        cmd.b = b;

        char uniqueKey[256];
        sprintf_s(uniqueKey, "%s_%d_%d_%d", key.c_str(), final_pixel_x, final_pixel_y, tile_h);

        Uint32 current_tick = SDL_GetTicks();

        if (g_renderCache.count(uniqueKey)) {
            g_renderCache[uniqueKey].last_update_tick = current_tick;

            g_renderCache[uniqueKey].x = final_pixel_x;
            g_renderCache[uniqueKey].y = final_pixel_y;
        }
        else {
            RenderItem item;
            item.x = final_pixel_x;
            item.y = final_pixel_y;
            /*item.w = 8;
            item.h = 12;*/
            item.last_update_tick = current_tick;

            item.texture = GetKoreanTexture(g_renderer, korText.c_str(), tile_h, &item.w, &item.h);

            if (item.texture) {
                // printf("Texture secure\n");
                SDL_SetTextureColorMod(item.texture, r, g, b);
                g_renderCache[uniqueKey] = item;
            }
        }

        size_t org_len = src->size;
        if (org_len == 0 && orgText) org_len = strlen(orgText);

        std::vector<char> placeholderBuf(org_len + 1, ' ');
        for (size_t i = 0; i < org_len; i++) {
            char c = orgText ? orgText[i] : 0;
            placeholderBuf[i] = (c == '\n' || c == '\r' || c == '\t') ? c : ' ';
        }
        placeholderBuf[org_len] = '\0';

        MSVC_String placeholderStr = MakeMSVCStringFromBuffer(placeholderBuf.data(), org_len);
        fpAddst(gps, &placeholderStr, justify, space);
    }
    else {
        fpAddst(gps, src, justify, space);
    }
}

//void __fastcall DetourAddst(void* gps, MSVC_String* src, uint8_t justify, uint32_t space) {
//    if (!gps || !src) return;
//
//    const char* orgText = src->c_str();
//    if (strstr(orgText, "Settings")) {
//
//        int* ptr = (int*)gps;
//        int y_pos = 10;
//
//        for (int i = 0; i < 250; i++) {
//            int val = ptr[i];
//
//            if (val > 1 && val < 4000) {
//                char debugBuf[64];
//                sprintf_s(debugBuf, "[0x%X]: %d", i * 4, val);
//
//                TextRenderCommand cmd;
//                cmd.text = debugBuf;
//                cmd.x = 10;
//                cmd.y = y_pos;
//                cmd.r = 0; cmd.g = 255; cmd.b = 0;
//
//                g_renderQueue.push_back(cmd);
//
//                y_pos += 12;
//
//                if (y_pos > 800) break;
//            }
//        }
//
//        fpAddst(gps, src, justify, space);
//        return;
//    }
//    string korText = SmartTranslate(orgText);
//    if (!korText.empty()) {
//        /*int x = *(int*)((uintptr_t)gps + 0x294);
//        int y = *(int*)((uintptr_t)gps + 0x294 + 4);
//        */
//        //printf("x: 0x%x, y: 0x%x\n", *(int*)((uintptr_t)gps), *(int*)((uintptr_t)gps + 4));
//        int x = *(int*)((uintptr_t)gps + 0x84);
//        int y = *(int*)((uintptr_t)gps + 0x88);
//        
//        int win_w = 0, win_h = 0;
//        if (g_renderer) {
//
//            SDL_GetRendererOutputSize(g_renderer, &win_w, &win_h);
//        }
//        int tile_w = 8; // (1920 / 149 = 약 12.8)
//        int tile_h = 12; // (1080 / 65 = 약 16.6)
//        
//        tile_w = win_w / x;
//        tile_h = win_h / y;
//
//        printf("%d, %d\n", win_w / x, win_h / y);
//
//        TextRenderCommand cmd;
//        cmd.text = korText;
//        cmd.x = (x * tile_w) / 2;
//        cmd.y = (y * tile_h) / 2;
//        cmd.life = 60;
//        cmd.r = 255; cmd.g = 255; cmd.b = 255;
//
//        bool exists = false;
//        for (auto& q : g_renderQueue) {
//            if (q.text == korText && abs(q.y - y) < 5) { q.life = 60; exists = true; break; }
//        }
//        if (!exists) g_renderQueue.push_back(cmd);
//
//        MSVC_String emptyStr = *src;
//        char nullChar = '\0';
//        if (emptyStr.capacity >= 16) emptyStr.data.ptr = &nullChar;
//        else emptyStr.data.buf[0] = '\0';
//        emptyStr.size = 0;
//        fpAddst(gps, &emptyStr, justify, space);
//    }
//    else {
//        fpAddst(gps, src, justify, space);
//    }
//}

// =========================================================
// 5. Helper Functions
// =========================================================

std::vector<std::string> ParseCSVLine(const std::string& line) {
    std::vector<std::string> result;
    std::string cell;
    bool inside_quote = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];

        if (c == '"') {
            if (inside_quote && i + 1 < line.size() && line[i + 1] == '"') {
                cell += '"';
                i++;
            }
            else {
                inside_quote = !inside_quote;
            }
        }
        else if (c == ',' && !inside_quote) {
            result.push_back(cell);
            cell.clear();
        }
        else {
            cell += c;
        }
    }
    result.push_back(cell);
    return result;
}

void LoadDataFromCSV(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        log_msg("[ERROR] Can't Open CSV File (%s).\n", filename.c_str());
        return;
    }

    char bom[3];
    file.read(bom, 3);
    if (!(bom[0] == (char)0xEF && bom[1] == (char)0xBB && bom[2] == (char)0xBF)) {
        file.seekg(0);
    }

    std::string line;
    int count_glossary = 0;
    int count_regex = 0;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::vector<std::string> cols = ParseCSVLine(line);

        if (cols.size() < 2) continue;

        std::string eng = cols[0];
        std::string kor = cols[1];
        if (eng.empty() || kor.empty()) continue;
        if (eng.front() == '^') {
            try {
                //pattern_list.push_back({ std::regex(eng), kor });
                pattern_list.push_back({ std::regex(eng, std::regex::icase), kor });
                count_regex++;
            }
            catch (...) {
                log_msg("[ERROR] Wrong Regex(CSV): %s\n", eng.c_str());
            }
        }
        else {
            g_glossaryTrie.Insert(eng, kor);
            count_glossary++;
        }
    }

    file.close();
    log_msg("[SYSTEM] CSV Load Complete! (Word: %d, Pattern: %d)\n", count_glossary, count_regex);
}

bool IsValidString(const char* str, size_t max_len) {
    if (str == nullptr) return false;
    if (IsBadReadPtr(str, 1)) return false;

    size_t len = 0;
    while (len < max_len) {
        char c = str[len];
        if (c == 0) break;
        if ((unsigned char)c < 32 && c != '\t' && c != '\n' && c != '\r') {
            return false;
        }
        len++;
    }
    if (len == 0 || len >= max_len) return false;
    return true;
}

int GetNextUTF8Char(const char* ptr, uint32_t* out_char) {
    unsigned char c = (unsigned char)*ptr;

    if (c < 0x80) {
        *out_char = c;
        return 1;
    }
    else if ((c & 0xE0) == 0xC0) {
        *out_char = ((ptr[0] & 0x1F) << 6) | (ptr[1] & 0x3F);
        return 2;
    }
    else if ((c & 0xF0) == 0xE0) {
        *out_char = ((ptr[0] & 0x0F) << 12) | ((ptr[1] & 0x3F) << 6) | (ptr[2] & 0x3F);
        return 3;
    }
    else if ((c & 0xF8) == 0xF0) {
        *out_char = ((ptr[0] & 0x07) << 18) | ((ptr[1] & 0x3F) << 12) | ((ptr[2] & 0x3F) << 6) | (ptr[3] & 0x3F);
        return 4;
    }

    *out_char = '?';
    return 1;
}

bool IsPotentialTextChar(char c) {
    return isalnum((unsigned char)c) || c == ' ' || c == '[' || c == '\"' || c == '\'';
}

std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

bool LoadOffsetsFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        // printf("[!] Can't Open %s. Make sure file exists.", filename);
        log_msg("[!] Can't Open %s. Make sure file exists.", filename);
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t delimiterPos = line.find('=');
        if (delimiterPos != std::string::npos) {
            std::string key = Trim(line.substr(0, delimiterPos));
            std::string valueStr = Trim(line.substr(delimiterPos + 1));
            uintptr_t value = std::strtoull(valueStr.c_str(), nullptr, 16);

            if (key == "DF_PTRST_OFFSET") {
                g_OFFSET_PTRST = value;
            }
            else if (key == "DF_ADDST_OFFSET") {
                g_OFFSET_ADDST = value;
            }
        }
    }

    file.close();
    return (g_OFFSET_PTRST != 0 && g_OFFSET_ADDST != 0);
}

bool IsWordBoundary(char c) {
    return !isalnum((unsigned char)c) && c != '_';
}

bool CompareLength(const GlossaryEntry& a, const GlossaryEntry& b) {
    if (a.first.length() == b.first.length()) {
        return a.first < b.first;
    }
    return a.first.length() > b.first.length();
}

void LoadGlossary(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) return;

    g_glossaryTrie.Clear();

    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;
        size_t delimiterPos = line.find('=');
        if (delimiterPos != string::npos) {
            string key = line.substr(0, delimiterPos);
            string value = line.substr(delimiterPos + 1);
            g_glossaryTrie.Insert(key, value);
        }
    }

    // log_msg("[System] Loaded %d glossary words (Sorted by length)\n");
}

void LoadKeyValueFile(const char* filename, map<string, string>& target_map) {
    ifstream file(filename);
    if (!file.is_open()) return;

    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;
        size_t delimiterPos = line.find('=');
        if (delimiterPos != string::npos) {
            string key = line.substr(0, delimiterPos);
            string value = line.substr(delimiterPos + 1);
            target_map[key] = value;
        }
    }
    log_msg("[System] Loaded %d entries from %s\n", target_map.size(), filename);
}

void LoadPatterns(const string& filename) {
    ifstream file(filename);

    if (!file.is_open()) {
        log_msg("[ERROR] Can't Open Pattern file (%s)\n", filename.c_str());
        return;
    }

    int count = 0;
    string line;

    while (getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t delimPos = line.find('=');
        if (delimPos == string::npos) continue;

        string patStr = line.substr(0, delimPos);
        string repStr = line.substr(delimPos + 1);

        patStr = Trim(patStr);
        repStr = Trim(repStr);

        try {
            std::regex re(patStr);
            pattern_list.push_back({ re, repStr });
            count++;
        }
        catch (const std::regex_error& e) {
            log_msg("[ERROR] Wronf Regex Pattern Found: %s\n", patStr.c_str());
        }
    }

    file.close();
    printf("[SYSTEM] Regex Pattern Loaded: %d\n", count);
}

void GetSurfaceColor(SDL_Surface* s, Uint8* r, Uint8* g, Uint8* b) {
    *r = 255; *g = 255; *b = 255;
    if (!s || !s->format) return;

    int bpp = s->format->BytesPerPixel;
    if (bpp != 4 && bpp != 3) return;

    Uint8 max_brightness = 0;
    Uint8 best_r = 255, best_g = 255, best_b = 255;

    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            Uint8* p = (Uint8*)s->pixels + y * s->pitch + x * bpp;

            Uint32 pixel = 0;
            if (bpp == 4) pixel = *(Uint32*)p;
            else if (SDL_BYTEORDER == SDL_BIG_ENDIAN) pixel = p[0] << 16 | p[1] << 8 | p[2];
            else pixel = p[0] | p[1] << 8 | p[2] << 16;

            Uint8 tr, tg, tb, ta;
            SDL_GetRGBA(pixel, s->format, &tr, &tg, &tb, &ta);

            if (ta > 0) {
                Uint8 brightness = (tr + tg + tb) / 3;
                if (brightness > max_brightness) {
                    max_brightness = brightness;
                    best_r = tr;
                    best_g = tg;
                    best_b = tb;
                }
            }
        }
    }

    if (max_brightness > 0) {
        *r = best_r;
        *g = best_g;
        *b = best_b;
    }
}

string TranslateVocabulary(string input) {
    string result = input;

    for (auto const& [eng, kor] : glossary_list) {
        size_t pos = 0;
        while ((pos = result.find(eng, pos)) != string::npos) {
            result.replace(pos, eng.length(), kor);
            pos += kor.length();
        }
    }
    return result;
}

string ApplyGlossary(const char* raw, size_t len) {
    if (len == 0) return "";

    string result;
    result.reserve(len + 256);

    size_t lastCopiedPos = 0;
    size_t i = 0;

    while (i < len) {
        if (i > 0 && isalnum((unsigned char)raw[i - 1])) {
            i++;
            continue;
        }

        size_t matchLen = 0;
        const string* replacement = g_glossaryTrie.Find(raw + i, len - i, matchLen);
        if (replacement && matchLen > 0) {
            size_t endPos = i + matchLen;
            bool isFullWord = (endPos == len || !isalnum((unsigned char)raw[endPos]));

            if (isFullWord) {
                if (i > lastCopiedPos) {
                    result.append(raw + lastCopiedPos, i - lastCopiedPos);
                }
                result.append(*replacement);
                i += matchLen;
                lastCopiedPos = i;
                continue;
            }
        }
        i++;
    }
    if (lastCopiedPos < len) {
        result.append(raw + lastCopiedPos, len - lastCopiedPos);
    }

    return result;
}

string SmartTranslate(std::string original) {
    if (original.empty()) return "";

    auto cacheIt = g_smartCache.find(original);
    if (cacheIt != g_smartCache.end()) {
        return cacheIt->second;
    }

    std::string working_text = original;
    bool matched_regex = false;

    for (const auto& entry : pattern_list) {
        if (regex_match(original, entry.pattern)) {

            working_text = std::regex_replace(original, entry.pattern, entry.replacement);
            matched_regex = true;
            break;
        }
    }
    
    std::string final_text = ApplyGlossary(working_text.c_str(), working_text.length());

    if (final_text != original) {
        g_smartCache[original] = final_text;
        return final_text;
    }

    /*if (translation_map.count(original)) {
        return translation_map[original];
    }*/

    g_smartCache[original] = "";
    return "";
}

void RegisterTexturePtr(SDL_Texture* tex, char c, Uint8 r, Uint8 g, Uint8 b) {
    int idx = -1;
    if (ptr_count < MAX_PTR_CACHE) {
        idx = ptr_count++;
    }
    else {
        static int ring_idx = 0;
        idx = ring_idx;
        ring_idx = (ring_idx + 1) % MAX_PTR_CACHE;
    }
    ptr_cache[idx].tex_ptr = tex;
    ptr_cache[idx].character = c;
    ptr_cache[idx].r = r;
    ptr_cache[idx].g = g;
    ptr_cache[idx].b = b;
}

void UnregisterTexturePtr(SDL_Texture* tex) {
    for (int i = 0; i < ptr_count; i++) {
        if (ptr_cache[i].tex_ptr == tex) {
            if (i < ptr_count - 1) {
                ptr_cache[i] = ptr_cache[ptr_count - 1];
            }
            ptr_count--;
            return;
        }
    }
}

char GetCharFromPtr(SDL_Texture* tex) {
    for (int i = ptr_count - 1; i >= 0; i--) {
        if (ptr_cache[i].tex_ptr == tex) return ptr_cache[i].character;
    }
    return 0;
}

char GetCharAndColorFromPtr(SDL_Texture* tex, Uint8* r, Uint8* g, Uint8* b) {
    for (int i = ptr_count - 1; i >= 0; i--) {
        if (ptr_cache[i].tex_ptr == tex) {
            if (r) *r = ptr_cache[i].r;
            if (g) *g = ptr_cache[i].g;
            if (b) *b = ptr_cache[i].b;
            return ptr_cache[i].character;
        }
    }
    return 0;
}

unsigned long generate_hash(SDL_Surface* surface) {
    unsigned long hash = 5381;
    unsigned char* pixels = (unsigned char*)surface->pixels;
    int size = surface->w * surface->h * surface->format->BytesPerPixel;
    for (int i = 0; i < size; i++) hash = ((hash << 5) + hash) + pixels[i];
    return hash;
}

void LoadHashMap() {
    char path[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, path);
    strcat_s(path, MAX_PATH, "\\dump\\*.bmp");
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(path, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char* filename = findData.cFileName;
            if (strncmp(filename, "Unknown", 7) == 0) continue;
            char* underscore = strchr(filename, '_');
            if (underscore) {
                char c = filename[0];
                unsigned long hash = strtoul(underscore + 1, NULL, 10);
                if (hash > 0 && map_count < 5000) {
                    global_hash_map[map_count].character = c;
                    global_hash_map[map_count].hash = hash;
                    map_count++;
                }
            }
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
}

const char* FindTranslation(const char* original) {
    for (int i = 0; i < trans_count; i++) {
        if (strcmp(trans_dict[i].original, original) == 0) return trans_dict[i].translated;
    }
    return NULL;
}

SDL_Texture* GetKoreanTexture(SDL_Renderer* renderer, const char* text, int tile_h, int* out_w, int* out_h) {
    if (!g_ft_face || !text || !*text) return NULL;
    if (tile_h <= 0) tile_h = 16;

    int font_px = CalcKoreanFontPixelHeight(tile_h);
    FT_Set_Pixel_Sizes(g_ft_face, 0, font_px);

    int pen_x = 0;
    const char* measure_p = text;
    while (*measure_p) {
        uint32_t unicode_char = 0;
        int step = GetNextUTF8Char(measure_p, &unicode_char);
        if (!FT_Load_Char(g_ft_face, unicode_char, FT_LOAD_DEFAULT)) {
            pen_x += (g_ft_face->glyph->advance.x >> 6);
        }
        measure_p += step;
    }

    int tex_h = tile_h;
    int pad_px = max(2, tile_h / 4);
    int tex_w = pen_x + pad_px;

    if (tex_w <= 0 || tex_h <= 0) return NULL;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, tex_w, tex_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return NULL;

    SDL_LockSurface(surface);
    memset(surface->pixels, 0, surface->h * surface->pitch);
    SDL_UnlockSurface(surface);

    pen_x = 0;
    int baseline = tex_h - 1;

    SDL_LockSurface(surface);
    Uint32* pixels = (Uint32*)surface->pixels;
    int width = surface->w;
    int height = surface->h;
    const char* p = text;

    while(*p) {
        uint32_t unicode_char = 0;
        int step = GetNextUTF8Char(p, &unicode_char);

        if (FT_Load_Char(g_ft_face, unicode_char, FT_LOAD_RENDER)) {
            p += step;
            continue;
        }

        FT_Bitmap* bitmap = &g_ft_face->glyph->bitmap;
        int bearingX = g_ft_face->glyph->bitmap_left;
        int bearingY = g_ft_face->glyph->bitmap_top;

        int x_pos = pen_x + bearingX;
        int y_pos = baseline - bearingY;

        for (int y = 0; y < bitmap->rows; y++) {
            for (int x = 0; x < bitmap->width; x++) {
                int draw_x = x_pos + x;
                int draw_y = y_pos + y;
                if (draw_x >= 0 && draw_x < width && draw_y >= 0 && draw_y < height) {
                    unsigned char alpha = bitmap->buffer[y * bitmap->width + x];
                    if (alpha > 0) {
                        pixels[draw_y * width + draw_x] = (alpha << 24) | 0x00FFFFFF;
                    }
                }
            }
        }
        p += step;
        pen_x += (g_ft_face->glyph->advance.x >> 6);
    }
    SDL_UnlockSurface(surface);

    SDL_Texture* texture = fpSDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);

    if (out_w) *out_w = tex_w;
    if (out_h) *out_h = tex_h;

    return texture;
}

int compare_ops(const void* a, const void* b) {
    RenderOp* A = (RenderOp*)a;
    RenderOp* B = (RenderOp*)b;
    if (abs(A->dst_rect.y - B->dst_rect.y) <= 8) return A->dst_rect.x - B->dst_rect.x;
    return A->dst_rect.y - B->dst_rect.y;
}

// ======================================
// Hooking Function
// ======================================

void SDLCALL Detour_SDL_DestroyTexture(SDL_Texture* texture) {
    UnregisterTexturePtr(texture);
    fpSDL_DestroyTexture(texture);
}

SDL_Texture* SDLCALL Detour_SDL_CreateTextureFromSurface(SDL_Renderer* renderer, SDL_Surface* surface) {
    SDL_Texture* tex = fpSDL_CreateTextureFromSurface(renderer, surface);

    if (surface && surface->w == 8 && surface->h == 12) {
        unsigned long hash = generate_hash(surface);
        char c = 0;
        for (int i = 0; i < map_count; i++) {
            if (global_hash_map[i].hash == hash) { c = global_hash_map[i].character; break; }
        }

        if (c != 0) {
            Uint8 r, g, b;
            GetSurfaceColor(surface, &r, &g, &b);

            RegisterTexturePtr(tex, c, r, g, b);
        }
        else {
            /*int exists = 0;
            for (int k = 0; k < dumped_count; k++) if (dumped_hashes[k] == hash) exists = 1;
            if (!exists && dumped_count < 5000) {
                dumped_hashes[dumped_count++] = hash;
                char f[64]; sprintf_s(f, 64, "dump\\Unknown_%lu.bmp", hash);
                SDL_SaveBMP(surface, f);
            }*/
        }
    }
    return tex;
}

int SDLCALL Detour_SDL_RenderCopy(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect)
{
    Uint8 r, g, b;
    char c = GetCharAndColorFromPtr(texture, &r, &g, &b);

    if (c != 0 && dstrect && op_count < 4096) {
        frame_ops[op_count].tex = texture;
        frame_ops[op_count].c = c;
        frame_ops[op_count].dst_rect = *dstrect;
        if (srcrect) { frame_ops[op_count].src_rect = *srcrect; frame_ops[op_count].has_src = 1; }
        else frame_ops[op_count].has_src = 0;

        frame_ops[op_count].r = r;
        frame_ops[op_count].g = g;
        frame_ops[op_count].b = b;

        SDL_GetTextureAlphaMod(texture, &frame_ops[op_count].a);

        op_count++;
        return 0;
    }
    return fpSDL_RenderCopy(renderer, texture, srcrect, dstrect);
}

// Debugging Code
//void SDLCALL Detour_SDL_RenderPresent(SDL_Renderer* renderer) {
//    g_renderer = renderer;
//    SDL_Texture* oldTarget = SDL_GetRenderTarget(renderer);
//    SDL_bool oldClipEnabled = SDL_RenderIsClipEnabled(renderer);
//    SDL_Rect oldClipRect;
//    SDL_RenderGetClipRect(renderer, &oldClipRect);
//    SDL_Rect oldViewport;
//    SDL_RenderGetViewport(renderer, &oldViewport);
//    float oldScaleX, oldScaleY;
//    SDL_RenderGetScale(renderer, &oldScaleX, &oldScaleY);
//
//    SDL_SetRenderTarget(renderer, NULL);
//    SDL_RenderSetClipRect(renderer, NULL);
//    SDL_RenderSetViewport(renderer, NULL);
//    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
//
//    auto it = g_renderQueue.begin();
//    for (auto it = g_renderQueue.begin(); it != g_renderQueue.end();) {
//        int w, h;
//        SDL_Texture* tex = GetKoreanTexture(renderer, it->text.c_str(), &w, &h);
//
//        if (tex) {
//            SDL_Rect dest = { it->x, it->y, w, h };
//
//            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
//            SDL_SetTextureColorMod(tex, it->r, it->g, it->b);
//            SDL_SetTextureAlphaMod(tex, 255);
//
//            SDL_RenderCopy(renderer, tex, NULL, &dest);
//            SDL_DestroyTexture(tex);
//        }
//
//        it->life--;
//
//        if (it->life <= 0) {
//            it = g_renderQueue.erase(it);
//        }
//        else {
//            ++it;
//        }
//    }
//
//    SDL_RenderSetScale(renderer, oldScaleX, oldScaleY);
//    SDL_RenderSetViewport(renderer, &oldViewport);
//    if (oldClipEnabled == SDL_TRUE) SDL_RenderSetClipRect(renderer, &oldClipRect);
//    else SDL_RenderSetClipRect(renderer, NULL);
//    SDL_SetRenderTarget(renderer, oldTarget);
//
//    fpSDL_RenderPresent(renderer);
//}

void ReloadTranslationData() {
    // printf("[SYSTEM] Reloading Translation Data...\n");
    log_msg("[SYSTEM] Reloading Translation Data...\n");

    translation_map.clear();
    pattern_list.clear();
    glossary_list.clear();
    LoadKeyValueFile("translations.txt", translation_map);
    LoadDataFromCSV("translation_data.csv");
    g_smartCache.clear();

}


void SDLCALL Detour_SDL_RenderPresent(SDL_Renderer* renderer) {
    g_renderer = renderer;
    Uint32 current_tick = SDL_GetTicks();

    if (GetAsyncKeyState(VK_F5) & 0x8000) {
        ReloadTranslationData();

        g_lastReloadTick = current_tick;

        g_showReloadMessage = true;
        g_reloadMessageTick = current_tick;
    }

    for (auto it = g_renderCache.begin(); it != g_renderCache.end(); ) {
        RenderItem& item = it->second;

        if (current_tick - item.last_update_tick > 70) {

            if (item.texture) SDL_DestroyTexture(item.texture);

            it = g_renderCache.erase(it);
        }
        else {
            if (item.texture) {
                SDL_Rect dest = { item.x, item.y, item.w, item.h };
                SDL_RenderCopy(renderer, item.texture, NULL, &dest);
            }
            ++it;
        }
    }

    if (g_showReloadMessage) {
        if (current_tick - g_reloadMessageTick > 2000) {
            g_showReloadMessage = false;
        }
        else {
            SDL_Rect notifyBox = { 10, 10, 20, 20 };
            SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
            SDL_RenderFillRect(renderer, &notifyBox);
        }
    }

    fpSDL_RenderPresent(renderer);
}

// =========================================================
// 6. Main
// =========================================================

DWORD WINAPI SetupHook(LPVOID lpParam) {
    LoadHashMap();
    LoadKeyValueFile("translations.txt", translation_map);
    LoadDataFromCSV("translation_data.csv");
    LoadOffsetsFromFile("offsets.txt");

    if (FT_Init_FreeType(&g_ft_lib)) return 1;
    if (FT_New_Face(g_ft_lib, "font.ttf", 0, &g_ft_face)) return 1;

    // For Debugging console
    /*AllocConsole();
    SetConsoleOutputCP(65001);
    FILE* f; 
    freopen_s(&f, "CONOUT$", "w", stdout);
    printf("Dwarf Fortress Hook Loaded.\n");*/
    log_msg("Dwarf Fortress Hook Loaded.\n");

    if (MH_Initialize() != MH_OK) {
        // printf("MinHook Initialize Failed \n");
        log_msg("Minhook Initialize Failed.\n");
        return -1;
    }

    HMODULE hSDL = GetModuleHandleA("SDL2.dll");
    while (!hSDL) { Sleep(100); hSDL = GetModuleHandleA("SDL2.dll"); }

    MH_CreateHookApi(L"SDL2.dll", "SDL_CreateTextureFromSurface", &Detour_SDL_CreateTextureFromSurface, (LPVOID*)&fpSDL_CreateTextureFromSurface);
    MH_CreateHookApi(L"SDL2.dll", "SDL_DestroyTexture", &Detour_SDL_DestroyTexture, (LPVOID*)&fpSDL_DestroyTexture);
    MH_CreateHookApi(L"SDL2.dll", "SDL_RenderCopy", &Detour_SDL_RenderCopy, (LPVOID*)&fpSDL_RenderCopy);
    MH_CreateHookApi(L"SDL2.dll", "SDL_RenderPresent", &Detour_SDL_RenderPresent, (LPVOID*)&fpSDL_RenderPresent);;

    uintptr_t baseAddress = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t targetAddress = baseAddress + g_OFFSET_ADDST;

    /*printf("0x%p", g_OFFSET_ADDST);
    printf("Base Address: 0x%llx\n", baseAddress);
    printf("Target Addst: 0x%llx\n", targetAddress);*/
    log_msg("0x%p", g_OFFSET_ADDST);
    log_msg("Base Address: 0x%llx\n", baseAddress);
    log_msg("Target Addst: 0x%llx\n", targetAddress);

    if (MH_CreateHook((LPVOID)targetAddress, &DetourAddst, (LPVOID*)&fpAddst) != MH_OK) {
        // printf("CreateHook Failed \n");
        log_msg("CreateHook Failed \n");
        return -1;
    }

    targetAddress = baseAddress + g_OFFSET_PTRST;

    /*printf("Base Address: 0x%llx\n", baseAddress);
    printf("Target Addst: 0x%llx\n", targetAddress);*/
    log_msg("Base Address: 0x%llx\n", baseAddress);
    log_msg("Target Addst: 0x%llx\n", targetAddress);

    if (MH_CreateHook((LPVOID)targetAddress, &DetourPtrst, (LPVOID*)&fpPtrst) != MH_OK) {
        // printf("CreateHook Failed \n");
        log_msg("CreateHook Failed \n");
        return -1;
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        // printf("EnableHook Failed\n");
        log_msg("EnableHook Failed\n");
        return -1;
    }

    // printf("Hook Enabled Successfully!\n");
    log_msg("Hook Enabled Successfully!\n");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        CreateThread(NULL, 0, SetupHook, NULL, 0, NULL);
    }
    else if (fdwReason == DLL_PROCESS_DETACH) {
        if (log_file) fclose(log_file);
        MH_Uninitialize();
    }
    return TRUE;
}
