// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/loot_radial.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace d2d::client::loot_radial {

int Model::step(float x, float y, float outer_threshold, float inner_threshold,
                       float angular_hysteresis) {
    if (!std::isfinite(x) || !std::isfinite(y)) { cancel(); return kNoSelection; }
    const float magnitude = std::hypot(x, y);
    if (gesture_active_) {
        if (magnitude <= inner_threshold) {
            gesture_active_ = false;
            const int committed = commit_armed_ ? selected_id() : kNoSelection;
            commit_armed_ = false;
            selected_sector_ = kNoSelection;
            return committed;
        }
        if (commit_armed_ && magnitude > inner_threshold) update_sector(x, y, angular_hysteresis);
        return kNoSelection;
    }
    if (magnitude < outer_threshold) return kNoSelection;
    gesture_active_ = true;
    commit_armed_ = page_has_items();
    selected_sector_ = kNoSelection;
    update_sector(x, y, angular_hysteresis);
    return kNoSelection;
}

void Model::change_page(int direction) {
    if (direction == 0 || page_count() <= 1) return;
    const auto pages = page_count();
    const auto current = page_;
    page_ = direction > 0 ? (current + 1) % pages : (current + pages - 1) % pages;
    selected_sector_ = kNoSelection;
    if (gesture_active_) commit_armed_ = false;
}

void Model::cancel() {
    // Keep consuming an already-active flick until neutral, but never commit it.
    commit_armed_ = false;
    selected_sector_ = kNoSelection;
}

void Model::reset(std::span<const int> candidate_ids) {
    candidate_ids_.assign(candidate_ids.begin(), candidate_ids.end());
    page_ = 0;
    gesture_active_ = false;
    commit_armed_ = false;
    selected_sector_ = kNoSelection;
}

int Model::selected_id() const {
    const auto index = page_ * kSectorsPerPage + static_cast<std::size_t>(std::max(selected_sector_, 0));
    return selected_sector_ >= 0 && index < candidate_ids_.size() ? candidate_ids_[index] : kNoSelection;
}

std::size_t Model::page_size() const {
    const auto start = page_ * kSectorsPerPage;
    return start >= candidate_ids_.size() ? 0 : std::min(kSectorsPerPage, candidate_ids_.size() - start);
}

void Model::update_sector(float x, float y, float angular_hysteresis) {
    const auto count = page_size();
    if (count == 0) { selected_sector_ = kNoSelection; return; }
    constexpr float tau = 2.0f * kPi;
    float angle = std::atan2(y, x) + kPi / 2.0f;
    if (angle < 0.0f) angle += tau;
    const float sector_width = tau / static_cast<float>(count);
    const float position = angle / sector_width;
    const int candidate = static_cast<int>(std::floor(position + 0.5f)) % static_cast<int>(count);
    if (selected_sector_ >= 0 && selected_sector_ < static_cast<int>(count)) {
        const float current_center = static_cast<float>(selected_sector_) * sector_width;
        float distance = std::fabs(angle - current_center);
        distance = std::min(distance, tau - distance);
        if (distance <= sector_width / 2.0f + angular_hysteresis) return;
    }
    selected_sector_ = candidate;
}

} // namespace d2d::client::loot_radial
