#include "database.hpp"

#include <cstdio>
#include <sqlite3.h>

// ===== Impl struct (holds the sqlite3 handle) =====

struct Database::Impl {
  sqlite3 *db = nullptr;
};

// ===== Lifecycle =====

std::vector<Database::DatedMealEntry>
Database::load_entries_in_range(const std::string &from,
                                const std::string &to) {
  std::vector<DatedMealEntry> out;
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(impl_->db,
                         "SELECT date, id, dish_id, weight_g FROM meal_entries "
                         "WHERE date >= ? AND date <= ? ORDER BY date;",
                         -1, &st, nullptr) != SQLITE_OK)
    return out;
  sqlite3_bind_text(st, 1, from.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, to.c_str(), -1, SQLITE_TRANSIENT);
  while (sqlite3_step(st) == SQLITE_ROW) {
    DatedMealEntry de;
    de.date = (const char *)sqlite3_column_text(st, 0);
    de.entry.id = sqlite3_column_int(st, 1);
    de.entry.dish_id = sqlite3_column_int(st, 2);
    de.entry.weight_g = (float)sqlite3_column_double(st, 3);
    out.push_back(de);
  }
  sqlite3_finalize(st);
  return out;
}

Database::~Database() {
  if (impl_) {
    if (impl_->db)
      sqlite3_close(impl_->db);
    delete impl_;
    impl_ = nullptr;
  }
}

bool Database::open(const std::string &path) {
  if (!impl_)
    impl_ = new Impl();
  if (sqlite3_open(path.c_str(), &impl_->db) != SQLITE_OK) {
    fprintf(stderr, "SQLite open failed: %s\n", sqlite3_errmsg(impl_->db));
    return false;
  }
  if (!create_schema())
    return false;
  if (!migrate_schema())
    return false;
  return true;
}

bool Database::migrate_schema() {
  bool has_col = false;
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(impl_->db, "PRAGMA table_info(meal_entries);", -1, &st,
                         nullptr) == SQLITE_OK) {
    while (sqlite3_step(st) == SQLITE_ROW) {
      const char *name = (const char *)sqlite3_column_text(st, 1);
      if (name && std::string(name) == "ingredient_id") {
        has_col = true;
        break;
      }
    }
    sqlite3_finalize(st);
  }
  if (has_col)
    return true;

  fprintf(stderr,
          "[db] Migrating meal_entries to support ingredient entries...\n");

  const char *sql =
      "PRAGMA foreign_keys = OFF;"
      "BEGIN;"
      "CREATE TABLE meal_entries_new ("
      "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  date          TEXT NOT NULL,"
      "  meal          INTEGER NOT NULL,"
      "  dish_id       INTEGER REFERENCES dishes(id)      ON DELETE CASCADE,"
      "  ingredient_id INTEGER REFERENCES ingredients(id) ON DELETE CASCADE,"
      "  weight_g      REAL NOT NULL DEFAULT 100,"
      "  CHECK ((dish_id IS NULL) != (ingredient_id IS NULL))"
      ");"
      "INSERT INTO meal_entries_new(id, date, meal, dish_id, weight_g) "
      "  SELECT id, date, meal, dish_id, weight_g FROM meal_entries;"
      "DROP TABLE meal_entries;"
      "ALTER TABLE meal_entries_new RENAME TO meal_entries;"
      "CREATE INDEX IF NOT EXISTS idx_entries_date ON meal_entries(date);"
      "COMMIT;"
      "PRAGMA foreign_keys = ON;";

  char *err = nullptr;
  if (sqlite3_exec(impl_->db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    fprintf(stderr, "[db] Migration failed: %s\n", err);
    sqlite3_free(err);
    return false;
  }
  return true;
}
// ===== Ingredients =====

std::vector<Ingredient> Database::load_ingredients() {
  std::vector<Ingredient> out;
  sqlite3_stmt *st = nullptr;
  const char *sql =
      "SELECT id,name,kcal,protein,fat,carbs FROM ingredients ORDER BY name;";
  if (sqlite3_prepare_v2(impl_->db, sql, -1, &st, nullptr) != SQLITE_OK)
    return out;

  while (sqlite3_step(st) == SQLITE_ROW) {
    Ingredient g;
    g.id = sqlite3_column_int(st, 0);
    g.name = (const char *)sqlite3_column_text(st, 1);
    g.kcal = (float)sqlite3_column_double(st, 2);
    g.protein = (float)sqlite3_column_double(st, 3);
    g.fat = (float)sqlite3_column_double(st, 4);
    g.carbs = (float)sqlite3_column_double(st, 5);
    out.push_back(g);
  }
  sqlite3_finalize(st);
  return out;
}

