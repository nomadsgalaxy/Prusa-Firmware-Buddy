/// @file
/// @brief Screen for selecting a filament preset to preload into all MMU slots
#pragma once

#include <window_menu_virtual.hpp>
#include <filament_list.hpp>
#include <screen_menu.hpp>

#include <MItem_tools.hpp>

namespace screen_mmu_preload_all {

class MI_FILAMENT final : public IWindowMenuItem {
public:
    MI_FILAMENT(FilamentType filament_type);

protected:
    void click(IWindowMenu &) override;

private:
    const FilamentType filament_type;
    const FilamentTypeParameters::Name filament_name;
};

class WindowMenuMMUPreloadAll final : public WindowMenuVirtual<MI_RETURN, MI_FILAMENT> {
public:
    WindowMenuMMUPreloadAll(window_t *parent, Rect16 rect);

public:
    int item_count() const final;

protected:
    void setup_item(ItemVariant &variant, int index) final;

private:
    FilamentList filament_list;
};

} // namespace screen_mmu_preload_all

class ScreenMenuMMUPreloadAll final : public ScreenMenuBase<screen_mmu_preload_all::WindowMenuMMUPreloadAll> {
public:
    ScreenMenuMMUPreloadAll();
};
