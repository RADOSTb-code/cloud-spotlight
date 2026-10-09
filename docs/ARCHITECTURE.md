# Cloud Spotlight — архитектура

Нативный C++20 лаунчер для Windows 11 в стиле macOS Spotlight. Без фреймворков: Win32 + Direct2D/DirectWrite +
DWM (Acrylic/Mica, скруглённые углы). Цели: старт < 100 мс, поиск на клавишу < 5 мс, ~15–30 МБ RAM, один .exe
без зависимостей (статический CRT).

## Сборка

```
# Windows (MSVC, VS 2022):
cmake -B build -A x64 && cmake --build build --config Release
# Кросс-компиляция с Linux (MinGW-w64, для CI/проверки):
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake && cmake --build build-win -j
# Юнит-тесты портируемого кода (Linux/Windows):
cmake -B build && cmake --build build -j && ./build/cs_tests
```

Код должен компилироваться **и MSVC, и MinGW-w64 (g++ 13)** без предупреждений. В заголовках MinGW нет некоторых
новых констант (например `DWMWA_SYSTEMBACKDROP_TYPE = 38`, `DWMWA_WINDOW_CORNER_PREFERENCE = 33`,
`DWMWA_USE_IMMERSIVE_DARK_MODE = 20`) — объявляйте их локально (`constexpr DWORD kX = 38;`), не полагаясь на SDK.

## Слои и правила

| Папка | Что | Windows-заголовки |
|---|---|---|
| `src/core/` | контракты (`Types.h`, `Config.h`), `Str`, `Fuzzy`, `Json`, `SearchEngine` | **запрещены** |
| `src/logic/` | чистая логика: парсер калькулятора, разбор валютных запросов, разбор команд, каталог настроек | **запрещены** (тестируется на Linux) |
| `src/platform/` | `Win.h` — общие Win32-хелперы (ShellOpen, RevealInExplorer, RunHidden, ExpandEnv, KnownFolder, ComInit) | да |
| `src/providers/` | реализации `IProvider` (Win32-часть) | да |
| `src/ui/` | окно лаунчера, рендеринг, иконки | да |
| `src/app/` | хост: конфиг, горячие клавиши, трей, автозапуск, IHost | да |
| `src/main.cpp` | `wWinMain` | да |
| `tests/` | `TEST(...)` из `tests/Test.h`, только для `core/` и `logic/` | нет |

CMake собирает файлы через glob — новые `.cpp` в этих папках подхватываются автоматически (перезапустите cmake).

## Потоки

* **UI-поток** (единственный message loop): хоткей → показ окна; каждое нажатие клавиши →
  `SearchEngine::Query()` синхронно вызывает `IProvider::Search()` у всех провайдеров. Бюджет провайдера ~2 мс,
  поэтому всё тяжёлое (обход диска, перечисление приложений, HTTP курсов) — в фоновых потоках провайдера, а
  `Search()` читает готовый снимок под `std::shared_mutex`/`std::atomic<std::shared_ptr>`.
* Фоновый поток провайдера, получив новые данные, может вызвать `IHost::RequestRefresh()` (потокобезопасно) —
  UI перезапустит текущий запрос.
* Иконки грузит отдельный поток UI-модуля (COM STA), готовые битмапы передаются в UI через `PostMessage`.

## Контракты (см. `src/core/Types.h`)

`Result.score` от провайдера — 0..1. Рекомендуемые шкалы, чтобы источники смешивались разумно:

| Провайдер | category | score |
|---|---|---|
| Калькулятор (валидное выражение с оператором/функцией) | `Калькулятор` | 0.99 |
| Конвертер валют / единиц | `Конвертер` | 0.98 |
| Быстрые команды (явная фраза) | `Команды` | 0.96 при полном совпадении, иначе fuzzy × 0.95 |
| Приложения | `Приложения` | fuzzy × 1.0 |
| Настройки / системные инструменты | `Настройки` | fuzzy × 0.9 |
| Папки | `Папки` | fuzzy × 0.84 |
| Файлы | `Файлы` | fuzzy × 0.8 |
| Поиск в интернете (fallback, всегда последний) | `Интернет` | 0.02 |

`SearchEngine` добавляет до ~+0.5 за историю выбора (частота, недавность, «этот запрос → этот элемент»), сортирует,
убирает дубликаты по `key` и обрезает до `maxResults`. Пустой `key` = не учитывать историю.