int Database::insert_ingredient(const Ingredient &g) {
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(
      impl_->db,
      "INSERT INTO ingredients(name,kcal,protein,fat,carbs) VALUES(?,?,?,?,?);",
      -1, &st, nullptr);
  sqlite3_bind_text(st, 1, g.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(st, 2, g.kcal);
  sqlite3_bind_double(st, 3, g.protein);
  sqlite3_bind_double(st, 4, g.fat);
  sqlite3_bind_double(st, 5, g.carbs);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE ? (int)sqlite3_last_insert_rowid(impl_->db) : -1;
}

bool Database::update_ingredient(const Ingredient &g) {
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(impl_->db,
                     "UPDATE ingredients SET "
                     "name=?,kcal=?,protein=?,fat=?,carbs=? WHERE id=?;",
                     -1, &st, nullptr);
  sqlite3_bind_text(st, 1, g.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(st, 2, g.kcal);
  sqlite3_bind_double(st, 3, g.protein);
  sqlite3_bind_double(st, 4, g.fat);
  sqlite3_bind_double(st, 5, g.carbs);
  sqlite3_bind_int(st, 6, g.id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
}

bool Database::delete_ingredient(int id) {
  exec("BEGIN;", {});
  exec("DELETE FROM meal_entries WHERE ingredient_id=?;", {std::to_string(id)});
  exec("DELETE FROM dish_ingredients WHERE ingredient_id=?;",
       {std::to_string(id)});
  bool ok = exec("DELETE FROM ingredients WHERE id=?;", {std::to_string(id)});
  exec(ok ? "COMMIT;" : "ROLLBACK;", {});
  return ok;
}

int Database::seed_ingredients_if_empty() {
  // Bail early if there's already data.
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(impl_->db, "SELECT COUNT(*) FROM ingredients;", -1,
                         &st, nullptr) != SQLITE_OK)
    return 0;
  int count = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
    count = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  if (count > 0)
    return 0;

  // name, kcal, protein, fat, carbs  (all per 100 g)
  struct Seed {
    const char *name;
    float k, p, f, c;
  };
  static const Seed seeds[] = {
      // ---- Eggs & dairy ----
      {"Egg, whole", 143, 12.6f, 9.5f, 0.7f},
      {"Egg white", 52, 10.9f, 0.2f, 0.7f},
      {"Egg yolk", 322, 15.9f, 26.5f, 3.6f},
      {"Milk, 2.5%", 52, 2.9f, 2.5f, 4.7f},
      {"Milk, 3.2%", 60, 3.2f, 3.2f, 4.7f},
      {"Skim milk", 34, 3.4f, 0.1f, 5.0f},
      {"Greek yogurt, plain", 59, 10.0f, 0.4f, 3.6f},
      {"Yogurt, plain", 61, 3.5f, 3.3f, 4.7f},
      {"Cottage cheese 5%", 121, 17.0f, 5.0f, 1.8f},
      {"Cream 10%", 119, 2.8f, 10.0f, 3.0f},
      {"Cream 20%", 206, 2.5f, 20.0f, 3.0f},
      {"Sour cream 20%", 206, 2.4f, 20.0f, 3.2f},
      {"Cheddar cheese", 403, 25.0f, 33.0f, 1.3f},
      {"Mozzarella", 280, 18.0f, 22.0f, 3.1f},
      {"Parmesan", 431, 38.0f, 29.0f, 4.1f},
      {"Feta", 264, 14.0f, 21.0f, 4.1f},
      {"Butter", 717, 0.9f, 81.0f, 0.1f},

      // ---- Meat & poultry ----
      {"Chicken breast", 165, 31.0f, 3.6f, 0.0f},
      {"Chicken thigh", 209, 26.0f, 10.9f, 0.0f},
      {"Chicken wing", 203, 30.5f, 8.1f, 0.0f},
      {"Turkey breast", 135, 30.0f, 1.0f, 0.0f},
      {"Beef, ground 15%", 250, 26.0f, 15.0f, 0.0f},
      {"Beef, ground 5%", 137, 21.4f, 5.0f, 0.0f},
      {"Beef steak", 271, 25.0f, 19.0f, 0.0f},
      {"Pork, lean", 143, 21.0f, 6.0f, 0.0f},
      {"Pork belly", 518, 9.3f, 53.0f, 0.0f},
      {"Bacon", 541, 37.0f, 42.0f, 1.4f},
      {"Ham", 145, 21.0f, 5.5f, 1.5f},
      {"Sausage", 301, 12.0f, 27.0f, 2.0f},
      {"Lamb", 294, 25.0f, 21.0f, 0.0f},
      {"Duck breast", 201, 24.0f, 11.0f, 0.0f},

      // ---- Fish & seafood ----
      {"Salmon", 208, 20.0f, 13.0f, 0.0f},
      {"Tuna, canned in water", 116, 26.0f, 0.8f, 0.0f},
      {"Tuna, canned in oil", 198, 29.0f, 8.2f, 0.0f},
      {"Cod", 82, 18.0f, 0.7f, 0.0f},
      {"Trout", 148, 21.0f, 6.6f, 0.0f},
      {"Mackerel", 205, 19.0f, 14.0f, 0.0f},
      {"Herring", 158, 18.0f, 9.0f, 0.0f},
      {"Sardines in oil", 208, 25.0f, 11.5f, 0.0f},
      {"Shrimp", 99, 24.0f, 0.3f, 0.2f},
      {"Squid", 92, 15.6f, 1.4f, 3.1f},
      {"Mussels", 86, 12.0f, 2.2f, 3.7f},
      {"Crab", 97, 19.0f, 1.5f, 0.0f},

      // ---- Grains & breads ----
      {"White rice, dry", 365, 7.1f, 0.7f, 80.0f},
      {"Brown rice, dry", 370, 7.9f, 2.9f, 77.2f},
      {"Buckwheat, dry", 343, 13.3f, 3.4f, 71.5f},
      {"Oats", 389, 16.9f, 6.9f, 66.3f},
      {"Pasta, dry", 371, 13.0f, 1.5f, 75.0f},
      {"Whole wheat pasta", 348, 14.0f, 2.5f, 68.0f},
      {"Couscous, dry", 376, 12.8f, 0.6f, 77.4f},
      {"Quinoa, dry", 368, 14.1f, 6.1f, 64.2f},
      {"Bread, white", 265, 9.0f, 3.2f, 49.0f},
      {"Bread, whole wheat", 247, 13.0f, 3.4f, 41.0f},
      {"Rye bread", 259, 8.5f, 3.3f, 48.0f},
      {"Baguette", 274, 8.9f, 1.0f, 55.0f},
      {"Flour, wheat", 364, 10.3f, 1.0f, 76.3f},
      {"Flour, rye", 350, 8.8f, 1.5f, 75.5f},
      {"Semolina", 360, 12.7f, 1.1f, 73.8f},

      // ---- Legumes ----
      {"Lentils, dry", 352, 24.6f, 1.1f, 60.1f},
      {"Chickpeas, dry", 364, 19.3f, 6.0f, 61.0f},
      {"Black beans, dry", 341, 21.6f, 1.4f, 62.4f},
      {"Kidney beans, dry", 333, 23.6f, 0.8f, 60.0f},
      {"White beans, dry", 333, 23.4f, 0.9f, 60.3f},
      {"Peas, green", 81, 5.4f, 0.4f, 14.5f},
      {"Green beans", 31, 1.8f, 0.2f, 7.0f},
      {"Soybeans, dry", 446, 36.5f, 20.0f, 30.2f},
      {"Tofu, firm", 144, 17.0f, 9.0f, 2.8f},

      // ---- Vegetables ----
      {"Potato", 87, 1.9f, 0.1f, 20.1f},
      {"Sweet potato", 90, 2.0f, 0.2f, 20.7f},
      {"Carrot", 41, 0.9f, 0.2f, 9.6f},
      {"Beetroot", 43, 1.6f, 0.2f, 9.6f},
      {"Broccoli", 34, 2.8f, 0.4f, 6.6f},
      {"Cauliflower", 25, 1.9f, 0.3f, 5.0f},
      {"Cabbage, white", 25, 1.3f, 0.1f, 5.8f},
      {"Cabbage, red", 31, 1.4f, 0.2f, 7.4f},
      {"Brussels sprouts", 43, 3.4f, 0.3f, 9.0f},
      {"Spinach", 23, 2.9f, 0.4f, 3.6f},
      {"Lettuce", 15, 1.4f, 0.2f, 2.9f},
      {"Arugula", 25, 2.6f, 0.7f, 3.7f},
      {"Kale", 49, 4.3f, 0.9f, 8.8f},
      {"Tomato", 18, 0.9f, 0.2f, 3.9f},
      {"Cherry tomato", 18, 0.9f, 0.2f, 3.9f},
      {"Cucumber", 15, 0.7f, 0.1f, 3.6f},
      {"Bell pepper, red", 31, 1.0f, 0.3f, 6.0f},
      {"Bell pepper, green", 20, 0.9f, 0.2f, 4.6f},
      {"Onion", 40, 1.1f, 0.1f, 9.3f},
      {"Red onion", 40, 1.1f, 0.1f, 9.3f},
      {"Garlic", 149, 6.4f, 0.5f, 33.1f},
      {"Leek", 61, 1.5f, 0.3f, 14.2f},
      {"Zucchini", 17, 1.2f, 0.3f, 3.1f},
      {"Eggplant", 25, 1.0f, 0.2f, 5.9f},
      {"Pumpkin", 26, 1.0f, 0.1f, 6.5f},
      {"Mushrooms", 22, 3.1f, 0.3f, 3.3f},
      {"Corn, sweet", 86, 3.3f, 1.4f, 19.0f},
      {"Avocado", 160, 2.0f, 14.7f, 8.5f},
      {"Olives, black", 115, 0.8f, 10.7f, 6.3f},

      // ---- Fruits ----
      {"Apple", 52, 0.3f, 0.2f, 14.0f},
      {"Pear", 57, 0.4f, 0.1f, 15.2f},
      {"Banana", 89, 1.1f, 0.3f, 22.8f},
      {"Orange", 47, 0.9f, 0.1f, 11.8f},
      {"Mandarin", 53, 0.8f, 0.3f, 13.3f},
      {"Lemon", 29, 1.1f, 0.3f, 9.3f},
      {"Grapefruit", 42, 0.8f, 0.1f, 10.7f},
      {"Grapes", 69, 0.7f, 0.2f, 18.1f},
      {"Strawberry", 32, 0.7f, 0.3f, 7.7f},
      {"Blueberry", 57, 0.7f, 0.3f, 14.5f},
      {"Raspberry", 52, 1.2f, 0.7f, 11.9f},
      {"Blackberry", 43, 1.4f, 0.5f, 9.6f},
      {"Cherry", 63, 1.1f, 0.2f, 16.0f},
      {"Peach", 39, 0.9f, 0.3f, 9.5f},
      {"Plum", 46, 0.7f, 0.3f, 11.4f},
      {"Apricot", 48, 1.4f, 0.4f, 11.1f},
      {"Kiwi", 61, 1.1f, 0.5f, 14.7f},
      {"Mango", 60, 0.8f, 0.4f, 15.0f},
      {"Pineapple", 50, 0.5f, 0.1f, 13.1f},
      {"Watermelon", 30, 0.6f, 0.2f, 7.6f},
      {"Melon", 34, 0.8f, 0.2f, 8.2f},
      {"Pomegranate", 83, 1.7f, 1.2f, 18.7f},

      // ---- Nuts & seeds ----
      {"Almonds", 579, 21.0f, 50.0f, 22.0f},
      {"Walnuts", 654, 15.0f, 65.0f, 14.0f},
      {"Hazelnuts", 628, 15.0f, 61.0f, 17.0f},
      {"Cashews", 553, 18.0f, 44.0f, 30.0f},
      {"Peanuts", 567, 26.0f, 49.0f, 16.0f},
      {"Pistachios", 560, 20.0f, 45.0f, 28.0f},
      {"Sunflower seeds", 584, 21.0f, 51.0f, 20.0f},
      {"Pumpkin seeds", 559, 30.0f, 49.0f, 11.0f},
      {"Sesame seeds", 573, 17.0f, 50.0f, 23.0f},
      {"Chia seeds", 486, 17.0f, 31.0f, 42.0f},
      {"Flax seeds", 534, 18.0f, 42.0f, 29.0f},
      {"Peanut butter", 588, 25.0f, 50.0f, 20.0f},

      // ---- Oils & fats ----
      {"Olive oil", 884, 0.0f, 100.0f, 0.0f},
      {"Sunflower oil", 884, 0.0f, 100.0f, 0.0f},
      {"Vegetable oil", 884, 0.0f, 100.0f, 0.0f},
      {"Coconut oil", 862, 0.0f, 100.0f, 0.0f},
      {"Lard", 902, 0.0f, 100.0f, 0.0f},

      // ---- Sweeteners & misc ----
      {"Sugar, white", 387, 0.0f, 0.0f, 100.0f},
      {"Sugar, brown", 380, 0.0f, 0.0f, 98.0f},
      {"Honey", 304, 0.3f, 0.0f, 82.4f},
      {"Maple syrup", 260, 0.0f, 0.1f, 67.0f},
      {"Dark chocolate 70%", 598, 7.8f, 43.0f, 46.0f},
      {"Milk chocolate", 535, 7.7f, 30.0f, 59.0f},
      {"Ketchup", 101, 1.0f, 0.1f, 24.0f},
      {"Mayonnaise", 680, 1.0f, 75.0f, 1.0f},
      {"Soy sauce", 53, 8.1f, 0.6f, 4.9f},
      {"Tomato paste", 82, 4.3f, 0.5f, 19.0f},
      {"Tomato sauce", 24, 1.2f, 0.2f, 5.3f},
      {"Coconut milk", 230, 2.3f, 24.0f, 5.5f},
      {"Cocoa powder", 228, 19.6f, 13.7f, 57.9f},
  };

  std::vector<Ingredient> batch;
  batch.reserve(sizeof(seeds) / sizeof(seeds[0]));
  for (const auto &s : seeds) {
    Ingredient g;
    g.name = s.name;
    g.kcal = s.k;
    g.protein = s.p;
    g.fat = s.f;
    g.carbs = s.c;
    batch.push_back(std::move(g));
  }

  int added = bulk_insert_ingredients(batch);
  fprintf(stderr, "[db] Seeded %d starter ingredients\n", added);
  return added;
}

// ===== Dishes =====

std::vector<Dish> Database::load_dishes() {
  std::vector<Dish> out;
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(impl_->db, "SELECT id,name FROM dishes ORDER BY name;",
                         -1, &st, nullptr) != SQLITE_OK)
    return out;
  while (sqlite3_step(st) == SQLITE_ROW) {
    Dish d;
    d.id = sqlite3_column_int(st, 0);
    d.name = (const char *)sqlite3_column_text(st, 1);
    out.push_back(d);
  }
  sqlite3_finalize(st);

  for (auto &d : out) {
    sqlite3_stmt *st2 = nullptr;
    if (sqlite3_prepare_v2(impl_->db,
                           "SELECT ingredient_id, weight_g FROM "
                           "dish_ingredients WHERE dish_id=?;",
                           -1, &st2, nullptr) != SQLITE_OK)
      continue;
    sqlite3_bind_int(st2, 1, d.id);
    while (sqlite3_step(st2) == SQLITE_ROW) {
      DishIngredient di;
      di.ingredient_id = sqlite3_column_int(st2, 0);
      di.weight_g = (float)sqlite3_column_double(st2, 1);
      d.ingredients.push_back(di);
    }
    sqlite3_finalize(st2);
  }
  return out;
}

int Database::insert_dish(const Dish &d) {
  exec("BEGIN;", {});
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(impl_->db, "INSERT INTO dishes(name) VALUES(?);", -1, &st,
                     nullptr);
  sqlite3_bind_text(st, 1, d.name.c_str(), -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) {
    exec("ROLLBACK;", {});
    return -1;
  }

  int new_id = (int)sqlite3_last_insert_rowid(impl_->db);
  if (!write_composition(new_id, d.ingredients)) {
    exec("ROLLBACK;", {});
    return -1;
  }
  exec("COMMIT;", {});
  return new_id;
}

bool Database::update_dish(const Dish &d) {
  exec("BEGIN;", {});
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(impl_->db, "UPDATE dishes SET name=? WHERE id=?;", -1, &st,
                     nullptr);
  sqlite3_bind_text(st, 1, d.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, d.id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) {
    exec("ROLLBACK;", {});
    return false;
  }

  exec("DELETE FROM dish_ingredients WHERE dish_id=?;", {std::to_string(d.id)});
  if (!write_composition(d.id, d.ingredients)) {
    exec("ROLLBACK;", {});
    return false;
  }
  exec("COMMIT;", {});
  return true;
}

bool Database::delete_dish(int id) {
  exec("BEGIN;", {});
  exec("DELETE FROM meal_entries WHERE dish_id=?;", {std::to_string(id)});
  exec("DELETE FROM dish_ingredients WHERE dish_id=?;", {std::to_string(id)});
  bool ok = exec("DELETE FROM dishes WHERE id=?;", {std::to_string(id)});
  exec(ok ? "COMMIT;" : "ROLLBACK;", {});
  return ok;
}

// ===== Meal entries =====

std::vector<MealEntry> Database::load_entries(const std::string &date,
                                              int meal) {
  std::vector<MealEntry> out;
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(
          impl_->db,
          "SELECT id, dish_id, ingredient_id, weight_g FROM meal_entries "
          "WHERE date=? AND meal=? ORDER BY id;",
          -1, &st, nullptr) != SQLITE_OK)
    return out;
  sqlite3_bind_text(st, 1, date.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, meal);
  while (sqlite3_step(st) == SQLITE_ROW) {
    MealEntry e;
    e.id = sqlite3_column_int(st, 0);
    e.dish_id = sqlite3_column_int(st, 1);       // 0 if NULL
    e.ingredient_id = sqlite3_column_int(st, 2); // 0 if NULL
    e.weight_g = (float)sqlite3_column_double(st, 3);
    out.push_back(e);
  }
  sqlite3_finalize(st);
  return out;
}

