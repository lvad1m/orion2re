#pragma once

#include <string>

#include "schema_model.h"

namespace config::tool {

// The settings catalog for front ends (the launcher): a JSON array, one object per scalar setting in
// schema order. Needs the defaults loaded (native defaults read), like the documentation.
std::string Build_Catalog_Json(const s_schema* schema);

bool Generate_Catalog(const s_schema* schema, const char* output_file, std::string* diagnostic);

}
