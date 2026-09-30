#include "ui/PaneLayout.h"
#include <iostream>
#include <cassert>

using namespace ft;

void TestPaneLayout() {
    std::cout << "[TEST] Starting PaneLayout BSP tree unit tests...\n";

    PaneLayout layout;
    assert(layout.PaneCount() == 1);

    D2D1_RECT_F screenRect = D2D1::RectF(0, 0, 1000, 600);

    // 1. Initial single pane layout test
    {
        auto panes = layout.ComputeLayout(screenRect);
        assert(panes.size() == 1);
        assert(panes[0].area.left == 0 && panes[0].area.right == 1000);
        assert(panes[0].area.top == 0 && panes[0].area.bottom == 600);
        assert(panes[0].isFocused == true);
        std::cout << "Test 1 (Initial single pane): PASSED\n";
    }

    // 2. Split vertical (Left / Right) test
    {
        bool ok = layout.SplitActive(SplitDirection::Vertical, nullptr);
        assert(ok);
        assert(layout.PaneCount() == 2);

        auto panes = layout.ComputeLayout(screenRect);
        assert(panes.size() == 2);
        // Left pane should be around 0..498.5, right pane around 501.5..1000
        std::cout << "Pane 0 right: " << panes[0].area.right << ", Pane 1 left: " << panes[1].area.left << "\n";
        assert(panes[0].area.right < panes[1].area.left);
        assert(panes[1].isFocused == true); // focus moved to new pane
        std::cout << "Test 2 (Vertical split 2 panes): PASSED\n";
    }

    // 3. Split horizontal on second pane (Top / Bottom) test
    {
        bool ok = layout.SplitActive(SplitDirection::Horizontal, nullptr);
        assert(ok);
        assert(layout.PaneCount() == 3);

        auto panes = layout.ComputeLayout(screenRect);
        assert(panes.size() == 3);
        assert(panes[2].isFocused == true);
        std::cout << "Test 3 (Horizontal split 3 panes): PASSED\n";
    }

    // 4. Zoom test
    {
        layout.ToggleZoom();
        assert(layout.IsZoomed());
        auto panes = layout.ComputeLayout(screenRect);
        assert(panes.size() == 1);
        assert(panes[0].area.left == 0 && panes[0].area.right == 1000);
        assert(panes[0].area.top == 0 && panes[0].area.bottom == 600);

        layout.ToggleZoom();
        assert(!layout.IsZoomed());
        panes = layout.ComputeLayout(screenRect);
        assert(panes.size() == 3);
        std::cout << "Test 4 (Zoom / Unzoom): PASSED\n";
    }

    // 5. Close active pane test
    {
        bool ok = layout.CloseActive();
        assert(ok);
        assert(layout.PaneCount() == 2);

        ok = layout.CloseActive();
        assert(ok);
        assert(layout.PaneCount() == 1);

        // Can't close last pane
        ok = layout.CloseActive();
        assert(!ok);
        assert(layout.PaneCount() == 1);
        std::cout << "Test 5 (Close pane tree collapse): PASSED\n";
    }

    // 6. Split and cycle focus / find tab test
    {
        auto t1 = std::make_shared<TerminalTab>();
        auto t2 = std::make_shared<TerminalTab>();
        PaneLayout pl(t1);
        assert(pl.FindPaneIdByTab(t1.get()) == 1);

        pl.SplitActive(SplitDirection::Vertical, t2);
        assert(pl.PaneCount() == 2);
        uint32_t id1 = pl.FindPaneIdByTab(t1.get());
        uint32_t id2 = pl.FindPaneIdByTab(t2.get());
        assert(id1 != 0 && id2 != 0);
        assert(id1 != id2);
        assert(pl.GetFocusedPaneId() == id2);

        pl.CycleFocus(true);
        assert(pl.GetFocusedPaneId() == id1);
        pl.CycleFocus(true);
        assert(pl.GetFocusedPaneId() == id2);
        std::cout << "Test 6 (FindPaneIdByTab & CycleFocus): PASSED\n";
    }

    std::cout << "[SUCCESS] ALL PaneLayout unit tests PASSED cleanly!\n";
}

int main() {
    TestPaneLayout();
    return 0;
}