int Database::insert_entry(const std::string &date, int meal, int dish_id,
                           int ingredient_id, float weight_g) {
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(
      impl_->db,
      "INSERT INTO meal_entries(date,meal,dish_id,ingredient_id,weight_g) "
      "VALUES(?,?,?,?,?);",
      -1, &st, nullptr);
  sqlite3_bind_text(st, 1, date.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, meal);
  if (dish_id > 0)
    sqlite3_bind_int(st, 3, dish_id);
  else
    sqlite3_bind_null(st, 3);
  if (ingredient_id > 0)
    sqlite3_bind_int(st, 4, ingredient_id);
  else
    sqlite3_bind_null(st, 4);
  sqlite3_bind_double(st, 5, weight_g);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE ? (int)sqlite3_last_insert_rowid(impl_->db) : -1;
}

bool Database::update_entry(int id, int dish_id, int ingredient_id,
                            float weight_g) {
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(impl_->db,
                     "UPDATE meal_entries SET dish_id=?, ingredient_id=?, "
                     "weight_g=? WHERE id=?;",
                     -1, &st, nullptr);
  if (dish_id > 0)
    sqlite3_bind_int(st, 1, dish_id);
  else
    sqlite3_bind_null(st, 1);
  if (ingredient_id > 0)
    sqlite3_bind_int(st, 2, ingredient_id);
  else
    sqlite3_bind_null(st, 2);
  sqlite3_bind_double(st, 3, weight_g);
  sqlite3_bind_int(st, 4, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
}

bool Database::delete_entry(int id) {
  return exec("DELETE FROM meal_entries WHERE id=?;", {std::to_string(id)});
}

// ===== Private helpers =====

bool Database::write_composition(int dish_id,
                                 const std::vector<DishIngredient> &comp) {
  for (const auto &di : comp) {
    sqlite3_stmt *st = nullptr;
    sqlite3_prepare_v2(impl_->db,
                       "INSERT OR REPLACE INTO "
                       "dish_ingredients(dish_id,ingredient_id,weight_g) "
                       "VALUES(?,?,?);",
                       -1, &st, nullptr);
    sqlite3_bind_int(st, 1, dish_id);
    sqlite3_bind_int(st, 2, di.ingredient_id);
    sqlite3_bind_double(st, 3, di.weight_g);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE)
      return false;
  }
  return true;
}

