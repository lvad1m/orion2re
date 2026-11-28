#include "pch.h"

#include "texts.h"

#include "text_language.h"
#include "vfs.h"
#include "yyjson.h"

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace texts {

    namespace {
        using t_table = std::map<std::string, std::string, std::less<>>;
        t_table english;
        t_table russian;
        t_table russian_utf8;  // the same Russian texts as written, for the Unicode windows
        std::set<std::string> reported;  // missing keys already logged
        bool loaded = false;

        void Report_Missing_(const char* key) {
            if (reported.insert(key).second) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "texts: no text for key %s", key);
            }
        }

        // Flattens the strings of `value` under `prefix` into `out`; other value types are skipped with a warning.
        void Flatten_(yyjson_val* value, const std::string& prefix, t_table* out, const char* origin) {
            if (yyjson_is_obj(value)) {
                size_t i, count;
                yyjson_val *key, *child;
                yyjson_obj_foreach(value, i, count, key, child) {
                    std::string name = prefix;
                    if (!name.empty()) name += '.';
                    name.append(yyjson_get_str(key), yyjson_get_len(key));
                    Flatten_(child, name, out, origin);
                }
            } else if (yyjson_is_str(value)) {
                (*out)[prefix].assign(yyjson_get_str(value), yyjson_get_len(value));
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "texts: %s: %s is not a string, skipped", origin, prefix.c_str());
            }
        }

        const char* Find_(const t_table& table, const char* key) {
            const auto found = table.find(std::string_view(key));
            return found == table.end() ? nullptr : found->second.c_str();
        }
    } // namespace

    bool Load_Source(int language, std::string_view json5, const char* origin) {
        loaded = true;
        std::string source(json5);
        yyjson_read_err error{};
        std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
            yyjson_read_opts(source.data(), source.size(), YYJSON_READ_JSON5, nullptr, &error), yyjson_doc_free);
        if (!doc || !yyjson_is_obj(yyjson_doc_get_root(doc.get()))) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "texts: %s: not a JSON5 object of texts%s%s (at byte %d), file skipped", origin,
                doc ? "" : ": ", doc ? "" : error.msg, doc ? 0 : (int)error.pos);
            return false;
        }
        t_table flat;
        Flatten_(yyjson_doc_get_root(doc.get()), std::string(), &flat, origin);
        const bool russian_text = language == text_language::LANGUAGE_RU;
        for (auto& entry : flat) {
            std::string bytes = entry.second;
            if (russian_text && !text_language::From_Utf8(entry.second, &bytes)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "texts: %s: %s has a character outside CP866", origin, entry.first.c_str());
            }
            if (russian_text) russian_utf8[entry.first] = entry.second;
            (russian_text ? russian : english)[entry.first] = std::move(bytes);
        }
        return true;
    }

    void Load() {
        if (loaded) return;
        loaded = true;
        static const struct { int language; const char* path; } files[] = {
            {text_language::LANGUAGE_EN, "texts/en.json5"},
            {text_language::LANGUAGE_RU, "texts/ru.json5"},
        };
        for (const auto& file : files) {
            // Layers come from the highest priority down; merge the lowest first so a higher layer wins.
            std::vector<std::string> layers;
            size_t cursor = 0;
            for (s_vfs_file* f = vfs::fopen_next_read(file.path, &cursor); f; f = vfs::fopen_next_read(file.path, &cursor)) {
                const int32_t size = vfs::fsize(f);
                if (size > 0 && size <= 4 * 1024 * 1024) {
                    std::string data(static_cast<size_t>(size), '\0');
                    if (vfs::fread(data.data(), 1, data.size(), f) == data.size()) layers.push_back(std::move(data));
                }
                vfs::fclose(f);
            }
            if (layers.empty() && file.language == text_language::LANGUAGE_EN) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "texts: %s not found, texts fall back to their keys", file.path);
            }
            for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) Load_Source(file.language, *layer, file.path);
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "texts: %d English and %d Russian texts, language %d", (int)english.size(), (int)russian.size(),
            (int)text_language::language);
    }

    void Reset() {
        english.clear();
        russian.clear();
        russian_utf8.clear();
        reported.clear();
        loaded = false;
    }

    const char* Get(const char* key) {
        if (!key) return "";
        if (!loaded) Load();
        if (text_language::language == text_language::LANGUAGE_RU) {
            if (const char* text = Find_(russian, key)) return text;
        }
        if (const char* text = Find_(english, key)) return text;
        Report_Missing_(key);
        return key;
    }

    const char* Get_Utf8(const char* key) {
        if (!key) return "";
        if (!loaded) Load();
        if (text_language::language == text_language::LANGUAGE_RU) {
            if (const char* text = Find_(russian_utf8, key)) return text;
        }
        if (const char* text = Find_(english, key)) return text;
        Report_Missing_(key);
        return key;
    }

    namespace {
        // The plural form index for the integer n: Russian 0 (1, 21), 1 (2-4, 22-24), 2 (the rest); others 0 (|n| == 1) or 1.
        size_t Plural_Index_(long long n) {
            const unsigned long long a = n < 0 ? 0ULL - static_cast<unsigned long long>(n) : static_cast<unsigned long long>(n);
            if (text_language::language == text_language::LANGUAGE_RU) {
                if (a % 10 == 1 && a % 100 != 11) return 0;
                if (a % 10 >= 2 && a % 10 <= 4 && (a % 100 < 12 || a % 100 > 14)) return 1;
                return 2;
            }
            return a == 1 ? 0 : 1;
        }

        // "{name|form1|form2|...}": the value, a space and the form for the value; false when the value is not an integer
        // (then the last form is used, still with the value).
        std::string Plural_(const std::string& value, const std::vector<std::string_view>& forms) {
            bool integer = !value.empty();
            size_t start = (integer && (value[0] == '-' || value[0] == '+')) ? 1 : 0;
            if (start == value.size()) integer = false;
            for (size_t k = start; integer && k < value.size(); ++k) integer = value[k] >= '0' && value[k] <= '9';
            size_t index = forms.size() - 1;
            if (integer && value.size() - start <= 18) {
                const size_t wanted = Plural_Index_(std::stoll(value));
                if (wanted < index) index = wanted;
            }
            std::string result = value;
            result += ' ';
            result += forms[index];
            return result;
        }

        // The text with its {name} and {name|form|form} placeholders replaced; `lookup` gives the value of a name or nullptr.
        template <typename Lookup>
        std::string Substitute_(std::string_view text, Lookup lookup) {
            std::string result;
            for (size_t i = 0; i < text.size();) {
                if (text[i] == '{') {
                    const size_t close = text.find('}', i + 1);
                    if (close != std::string_view::npos) {
                        const std::string_view body = text.substr(i + 1, close - i - 1);
                        const size_t bar = body.find('|');
                        if (bar == std::string_view::npos) {
                            if (const std::string* value = lookup(body)) {
                                result += *value;
                                i = close + 1;
                                continue;
                            }
                        } else if (body.find('{') == std::string_view::npos) {
                            if (const std::string* value = lookup(body.substr(0, bar))) {
                                std::vector<std::string_view> forms;
                                for (size_t from = bar + 1;;) {
                                    const size_t next = body.find('|', from);
                                    forms.push_back(body.substr(from, next == std::string_view::npos ? std::string_view::npos : next - from));
                                    if (next == std::string_view::npos) break;
                                    from = next + 1;
                                }
                                result += Plural_(*value, forms);
                                i = close + 1;
                                continue;
                            }
                        }
                    }
                }
                result += text[i++];
            }
            return result;
        }

        void Copy_Cut_(char* out, size_t size, const std::string& result) {
            const size_t n = std::min(result.size(), size - 1);
            memcpy(out, result.data(), n);
            out[n] = '\0';
        }
    } // namespace

    std::string Expand(const char* key, const t_args& args, bool utf8) {
        const std::string_view text = utf8 ? Get_Utf8(key) : Get(key);
        return Substitute_(text, [&](std::string_view name) -> const std::string* {
            for (const auto& arg : args) {
                if (name == arg.first) return &arg.second;
            }
            return nullptr;
        });
    }

    void Format_Utf8(char* out, size_t size, const char* key, std::initializer_list<std::pair<const char*, std::string>> args) {
        if (!out || size == 0) return;
        const t_args list(args.begin(), args.end());
        Copy_Cut_(out, size, Expand(key, list, true));
    }

    void Format(char* out, size_t size, const char* key, std::initializer_list<std::pair<const char*, std::string>> args) {
        if (!out || size == 0) return;
        const t_args list(args.begin(), args.end());
        Copy_Cut_(out, size, Expand(key, list, false));
    }

} // namespace texts
