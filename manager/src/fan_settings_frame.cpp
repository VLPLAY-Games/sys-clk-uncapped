#include "fan_settings_frame.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <switch.h>
#include <borealis.hpp>

#define SYSTEM_SETTINGS_PATH "/atmosphere/config/system_settings.ini"
#define TC_SECTION "[tc]"
#define HANDHELD_KEY "tskin_rate_table_handheld"
#define DOCKED_KEY "tskin_rate_table_console"

namespace {
    constexpr int TEMP_LOW_SENTINEL  = -1000000;
    constexpr int TEMP_HIGH_SENTINEL = 1000000;

    constexpr int TEMP_FIRST = 35000;
    constexpr int TEMP_LAST   = 80000;
    constexpr int TEMP_STEP   = 5000;

    constexpr std::array<int, 10> kCurveTemps = {
        35000, 40000, 45000, 50000, 55000,
        60000, 65000, 70000, 75000, 80000
    };

    static inline void trimInPlace(std::string& s) {
        auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };

        auto start = std::find_if_not(s.begin(), s.end(), isSpace);
        auto end   = std::find_if_not(s.rbegin(), s.rend(), isSpace).base();

        s = (start < end) ? std::string(start, end) : "";
    }

    static inline bool startsWith(const std::string& s, const char* prefix) {
        return s.rfind(prefix, 0) == 0;
    }

    static inline int pwmToPercent(int pwm) {
        pwm = std::clamp(pwm, 0, 255);
        return (pwm * 100 + 127) / 255;
    }

    static inline int percentToPwm(int percent) {
        percent = std::clamp(percent, 0, 100);
        return (percent * 255 + 50) / 100;
    }

    static inline bool isLowerEdgeRow(const FanTableEntry& e) {
        return e.minTemp <= TEMP_LOW_SENTINEL / 2;
    }

    static inline bool isUpperEdgeRow(const FanTableEntry& e) {
        return e.maxTemp >= TEMP_HIGH_SENTINEL / 2;
    }

    static std::string formatRowLabel(const FanTableEntry& e) {
        if (isLowerEdgeRow(e)) return "<35°C";
        if (isUpperEdgeRow(e)) return ">80°C";

        char buf[64];
        snprintf(buf, sizeof(buf), "%d-%d°C", e.minTemp / 1000, e.maxTemp / 1000);
        return buf;
    }

    static bool rowMatchesTemp(const FanTableEntry& e, int temp) {
        return temp >= e.minTemp && temp <= e.maxTemp;
    }

    static int lerpPwm(const FanTableEntry& e, int temp) {
        if (e.maxTemp <= e.minTemp) {
            return e.maxPwm;
        }

        if (e.minPwm == e.maxPwm) {
            return e.maxPwm;
        }

        const double t = double(temp - e.minTemp) / double(e.maxTemp - e.minTemp);
        const double pwm = double(e.minPwm) + t * double(e.maxPwm - e.minPwm);
        return std::clamp(static_cast<int>(std::lround(pwm)), 0, 255);
    }

    // Вычисляет PWM в конкретной температурной точке по сырой таблице OFW.
    // Для перекрывающихся строк берём последнюю подходящую, чтобы сохранить
    // поведение более специфичных диапазонов.
    static int evaluateRawTableAtTemp(const std::vector<FanTableEntry>& raw, int temp) {
        int result = 0;
        bool matched = false;

        for (const auto& row : raw) {
            if (!rowMatchesTemp(row, temp)) {
                continue;
            }

            matched = true;
            result = lerpPwm(row, temp);
        }

        if (!matched) {
            if (raw.empty()) {
                return 0;
            }

            if (temp < raw.front().minTemp) {
                return std::clamp(raw.front().minPwm, 0, 255);
            }

            return std::clamp(raw.back().maxPwm, 0, 255);
        }

        return result;
    }

    // Преобразование OFW-таблицы в наши точки 35/40/45/.../80.
    // Внутри UI это хранится как 11 строк:
    // <35, 35-40, 40-45, ..., 75-80, >80
    static std::vector<FanTableEntry> buildUiTableFromRaw(const std::vector<FanTableEntry>& raw) {
        std::array<int, kCurveTemps.size()> points{};

        for (size_t i = 0; i < kCurveTemps.size(); ++i) {
            points[i] = evaluateRawTableAtTemp(raw, kCurveTemps[i]);
        }

        std::vector<FanTableEntry> result;
        result.reserve(11);

        // <35°C
        result.push_back(FanTableEntry{
            TEMP_LOW_SENTINEL,
            kCurveTemps[0],
            points[0],
            points[0]
        });

        // 35-40, 40-45, ..., 75-80
        for (size_t i = 0; i + 1 < kCurveTemps.size(); ++i) {
            result.push_back(FanTableEntry{
                kCurveTemps[i],
                kCurveTemps[i + 1],
                points[i],
                points[i + 1]
            });
        }

        // >80°C
        result.push_back(FanTableEntry{
            kCurveTemps.back(),
            TEMP_HIGH_SENTINEL,
            points.back(),
            points.back()
        });

        return result;
    }

    // Преобразование наших точек обратно в нормализованную OFW-таблицу
    // для записи в system_settings.ini.
    static std::vector<FanTableEntry> buildRawTableFromUi(const std::vector<FanTableEntry>& ui) {
        std::array<int, kCurveTemps.size()> points{};

        if (ui.size() < 11) {
            return {};
        }

        // Берём опорные значения по верхним границам точек:
        // row0 = <35
        // row1 = 35-40
        // ...
        // row9 = 75-80
        points[0] = std::clamp(ui[0].maxPwm, 0, 255);
        for (size_t i = 1; i <= 9; ++i) {
            points[i] = std::clamp(ui[i].maxPwm, 0, 255);
        }

        std::vector<FanTableEntry> raw;
        raw.reserve(11);

        raw.push_back(FanTableEntry{
            TEMP_LOW_SENTINEL,
            TEMP_FIRST,
            points[0],
            points[0]
        });

        for (size_t i = 0; i + 1 < kCurveTemps.size(); ++i) {
            raw.push_back(FanTableEntry{
                kCurveTemps[i],
                kCurveTemps[i + 1],
                points[i],
                points[i + 1]
            });
        }

        raw.push_back(FanTableEntry{
            TEMP_LAST,
            TEMP_HIGH_SENTINEL,
            points.back(),
            points.back()
        });

        return raw;
    }
}

