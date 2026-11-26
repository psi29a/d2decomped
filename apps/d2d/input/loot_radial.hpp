// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace d2d::client::loot_radial {

inline constexpr int kNoSelection = -1;
inline constexpr std::size_t kSectorsPerPage = 8;
inline constexpr float kPi = 3.14159265358979323846f;

/**
 * Pure input state for a paged loot radial. IDs are copied at construction so a selection
 * remains tied to the item snapshot even if the caller's source list changes.
 */
class Model {
public:
    explicit Model(std::span<const int> candidate_ids) : candidate_ids_(candidate_ids.begin(), candidate_ids.end()) {}

    /** Feed one stick sample. Returns an item ID exactly once, when an armed flick returns to neutral. */
    [[nodiscard]] int step(float x, float y, float outer_threshold = 0.55f, float inner_threshold = 0.30f,
                           float angular_hysteresis = 0.10f);

    /** Move a page explicitly. A page change disarms an in-progress flick until it returns to neutral. */
    void change_page(int direction);

    /** Cancel the current gesture and clear its highlight; the candidate snapshot and page remain. */
    void cancel();

    /** Replace the item snapshot and return the model to its initial page and neutral state. */
    void reset(std::span<const int> candidate_ids);

    [[nodiscard]] int selected_id() const;
    [[nodiscard]] int selected_sector() const { return selected_sector_; }
    [[nodiscard]] std::size_t page() const { return page_; }
    [[nodiscard]] std::size_t page_count() const { return std::max<std::size_t>(1, (candidate_ids_.size() + kSectorsPerPage - 1) / kSectorsPerPage); }
    [[nodiscard]] std::size_t page_size() const;
    [[nodiscard]] bool gesture_active() const { return gesture_active_; }

private:
    [[nodiscard]] bool page_has_items() const { return page_size() != 0; }

    void update_sector(float x, float y, float angular_hysteresis);

    std::vector<int> candidate_ids_;
    std::size_t page_ = 0;
    int selected_sector_ = kNoSelection;
    bool gesture_active_ = false;
    bool commit_armed_ = false;
};

} // namespace d2d::client::loot_radial
