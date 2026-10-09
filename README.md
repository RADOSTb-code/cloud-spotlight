# Cloud Spotlight

Spotlight из macOS — для Windows 11. Нативный C++20 (Win32 + Direct2D + DWM Acrylic), без фреймворков и
рантаймов: один `.exe`, мгновенный показ, минимум памяти в фоне.

**Alt+Space** (запасной вариант — **Ctrl+Space**) → начинайте печатать.

## Что умеет

| Пишете | Получаете |
|---|---|
| `хром`, `vsc`, `ntktuhfv` (не та раскладка) | приложения из меню Пуск, включая Store-приложения |
| `отчёт`, `readme`, `C:\Users\`, `~\Downloads\` | файлы и папки (свой компактный индекс + навигация по пути) |
| `яркость`, `блютуз`, `автозагрузка`, `реестр` | страницы Параметров Windows и системные утилиты |
| `2*(3+4)^2`, `200 + 15%`, `sin 30°`, `0xFF + 1` | калькулятор (Enter — скопировать результат) |
| `100 usd`, `100$ в евро`, `1к юаней в рубли` | конвертер валют (курсы кэшируются, работают офлайн) |
| `10 km в милях`, `30°C в F`, `1,5 гб в мб` | конвертер единиц |
| `выключить через 30 минут`, `перезагрузка в 23:30`, `отменить выключение` | таймер выключения/перезагрузки |
| `сон`, `заблокировать`, `выйти из системы`, `очистить корзину`, `выключить монитор` | системные команды |
| `таймер 25 минут`, `напомни через 10 минут чайник` | напоминания |
| `открыть папку проектов`, `загрузки`, `автозагрузка` | быстрые папки (свои — в конфиге) |
| `g запрос`, `yt …`, `gh …`, `вики …`, `github.com` | поиск в интернете / открыть сайт |
| `пароль 20`, `guid`, `мой ip` | утилиты |

Лаунчер запоминает, что вы выбираете: то, что вы открыли по запросу `т`, в следующий раз окажется первым.

### Клавиши

| | |
|---|---|
| `↑` `↓` / `PgUp` `PgDn` | выбор |
| `Enter` | открыть |
| `Ctrl+Enter` | показать в проводнике |
| `Ctrl+Shift+Enter` | запустить от администратора |
| `Ctrl+C` (без выделения) | скопировать путь / результат |
| `Tab` | дополнить запрос |
| `Ctrl+1…9` | открыть n-й результат |
| `Esc` | очистить, повторно — скрыть |

## Установка

1. Скачайте `CloudSpotlight.exe` из артефактов GitHub Actions (workflow **build**) или соберите сами (ниже).
2. Положите в любую папку и запустите. Иконка появится в трее; автозапуск включается сам
   (переключается в меню трея или ключом `autostart` в конфиге).

## Настройка

Трей → «Настройки…» откроет `%APPDATA%\CloudSpotlight\config.json` (изменения применяются на лету):

```jsonc
{
  "hotkey": "Alt+Space",            // или "Ctrl+Space", "Win+Shift+K"…
  "fallbackHotkey": "Ctrl+Space",
  "theme": "system",                // system | light | dark
  "backdrop": "acrylic",            // acrylic | mica | none
  "autostart": true,
  "fileRoots": ["%USERPROFILE%\\Desktop", "%USERPROFILE%\\Documents", "%USERPROFILE%\\Projects"],
  "folders": [ { "name": "проекты", "path": "%USERPROFILE%\\Projects" } ],
  "commands": [
    { "keywords": ["терминал", "wt"], "title": "Windows Terminal", "target": "wt.exe", "args": "", "admin": false }
  ],
  "currencyBase": "RUB",
  "webSearch": "https://www.google.com/search?q={q}"
}
```

## Сборка

```powershell
# Visual Studio 2022 + CMake
cmake -B build -A x64
cmake --build build --config Release
build\Release\CloudSpotlight.exe
```

С Linux (MinGW-w64): `cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake && cmake --build build-win -j`.
Тесты переносимой логики: `cmake -B build && cmake --build build -j && ./build/cs_tests`.

## Расширение

Новый источник результатов = класс, реализующий `cs::IProvider` (`src/core/Types.h`): `Search()` добавляет
`Result`'ы, `Execute()` выполняет выбранный. Зарегистрируйте его в `src/app/App.cpp`. Подробности, потоки и
шкалы релевантности — в [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
