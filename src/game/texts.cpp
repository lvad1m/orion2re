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
        // The text with its {name} placeholders replaced; `lookup` gives the value of a name or nullptr.
        template <typename Lookup>
        std::string Substitute_(std::string_view text, Lookup lookup) {
            std::string result;
            for (size_t i = 0; i < text.size();) {
                if (text[i] == '{') {
                    const size_t close = text.find('}', i + 1);
                    if (close != std::string_view::npos) {
                        if (const std::string* value = lookup(text.substr(i + 1, close - i - 1))) {
                            result += *value;
                            i = close + 1;
                            continue;
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
        const std::string_view text = Get(key);
        std::string result;
        for (size_t i = 0; i < text.size();) {
            if (text[i] == '{') {
                const size_t close = text.find('}', i + 1);
                if (close != std::string_view::npos) {
                    const std::string_view name = text.substr(i + 1, close - i - 1);
                    const std::pair<const char*, std::string>* hit = nullptr;
                    for (const auto& arg : args) {
                        if (name == arg.first) {
                            hit = &arg;
                            break;
                        }
                    }
                    if (hit) {
                        result += hit->second;
                        i = close + 1;
                        continue;
                    }
                }
            }
            result += text[i++];
        }
        const size_t n = std::min(result.size(), size - 1);
        memcpy(out, result.data(), n);
        out[n] = '\0';
    }

} // namespace texts
