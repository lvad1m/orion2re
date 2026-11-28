#include "generate_catalog.h"

#include <cstdio>

#include "file_util.h"

namespace config::tool {
    namespace {
        std::string Quote_(const std::string& text) {
            std::string out = "\"";
            for (char character : text) {
                switch (character) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(character) < 0x20) {
                            char buffer[8];
                            std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(character));
                            out += buffer;
                        } else {
                            out += character;
                        }
                        break;
                }
            }
            return out + "\"";
        }

        const char* Type_Name_(const s_parameter& parameter) {
            switch (parameter.scalar_kind) {
                case SCALAR_ENUM: return "enum";
                case SCALAR_MASK: return "mask";
                case SCALAR_BITMAP: return "bitmap";
                case SCALAR_RATIO: return "ratio";
                case SCALAR_PRIMITIVE: break;
            }
            switch (parameter.type) {
                case PRIMITIVE_BOOL: return "bool";
                case PRIMITIVE_STRING: return "string";
                case PRIMITIVE_INT8:
                case PRIMITIVE_UINT8:
                case PRIMITIVE_INT16:
                case PRIMITIVE_UINT16:
                case PRIMITIVE_INT32:
                case PRIMITIVE_UINT32: return "int";
                default: return "other";
            }
        }

        std::string Default_Json_(const s_parameter& parameter) {
            switch (parameter.scalar_kind) {
                case SCALAR_ENUM:
                    for (const s_symbol& symbol : parameter.values) {
                        if (symbol.value == parameter.default_integer) return Quote_(symbol.name);
                    }
                    return std::to_string(parameter.default_integer);
                case SCALAR_MASK: return std::to_string(parameter.default_integer);
                case SCALAR_BITMAP: return "null";
                case SCALAR_RATIO:
                    return Quote_(std::to_string(parameter.default_ratio_numerator) + "/" +
                        std::to_string(parameter.default_ratio_denominator));
                case SCALAR_PRIMITIVE: break;
            }
            if (parameter.type == PRIMITIVE_BOOL) return parameter.default_bool ? "true" : "false";
            if (parameter.type == PRIMITIVE_STRING) return Quote_(parameter.default_string);
            return std::to_string(parameter.default_integer);
        }

        std::string Category_(const s_schema* schema, const std::string& name_space, const std::string& group) {
            for (const s_documentation_category& category : schema->documentation_categories) {
                for (const std::string& candidate : category.namespaces) {
                    if (candidate == name_space) return category.id;
                }
            }
            return group;
        }
    } // namespace

    std::string Build_Catalog_Json(const s_schema* schema) {
        std::string out = "[\n";
        bool first = true;
        for (const s_parameter& parameter : schema->parameters) {
            if (parameter.aggregate || parameter.vector) continue;
            const size_t dot = parameter.path.find('.');
            const std::string name_space = dot == std::string::npos ? "" : parameter.path.substr(0, dot);
            const std::string name = dot == std::string::npos ? parameter.path : parameter.path.substr(dot + 1);
            if (name.find('.') != std::string::npos) continue;  // a leaf of a record or array
            if (!first) out += ",\n";
            first = false;
            out += "  {\"namespace\": " + Quote_(name_space);
            out += ", \"name\": " + Quote_(name);
            out += ", \"group\": " + Quote_(parameter.group);
            out += ", \"category\": " + Quote_(Category_(schema, name_space, parameter.group));
            out += ", \"title\": " + Quote_(parameter.metadata.title);
            out += ", \"description\": " + Quote_(parameter.metadata.description);
            out += ", \"remarks\": " + Quote_(parameter.metadata.remarks);
            out += std::string(", \"type\": ") + Quote_(Type_Name_(parameter));
            out += ", \"default\": " + Default_Json_(parameter);
            if (parameter.scalar_kind == SCALAR_ENUM || parameter.scalar_kind == SCALAR_MASK) {
                out += ", \"values\": [";
                const std::vector<s_symbol>& symbols =
                    parameter.scalar_kind == SCALAR_ENUM ? parameter.values : parameter.flags;
                for (size_t index = 0; index < symbols.size(); ++index) {
                    if (index != 0) out += ", ";
                    out += "{\"name\": " + Quote_(symbols[index].name) +
                        ", \"title\": " + Quote_(symbols[index].description) + "}";
                }
                out += "]";
            }
            if (parameter.scalar_kind == SCALAR_PRIMITIVE && parameter.type != PRIMITIVE_BOOL &&
                parameter.type != PRIMITIVE_STRING) {
                if (parameter.constraints.has_min) out += ", \"min\": " + std::to_string(parameter.constraints.min);
                if (parameter.constraints.has_max) out += ", \"max\": " + std::to_string(parameter.constraints.max);
            }
            out += "}";
        }
        out += "\n]\n";
        return out;
    }

    bool Generate_Catalog(const s_schema* schema, const char* output_file, std::string* diagnostic) {
        if (schema == nullptr || output_file == nullptr || diagnostic == nullptr) return false;
        for (const s_parameter& parameter : schema->parameters) {
            if (parameter.default_from_native) {
                *diagnostic = parameter.path + ": native default value was not loaded";
                return false;
            }
        }
        return Write_File_If_Different(output_file, Build_Catalog_Json(schema), diagnostic);
    }

} // namespace config::tool
