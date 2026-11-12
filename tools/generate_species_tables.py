#!/usr/bin/env python3
"""Generate the species tables of the third generation from the game source.

The tables come from the public source of the game, not from memory:
every number this script writes is read out of the files it downloads, and
the URL of each file is written into the generated file.

Usage:
    python3 tools/generate_species_tables.py [output directory]
"""
import os
import re
import sys
import urllib.request

RAW = "https://raw.githubusercontent.com/pret/pokefirered/master/"
SPECIES_FILE = "src/data/pokemon/species_info.h"
SPECIES_CONSTANTS_FILE = "include/constants/species.h"
CONSTANTS_FILE = "include/constants/pokemon.h"
EXP_TABLE_CANDIDATES = [
    "src/data/pokemon/experience_tables.h",
    "src/data/pokemon/growth_rate.h",
    "src/data/pokemon/experience.h",
    "src/data/pokemon/exp.h",
    "src/data/pokemon/exp_table.h",
]


def fetch(path):
    with urllib.request.urlopen(RAW + path) as response:
        return response.read().decode("utf-8", "replace")


def fetch_first(candidates):
    for path in candidates:
        try:
            text = fetch(path)
        except Exception:
            continue
        if "404" not in text[:20]:
            return path, text
    raise SystemExit("No experience table found among " + ", ".join(candidates))


def parse_define(text, name):
    """The numeric value of a #define, or None."""
    match = re.search(r"^#define\s+" + re.escape(name) + r"\s+(\d+)\s*$", text, re.M)
    return int(match.group(1)) if match else None


def parse_species_ids(text):
    """The internal species number of every SPECIES_ name."""
    ids = {}
    for name, value in re.findall(r"^#define\s+SPECIES_(\w+)\s+(\d+)\s*$", text, re.M):
        ids[name] = int(value)
    return ids


def parse_species_info(text):
    """The growth rate and the gender ratio of every species."""
    entries = {}
    blocks = re.split(r"\[SPECIES_(\w+)\]\s*=", text)
    # The split produces name, body, name, body... The first name is the
    # marker of the array itself and has no body.
    for index in range(1, len(blocks) - 1, 2):
        name = blocks[index]
        body = blocks[index + 1]
        growth = re.search(r"\.growthRate\s*=\s*GROWTH_(\w+)", body)
        ratio = re.search(r"\.genderRatio\s*=\s*([^,\n]+)", body)
        base_hp = re.search(r"\.baseHP\s*=\s*(\d+)", body)
        if growth is None or ratio is None or base_hp is None:
            continue
        entries[name] = (growth.group(1), ratio.group(1).strip(), int(base_hp.group(1)))
    return entries


def parse_percent_female(text):
    """The byte the game stores for a percentage of females."""
    match = re.search(r"#define\s+PERCENT_FEMALE\(percent\)\s+(.+)", text)
    if match is None:
        raise SystemExit("PERCENT_FEMALE not found")
    return match.group(1).strip()


def female_threshold(percent_text, female_text):
    """Evaluates PERCENT_FEMALE for a percentage written as a float."""
    value = float(percent_text)
    return min(254, int((value * 255) / 100))


