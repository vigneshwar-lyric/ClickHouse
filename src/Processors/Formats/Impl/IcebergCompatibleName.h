#pragma once

#include <base/types.h>
#include <cstdio>

namespace DB
{

/// Iceberg sanitizes column names into Avro-compatible identifiers when it writes
/// Parquet/ORC data files: characters that are not valid in an Avro name are escaped
/// as "_x<HEX>" (e.g. a space 0x20 -> "_x20", '.' 0x2E -> "_x2E", '{' -> "_x7B"), and a
/// leading digit is prefixed with '_'. The *logical* column name in Iceberg metadata is
/// kept as-is, so a ClickHouse read that matches by logical name fails to find the
/// physical (sanitized) column and returns NULL. This reproduces Iceberg's sanitizer so
/// the reader can fall back to the physical name.
///
/// Mirrors Apache Iceberg's org.apache.iceberg.avro.AvroSchemaUtil.makeCompatibleName /
/// sanitize. Note: non-ASCII bytes are passed through unchanged, matching Iceberg's
/// Unicode-aware Character.isLetterOrDigit (so e.g. CJK column names are left intact).
/// Non-ASCII *symbols*/emoji are a documented gap (rare as identifiers).
inline String icebergMakeCompatibleName(const String & name)
{
    auto is_valid = [](unsigned char c, bool first) -> bool
    {
        if (c >= 0x80)
            return true; /// non-ASCII: treat as letter (Unicode) and keep, as Iceberg does
        if (c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            return true;
        if (!first && c >= '0' && c <= '9')
            return true;
        return false;
    };

    bool needs_sanitize = false;
    for (size_t i = 0; i < name.size(); ++i)
    {
        if (!is_valid(static_cast<unsigned char>(name[i]), i == 0))
        {
            needs_sanitize = true;
            break;
        }
    }
    if (!needs_sanitize)
        return name;

    String out;
    out.reserve(name.size() + 8);
    for (size_t i = 0; i < name.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (is_valid(c, i == 0))
        {
            out += static_cast<char>(c);
            continue;
        }
        if (c >= '0' && c <= '9') /// only reachable for a leading digit
        {
            out += '_';
            out += static_cast<char>(c);
        }
        else
        {
            /// Integer.toHexString(c).toUpperCase() — uppercase, no zero padding.
            char buf[8];
            std::snprintf(buf, sizeof(buf), "_x%X", static_cast<unsigned>(c));
            out += buf;
        }
    }
    return out;
}

}
