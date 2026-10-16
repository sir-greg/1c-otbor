#pragma once
#include <string>
#include <vector>

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
  int dish_id = 0;      
  int ingredient_id = 0;
  float weight_g = 100.0f;
};

struct Nutrition {
  float kcal = 0.0f, protein = 0.0f, fat = 0.0f, carbs = 0.0f;
};

class Database {
public:
  struct DatedMealEntry {
    std::string date;
    MealEntry entry;
  };

  Database() = default;
  ~Database();

  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;

  bool open(const std::string &path);

  std::vector<Ingredient> load_ingredients();
  int insert_ingredient(const Ingredient &g);
  bool update_ingredient(const Ingredient &g);
  bool delete_ingredient(int id);

  std::vector<Dish> load_dishes();
  int insert_dish(const Dish &d);
  bool update_dish(const Dish &d);
  bool delete_dish(int id);

  std::vector<MealEntry> load_entries(const std::string &date, int meal);
  int insert_entry(const std::string &date, int meal, int dish_id,
                   int ingredient_id, float weight_g);
  bool update_entry(int id, int dish_id, int ingredient_id, float weight_g);
  bool delete_entry(int id);

  std::vector<DatedMealEntry> load_entries_in_range(const std::string &from,
                                                    const std::string &to);
  std::string get_metadata(const std::string &key);
  bool set_metadata(const std::string &key, const std::string &value);

  int bulk_insert_ingredients(const std::vector<Ingredient> &batch);

  int seed_ingredients_if_empty();

private:
  struct Impl;          
  Impl *impl_ = nullptr;

  bool write_composition(int dish_id, const std::vector<DishIngredient> &comp);
  bool create_schema();
  bool exec(const std::string &sql, const std::vector<std::string> &args);
  bool migrate_schema();
};