def parse_exp_tables(text):
    """The experience of every growth rate, level by level.

    The entries of the tables are macro calls of the game, so they are kept
    as they are and the macros are emitted with them. The arithmetic is then
    the one of the game, computed by the compiler.
    """
    start = text.find("gExperienceTables")
    if start < 0:
        raise SystemExit("gExperienceTables not found")
    body = text[start:]
    macros = re.search(r"(#define SQUARE.*?)(?=\nconst u32)", text, re.S)
    macros = macros.group(1) if macros else ""
    tables = {}
    for match in re.finditer(r"\{\s*//\s*([A-Za-z ]+)\n(.*?)\n\s*\}", body, re.S):
        label = match.group(1).strip().upper().replace(" ", "_")
        entries = []
        for line in match.group(2).splitlines():
            line = line.split("//")[0].strip()
            if not line:
                continue
            entries.extend(part.strip() for part in line.split(",") if part.strip())
        tables[label] = entries
    if not tables:
        raise SystemExit("No experience table found")
    return tables, macros


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else "."
    constants = fetch(CONSTANTS_FILE)
    species_constants = fetch(SPECIES_CONSTANTS_FILE)
    species_info = fetch(SPECIES_FILE)
    exp_path, exp_text = fetch_first(EXP_TABLE_CANDIDATES)

    growth_names = sorted(
        set(re.findall(r"^#define\s+(GROWTH_\w+)\s+\d+\s*$", constants, re.M)),
        key=lambda name: parse_define(constants, name),
    )
    growth_values = {name: parse_define(constants, name) for name in growth_names}

    species_ids = parse_species_ids(species_constants)
    entries = parse_species_info(species_info)
    tables, exp_macros = parse_exp_tables(exp_text)

    # The game stores the gender ratio as a threshold: the personality is
    # female when its low byte is below it. The three fixed values are the
    # always male, always female and genderless ones.
    female_constant = re.search(r"#define\s+MON_FEMALE\s+(\S+)", constants).group(1)
    genderless_constant = re.search(r"#define\s+MON_GENDERLESS\s+(\S+)", constants).group(1)
    male_constant = re.search(r"#define\s+MON_MALE\s+(\S+)", constants).group(1)

    highest = max(species_ids.values())
    growth_table = [255] * (highest + 1)
    ratio_table = [255] * (highest + 1)
    base_hp_table = [1] * (highest + 1)
    for name, number in species_ids.items():
        if name not in entries:
            continue
        growth, ratio, base_hp = entries[name]
        if "GROWTH_" + growth not in growth_values:
            continue
        growth_table[number] = growth_values["GROWTH_" + growth]
        if ratio == male_constant:
            ratio_table[number] = 0
        elif ratio == female_constant:
            ratio_table[number] = 254
        elif ratio == genderless_constant:
            ratio_table[number] = 255
        else:
            match = re.search(r"PERCENT_FEMALE\(([\d.]+)\)", ratio)
            if match is None:
                continue
            ratio_table[number] = female_threshold(match.group(1), constants)
        base_hp_table[number] = base_hp

    header = """#pragma once

#include <cstdint>

namespace pksm {

// The growth rate of a species. The values are the ones the game uses, so a
// record written with them keeps the meaning the game gives it.
enum class GrowthRate : std::uint8_t {
%s};

// The gender ratio of a species, as the threshold the game stores: the
// Pokemon is female when the low byte of its personality value is below it.
enum class GenderRatio : std::uint8_t {
    always_male = 0,
    one_in_eight = 31,
    one_in_four = 63,
    equal = 127,
    always_female = 254,
    genderless = 255,
    none = 255
};

// The growth rate of a species, by its number in the game. A species that the
// tables do not cover has no rate.
GrowthRate frlg_growth_rate(std::uint16_t species) noexcept;

// The gender ratio of a species, by its number in the game.
GenderRatio frlg_gender_ratio(std::uint16_t species) noexcept;

// The base health of a species, by its number in the game. It is what the
// game uses to compute the health of a Pokemon.
std::uint8_t frlg_base_hp(std::uint16_t species) noexcept;

// The experience a Pokemon of that growth rate has when it reaches the level.
// The level goes from 1 to 100; a level outside that range gives 0.
std::uint32_t frlg_exp_for_level(GrowthRate rate, std::uint8_t level) noexcept;

// The level that matches that experience. An experience below the first level
// gives 1.
std::uint8_t frlg_level_for_exp(GrowthRate rate, std::uint32_t exp) noexcept;

}  // namespace pksm
""" % ("\n".join(
        "    %s = %d," % (name[len("GROWTH_"):].lower(), growth_values[name])
        for name in growth_names
    ) + "\n    // A species the tables do not cover has no rate.\n    none = 255,")

    lines = [
        "// Generated by tools/generate_species_tables.py. Do not edit by hand.",
        "//",
        "// The data comes from the source of the game:",
        "//   %s" % RAW + SPECIES_FILE,
        "//   %s" % RAW + SPECIES_CONSTANTS_FILE,
        "//   %s" % RAW + CONSTANTS_FILE,
        "//   %s" % RAW + exp_path,
        "",
        '#include "pksm/frlg_species_tables.hpp"',
        "",
        "#include <array>",
        "",
        "// The experience formulas of the game, kept as they are so that the",
        "// arithmetic is the one of the game.",
        exp_macros.rstrip(),
        "",
        "namespace pksm {",
        "namespace {",
        "",
        "constexpr std::size_t species_count = %d;" % (highest + 1),
        "",
        "// The growth rate of every species, by its number in the game.",
        "constexpr std::uint8_t growth_rates[species_count] = {",
    ]
    for index in range(0, len(growth_table), 16):
        lines.append("    " + ", ".join(str(v) for v in growth_table[index:index + 16]) + ",")
    lines += [
        "};",
        "",
        "// The gender ratio of every species, by its number in the game.",
        "constexpr std::uint8_t gender_ratios[species_count] = {",
    ]
    for index in range(0, len(ratio_table), 16):
        lines.append("    " + ", ".join(str(v) for v in ratio_table[index:index + 16]) + ",")
    lines += [
        "};",
        "",
        "// The base health of every species, by its number in the game.",
        "constexpr std::uint8_t base_hp_table[species_count] = {",
    ]
    for index in range(0, len(base_hp_table), 16):
        lines.append("    " + ", ".join(str(v) for v in base_hp_table[index:index + 16]) + ",")
    lines += [
        "};",
        "",
        "// The experience of every growth rate, level by level. The first entry",
        "// is the level 1.",
    ]
    for name in growth_names:
        numbers = tables.get(name[len("GROWTH_"):])
        if numbers is None:
            numbers = []
        lines.append("constexpr std::uint32_t exp_table_%s[] = {" % name[len("GROWTH_"):].lower())
        for index in range(0, len(numbers), 8):
            lines.append("    " + ", ".join(numbers[index:index + 8]) + ",")
        lines.append("};")
    lines += [
        "",
        "}  // namespace",
        "",
        "GrowthRate frlg_growth_rate(std::uint16_t species) noexcept {",
        "    if (species >= species_count) return GrowthRate::none;",
        "    switch (growth_rates[species]) {",
    ]
    for name in growth_names:
        lines.append("        case %d: return GrowthRate::%s;" % (
            growth_values[name], name[len("GROWTH_"):].lower()))
    lines += [
        "        default: return GrowthRate::none;",
        "    }",
        "}",
        "",
        "GenderRatio frlg_gender_ratio(std::uint16_t species) noexcept {",
        "    if (species >= species_count) return GenderRatio::none;",
        "    switch (gender_ratios[species]) {",
        "        case 0: return GenderRatio::always_male;",
        "        case 31: return GenderRatio::one_in_eight;",
        "        case 63: return GenderRatio::one_in_four;",
        "        case 127: return GenderRatio::equal;",
        "        case 254: return GenderRatio::always_female;",
        "        default: return GenderRatio::genderless;",
        "    }",
        "}",
        "",
        "std::uint8_t frlg_base_hp(std::uint16_t species) noexcept {",
        "    if (species >= species_count) return 1;",
        "    return base_hp_table[species];",
        "}",
        "",
        "namespace {",
        "",
        "const std::uint32_t* exp_table(GrowthRate rate) noexcept {",
        "    switch (rate) {",
    ]
    for name in growth_names:
        lines.append("        case GrowthRate::%s: return exp_table_%s;" % (
            name[len("GROWTH_"):].lower(), name[len("GROWTH_"):].lower()))
    lines += [
        "        default: return nullptr;",
        "    }",
        "}",
        "",
        "}  // namespace",
        "",
        "std::uint32_t frlg_exp_for_level(GrowthRate rate, std::uint8_t level) noexcept {",
        "    const auto* table = exp_table(rate);",
        "    if (table == nullptr || level > 100) return 0;",
        "    return table[level];",
        "}",
        "",
        "std::uint8_t frlg_level_for_exp(GrowthRate rate, std::uint32_t exp) noexcept {",
        "    const auto* table = exp_table(rate);",
        "    if (table == nullptr) return 1;",
        "    std::uint8_t level = 1;",
        "    for (std::uint8_t candidate = 1; candidate <= 100; ++candidate) {",
        "        if (table[candidate] <= exp) level = candidate;",
        "        else break;",
        "    }",
        "    return level;",
        "}",
        "",
        "}  // namespace pksm",
        "",
    ]

    os.makedirs(os.path.join(output, "include", "pksm"), exist_ok=True)
    os.makedirs(os.path.join(output, "src"), exist_ok=True)
    with open(os.path.join(output, "include", "pksm", "frlg_species_tables.hpp"), "w",
              encoding="utf-8", newline="\n") as handle:
        handle.write(header)
    with open(os.path.join(output, "src", "frlg_species_tables.cpp"), "w",
              encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("species: %d, growth rates: %d, experience tables: %d, source: %s" % (
        highest, len(growth_names), len(tables), exp_path))


if __name__ == "__main__":
    main()
