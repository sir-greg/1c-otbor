#pragma once
#include <string>
#include <vector>

// ===== Plain data types =====

struct Ingredient {
  int id = 0;
  std::string name;
  float kcal = 0.0f, protein = 0.0f, fat = 0.0f, carbs = 0.0f; // per 100 g
};

struct DishIngredient {
  int ingredient_id = 0;
  float weight_g = 100.0f;
};

struct Dish {
  int id = 0;
  std::string name;
  std::vector<DishIngredient> ingredients;
};

struct MealEntry {
  int id = 0;
  int dish_id = 0;       // 0 if this entry is a raw ingredient
  int ingredient_id = 0; // 0 if this entry is a dish
  float weight_g = 100.0f;
};

struct Nutrition {
  float kcal = 0.0f, protein = 0.0f, fat = 0.0f, carbs = 0.0f;
};

// ===== Database class =====

class Database {
public:
  struct DatedMealEntry {
    std::string date;
    MealEntry entry;
  };

  Database() = default;
  ~Database();

  // Non-copyable (owns a sqlite3* handle)
  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;

  bool open(const std::string &path);

  // ---- Ingredients ----
  std::vector<Ingredient> load_ingredients();
  int insert_ingredient(const Ingredient &g);
  bool update_ingredient(const Ingredient &g);
  bool delete_ingredient(int id);

  // ---- Dishes ----
  std::vector<Dish> load_dishes();
  int insert_dish(const Dish &d);
  bool update_dish(const Dish &d);
  bool delete_dish(int id);

  // ---- Meal entries ----
  std::vector<MealEntry> load_entries(const std::string &date, int meal);
  int insert_entry(const std::string &date, int meal, int dish_id,
                   int ingredient_id, float weight_g);
  bool update_entry(int id, int dish_id, int ingredient_id, float weight_g);
  bool delete_entry(int id);

  // (inside class Database, public:)
  std::vector<DatedMealEntry> load_entries_in_range(const std::string &from,
                                                    const std::string &to);
  // ---- Metadata (key/value store) ----
  std::string get_metadata(const std::string &key);
  bool set_metadata(const std::string &key, const std::string &value);

  // ---- Bulk import ----
  // Inserts ingredients not already present (matches by name).
  // Returns count of new rows inserted.
  int bulk_insert_ingredients(const std::vector<Ingredient> &batch);

  // Populates the ingredients table with a starter set if it's empty.
  // Returns the number of ingredients inserted.
  int seed_ingredients_if_empty();

private:
  struct Impl;           // forward-declared; defined in database.cpp
  Impl *impl_ = nullptr; // pimpl idiom

  bool write_composition(int dish_id, const std::vector<DishIngredient> &comp);
  bool create_schema();
  bool exec(const std::string &sql, const std::vector<std::string> &args);
  bool migrate_schema();
};