// --- Парсинг ---
std::vector<FanTableEntry> FanSettingsFrame::parseTableString(const std::string& str) {
    std::vector<FanTableEntry> result;

    std::string cleaned = str;
    cleaned.erase(
        std::remove_if(cleaned.begin(), cleaned.end(),
            [](unsigned char ch) { return std::isspace(ch) || ch == '"'; }),
        cleaned.end()
    );

    if (cleaned.rfind("str!", 0) == 0) {
        cleaned.erase(0, 4);
    }

    if (cleaned.empty() || cleaned.front() != '[' || cleaned.back() != ']') {
        return result;
    }

    cleaned = cleaned.substr(1, cleaned.length() - 2);

    std::vector<std::string> entries;
    size_t start = 0, end;
    while ((end = cleaned.find("],[", start)) != std::string::npos) {
        entries.push_back(cleaned.substr(start, end - start));
        start = end + 3;
    }
    entries.push_back(cleaned.substr(start));

    for (const auto& entry : entries) {
        std::stringstream ss(entry);
        std::string token;
        std::vector<int> values;

        while (std::getline(ss, token, ',')) {
            if (!token.empty()) {
                try {
                    values.push_back(std::stoi(token));
                } catch (...) {
                    continue;
                }
            }
        }

        if (values.size() == 4) {
            result.push_back({values[0], values[1], values[2], values[3]});
        }
    }

    return result;
}

// --- Сериализация ---
std::string FanSettingsFrame::serializeTable(const std::vector<FanTableEntry>& table) {
    std::string result = "[";
    for (size_t i = 0; i < table.size(); ++i) {
        const auto& e = table[i];
        char buf[128];
        snprintf(buf, sizeof(buf), "[%d, %d, %d, %d]", e.minTemp, e.maxTemp, e.minPwm, e.maxPwm);
        result += buf;
        if (i != table.size() - 1) {
            result += ", ";
        }
    }
    result += "]";
    return result;
}

// --- Дефолты из OFW-подобной логики ---
std::string FanSettingsFrame::getDefaultHandheldTable() {
    return "[[-1000000, 40000, 0, 0], [36000, 43000, 51, 51], [43000, 48000, 51, 102], [48000, 53000, 102, 153], [53000, 1000000, 153, 153], [48000, 1000000, 153, 153]]";
}

std::string FanSettingsFrame::getDefaultDockedTable() {
    return "[[-1000000, 40000, 0, 0], [36000, 43000, 51, 51], [43000, 53000, 51, 153], [53000, 58000, 153, 255], [58000, 1000000, 255, 255]]";
}

