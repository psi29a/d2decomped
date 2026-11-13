// SPDX-License-Identifier: GPL-3.0-or-later
// One automap list's cell tree (UI\automap.cpp; docs/research/re/automap.md
// "The .ma file"): FUN_00457b00's AVL insert of 0x14-byte cells keyed by y,
// then x. At the same (x, y) a new cel whose group (0x7a3150, from the pairs
// at 0x711258) is -1, or the group of the cell it meets there, is dropped;
// otherwise the cel orders it. Only the cells on the search path are met, so
// which of two same-group cels at one (x, y) stays depends on the insert
// order and the tree's shape (a plain "any cell here" set differs from it).
// Checked against game.exe by tools/emu/automap_tree.py.
#pragma once

#include <cstddef>
#include <vector>

namespace d2d::rules {

// The {cel, group} pairs at 0x711258..0x7113e0 that FUN_0045a4c0 puts in the
// table at 0x7a3150 (every other cel -1).
inline constexpr int kAutomapCelGroups[][2] = {
    {0,0},{1,0},{2,0},{3,0},{6,1},{7,1},{8,1},{11,2},{12,2},{13,3},{14,3},{20,4},{38,4},{21,5},{39,5},{46,6},{47,6},
    {48,6},{49,6},{51,7},{52,7},{53,7},{54,7},{60,8},{70,8},{61,9},{71,9},{120,10},{169,10},{171,10},{121,11},{170,11},
    {172,11},{257,12},{258,12},{259,12},{266,13},{267,13},{337,14},{338,14},{472,15},{473,15},{474,15},{475,15},
    {520,16},{521,16},{522,16},{533,17},{534,17} };

// FUN_0045a4c0's table at `cel`: its group, -1 for none (or past 0x7ff).
inline int automap_cel_group(int cel) {
    for (const auto& pair : kAutomapCelGroups)
        if (pair[0] == cel) return pair[1];
    return -1;
}

struct AutomapNode {
    int cel = 0, cell_x = 0, cell_y = 0, group = -1;   // FUN_00457b00 reads +4 cel, +6 x, +8 y, and the group table at the cel
    int balance = 0;                          // +0xa: -1 left deeper, 0, 1 right deeper
    int left = -1, right = -1;                // +0xc / +0x10: node indices, -1 none
};

class AutomapTree {
public:
    // FUN_00457b00: puts the cell in unless it is dropped; true when it went in.
    // `group` is the cel's group (-1 none).
    bool insert(int cel, int cell_x, int cell_y, int group) {
        const int made = int(nodes_.size());
        nodes_.push_back({ cel, cell_x, cell_y, group, 0, -1, -1 });
        bool grew = false;
        if (!place(root_, made, grew)) {
            nodes_.pop_back();
            return false;
        }
        return true;
    }
    // The cells in order (FUN_00458470's walk: y, x, cel).
    [[nodiscard]] std::vector<AutomapNode> in_order() const {
        std::vector<AutomapNode> out;
        walk(root_, out);
        return out;
    }
    [[nodiscard]] std::size_t size() const { return nodes_.size(); }
    // FUN_00458680: every node freed.
    void clear() { nodes_.clear(); root_ = -1; }

private:
    std::vector<AutomapNode> nodes_;
    int root_ = -1;

    void walk(int slot, std::vector<AutomapNode>& out) const {
        if (slot < 0) return;
        walk(nodes_[std::size_t(slot)].left, out);
        out.push_back(nodes_[std::size_t(slot)]);
        walk(nodes_[std::size_t(slot)].right, out);
    }
    // FUN_00457860 / FUN_004578b0: the right child (left child) up over `slot`.
    void rotate_right(int& slot) {
        const int top = nodes_[std::size_t(slot)].left;
        nodes_[std::size_t(slot)].left = nodes_[std::size_t(top)].right;
        nodes_[std::size_t(top)].right = slot;
        slot = top;
    }
    void rotate_left(int& slot) {
        const int top = nodes_[std::size_t(slot)].right;
        nodes_[std::size_t(slot)].right = nodes_[std::size_t(top)].left;
        nodes_[std::size_t(top)].left = slot;
        slot = top;
    }
    // Sets the balance of a double rotation's three nodes: `middle` is the grandchild's old balance.
    void double_balance(int low, int mid, int high, int middle) {
        nodes_[std::size_t(low)].balance = middle == 1 ? -1 : 0;
        nodes_[std::size_t(high)].balance = middle == -1 ? 1 : 0;
        nodes_[std::size_t(mid)].balance = 0;
    }
    // FUN_00457b00's recursion: puts node `made` under `slot`; `grew` says that subtree got deeper.
    // False when dropped.
    bool place(int& slot, int made, bool& grew) {
        if (slot < 0) {
            slot = made;
            grew = true;
            return true;
        }
        AutomapNode& fresh = nodes_[std::size_t(made)];
        const AutomapNode& here = nodes_[std::size_t(slot)];
        int order = fresh.cell_y - here.cell_y;
        if (order == 0) order = fresh.cell_x - here.cell_x;
        if (order == 0) {
            if (fresh.group == -1 || fresh.group == here.group) return false;
            order = fresh.cel - here.cel;
        }
        if (order == 0) return false;   // the same cel and group (0x457b80: left alone)
        grew = false;
        if (order < 0) {
            bool child_grew = false;
            if (!place(nodes_[std::size_t(slot)].left, made, child_grew)) return false;
            if (!child_grew) return true;
            AutomapNode& node = nodes_[std::size_t(slot)];
            if (node.balance == 1) node.balance = 0;
            else if (node.balance == 0) { node.balance = -1; grew = true; }
            else rebalance_left(slot);   // FUN_00457900
        } else {
            bool child_grew = false;
            if (!place(nodes_[std::size_t(slot)].right, made, child_grew)) return false;
            if (!child_grew) return true;
            AutomapNode& node = nodes_[std::size_t(slot)];
            if (node.balance == -1) node.balance = 0;
            else if (node.balance == 0) { node.balance = 1; grew = true; }
            else rebalance_right(slot);  // FUN_004579e0
        }
        return true;
    }
    // FUN_00457900: `slot` is two deeper on the left.
    void rebalance_left(int& slot) {
        const int child = nodes_[std::size_t(slot)].left;
        if (nodes_[std::size_t(child)].balance == -1) {
            nodes_[std::size_t(slot)].balance = 0;
            nodes_[std::size_t(child)].balance = 0;
            rotate_right(slot);
        } else {
            const int grand = nodes_[std::size_t(child)].right, middle = nodes_[std::size_t(grand)].balance;
            const int old = slot;
            rotate_left(nodes_[std::size_t(slot)].left);
            rotate_right(slot);
            double_balance(child, grand, old, middle);
        }
    }
    // FUN_004579e0: `slot` is two deeper on the right.
    void rebalance_right(int& slot) {
        const int child = nodes_[std::size_t(slot)].right;
        if (nodes_[std::size_t(child)].balance == 1) {
            nodes_[std::size_t(slot)].balance = 0;
            nodes_[std::size_t(child)].balance = 0;
            rotate_left(slot);
        } else {
            const int grand = nodes_[std::size_t(child)].left, middle = nodes_[std::size_t(grand)].balance;
            const int old = slot;
            rotate_right(nodes_[std::size_t(slot)].right);
            rotate_left(slot);
            double_balance(old, grand, child, middle);
        }
    }
};

}  // namespace d2d::rules
