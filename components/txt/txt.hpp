// D2Decomp reader for D2's excel .txt tables.
//
// Tab-separated, first row = column names, CRLF line ends, Latin-1. 1.14d
// ships the .txt next to the compiled .bin in the MPQs. Rows named
// "Expansion" are section separators the game drops when compiling most
// tables (they'd shift every index after them), so we drop them too, along
// with blank-first-cell rows. Magic affixes are the exception: their IDs
// count every data row, the blank "none" row 0 and the separators
// included — pass keep_all.
#pragma once

#include <cctype>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::txt {

class Table {
public:
    Table() = default;
    explicit Table(std::span<const std::byte> bytes, bool keep_all = false) {
        std::string_view all(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        bool header = true;
        while (!all.empty()) {
            const auto newline = all.find('\n');
            auto line = all.substr(0, newline);
            all = newline == all.npos ? std::string_view{} : all.substr(newline + 1);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (line.empty()) continue;
            std::vector<std::string> cells;
            for (std::size_t at = 0;;) {
                const auto tab = line.find('\t', at);
                cells.emplace_back(line.substr(at, tab == line.npos ? line.npos : tab - at));
                if (tab == line.npos) break;
                at = tab + 1;
            }
            if (header) { cols_ = std::move(cells); header = false; continue; }
            if (!keep_all && (cells[0] == "Expansion" || cells[0].empty())) continue;
            rows_.push_back(std::move(cells));
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }

    // Column index by name, case-insensitive (weapons.txt says
    // "alternateGfx", armor.txt "alternategfx").
    [[nodiscard]] std::optional<std::size_t> col(std::string_view name) const {
        for (std::size_t i = 0; i < cols_.size(); ++i) {
            if (cols_[i].size() != name.size()) continue;
            bool equal = true;
            for (std::size_t k = 0; k < name.size() && equal; ++k)
                equal = std::tolower((unsigned char)cols_[i][k]) == std::tolower((unsigned char)name[k]);
            if (equal) return i;
        }
        return std::nullopt;
    }

    // Cell text, "" for a missing column or short row.
    [[nodiscard]] std::string_view get(std::size_t row, std::optional<std::size_t> column) const {
        if (!column || row >= rows_.size() || *column >= rows_[row].size()) return {};
        return rows_[row][*column];
    }
    [[nodiscard]] std::string_view get(std::size_t row, std::string_view name) const {
        return get(row, col(name));
    }

private:
    std::vector<std::string>              cols_;
    std::vector<std::vector<std::string>> rows_;
};

}  // namespace d2d::txt