// --- Загрузка из INI ---
void FanSettingsFrame::loadFromIni() {
    FILE* f = fopen(SYSTEM_SETTINGS_PATH, "r");

    std::string handheldStr;
    std::string dockedStr;

    if (!f) {
        handheldStr = getDefaultHandheldTable();
        dockedStr   = getDefaultDockedTable();
    } else {
        char line[1024];
        bool inTcSection = false;

        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            std::string s(line);
            trimInPlace(s);

            if (s.empty()) {
                continue;
            }

            if (s == TC_SECTION) {
                inTcSection = true;
                continue;
            }

            if (inTcSection && !s.empty() && s.front() == '[' && s != TC_SECTION) {
                inTcSection = false;
            }

            if (inTcSection) {
                if (startsWith(s, HANDHELD_KEY)) {
                    size_t eq = s.find('=');
                    if (eq != std::string::npos) {
                        handheldStr = s.substr(eq + 1);
                        trimInPlace(handheldStr);
                    }
                } else if (startsWith(s, DOCKED_KEY)) {
                    size_t eq = s.find('=');
                    if (eq != std::string::npos) {
                        dockedStr = s.substr(eq + 1);
                        trimInPlace(dockedStr);
                    }
                }
            }
        }

        fclose(f);

        if (handheldStr.empty()) {
            handheldStr = getDefaultHandheldTable();
        }

        if (dockedStr.empty()) {
            dockedStr = getDefaultDockedTable();
        }
    }

    const auto handheldRaw = parseTableString(handheldStr);
    const auto dockedRaw   = parseTableString(dockedStr);

    handheldTable = buildUiTableFromRaw(handheldRaw);
    dockedTable   = buildUiTableFromRaw(dockedRaw);
}

// --- Сохранение в INI ---
void FanSettingsFrame::saveToIni() {
    std::vector<std::string> lines;

    FILE* f = fopen(SYSTEM_SETTINGS_PATH, "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            lines.emplace_back(line);
        }
        fclose(f);
    }

    f = fopen(SYSTEM_SETTINGS_PATH, "w");
    if (!f) {
        brls::Application::notify("Failed to open system_settings.ini for writing");
        return;
    }

    const std::vector<FanTableEntry> handheldRaw = buildRawTableFromUi(handheldTable);
    const std::vector<FanTableEntry> dockedRaw   = buildRawTableFromUi(dockedTable);

    bool tcSectionFound = false;
    bool inTcSection = false;
    bool handheldWritten = false;
    bool dockedWritten = false;

    auto writeMissing = [&]() {
        if (!handheldWritten) {
            fprintf(f, "%s = str!\"%s\"\n", HANDHELD_KEY, serializeTable(handheldRaw).c_str());
            handheldWritten = true;
        }
        if (!dockedWritten) {
            fprintf(f, "%s = str!\"%s\"\n", DOCKED_KEY, serializeTable(dockedRaw).c_str());
            dockedWritten = true;
        }
    };

    for (const auto& line : lines) {
        std::string trimmed = line;
        trimInPlace(trimmed);

        if (trimmed == TC_SECTION) {
            if (inTcSection) {
                writeMissing();
            }

            tcSectionFound = true;
            inTcSection = true;
            handheldWritten = dockedWritten = false;

            fprintf(f, "%s", line.c_str());
            continue;
        }

        if (inTcSection && !trimmed.empty() && trimmed.front() == '[' && trimmed != TC_SECTION) {
            writeMissing();
            inTcSection = false;
        }

        if (inTcSection && startsWith(trimmed, HANDHELD_KEY)) {
            if (!handheldWritten) {
                fprintf(f, "%s = str!\"%s\"\n", HANDHELD_KEY, serializeTable(handheldRaw).c_str());
                handheldWritten = true;
            }
            continue;
        }

        if (inTcSection && startsWith(trimmed, DOCKED_KEY)) {
            if (!dockedWritten) {
                fprintf(f, "%s = str!\"%s\"\n", DOCKED_KEY, serializeTable(dockedRaw).c_str());
                dockedWritten = true;
            }
            continue;
        }

        fprintf(f, "%s", line.c_str());
    }

    if (inTcSection) {
        writeMissing();
    }

    if (!tcSectionFound) {
        fprintf(f, "\n%s\n", TC_SECTION);
        fprintf(f, "%s = str!\"%s\"\n", HANDHELD_KEY, serializeTable(handheldRaw).c_str());
        fprintf(f, "%s = str!\"%s\"\n", DOCKED_KEY, serializeTable(dockedRaw).c_str());
    }

    fclose(f);
    brls::Application::notify("Fan settings saved. Reboot for changes to take effect.");
}

