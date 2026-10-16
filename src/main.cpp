// Dear ImGui: standalone example application for SDL3 + OpenGL
// (SDL is a cross-platform general purpose library for handling windows,
// inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/
// folder).
// - Introduction, links and more at the top of imgui.cpp

#include "database.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "implot.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdio.h>
#include <string>
#include <vector>

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

struct MealPlan {
  std::vector<MealEntry> meals[3]; // 0=Morning, 1=Dinner, 2=Supper
};

class AppState {

private:
  float main_scale;
  SDL_WindowFlags window_flags;
  SDL_Window *window;
  SDL_GLContext gl_context;
  const char *glsl_version;

  const float left_panel_width = 260.0f;  // Dishes list
  const float right_panel_width = 340.0f; // Add Dish form

public:
  int Run() {
    if (SomeSDLSetup()) {
      return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();

    SetupDearImGuiAndFonts(io);

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    while (!done) {
      // Poll and handle events (inputs, window resize, etc.)
      // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to
      // tell if dear imgui wants to use your inputs.
      // - When io.WantCaptureMouse is true, do not dispatch mouse input data to
      // your main application, or clear/overwrite your copy of the mouse data.
      // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input
      // data to your main application, or clear/overwrite your copy of the
      // keyboard data. Generally you may always pass all inputs to dear imgui,
      // and hide them from your application based on those two flags. [If using
      // SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your
      // SDL_AppEvent() function]

      // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your
      // SDL_AppIterate() function]
      SDL_EventCycle(done);

      if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(10);
        continue;
      }

      ImGui_LoopStep(clear_color, io);
    }

    return 0;
  }

private:
  void SDL_EventCycle(bool &done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT)
        done = true;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window))
        done = true;
    }
  }

  static std::string date_add_days(const std::string &iso, int days) {
    std::tm tm{};
    std::istringstream ss(iso);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    tm.tm_mday += days;
    tm.tm_isdst = -1;
    std::time_t t = std::mktime(&tm);
    std::tm result{};
    localtime_r(&t, &result);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &result);
    return std::string(buf);
  }

  void ImGui_LoopStep(ImVec4 clear_color, ImGuiIO &io) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // ===== Persistent state =====
    static Database db;
    static bool db_ready = false;
    if (!db_ready) {
      db_ready = db.open("nutrition.db");
      if (!db_ready)
        fprintf(stderr, "Failed to open database!\n");
    }

    static std::vector<Ingredient> ingredients;
    static std::vector<Dish> dishes;
    static bool data_loaded = false;
    if (!data_loaded && db_ready) {
      db.seed_ingredients_if_empty(); // populates on first launch only
      ingredients = db.load_ingredients();
      dishes = db.load_dishes();
      data_loaded = true;
    }

    static std::string current_date = [] {
      std::time_t t = std::time(nullptr);
      std::tm tm{};
      localtime_r(&t, &tm);
      char buf[16];
      std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
      return std::string(buf);
    }();

    static std::vector<MealEntry> day_entries[3];
    static bool entries_loaded = false;
    if (!entries_loaded && db_ready) {
      for (int m = 0; m < 3; ++m)
        day_entries[m] = db.load_entries(current_date, m);
      entries_loaded = true;
    }
    auto reload_day = [&]() {
      if (!db_ready)
        return;
      for (int m = 0; m < 3; ++m)
        day_entries[m] = db.load_entries(current_date, m);
    };

    enum class RightMode { None, Dish, Ingredient };
    static RightMode right_mode = RightMode::None;
    static int right_edit_index = -1;

    static char edit_dish_name[128] = "";
    static std::vector<DishIngredient> edit_dish_ingredients;

    static char edit_ing_name[128] = "";
    static float edit_ing_kcal = 0, edit_ing_p = 0, edit_ing_f = 0,
                 edit_ing_c = 0;

    static bool show_dishes_panel = true;
    static bool show_meal_plan = true;
    static bool show_stats = false;
    static int stats_interval_idx = 2;

    // Meal entry popup
    static bool show_entry_popup = false;
    static int popup_meal = -1;
    static int popup_entry_id = -1;
    static int popup_kind = 0;    // 0 = dish, 1 = ingredient
    static int popup_item_id = 0; // dish_id or ingredient_id
    static float popup_weight = 100.0f;

    static const char *meal_names[3] = {"Morning", "Dinner", "Supper"};

    // ===== Helpers =====
    auto find_ingredient = [&](int id) -> Ingredient * {
      for (auto &g : ingredients)
        if (g.id == id)
          return &g;
      return nullptr;
    };
    auto find_dish = [&](int id) -> Dish * {
      for (auto &d : dishes)
        if (d.id == id)
          return &d;
      return nullptr;
    };

    auto nutrition_from = [&](const std::vector<DishIngredient> &comp,
                              float *out_total_weight = nullptr) -> Nutrition {
      Nutrition n;
      float total_w = 0;
      for (const auto &di : comp) {
        if (const Ingredient *g = find_ingredient(di.ingredient_id)) {
          float f = di.weight_g / 100.0f;
          n.kcal += g->kcal * f;
          n.protein += g->protein * f;
          n.fat += g->fat * f;
          n.carbs += g->carbs * f;
          total_w += di.weight_g;
        }
      }
      if (out_total_weight)
        *out_total_weight = total_w;
      return n;
    };
    auto dish_nutrition_per_100g = [&](const Dish &d) -> Nutrition {
      float total_w = 0;
      Nutrition n = nutrition_from(d.ingredients, &total_w);
      if (total_w > 0) {
        float k = 100.0f / total_w;
        n.kcal *= k;
        n.protein *= k;
        n.fat *= k;
        n.carbs *= k;
      }
      return n;
    };

    // Unified per-100g lookup for any meal entry
    auto entry_per_100g = [&](const MealEntry &e) -> Nutrition {
      Nutrition n;
      if (e.dish_id) {
        if (Dish *d = find_dish(e.dish_id))
          n = dish_nutrition_per_100g(*d);
      } else if (e.ingredient_id) {
        if (Ingredient *g = find_ingredient(e.ingredient_id)) {
          n.kcal = g->kcal;
          n.protein = g->protein;
          n.fat = g->fat;
          n.carbs = g->carbs;
        }
      }
      return n;
    };
    auto entry_nutrition = [&](const MealEntry &e) -> Nutrition {
      Nutrition p = entry_per_100g(e);
      float k = e.weight_g / 100.0f;
      Nutrition n;
      n.kcal = p.kcal * k;
      n.protein = p.protein * k;
      n.fat = p.fat * k;
      n.carbs = p.carbs * k;
      return n;
    };
    auto entry_name = [&](const MealEntry &e) -> std::string {
      if (e.dish_id) {
        if (Dish *d = find_dish(e.dish_id))
          return d->name;
      } else if (e.ingredient_id) {
        if (Ingredient *g = find_ingredient(e.ingredient_id))
          return g->name;
      }
      return "(missing)";
    };

    // Case-insensitive substring search
    auto icontains = [](const std::string &hay,
                        const std::string &needle) -> bool {
      if (needle.empty())
        return true;
      auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return s;
      };
      return lower(hay).find(lower(needle)) != std::string::npos;
    };

    auto open_dish_editor = [&](int idx) {
      right_mode = RightMode::Dish;
      right_edit_index = idx;
      if (idx >= 0 && idx < (int)dishes.size()) {
        snprintf(edit_dish_name, sizeof(edit_dish_name), "%s",
                 dishes[idx].name.c_str());
        edit_dish_ingredients = dishes[idx].ingredients;
      } else {
        edit_dish_name[0] = '\0';
        edit_dish_ingredients.clear();
      }
    };
    auto open_ingredient_editor = [&](int idx) {
      right_mode = RightMode::Ingredient;
      right_edit_index = idx;
      if (idx >= 0 && idx < (int)ingredients.size()) {
        const auto &g = ingredients[idx];
        snprintf(edit_ing_name, sizeof(edit_ing_name), "%s", g.name.c_str());
        edit_ing_kcal = g.kcal;
        edit_ing_p = g.protein;
        edit_ing_f = g.fat;
        edit_ing_c = g.carbs;
      } else {
        edit_ing_name[0] = '\0';
        edit_ing_kcal = edit_ing_p = edit_ing_f = edit_ing_c = 0.0f;
      }
    };

    // ===== Layout =====
    const float left_panel_width = 280.0f;
    const float right_panel_width = 380.0f;
    const float totals_height = 90.0f;
    const float menu_height = ImGui::GetFrameHeight();

    // ===== Menu bar =====
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("Menu")) {
        if (ImGui::MenuItem("Add dish"))
          open_dish_editor(-1);
        if (ImGui::MenuItem("Add ingredient"))
          open_ingredient_editor(-1);
        ImGui::Separator();
        if (ImGui::MenuItem("Library", nullptr, show_dishes_panel))
          show_dishes_panel = !show_dishes_panel;
        if (ImGui::MenuItem("Meal plan", nullptr, show_meal_plan))
          show_meal_plan = !show_meal_plan;
        if (ImGui::MenuItem("Statistics", nullptr, show_stats))
          show_stats = !show_stats;
        ImGui::EndMenu();
      }

      static char date_buf[16] = "";
      if (!ImGui::IsAnyItemActive())
        snprintf(date_buf, sizeof(date_buf), "%s", current_date.c_str());

      const float right_block = 320.0f;
      ImGui::SameLine(ImGui::GetWindowWidth() - right_block);

      if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) {
        current_date = date_add_days(current_date, -1);
        reload_day();
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(110.0f);
      if (ImGui::InputText("##date", date_buf, sizeof(date_buf),
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        current_date = date_buf;
        reload_day();
      }
      ImGui::SameLine();
      if (ImGui::ArrowButton("##next", ImGuiDir_Right)) {
        current_date = date_add_days(current_date, +1);
        reload_day();
      }
      ImGui::SameLine();
      if (ImGui::Button("Today")) {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        current_date = buf;
        reload_day();
      }
      ImGui::EndMainMenuBar();
    }

    // ===== Left panel: Library =====
    if (show_dishes_panel) {
      ImGui::SetNextWindowPos(ImVec2(0.0f, menu_height));
      ImGui::SetNextWindowSize(ImVec2(
          left_panel_width, io.DisplaySize.y - menu_height - totals_height));
      ImGui::Begin("Library", &show_dishes_panel,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoCollapse);

      if (ImGui::BeginTabBar("##left_tabs")) {
        if (ImGui::BeginTabItem("Dishes")) {
          if (ImGui::Button("+ New dish", ImVec2(-FLT_MIN, 0)))
            open_dish_editor(-1);
          ImGui::Separator();
          if (dishes.empty())
            ImGui::TextDisabled("No dishes yet.");
          for (size_t i = 0; i < dishes.size(); ++i) {
            const bool sel =
                (right_mode == RightMode::Dish && right_edit_index == (int)i);
            ImGui::PushID((int)i);
            if (ImGui::Selectable(dishes[i].name.c_str(), sel))
              open_dish_editor((int)i);
            ImGui::PopID();
          }
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Ingredients")) {
          if (ImGui::Button("+ New ingredient", ImVec2(-FLT_MIN, 0)))
            open_ingredient_editor(-1);
          ImGui::Separator();
          if (ingredients.empty())
            ImGui::TextDisabled("No ingredients yet.");
          for (size_t i = 0; i < ingredients.size(); ++i) {
            const bool sel = (right_mode == RightMode::Ingredient &&
                              right_edit_index == (int)i);
            ImGui::PushID((int)i);
            if (ImGui::Selectable(ingredients[i].name.c_str(), sel))
              open_ingredient_editor((int)i);
            ImGui::PopID();
          }
          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }
      ImGui::End();
    }

    // ===== Right panel: Dish or Ingredient editor =====
    if (right_mode != RightMode::None) {
      const bool editing = (right_edit_index >= 0);
      const bool is_dish = (right_mode == RightMode::Dish);

      ImGui::SetNextWindowPos(
          ImVec2(io.DisplaySize.x - right_panel_width, menu_height));
      ImGui::SetNextWindowSize(ImVec2(
          right_panel_width, io.DisplaySize.y - menu_height - totals_height));

      const char *title =
          is_dish ? (editing ? "Edit Dish" : "New Dish")
                  : (editing ? "Edit Ingredient" : "New Ingredient");

      bool open = true;
      ImGui::Begin(title, &open,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoCollapse);
      if (!open)
        right_mode = RightMode::None;

      // ---------- Ingredient editor ----------
      if (!is_dish) {
        ImGui::Text("Name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##iname", edit_ing_name, IM_ARRAYSIZE(edit_ing_name));

        ImGui::Spacing();
        ImGui::Text("Nutrition per 100 g");

        ImGui::Text("Calories (kcal)");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputFloat("##ikcal", &edit_ing_kcal, 1, 10, "%.1f");

        ImGui::Text("Protein, g");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputFloat("##ip", &edit_ing_p, 0.1f, 1, "%.1f");

        ImGui::Text("Fat, g");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputFloat("##if", &edit_ing_f, 0.1f, 1, "%.1f");

        ImGui::Text("Carbs, g");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputFloat("##ic", &edit_ing_c, 0.1f, 1, "%.1f");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const bool name_ok = (edit_ing_name[0] != '\0');
        ImGui::BeginDisabled(!name_ok);
        if (ImGui::Button(editing ? "Save" : "Add", ImVec2(-FLT_MIN, 0))) {
          Ingredient g;
          g.name = edit_ing_name;
          g.kcal = edit_ing_kcal;
          g.protein = edit_ing_p;
          g.fat = edit_ing_f;
          g.carbs = edit_ing_c;

          if (editing) {
            g.id = ingredients[right_edit_index].id;
            if (db.update_ingredient(g))
              ingredients[right_edit_index] = g;
          } else {
            int new_id = db.insert_ingredient(g);
            if (new_id > 0) {
              g.id = new_id;
              ingredients.push_back(g);
            }
          }
          right_mode = RightMode::None;
        }
        ImGui::EndDisabled();

        if (editing) {
          ImGui::Spacing();
          if (ImGui::Button("Delete ingredient", ImVec2(-FLT_MIN, 0))) {
            int id = ingredients[right_edit_index].id;
            db.delete_ingredient(id);
            ingredients.erase(ingredients.begin() + right_edit_index);
            for (auto &d : dishes) {
              d.ingredients.erase(
                  std::remove_if(d.ingredients.begin(), d.ingredients.end(),
                                 [&](const DishIngredient &di) {
                                   return di.ingredient_id == id;
                                 }),
                  d.ingredients.end());
            }
            for (int m = 0; m < 3; ++m) {
              auto &v = day_entries[m];
              v.erase(std::remove_if(v.begin(), v.end(),
                                     [&](const MealEntry &e) {
                                       return e.ingredient_id == id;
                                     }),
                      v.end());
            }
            right_mode = RightMode::None;
          }
        }
      }
      // ---------- Dish editor ----------
      else {
        ImGui::Text("Name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##dname", edit_dish_name,
                         IM_ARRAYSIZE(edit_dish_name));

        ImGui::Spacing();
        ImGui::Text("Ingredients");
        if (ingredients.empty())
          ImGui::TextDisabled("Add ingredients first.");

        if (ImGui::BeginTable("comp", 3,
                              ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_Borders)) {
          ImGui::TableSetupColumn("Ingredient",
                                  ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableSetupColumn("g", ImGuiTableColumnFlags_WidthFixed, 80.0f);
          ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24.0f);

          int remove_idx = -1;
          for (size_t r = 0; r < edit_dish_ingredients.size(); ++r) {
            auto &di = edit_dish_ingredients[r];
            ImGui::PushID((int)r);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            const Ingredient *cur = find_ingredient(di.ingredient_id);
            const char *preview = cur ? cur->name.c_str() : "(pick ingredient)";

            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##ing", preview)) {
              // One filter buffer, one combo open at a time
              static char filter_buf[128] = "";
              if (ImGui::IsWindowAppearing()) {
                filter_buf[0] = '\0';
                ImGui::SetKeyboardFocusHere();
              }

              ImGui::SetNextItemWidth(-FLT_MIN);
              bool commit = ImGui::InputTextWithHint(
                  "##filter", "Type to search...", filter_buf,
                  sizeof(filter_buf), ImGuiInputTextFlags_EnterReturnsTrue);

              int shown = 0;
              int first_match_id = 0;
              const int kMaxShown = 200;

              if (ImGui::BeginChild("##list", ImVec2(0, 240.0f))) {
                for (auto &g : ingredients) {
                  if (!icontains(g.name, filter_buf))
                    continue;
                  if (first_match_id == 0)
                    first_match_id = g.id;
                  if (++shown > kMaxShown)
                    break;
                  bool sel = (g.id == di.ingredient_id);
                  if (ImGui::Selectable(g.name.c_str(), sel)) {
                    di.ingredient_id = g.id;
                    filter_buf[0] = '\0';
                    ImGui::CloseCurrentPopup();
                  }
                }
                if (shown == 0)
                  ImGui::TextDisabled("No matches");
              }
              ImGui::EndChild();

              // Enter commits the first visible match
              if (commit && first_match_id != 0) {
                di.ingredient_id = first_match_id;
                filter_buf[0] = '\0';
                ImGui::CloseCurrentPopup();
              }

              ImGui::EndCombo();
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputFloat("##w", &di.weight_g, 5, 25, "%.0f");
            if (di.weight_g < 0)
              di.weight_g = 0;

            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("x"))
              remove_idx = (int)r;
            ImGui::PopID();
          }
          ImGui::EndTable();

          if (remove_idx >= 0)
            edit_dish_ingredients.erase(edit_dish_ingredients.begin() +
                                        remove_idx);
        }

        ImGui::BeginDisabled(ingredients.empty());
        if (ImGui::Button("+ Add row", ImVec2(-FLT_MIN, 0)))
          edit_dish_ingredients.push_back({ingredients[0].id, 100.0f});
        ImGui::EndDisabled();

        float total_w = 0;
        Nutrition n = nutrition_from(edit_dish_ingredients, &total_w);
        Nutrition per100 = n;
        if (total_w > 0) {
          float k = 100.0f / total_w;
          per100.kcal *= k;
          per100.protein *= k;
          per100.fat *= k;
          per100.carbs *= k;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("Total weight: %.0f g", total_w);
        ImGui::Text("Total:   %.0f kcal  P%.1f  F%.1f  C%.1f", n.kcal,
                    n.protein, n.fat, n.carbs);
        ImGui::TextDisabled("Per 100 g: %.0f kcal  P%.1f  F%.1f  C%.1f",
                            per100.kcal, per100.protein, per100.fat,
                            per100.carbs);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const bool name_ok = (edit_dish_name[0] != '\0');
        ImGui::BeginDisabled(!name_ok);
        if (ImGui::Button(editing ? "Save" : "Add", ImVec2(-FLT_MIN, 0))) {
          Dish d;
          d.name = edit_dish_name;
          d.ingredients = edit_dish_ingredients;

          if (editing) {
            d.id = dishes[right_edit_index].id;
            if (db.update_dish(d))
              dishes[right_edit_index] = d;
          } else {
            int new_id = db.insert_dish(d);
            if (new_id > 0) {
              d.id = new_id;
              dishes.push_back(d);
            }
          }
          right_mode = RightMode::None;
        }
        ImGui::EndDisabled();

        if (editing) {
          ImGui::Spacing();
          if (ImGui::Button("Delete dish", ImVec2(-FLT_MIN, 0))) {
            int id = dishes[right_edit_index].id;
            db.delete_dish(id);
            dishes.erase(dishes.begin() + right_edit_index);
            for (int m = 0; m < 3; ++m) {
              auto &v = day_entries[m];
              v.erase(std::remove_if(
                          v.begin(), v.end(),
                          [&](const MealEntry &e) { return e.dish_id == id; }),
                      v.end());
            }
            right_mode = RightMode::None;
          }
        }
      }
      ImGui::End();
    }

    // ===== Center: Meal Plan =====
    if (show_meal_plan) {
      const float left_offset = show_dishes_panel ? left_panel_width : 0.0f;
      const float right_offset =
          (right_mode != RightMode::None) ? right_panel_width : 0.0f;

      ImGui::SetNextWindowPos(ImVec2(left_offset, menu_height));
      ImGui::SetNextWindowSize(
          ImVec2(io.DisplaySize.x - left_offset - right_offset,
                 io.DisplaySize.y - menu_height - totals_height));

      ImGui::Begin("Meal Plan", &show_meal_plan,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoBringToFrontOnFocus);

      if (ImGui::BeginTable("meal_plan_table", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchSame)) {
        for (int m = 0; m < 3; ++m)
          ImGui::TableSetupColumn(meal_names[m]);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        for (int m = 0; m < 3; ++m) {
          ImGui::TableSetColumnIndex(m);
          char child_id[32];
          snprintf(child_id, sizeof(child_id), "##meal_%d", m);
          ImGui::BeginChild(child_id, ImVec2(0, 0), ImGuiChildFlags_Borders);

          // Two add buttons side by side: dish | ingredient
          const float avail = ImGui::GetContentRegionAvail().x;
          const float half = (avail - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

          ImGui::BeginDisabled(dishes.empty());
          if (ImGui::Button("+ Dish", ImVec2(half, 0))) {
            popup_meal = m;
            popup_entry_id = -1;
            popup_kind = 0;
            popup_item_id = dishes.empty() ? 0 : dishes[0].id;
            popup_weight = 100.0f;
            show_entry_popup = true;
          }
          ImGui::EndDisabled();
          ImGui::SameLine();
          ImGui::BeginDisabled(ingredients.empty());
          if (ImGui::Button("+ Ingredient", ImVec2(half, 0))) {
            popup_meal = m;
            popup_entry_id = -1;
            popup_kind = 1;
            popup_item_id = ingredients.empty() ? 0 : ingredients[0].id;
            popup_weight = 100.0f;
            show_entry_popup = true;
          }
          ImGui::EndDisabled();

          if (dishes.empty() && ingredients.empty())
            ImGui::TextDisabled("Add a dish or ingredient first.");

          ImGui::Separator();

          auto &entries = day_entries[m];
          int delete_idx = -1;

          for (size_t e = 0; e < entries.size(); ++e) {
            MealEntry &ent = entries[e];

            Nutrition per100 = entry_per_100g(ent);
            Nutrition eaten = entry_nutrition(ent);

            ImGui::PushID((int)e);
            if (ImGui::BeginTable("row", 2,
                                  ImGuiTableFlags_SizingStretchProp |
                                      ImGuiTableFlags_NoBordersInBody)) {
              ImGui::TableSetupColumn("main",
                                      ImGuiTableColumnFlags_WidthStretch);
              ImGui::TableSetupColumn("del", ImGuiTableColumnFlags_WidthFixed,
                                      22.0f);

              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex(0);

              std::string name = entry_name(ent);
              if (ImGui::Selectable(name.c_str())) {
                popup_meal = m;
                popup_entry_id = ent.id;
                popup_kind = (ent.dish_id != 0) ? 0 : 1;
                popup_item_id =
                    (ent.dish_id != 0) ? ent.dish_id : ent.ingredient_id;
                popup_weight = ent.weight_g;
                show_entry_popup = true;
              }
              ImGui::TextDisabled("%.0f g | %.0f kcal", ent.weight_g,
                                  eaten.kcal);

              ImGui::TableSetColumnIndex(1);
              if (ImGui::SmallButton("x"))
                delete_idx = (int)e;
              ImGui::EndTable();
            }
            ImGui::PopID();
          }

          if (delete_idx >= 0) {
            int id = entries[delete_idx].id;
            db.delete_entry(id);
            entries.erase(entries.begin() + delete_idx);
          }

          if (!entries.empty()) {
            Nutrition sum;
            for (auto &ent : entries) {
              Nutrition e = entry_nutrition(ent);
              sum.kcal += e.kcal;
              sum.protein += e.protein;
              sum.fat += e.fat;
              sum.carbs += e.carbs;
            }
            ImGui::Separator();
            ImGui::Text("Total: %.0f kcal", sum.kcal);
            ImGui::TextDisabled("P %.1f  F %.1f  C %.1f", sum.protein, sum.fat,
                                sum.carbs);
          }
          ImGui::EndChild();
        }
        ImGui::EndTable();
      }
      ImGui::End();
    }

    // ===== Bottom: Totals =====
    {
      const float left_offset = show_dishes_panel ? left_panel_width : 0.0f;
      const float right_offset =
          (right_mode != RightMode::None) ? right_panel_width : 0.0f;

      ImGui::SetNextWindowPos(
          ImVec2(left_offset, io.DisplaySize.y - totals_height));
      ImGui::SetNextWindowSize(
          ImVec2(io.DisplaySize.x - left_offset - right_offset, totals_height));

      ImGui::Begin("Totals", nullptr,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoTitleBar |
                       ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoBringToFrontOnFocus);

      Nutrition total;
      float meal_kcal[3] = {0, 0, 0};

      for (int m = 0; m < 3; ++m) {
        for (const MealEntry &ent : day_entries[m]) {
          Nutrition e = entry_nutrition(ent);
          total.kcal += e.kcal;
          total.protein += e.protein;
          total.fat += e.fat;
          total.carbs += e.carbs;
          meal_kcal[m] += e.kcal;
        }
      }

      ImGui::TextUnformatted("Daily totals");
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::BeginGroup();
      ImGui::TextDisabled("Calories");
      ImGui::SetWindowFontScale(1.6f);
      ImGui::Text("%.0f kcal", total.kcal);
      ImGui::SetWindowFontScale(1.0f);
      ImGui::EndGroup();

      ImGui::SameLine(0, 40);
      ImGui::BeginGroup();
      ImGui::TextDisabled("Protein / Fat / Carbs");
      ImGui::Text("P  %.1f g", total.protein);
      ImGui::SameLine(0, 20);
      ImGui::Text("F  %.1f g", total.fat);
      ImGui::SameLine(0, 20);
      ImGui::Text("C  %.1f g", total.carbs);
      ImGui::EndGroup();

      ImGui::SameLine(0, 40);
      ImGui::BeginGroup();
      ImGui::TextDisabled("By meal");
      ImGui::Text("Morning %.0f | Dinner %.0f | Supper %.0f", meal_kcal[0],
                  meal_kcal[1], meal_kcal[2]);
      ImGui::EndGroup();
      ImGui::End();
    }

    // ===== Statistics window =====
    if (show_stats) {
      ImGui::SetNextWindowSize(ImVec2(820, 660), ImGuiCond_FirstUseEver);
      ImGui::Begin("Statistics", &show_stats);

      const char *interval_labels[] = {"7 days",  "14 days",  "30 days",
                                       "90 days", "180 days", "1 year"};
      const int interval_values[] = {7, 14, 30, 90, 180, 365};

      ImGui::TextUnformatted("Interval:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(140.0f);
      ImGui::Combo("##interval", &stats_interval_idx, interval_labels,
                   IM_ARRAYSIZE(interval_labels));

      const int stats_days = interval_values[stats_interval_idx];
      ImGui::SameLine();
      ImGui::TextDisabled("ending %s", current_date.c_str());

      const std::string to = current_date;
      const std::string from = date_add_days(current_date, -(stats_days - 1));

      std::map<std::string, Nutrition> per_day;
      for (int i = 0; i < stats_days; ++i)
        per_day[date_add_days(from, i)] = Nutrition{};

      if (db_ready) {
        auto dated = db.load_entries_in_range(from, to);
        for (const auto &de : dated) {
          Nutrition e = entry_nutrition(de.entry);
          auto &t = per_day[de.date];
          t.kcal += e.kcal;
          t.protein += e.protein;
          t.fat += e.fat;
          t.carbs += e.carbs;
        }
      }

      std::vector<double> xs, kcal, protein, fat, carbs;
      std::vector<int> day_numbers;
      xs.reserve(stats_days);
      day_numbers.reserve(stats_days);
      int idx = 0;
      for (auto &kv : per_day) {
        xs.push_back((double)idx++);
        kcal.push_back(kv.second.kcal);
        protein.push_back(kv.second.protein);
        fat.push_back(kv.second.fat);
        carbs.push_back(kv.second.carbs);
      }
      const int n = (int)xs.size();
      day_numbers.resize(n);
      for (int i = 0; i < n; ++i)
        day_numbers[i] = n - i;

      if (n > 0) {
        double s_kcal = 0, s_p = 0, s_f = 0, s_c = 0;
        int logged = 0;
        for (int i = 0; i < n; ++i) {
          s_kcal += kcal[i];
          s_p += protein[i];
          s_f += fat[i];
          s_c += carbs[i];
          if (kcal[i] > 0)
            logged++;
        }
        const int denom = std::max(1, logged);
        ImGui::Separator();
        ImGui::Text("Average over %d days (%d logged):", n, logged);
        ImGui::SameLine();
        ImGui::Text("%.0f kcal | P %.1f | F %.1f | C %.1f", s_kcal / denom,
                    s_p / denom, s_f / denom, s_c / denom);
        ImGui::Separator();
      }

      const int tick_step = std::max(1, stats_days / 8);
      std::vector<double> tick_xs;
      std::vector<std::string> tick_strings;
      std::vector<const char *> tick_ptrs;
      for (int i = 0; i < n; i += tick_step) {
        tick_xs.push_back((double)i);
        tick_strings.push_back(std::to_string(day_numbers[i]));
      }
      for (auto &s : tick_strings)
        tick_ptrs.push_back(s.c_str());

      if (ImPlot::BeginSubplots("##nutrition_grid", 2, 2, ImVec2(-1, -1),
                                ImPlotSubplotFlags_LinkAllX)) {
        auto draw_series = [&](const char *title, const char *ylabel,
                               const std::vector<double> &ys, ImVec4 color) {
          if (ImPlot::BeginPlot(title, ImVec2(-1, 0))) {
            ImPlot::SetupAxes("Days (1 = today)", ylabel,
                              ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxisTicks(ImAxis_X1, tick_xs.data(),
                                   (int)tick_xs.size(), tick_ptrs.data());
            ImPlot::PushStyleColor(ImPlotCol_Line, color);
            ImPlot::PushStyleColor(ImPlotCol_Fill,
                                   ImVec4(color.x, color.y, color.z, 0.25f));
            ImPlot::PlotShaded(title, xs.data(), ys.data(), n, 0.0);
            ImPlot::PlotLine(title, xs.data(), ys.data(), n);
            ImPlot::PopStyleColor(2);
            ImPlot::EndPlot();
          }
        };
        draw_series("Calories (kcal)", "kcal", kcal,
                    ImVec4(0.95f, 0.45f, 0.25f, 1.0f));
        draw_series("Protein (g)", "g", protein,
                    ImVec4(0.30f, 0.70f, 0.95f, 1.0f));
        draw_series("Fat (g)", "g", fat, ImVec4(0.95f, 0.80f, 0.20f, 1.0f));
        draw_series("Carbs (g)", "g", carbs, ImVec4(0.45f, 0.85f, 0.40f, 1.0f));
        ImPlot::EndSubplots();
      }
      ImGui::End();
    }

    // ===== Meal Entry popup (dish or ingredient) =====
    if (show_entry_popup) {
      ImGui::OpenPopup("Meal Entry");
      show_entry_popup = false;
    }
    if (ImGui::BeginPopupModal("Meal Entry", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      const bool editing = (popup_entry_id >= 0);

      if (popup_meal >= 0 && popup_meal < 3)
        ImGui::Text("Meal: %s", meal_names[popup_meal]);
      ImGui::Separator();
      ImGui::Spacing();

      // Kind selector
      if (ImGui::RadioButton("Dish", popup_kind == 0)) {
        popup_kind = 0;
        popup_item_id = dishes.empty() ? 0 : dishes[0].id;
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Ingredient", popup_kind == 1)) {
        popup_kind = 1;
        popup_item_id = ingredients.empty() ? 0 : ingredients[0].id;
      }

      ImGui::Spacing();

      // Searchable combo depending on kind
      ImGui::Text(popup_kind == 0 ? "Dish" : "Ingredient");
      ImGui::SetNextItemWidth(320.0f);

      // Build a unified (id, name) view over whichever list is active.
      struct ItemRef {
        int id;
        const std::string *name;
      };
      std::vector<ItemRef> list;
      if (popup_kind == 0) {
        list.reserve(dishes.size());
        for (auto &d : dishes)
          list.push_back({d.id, &d.name});
      } else {
        list.reserve(ingredients.size());
        for (auto &g : ingredients)
          list.push_back({g.id, &g.name});
      }

      const char *preview = "(pick)";
      if (popup_kind == 0) {
        if (Dish *d = find_dish(popup_item_id))
          preview = d->name.c_str();
      } else {
        if (Ingredient *g = find_ingredient(popup_item_id))
          preview = g->name.c_str();
      }

      if (ImGui::BeginCombo("##item_combo", preview)) {
        static char filter_buf[128] = "";
        if (ImGui::IsWindowAppearing()) {
          filter_buf[0] = '\0';
          ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool commit = ImGui::InputTextWithHint(
            "##filter", "Type to search...", filter_buf, sizeof(filter_buf),
            ImGuiInputTextFlags_EnterReturnsTrue);

        int shown = 0;
        int first_match_id = 0;
        const int kMaxShown = 200;
        if (ImGui::BeginChild("##list", ImVec2(0, 240.0f))) {
          for (auto &item : list) {
            if (!icontains(*item.name, filter_buf))
              continue;
            if (first_match_id == 0)
              first_match_id = item.id;
            if (++shown > kMaxShown)
              break;
            bool sel = (item.id == popup_item_id);
            if (ImGui::Selectable(item.name->c_str(), sel)) {
              popup_item_id = item.id;
              filter_buf[0] = '\0';
              ImGui::CloseCurrentPopup();
            }
          }
          if (shown == 0)
            ImGui::TextDisabled("No matches");
        }
        ImGui::EndChild();

        if (commit && first_match_id != 0) {
          popup_item_id = first_match_id;
          filter_buf[0] = '\0';
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndCombo();
      }

      ImGui::Spacing();
      ImGui::Text("Weight, g");
      ImGui::SetNextItemWidth(320.0f);
      ImGui::InputFloat("##weight", &popup_weight, 10, 50, "%.0f");
      if (popup_weight < 0)
        popup_weight = 0;

      // Live preview
      MealEntry preview_entry;
      preview_entry.dish_id = (popup_kind == 0) ? popup_item_id : 0;
      preview_entry.ingredient_id = (popup_kind == 1) ? popup_item_id : 0;
      preview_entry.weight_g = popup_weight;
      Nutrition n = entry_nutrition(preview_entry);

      if (n.kcal > 0 || n.protein > 0 || n.fat > 0 || n.carbs > 0) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Nutrition for %.0f g", popup_weight);
        ImGui::BulletText("Kcal:    %.1f", n.kcal);
        ImGui::BulletText("Protein: %.1f g", n.protein);
        ImGui::BulletText("Fat:     %.1f g", n.fat);
        ImGui::BulletText("Carbs:   %.1f g", n.carbs);
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      const bool can_save = (popup_item_id != 0) && popup_weight > 0;
      ImGui::BeginDisabled(!can_save);
      if (ImGui::Button(editing ? "Save" : "Add", ImVec2(120, 0))) {
        int dish_id = (popup_kind == 0) ? popup_item_id : 0;
        int ingredient_id = (popup_kind == 1) ? popup_item_id : 0;

        auto &entries = day_entries[popup_meal];
        if (editing) {
          db.update_entry(popup_entry_id, dish_id, ingredient_id, popup_weight);
          for (auto &e : entries)
            if (e.id == popup_entry_id) {
              e.dish_id = dish_id;
              e.ingredient_id = ingredient_id;
              e.weight_g = popup_weight;
            }
        } else {
          int new_id = db.insert_entry(current_date, popup_meal, dish_id,
                                       ingredient_id, popup_weight);
          if (new_id > 0) {
            MealEntry e;
            e.id = new_id;
            e.dish_id = dish_id;
            e.ingredient_id = ingredient_id;
            e.weight_g = popup_weight;
            entries.push_back(e);
          }
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    // ===== Rendering =====
    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                 clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
  }
  void SetupDearImGuiAndFonts(ImGuiIO &io) {
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();
    // style.FontSizeBase = 16.0f;           // Default to 13.0f when
    // unspecified, which allows crisp legacy font for non-4K screens.
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;

    // Setup scaling
    style.ScaleAllSizes(
        main_scale); // Bake a fixed style scale. (until we have a solution for
                     // dynamic style scaling, changing this requires resetting
                     // Style + calling this again)
    style.FontScaleDpi =
        main_scale; // Set initial font scale. (in docking branch: using
                    // io.ConfigDpiScaleFonts=true automatically overrides this
                    // for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will select an embedded
    // font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain *
    //   style.FontScaleDpi) reaching a threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to
    // select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr.
    // Please handle those errors in your code (e.g. use an assertion, display
    // an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use
    // FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string
    // literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at
    // runtime from the "fonts/" folder. See Makefile.emscripten for details.
    // io.Fonts->AddFontDefaultVector();
    // io.Fonts->AddFontDefaultBitmap();
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    // ImFont* font =
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    // IM_ASSERT(font != nullptr);
  }

  void Cleanup() {
    // Cleanup
    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your
    // SDL_AppQuit() function]

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
  }

  int SomeSDLSetup() {
    // Setup SDL
    // [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop
    // starts would likely be your SDL_AppInit() function]
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
      printf("Error: SDL_Init(): %s\n", SDL_GetError());
      return 1;
    }

    // Select GL version + let the backend select a GLSL version
    glsl_version = nullptr;
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + generally GLSL 150
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_FLAGS,
        SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + generally GLSL 130
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                   SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window = SDL_CreateWindow("Dear ImGui SDL3+OpenGL3 example",
                              (int)(1280 * main_scale), (int)(800 * main_scale),
                              window_flags);
    if (window == nullptr) {
      printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
      return 1;
    }
    gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
      printf("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
      return 1;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    return 0;
  }
};

// Main code
int main(int, char **) {
  AppState app_state;
  return app_state.Run();
}