Иконки: `IconKind::Glyph` — символ шрифта **Segoe Fluent Icons** (Win11; UI откатывается на Segoe MDL2 Assets);
`FilePath` — путь, иконку достаёт оболочка; `ShellItem` — parsing name (`shell:AppsFolder\<AUMID>`).
Полезные глифы: калькулятор ``, валюта ``/``, питание ``, перезагрузка ``,
сон ``, замок ``, папка ``, файл ``, настройки ``, глобус ``,
поиск ``, таймер ``, терминал ``, корзина ``, приложение ``, выход ``.

Действия (`Action`): Enter — `Open`, Ctrl+Enter — `Reveal`, Ctrl+Shift+Enter — `RunAsAdmin`,
Ctrl+C (без выделения в поле) — `Copy`. UI показывает подсказки только для действий из `Result.actions`.
Для калькулятора/конвертера Enter = копировать результат (провайдер сам вызывает `IHost::CopyToClipboard`).

## Модули и владельцы

* **App shell** — `src/main.cpp`, `src/app/*`, `res/*` (manifest, .rc, .ico), `scripts/`.
* **UI** — `src/ui/*`. Публичный интерфейс — `src/ui/LauncherWindow.h` (ниже).
* **Providers A** — `src/providers/{Apps,Files,Settings}Provider.*`, `src/logic/SettingsCatalog.*`.
* **Providers B** — `src/providers/{Calculator,Currency,Commands,WebSearch}Provider.*`,
  `src/logic/{Calc,CurrencyQuery,CommandParse}.*` (+ конвертер единиц, если есть).
* Общие файлы (`core/`, `platform/`, `CMakeLists.txt`) правит только интегратор — если что-то нужно, опишите в отчёте.

### `src/ui/LauncherWindow.h` (интерфейс, реализует UI, использует App shell)

```cpp
namespace cs {
class LauncherWindow {
 public:
  LauncherWindow(SearchEngine& engine, IHost& host);
  ~LauncherWindow();
  bool Create(HINSTANCE inst, const Config& cfg);  // создаёт скрытое окно и D2D-ресурсы заранее (мгновенный показ)
  void Toggle();                  // хоткей
  void Show();
  void Hide();
  bool IsVisible() const;
  void Refresh();                 // UI-поток: повторить текущий запрос (вызывается хостом по RequestRefresh)
  void ApplyConfig(const Config& cfg);  // тема/backdrop/visibleRows изменились
  void OnSystemThemeChanged();    // WM_SETTINGCHANGE "ImmersiveColorSet"
  HWND Hwnd() const;
};
}
```

### Провайдеры (заголовки с классами)

```cpp
// src/providers/AppsProvider.h      class AppsProvider final : public IProvider { ... };
// src/providers/FilesProvider.h     class FilesProvider final : public IProvider { ... };
// src/providers/SettingsProvider.h  class SettingsProvider final : public IProvider { ... };
// src/providers/CalculatorProvider.h class CalculatorProvider final : public IProvider { ... };
// src/providers/CurrencyProvider.h  class CurrencyProvider final : public IProvider { ... };
// src/providers/CommandsProvider.h  class CommandsProvider final : public IProvider { ... };
// src/providers/WebSearchProvider.h class WebSearchProvider final : public IProvider { ... };
```

Все — с конструктором по умолчанию; конфигурация приходит в `Init(cfg, host)`.
Порядок регистрации в `main`: Calculator, Currency, Commands, Apps, Settings, Files, WebSearch.

## Конфиг

`%APPDATA%\CloudSpotlight\config.json` (JSON с комментариями допускается). Ключи — camelCase-имена полей
`cs::Config` (`hotkey`, `fallbackHotkey`, `theme`, `backdrop`, `autostart`, `maxResults`, `visibleRows`,
`fileRoots`, `fileExclude`, `fileMaxDepth`, `fileReindexMinutes`, `fileMaxEntries`,
`folders: [{name, path}]`, `commands: [{keywords, title, target, args, admin}]`, `currencyBase`, `currencyApi`,
`currencyCacheHours`, `webSearch`). Если файла нет — создаётся с дефолтами. История выбора — `usage.tsv`,
кэш курсов — `rates.json` в той же папке.