// --- Строка таблицы ---
class FanTableRow : public brls::ListItem {
public:
    FanTableRow(const std::string& label, FanTableEntry* tableEntry, FanTableEntry* nextEntry = nullptr)
        : brls::ListItem(label), entry(tableEntry), next(nextEntry), editing(false) {
        updateValue();
    }

    void onFocusGained() override {
        brls::ListItem::onFocusGained();

        registerAction("+", brls::Key::DUP, [this]() {
            if (!editing) return false;
            changeValue(1);
            return true;
        });

        registerAction("-", brls::Key::DDOWN, [this]() {
            if (!editing) return false;
            changeValue(-1);
            return true;
        });

        registerAction("+5", brls::Key::R, [this]() {
            if (!editing) return false;
            changeValue(5);
            return true;
        });

        registerAction("-5", brls::Key::L, [this]() {
            if (!editing) return false;
            changeValue(-5);
            return true;
        });

        registerAction("Edit", brls::Key::A, [this]() {
            editing = true;
            updateValue();
            return true;
        });

        registerAction("CancelEdit", brls::Key::B, [this]() {
            if (editing) {
                editing = false;
                updateValue();
                return true;
            }
            return false;
        });
    }

private:
    FanTableEntry* entry;
    FanTableEntry* next;
    bool editing;

    bool isEdgeRow() const {
        return isLowerEdgeRow(*entry) || isUpperEdgeRow(*entry);
    }

    void updateValue() {
        std::string display;

        if (isEdgeRow()) {
            display = std::to_string(pwmToPercent(entry->maxPwm)) + "%";
        } else {
            display = std::to_string(pwmToPercent(entry->minPwm)) + "% -> " +
                      std::to_string(pwmToPercent(entry->maxPwm)) + "%";
        }

        if (editing) {
            display = "* " + display;
        }

        setValue(display);
    }

    void setPercentValue(int percent) {
        percent = std::clamp(percent, 0, 100);
        int pwm = percentToPwm(percent);

        int lowerBound = entry->minPwm;
        int upperBound = next ? next->maxPwm : 255;

        if (lowerBound > upperBound) {
            upperBound = lowerBound;
        }

        pwm = std::clamp(pwm, lowerBound, upperBound);

        if (isEdgeRow()) {
            entry->minPwm = pwm;
            entry->maxPwm = pwm;
        } else {
            entry->maxPwm = pwm;
            if (next) {
                next->minPwm = pwm;
            }
        }

        updateValue();
    }

    void changeValue(int deltaPercent) {
        int currentPercent = pwmToPercent(entry->maxPwm);
        setPercentValue(currentPercent + deltaPercent);
    }
};

// --- Основное окно ---
void FanSettingsFrame::buildUI() {
    list = new brls::List();

    list->addView(new brls::Header("Handheld Mode"));
    for (size_t i = 0; i < handheldTable.size(); ++i) {
        std::string label = formatRowLabel(handheldTable[i]);
        FanTableEntry* next = (i + 1 < handheldTable.size()) ? &handheldTable[i + 1] : nullptr;
        list->addView(new FanTableRow(label, &handheldTable[i], next));
    }

    list->addView(new brls::Header("Docked Mode"));
    for (size_t i = 0; i < dockedTable.size(); ++i) {
        std::string label = formatRowLabel(dockedTable[i]);
        FanTableEntry* next = (i + 1 < dockedTable.size()) ? &dockedTable[i + 1] : nullptr;
        list->addView(new FanTableRow(label, &dockedTable[i], next));
    }

    auto* saveItem = new brls::ListItem("Save changes");
    saveItem->registerAction("Save", brls::Key::A, [this]() {
        saveToIni();
        return true;
    });
    list->addView(new brls::Header("Actions"));
    list->addView(saveItem);

    this->setContentView(list);
}

FanSettingsFrame::FanSettingsFrame() {
    this->setTitle("Fan Curve Configuration");
    this->setIcon(new brls::MaterialIcon("\uE3E9"));
    loadFromIni();
    buildUI();
}

FanSettingsFrame::~FanSettingsFrame() {}

bool FanSettingsFrame::onCancel() {
    brls::Application::popView();
    return true;
}