std::string Database::get_metadata(const std::string &key) {
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(impl_->db, "SELECT value FROM metadata WHERE key=?;",
                         -1, &st, nullptr) != SQLITE_OK)
    return "";
  sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  std::string result;
  if (sqlite3_step(st) == SQLITE_ROW)
    result = (const char *)sqlite3_column_text(st, 0);
  sqlite3_finalize(st);
  return result;
}

bool Database::set_metadata(const std::string &key, const std::string &value) {
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(
          impl_->db, "INSERT OR REPLACE INTO metadata(key,value) VALUES(?,?);",
          -1, &st, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, value.c_str(), -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
}

int Database::bulk_insert_ingredients(const std::vector<Ingredient> &batch) {
  exec("BEGIN;", {});
  sqlite3_stmt *check = nullptr;
  sqlite3_prepare_v2(impl_->db,
                     "SELECT id FROM ingredients WHERE name=? LIMIT 1;", -1,
                     &check, nullptr);

  sqlite3_stmt *ins = nullptr;
  sqlite3_prepare_v2(
      impl_->db,
      "INSERT INTO ingredients(name,kcal,protein,fat,carbs) VALUES(?,?,?,?,?);",
      -1, &ins, nullptr);

  int added = 0;
  for (const auto &g : batch) {
    // Skip duplicates by name
    sqlite3_reset(check);
    sqlite3_bind_text(check, 1, g.name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(check) == SQLITE_ROW)
      continue;

    sqlite3_reset(ins);
    sqlite3_bind_text(ins, 1, g.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(ins, 2, g.kcal);
    sqlite3_bind_double(ins, 3, g.protein);
    sqlite3_bind_double(ins, 4, g.fat);
    sqlite3_bind_double(ins, 5, g.carbs);
    if (sqlite3_step(ins) == SQLITE_DONE)
      added++;
  }

  sqlite3_finalize(check);
  sqlite3_finalize(ins);
  exec("COMMIT;", {});
  return added;
}

bool Database::create_schema() {
  const char *sql =
      "PRAGMA foreign_keys = ON;"

      "CREATE TABLE IF NOT EXISTS metadata ("
      "  key   TEXT PRIMARY KEY,"
      "  value TEXT"
      ");"

      "CREATE TABLE IF NOT EXISTS ingredients ("
      "  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  name    TEXT NOT NULL,"
      "  kcal    REAL NOT NULL DEFAULT 0,"
      "  protein REAL NOT NULL DEFAULT 0,"
      "  fat     REAL NOT NULL DEFAULT 0,"
      "  carbs   REAL NOT NULL DEFAULT 0"
      ");"

      "CREATE TABLE IF NOT EXISTS dishes ("
      "  id   INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  name TEXT NOT NULL"
      ");"

      "CREATE TABLE IF NOT EXISTS dish_ingredients ("
      "  dish_id       INTEGER NOT NULL REFERENCES dishes(id) ON DELETE "
      "CASCADE,"
      "  ingredient_id INTEGER NOT NULL REFERENCES ingredients(id) ON DELETE "
      "CASCADE,"
      "  weight_g      REAL NOT NULL DEFAULT 100,"
      "  PRIMARY KEY (dish_id, ingredient_id)"
      ");"

      "CREATE TABLE IF NOT EXISTS meal_entries ("
      "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  date          TEXT NOT NULL,"
      "  meal          INTEGER NOT NULL,"
      "  dish_id       INTEGER REFERENCES dishes(id)      ON DELETE CASCADE,"
      "  ingredient_id INTEGER REFERENCES ingredients(id) ON DELETE CASCADE,"
      "  weight_g      REAL NOT NULL DEFAULT 100,"
      "  CHECK ((dish_id IS NULL) != (ingredient_id IS NULL))"
      ");"

      "CREATE INDEX IF NOT EXISTS idx_entries_date ON meal_entries(date);";

  char *err = nullptr;
  if (sqlite3_exec(impl_->db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    fprintf(stderr, "Schema error: %s\n", err);
    sqlite3_free(err);
    return false;
  }
  return true;
}

bool Database::exec(const std::string &sql,
                    const std::vector<std::string> &args) {
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
    return false;
  for (size_t i = 0; i < args.size(); ++i)
    sqlite3_bind_text(st, (int)i + 1, args[i].c_str(), -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE || rc == SQLITE_ROW;
}
