#include "logic/SettingsCatalog.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

#include "core/Str.h"

namespace cs::settings {
namespace {

// Sections (Windows 11, Russian UI)
constexpr const wchar_t* kSys = L"Система";
constexpr const wchar_t* kDev = L"Bluetooth и устройства";
constexpr const wchar_t* kNet = L"Сеть и Интернет";
constexpr const wchar_t* kPers = L"Персонализация";
constexpr const wchar_t* kApps = L"Приложения";
constexpr const wchar_t* kAcc = L"Учётные записи";
constexpr const wchar_t* kTime = L"Время и язык";
constexpr const wchar_t* kGame = L"Игры";
constexpr const wchar_t* kEase = L"Специальные возможности";
constexpr const wchar_t* kPriv = L"Конфиденциальность и защита";
constexpr const wchar_t* kUpd = L"Центр обновления Windows";
constexpr const wchar_t* kTool = nullptr;

// Segoe Fluent Icons
constexpr const wchar_t* G_SET = L"\uE713";
constexpr const wchar_t* G_DISPLAY = L"\uE7F4";
constexpr const wchar_t* G_BT = L"\uE702";
constexpr const wchar_t* G_WIFI = L"\uE701";
constexpr const wchar_t* G_ETH = L"\uE839";
constexpr const wchar_t* G_VPN = L"\uE705";
constexpr const wchar_t* G_SOUND = L"\uE767";
constexpr const wchar_t* G_MIC = L"\uE720";
constexpr const wchar_t* G_CAM = L"\uE722";
constexpr const wchar_t* G_BELL = L"\uEA8F";
constexpr const wchar_t* G_MOON = L"\uE708";
constexpr const wchar_t* G_POWER = L"\uE7E8";
constexpr const wchar_t* G_BATT = L"\uE83F";
constexpr const wchar_t* G_DRIVE = L"\uEDA2";
constexpr const wchar_t* G_PASTE = L"\uE77F";
constexpr const wchar_t* G_PERS = L"\uE771";
constexpr const wchar_t* G_COLOR = L"\uE790";
constexpr const wchar_t* G_PIC = L"\uE8B9";
constexpr const wchar_t* G_LOCK = L"\uE72E";
constexpr const wchar_t* G_FONT = L"\uE8D2";
constexpr const wchar_t* G_APPS = L"\uE71D";
constexpr const wchar_t* G_USER = L"\uE77B";
constexpr const wchar_t* G_PEOPLE = L"\uE716";
constexpr const wchar_t* G_CLOCK = L"\uE823";
constexpr const wchar_t* G_GLOBE = L"\uE774";
constexpr const wchar_t* G_KEYB = L"\uE765";
constexpr const wchar_t* G_GAME = L"\uE7FC";
constexpr const wchar_t* G_EASE = L"\uE776";
constexpr const wchar_t* G_PIN = L"\uE707";
constexpr const wchar_t* G_SHIELD = L"\uEA18";
constexpr const wchar_t* G_UPDATE = L"\uE895";
constexpr const wchar_t* G_HIST = L"\uE81C";
constexpr const wchar_t* G_RECOV = L"\uE777";
constexpr const wchar_t* G_INFO = L"\uE946";
constexpr const wchar_t* G_REPAIR = L"\uE90F";
constexpr const wchar_t* G_DEVTOOL = L"\uEC7A";
constexpr const wchar_t* G_MOUSE = L"\uE962";
constexpr const wchar_t* G_PRINT = L"\uE749";
constexpr const wchar_t* G_USB = L"\uE88E";
constexpr const wchar_t* G_PEN = L"\uE76D";
constexpr const wchar_t* G_DEVICES = L"\uE772";
constexpr const wchar_t* G_BRIGHT = L"\uE706";
constexpr const wchar_t* G_CMD = L"\uE756";
constexpr const wchar_t* G_DIAG = L"\uE9D9";
constexpr const wchar_t* G_SYS = L"\uE770";
constexpr const wchar_t* G_ADMIN = L"\uE7EF";
constexpr const wchar_t* G_DEL = L"\uE74D";
constexpr const wchar_t* G_CONNECT = L"\uE703";
constexpr const wchar_t* G_PLANE = L"\uE709";

constexpr Entry kCatalog[] = {
    // --- Система
    {L"ms-settings:display", L"", L"Дисплей", L"Display", kSys,
     L"экран|монитор|яркость|разрешение|разрешение экрана|масштаб|масштабирование|ориентация|несколько мониторов|hdr|"
     L"частота обновления|герцовка|brightness|resolution|scale|scaling|monitor|screen|refresh rate",
     G_DISPLAY, false},
    {L"ms-settings:nightlight", L"", L"Ночной свет", L"Night light", kSys,
     L"ночной режим|синий свет|теплый цвет|night mode|blue light", G_MOON, false},
    {L"ms-settings:display-advancedgraphics", L"", L"Графика", L"Graphics settings", kSys,
     L"видеокарта|gpu|графический процессор|производительность графики|graphics", G_DISPLAY, false},
    {L"ms-settings:sound", L"", L"Звук", L"Sound", kSys,
     L"громкость|динамики|колонки|наушники|микрофон|аудио|вывод звука|volume|speakers|headphones|audio|output",
     G_SOUND, false},
    {L"ms-settings:apps-volume", L"", L"Громкость приложений", L"App volume and device preferences", kSys,
     L"микшер громкости|микшер|volume mixer|mixer", G_SOUND, false},
    {L"ms-settings:notifications", L"", L"Уведомления", L"Notifications", kSys,
     L"оповещения|баннеры|не беспокоить|notification|alerts|do not disturb", G_BELL, false},
    {L"ms-settings:quiethours", L"", L"Фокусировка внимания", L"Focus assist", kSys,
     L"фокусировка|не беспокоить|тихие часы|focus|quiet hours|do not disturb", G_MOON, false},
    {L"ms-settings:powersleep", L"", L"Питание и батарея", L"Power & battery", kSys,
     L"питание|сон|спящий режим|отключение экрана|режим питания|электропитание|power|sleep|battery|power mode",
     G_POWER, false},
    {L"ms-settings:batterysaver", L"", L"Экономия заряда", L"Battery saver", kSys,
     L"батарея|аккумулятор|энергосбережение|заряд|battery|energy saver", G_BATT, false},
    {L"ms-settings:storagesense", L"", L"Память", L"Storage", kSys,
     L"хранилище|диск|место на диске|контроль памяти|освободить место|временные файлы|storage sense|disk space|"
     L"free up space|temporary files",
     G_DRIVE, false},
    {L"ms-settings:disksandvolumes", L"", L"Диски и тома", L"Disks & volumes", kSys,
     L"разделы|тома|диски|partitions|volumes|disks", G_DRIVE, false},
    {L"ms-settings:savelocations", L"", L"Место сохранения нового содержимого", L"Where new content is saved", kSys,
     L"куда сохранять|сохранение по умолчанию|save locations", G_DRIVE, false},
    {L"ms-settings:multitasking", L"", L"Многозадачность", L"Multitasking", kSys,
     L"прикрепление окон|snap|alt tab|виртуальные рабочие столы|рабочие столы|snap windows|virtual desktops",
     G_SET, false},
    {L"ms-settings:project", L"", L"Проецирование на этот компьютер", L"Projecting to this PC", kSys,
     L"проецирование|miracast|беспроводной дисплей|wireless display|projecting", G_CONNECT, false},
    {L"ms-settings:remotedesktop", L"", L"Удалённый рабочий стол (параметры)", L"Remote Desktop settings", kSys,
     L"rdp|удаленный доступ|удаленный рабочий стол|remote desktop|remote access", G_CONNECT, false},
    {L"ms-settings:clipboard", L"", L"Буфер обмена", L"Clipboard", kSys,
     L"журнал буфера обмена|копировать вставить|clipboard history|win v|copy paste", G_PASTE, false},
    {L"ms-settings:about", L"", L"О системе", L"About", kSys,
     L"о компьютере|характеристики|имя компьютера|переименовать компьютер|версия windows|процессор|оперативная память|"
     L"озу|система|about pc|device specifications|rename pc|windows version|specs|ram|cpu|system info",
     G_INFO, false},
    {L"ms-settings:activation", L"", L"Активация", L"Activation", kSys,
     L"ключ продукта|лицензия|активировать windows|product key|license|activate", G_SYS, false},
    {L"ms-settings:troubleshoot", L"", L"Устранение неполадок", L"Troubleshoot", kSys,
     L"неполадки|диагностика|починить|исправить|troubleshooter|fix problems|diagnose", G_REPAIR, false},
    {L"ms-settings:recovery", L"", L"Восстановление", L"Recovery", kSys,
     L"сброс|вернуть компьютер в исходное состояние|переустановка|особые варианты загрузки|reset this pc|reset|"
     L"advanced startup|reinstall",
     G_RECOV, false},
    {L"ms-settings:optionalfeatures", L"", L"Дополнительные компоненты", L"Optional features", kSys,
     L"компоненты windows|дополнительные функции|optional features|windows features", G_APPS, false},
    {L"ms-settings:developers", L"", L"Для разработчиков", L"For developers", kSys,
     L"режим разработчика|разработчик|developer mode|developers|dev mode|sudo", G_DEVTOOL, false},
    {L"ms-settings:windowsinsider", L"", L"Программа предварительной оценки Windows", L"Windows Insider Program", kUpd,
     L"инсайдер|insider|preview builds|предварительные сборки", G_UPDATE, false},

    // --- Bluetooth и устройства
    {L"ms-settings:bluetooth", L"", L"Bluetooth", L"Bluetooth", kDev,
     L"блютуз|блютус|bluetooth|bt|беспроводные наушники|подключить наушники|сопряжение|pair|pairing", G_BT, false},
    {L"ms-settings:connecteddevices", L"", L"Устройства", L"Devices", kDev,
     L"подключенные устройства|добавить устройство|connected devices|add device", G_DEVICES, false},
    {L"ms-settings:printers", L"", L"Принтеры и сканеры", L"Printers & scanners", kDev,
     L"принтер|печать|сканер|printer|print|scanner", G_PRINT, false},
    {L"ms-settings:mobile-devices", L"", L"Мобильные устройства", L"Mobile devices", kDev,
     L"телефон|связь с телефоном|смартфон|phone link|your phone|phone", G_DEVICES, false},
    {L"ms-settings:mousetouchpad", L"", L"Мышь", L"Mouse", kDev,
     L"мышка|курсор|скорость указателя|прокрутка колесико|основная кнопка|mouse|pointer speed|scroll", G_MOUSE, false},
    {L"ms-settings:devices-touchpad", L"", L"Сенсорная панель", L"Touchpad", kDev,
     L"тачпад|жесты|touchpad|trackpad|gestures", G_MOUSE, false},
    {L"ms-settings:pen", L"", L"Перо и Windows Ink", L"Pen & Windows Ink", kDev,
     L"перо|стилус|рукописный ввод|pen|stylus|ink", G_PEN, false},
    {L"ms-settings:autoplay", L"", L"Автозапуск", L"AutoPlay", kDev,
     L"автозапуск носителей|автовоспроизведение|autoplay|autorun", G_DEVICES, false},
    {L"ms-settings:usb", L"", L"USB", L"USB", kDev, L"юсб|флешка|usb", G_USB, false},
    {L"ms-settings:camera", L"", L"Камеры", L"Cameras", kDev, L"веб камера|вебкамера|webcam|camera", G_CAM, false},

    // --- Сеть и Интернет
    {L"ms-settings:network-status", L"", L"Сеть и Интернет", L"Network & internet", kNet,
     L"сеть|интернет|состояние сети|подключение|network|internet|network status|connection", G_GLOBE, false},
    {L"ms-settings:network-wifi", L"", L"Wi-Fi", L"Wi-Fi", kNet,
     L"wifi|wi fi|вайфай|вай фай|вай-фай|беспроводная сеть|беспроводная|wlan|wireless|сети wi-fi", G_WIFI, false},
    {L"ms-settings:network-ethernet", L"", L"Ethernet", L"Ethernet", kNet,
     L"проводная сеть|кабель|лан|ethernet|lan|wired", G_ETH, false},
    {L"ms-settings:network-vpn", L"", L"VPN", L"VPN", kNet, L"впн|vpn|виртуальная частная сеть", G_VPN, false},
    {L"ms-settings:network-mobilehotspot", L"", L"Мобильный хот-спот", L"Mobile hotspot", kNet,
     L"точка доступа|хотспот|раздать интернет|раздать wifi|hotspot|tethering", G_WIFI, false},
    {L"ms-settings:network-airplanemode", L"", L"Режим «в самолёте»", L"Airplane mode", kNet,
     L"авиарежим|режим полета|в самолете|airplane|flight mode", G_PLANE, false},
    {L"ms-settings:network-proxy", L"", L"Прокси-сервер", L"Proxy", kNet, L"прокси|proxy", G_GLOBE, false},
    {L"ms-settings:network-dialup", L"", L"Набор номера", L"Dial-up", kNet, L"модем|dial up|dialup", G_GLOBE, false},
    {L"ms-settings:datausage", L"", L"Использование данных", L"Data usage", kNet,
     L"трафик|расход трафика|лимитное подключение|data usage|traffic|metered", G_GLOBE, false},

    // --- Персонализация
    {L"ms-settings:personalization", L"", L"Персонализация", L"Personalization", kPers,
     L"оформление|внешний вид|appearance|customize", G_PERS, false},
    {L"ms-settings:personalization-background", L"", L"Фон", L"Background", kPers,
     L"обои|фон рабочего стола|картинка рабочего стола|wallpaper|desktop background", G_PIC, false},
    {L"ms-settings:colors", L"", L"Цвета", L"Colors", kPers,
     L"темная тема|светлая тема|темный режим|цвет акцента|прозрачность|dark mode|light mode|dark theme|accent color|"
     L"transparency",
     G_COLOR, false},
    {L"ms-settings:themes", L"", L"Темы", L"Themes", kPers,
     L"тема|значки рабочего стола|указатели мыши|theme|desktop icons|cursors", G_PERS, false},
    {L"ms-settings:lockscreen", L"", L"Экран блокировки", L"Lock screen", kPers,
     L"блокировка|заставка|экранная заставка|lock screen|screensaver|screen saver", G_LOCK, false},
    {L"ms-settings:personalization-start", L"", L"Пуск", L"Start", kPers,
     L"меню пуск|start menu|рекомендуемые|recommended", G_PERS, false},
    {L"ms-settings:personalization-start-places", L"", L"Папки в меню «Пуск»", L"Start folders", kPers,
     L"папки возле кнопки питания|start folders", G_PERS, false},
    {L"ms-settings:taskbar", L"", L"Панель задач", L"Taskbar", kPers,
     L"таскбар|трей|значки в трее|выравнивание панели задач|taskbar|tray|system tray|notification area", G_PERS,
     false},
    {L"ms-settings:fonts", L"", L"Шрифты", L"Fonts", kPers, L"шрифт|установить шрифт|font|typeface", G_FONT, false},
    {L"ms-settings:personalization-touchkeyboard", L"", L"Сенсорная клавиатура", L"Touch keyboard", kPers,
     L"экранная клавиатура|touch keyboard|on screen keyboard", G_KEYB, false},

    // --- Приложения
    {L"ms-settings:appsfeatures", L"", L"Установленные приложения", L"Installed apps", kApps,
     L"удалить программу|удаление программ|приложения и возможности|программы|uninstall|apps & features|"
     L"remove program|installed programs",
     G_APPS, false},
    {L"ms-settings:defaultapps", L"", L"Приложения по умолчанию", L"Default apps", kApps,
     L"браузер по умолчанию|программы по умолчанию|ассоциации файлов|открывать с помощью|default browser|"
     L"file associations|open with",
     G_APPS, false},
    {L"ms-settings:startupapps", L"", L"Автозагрузка", L"Startup apps", kApps,
     L"автозапуск программ|запуск при старте|автозагрузка приложений|startup|autostart|run at startup", G_APPS,
     false},
    {L"ms-settings:appsforwebsites", L"", L"Приложения для веб-сайтов", L"Apps for websites", kApps,
     L"ссылки в приложениях|apps for websites", G_APPS, false},
    {L"ms-settings:videoplayback", L"", L"Воспроизведение видео", L"Video playback", kApps,
     L"видео|hdr видео|video playback|video", G_APPS, false},
    {L"ms-settings:maps", L"", L"Автономные карты", L"Offline maps", kApps, L"карты|offline maps|maps", G_PIN, false},

    // --- Учётные записи
    {L"ms-settings:yourinfo", L"", L"Ваши данные", L"Your info", kAcc,
     L"учетная запись|аккаунт|профиль|аватар|account|profile|avatar|microsoft account", G_USER, false},
    {L"ms-settings:signinoptions", L"", L"Варианты входа", L"Sign-in options", kAcc,
     L"пароль|пин код|пин-код|windows hello|отпечаток|распознавание лица|сменить пароль|password|pin|fingerprint|"
     L"face recognition|change password|login",
     G_LOCK, false},
    {L"ms-settings:emailandaccounts", L"", L"Электронная почта и учётные записи", L"Email & accounts", kAcc,
     L"почта|email|учетные записи|accounts|mail", G_USER, false},
    {L"ms-settings:otherusers", L"", L"Другие пользователи", L"Other users", kAcc,
     L"добавить пользователя|пользователи|новый пользователь|add user|users|other users", G_PEOPLE, false},
    {L"ms-settings:family-group", L"", L"Семья", L"Family", kAcc,
     L"семейная группа|родительский контроль|family|parental controls", G_PEOPLE, false},
    {L"ms-settings:backup", L"", L"Архивация Windows", L"Windows backup", kAcc,
     L"резервная копия|бэкап|синхронизация|onedrive|backup|sync", G_RECOV, false},
    {L"ms-settings:workplace", L"", L"Доступ к учётной записи места работы или учебного заведения",
     L"Access work or school", kAcc, L"рабочая учетная запись|домен|work account|school|domain|azure ad", G_USER,
     false},

    // --- Время и язык
    {L"ms-settings:dateandtime", L"", L"Дата и время", L"Date & time", kTime,
     L"время|дата|часовой пояс|синхронизация времени|часы|time|date|time zone|clock", G_CLOCK, false},
    {L"ms-settings:regionlanguage", L"", L"Язык и регион", L"Language & region", kTime,
     L"язык|язык интерфейса|добавить язык|языковой пакет|language|display language|add language", G_GLOBE, false},
    {L"ms-settings:regionformatting", L"", L"Региональный формат", L"Regional format", kTime,
     L"регион|формат даты|формат чисел|страна|region|regional format|country", G_GLOBE, false},
    {L"ms-settings:keyboard", L"", L"Клавиатура", L"Keyboard", kTime,
     L"раскладка|раскладка клавиатуры|переключение языка|сменить раскладку|keyboard layout|input language|"
     L"switch layout",
     G_KEYB, false},
    {L"ms-settings:typing", L"", L"Ввод текста", L"Typing", kTime,
     L"автозамена|проверка орфографии|подсказки текста|autocorrect|spell check|text suggestions|typing", G_KEYB,
     false},
    {L"ms-settings:speech", L"", L"Речь", L"Speech", kTime,
     L"распознавание речи|голос|голосовой ввод|speech recognition|voice|voice typing", G_MIC, false},

    // --- Игры
    {L"ms-settings:gaming-gamebar", L"", L"Xbox Game Bar", L"Xbox Game Bar", kGame,
     L"игровая панель|гейм бар|game bar|xbox", G_GAME, false},
    {L"ms-settings:gaming-gamedvr", L"", L"Клипы", L"Captures", kGame,
     L"запись экрана|запись игр|скриншоты игр|captures|game dvr|screen recording", G_GAME, false},
    {L"ms-settings:gaming-gamemode", L"", L"Игровой режим", L"Game Mode", kGame,
     L"режим игры|игры|game mode|gaming", G_GAME, false},

    // --- Специальные возможности
    {L"ms-settings:easeofaccess-display", L"", L"Размер текста", L"Text size", kEase,
     L"крупный шрифт|увеличить текст|размер шрифта|text size|font size|bigger text", G_FONT, false},
    {L"ms-settings:easeofaccess-visualeffects", L"", L"Визуальные эффекты", L"Visual effects", kEase,
     L"анимация|эффекты анимации|полосы прокрутки|animations|animation effects|scrollbars", G_EASE, false},
    {L"ms-settings:easeofaccess-mousepointer", L"", L"Указатель мыши и сенсорный ввод", L"Mouse pointer and touch",
     kEase, L"размер курсора|цвет курсора|указатель|cursor size|pointer size|pointer color", G_MOUSE, false},
    {L"ms-settings:easeofaccess-cursor", L"", L"Текстовый курсор", L"Text cursor", kEase,
     L"курсор ввода|толщина курсора|text cursor|caret", G_EASE, false},
    {L"ms-settings:easeofaccess-magnifier", L"", L"Экранная лупа", L"Magnifier", kEase,
     L"лупа|увеличение экрана|magnifier|zoom", G_EASE, false},
    {L"ms-settings:easeofaccess-colorfilter", L"", L"Цветовые фильтры", L"Color filters", kEase,
     L"дальтонизм|оттенки серого|инверсия цветов|color filters|colorblind|grayscale|invert colors", G_COLOR, false},
    {L"ms-settings:easeofaccess-highcontrast", L"", L"Контрастные темы", L"Contrast themes", kEase,
     L"высокая контрастность|контраст|high contrast|contrast", G_COLOR, false},
    {L"ms-settings:easeofaccess-narrator", L"", L"Экранный диктор", L"Narrator", kEase,
     L"диктор|чтение с экрана|narrator|screen reader", G_EASE, false},
    {L"ms-settings:easeofaccess-audio", L"", L"Звук (специальные возможности)", L"Audio accessibility", kEase,
     L"моно звук|монофонический звук|mono audio", G_SOUND, false},
    {L"ms-settings:easeofaccess-closedcaptioning", L"", L"Субтитры", L"Captions", kEase,
     L"скрытые субтитры|captions|subtitles|closed captions", G_EASE, false},
    {L"ms-settings:easeofaccess-speechrecognition", L"", L"Голосовой доступ", L"Voice access", kEase,
     L"управление голосом|voice access|speech", G_MIC, false},
    {L"ms-settings:easeofaccess-keyboard", L"", L"Клавиатура (специальные возможности)", L"Accessibility keyboard",
     kEase,
     L"залипание клавиш|фильтрация ввода|экранная клавиатура|sticky keys|filter keys|on-screen keyboard|"
     L"toggle keys",
     G_KEYB, false},
    {L"ms-settings:easeofaccess-mouse", L"", L"Управление мышью с клавиатуры", L"Mouse keys", kEase,
     L"мышь с клавиатуры|mouse keys", G_MOUSE, false},
    {L"ms-settings:easeofaccess-eyecontrol", L"", L"Управление глазами", L"Eye control", kEase,
     L"отслеживание глаз|eye control|eye tracking", G_EASE, false},

    // --- Конфиденциальность и защита
    {L"ms-settings:windowsdefender", L"", L"Безопасность Windows", L"Windows Security", kPriv,
     L"антивирус|защитник|защитник windows|defender|antivirus|virus|вирусы|security", G_SHIELD, false},
    {L"ms-settings:findmydevice", L"", L"Поиск устройства", L"Find my device", kPriv,
     L"найти устройство|потерянный ноутбук|find my device", G_PIN, false},
    {L"ms-settings:deviceencryption", L"", L"Шифрование устройства", L"Device encryption", kPriv,
     L"шифрование|bitlocker|encryption", G_LOCK, false},
    {L"ms-settings:privacy", L"", L"Конфиденциальность (общие)", L"Privacy", kPriv,
     L"приватность|конфиденциальность|разрешения приложений|privacy|permissions|рекламный идентификатор", G_LOCK,
     false},
    {L"ms-settings:privacy-location", L"", L"Расположение", L"Location", kPriv,
     L"местоположение|геолокация|gps|геопозиция|location", G_PIN, false},
    {L"ms-settings:privacy-webcam", L"", L"Доступ к камере", L"Camera privacy", kPriv,
     L"камера|разрешение камеры|camera access|webcam privacy|camera", G_CAM, false},
    {L"ms-settings:privacy-microphone", L"", L"Доступ к микрофону", L"Microphone privacy", kPriv,
     L"микрофон|разрешение микрофона|microphone access|microphone", G_MIC, false},
    {L"ms-settings:privacy-speech", L"", L"Распознавание голоса (конфиденциальность)", L"Online speech recognition",
     kPriv, L"онлайн распознавание речи|online speech", G_MIC, false},
    {L"ms-settings:privacy-feedback", L"", L"Диагностика и отзывы", L"Diagnostics & feedback", kPriv,
     L"телеметрия|диагностические данные|отзывы|telemetry|diagnostic data|feedback", G_LOCK, false},
    {L"ms-settings:privacy-activityhistory", L"", L"Журнал действий", L"Activity history", kPriv,
     L"история активности|activity history|timeline", G_HIST, false},
    {L"ms-settings:privacy-broadfilesystemaccess", L"", L"Доступ к файловой системе", L"File system access", kPriv,
     L"файловая система|file system", G_LOCK, false},

    // --- Центр обновления Windows
    {L"ms-settings:windowsupdate", L"", L"Центр обновления Windows", L"Windows Update", kUpd,
     L"обновления|обновить windows|проверить обновления|апдейт|update|updates|check for updates", G_UPDATE, false},
    {L"ms-settings:windowsupdate-history", L"", L"Журнал обновлений", L"Update history", kUpd,
     L"история обновлений|удалить обновление|update history|uninstall updates", G_HIST, false},
    {L"ms-settings:windowsupdate-options", L"", L"Дополнительные параметры обновления", L"Advanced update options",
     kUpd, L"часы активности|необязательные обновления|active hours|optional updates", G_UPDATE, false},
    {L"ms-settings:delivery-optimization", L"", L"Оптимизация доставки", L"Delivery Optimization", kUpd,
     L"оптимизация доставки|delivery optimization", G_UPDATE, false},

    // --- Классические системные инструменты
    {L"taskmgr", L"", L"Диспетчер задач", L"Task Manager", kTool,
     L"процессы|завис|снять задачу|загрузка процессора|task manager|processes|kill process|taskmgr", G_DIAG, true},
    {L"control", L"", L"Панель управления", L"Control Panel", kTool, L"control panel|контрольная панель", G_SET,
     false},
    {L"devmgmt.msc", L"", L"Диспетчер устройств", L"Device Manager", kTool,
     L"драйверы|драйвер|оборудование|device manager|drivers|hardware|devmgmt", G_DEVICES, true},
    {L"diskmgmt.msc", L"", L"Управление дисками", L"Disk Management", kTool,
     L"разделы диска|форматировать диск|буква диска|disk management|partitions|format disk|diskmgmt", G_DRIVE, true},
    {L"services.msc", L"", L"Службы", L"Services", kTool, L"сервисы|services|службы windows", G_SET, true},
    {L"eventvwr.msc", L"", L"Просмотр событий", L"Event Viewer", kTool,
     L"журнал событий|логи|ошибки системы|event viewer|event log|logs|eventvwr", G_HIST, true},
    {L"taskschd.msc", L"", L"Планировщик заданий", L"Task Scheduler", kTool,
     L"планировщик|задания по расписанию|task scheduler|scheduled tasks|cron", G_CLOCK, true},
    {L"compmgmt.msc", L"", L"Управление компьютером", L"Computer Management", kTool,
     L"computer management|compmgmt|локальные пользователи и группы", G_ADMIN, true},
    {L"regedit", L"", L"Редактор реестра", L"Registry Editor", kTool,
     L"реестр|registry|regedit|regedit.exe", G_ADMIN, true},
    {L"gpedit.msc", L"", L"Редактор локальной групповой политики", L"Group Policy Editor", kTool,
     L"групповая политика|политики|gpedit|group policy|local group policy", G_ADMIN, true},
    {L"rundll32.exe", L"sysdm.cpl,EditEnvironmentVariables", L"Переменные среды", L"Environment Variables", kTool,
     L"переменные окружения|path|env|environment|переменная path", G_SYS, false},
    {L"SystemPropertiesAdvanced.exe", L"", L"Свойства системы", L"System Properties", kTool,
     L"дополнительные параметры системы|файл подкачки|быстродействие|виртуальная память|advanced system settings|"
     L"page file|performance options|virtual memory|sysdm",
     G_SYS, true},
    {L"appwiz.cpl", L"", L"Программы и компоненты", L"Programs and Features", kTool,
     L"удаление программ|установка и удаление программ|uninstall a program|add remove programs|appwiz", G_APPS,
     false},
    {L"ncpa.cpl", L"", L"Сетевые подключения", L"Network Connections", kTool,
     L"сетевые адаптеры|адаптеры|настройки адаптера|ip адрес|dns|network adapters|adapter settings|ncpa",
     G_ETH, true},
    {L"mmsys.cpl", L"", L"Звук (классическая панель)", L"Sound Control Panel", kTool,
     L"устройства воспроизведения|записывающие устройства|playback devices|recording devices|mmsys", G_SOUND,
     false},
    {L"powercfg.cpl", L"", L"Электропитание", L"Power Options", kTool,
     L"схема электропитания|план электропитания|power plan|power options|powercfg", G_POWER, false},
    {L"main.cpl", L"", L"Свойства мыши", L"Mouse Properties", kTool,
     L"мышь классическая|двойной щелчок|скорость двойного щелчка|mouse properties|double click", G_MOUSE, false},
    {L"netplwiz", L"", L"Учётные записи пользователей", L"User Accounts", kTool,
     L"автовход|автоматический вход|пользователи|netplwiz|user accounts|auto login", G_PEOPLE, true},
    {L"resmon", L"", L"Монитор ресурсов", L"Resource Monitor", kTool,
     L"ресурсы|нагрузка на диск|сетевая активность|resource monitor|resmon", G_DIAG, true},
    {L"msinfo32", L"", L"Сведения о системе", L"System Information", kTool,
     L"информация о системе|конфигурация|system information|msinfo|specs", G_INFO, false},
    {L"cleanmgr", L"", L"Очистка диска", L"Disk Cleanup", kTool,
     L"очистить диск|удалить временные файлы|disk cleanup|cleanmgr|clean up", G_DEL, true},
    {L"dfrgui", L"", L"Оптимизация дисков", L"Defragment and Optimize Drives", kTool,
     L"дефрагментация|оптимизация диска|defrag|defragment|trim|dfrgui", G_DRIVE, true},
    {L"wf.msc", L"", L"Брандмауэр Защитника Windows", L"Windows Defender Firewall", kTool,
     L"брандмауэр|фаервол|файрвол|сетевой экран|firewall|wf.msc", G_SHIELD, true},
    {L"mstsc", L"", L"Подключение к удалённому рабочему столу", L"Remote Desktop Connection", kTool,
     L"удаленный рабочий стол|rdp|удаленное подключение|remote desktop|mstsc", G_CONNECT, false},
    {L"cmd.exe", L"", L"Командная строка", L"Command Prompt", kTool,
     L"консоль|терминал|cmd|command prompt|console|командная строка", G_CMD, true},
    {L"powershell.exe", L"", L"Windows PowerShell", L"PowerShell", kTool,
     L"powershell|пауэршелл|повершелл|консоль|shell|ps", G_CMD, true},
    {L"wt.exe", L"", L"Терминал Windows", L"Windows Terminal", kTool,
     L"терминал|terminal|wt|windows terminal|консоль", G_CMD, true},
    {L"msconfig", L"", L"Конфигурация системы", L"System Configuration", kTool,
     L"msconfig|безопасный режим|параметры загрузки|safe mode|boot options", G_SYS, true},
    {L"perfmon", L"", L"Системный монитор", L"Performance Monitor", kTool,
     L"производительность|счетчики|performance monitor|perfmon", G_DIAG, true},
    {L"lusrmgr.msc", L"", L"Локальные пользователи и группы", L"Local Users and Groups", kTool,
     L"локальные пользователи|группы|local users|lusrmgr", G_PEOPLE, true},
    {L"certmgr.msc", L"", L"Сертификаты", L"Certificates", kTool, L"сертификаты|certificates|certmgr", G_LOCK, true},
    {L"osk", L"", L"Экранная клавиатура", L"On-Screen Keyboard", kTool,
     L"экранная клавиатура|on screen keyboard|osk", G_KEYB, false},
    {L"charmap", L"", L"Таблица символов", L"Character Map", kTool,
     L"символы|спецсимволы|юникод|character map|special characters|unicode|charmap", G_FONT, false},
};


// Lowercased copies + split keyword phrases, built once on first use.
struct Prepared {
  std::wstring ruLower, enLower;
  std::vector<std::wstring_view> keywords;  // views into `kw`
  std::wstring kw;
};

const std::vector<Prepared>& PreparedCatalog() {
  static const std::vector<Prepared> prep = [] {
    std::vector<Prepared> v(std::size(kCatalog));
    for (size_t i = 0; i < v.size(); ++i) {
      const Entry& e = kCatalog[i];
      v[i].ruLower = str::ToLower(e.ru);
      v[i].enLower = str::ToLower(e.en);
      v[i].kw = str::ToLower(e.keywords);
      v[i].keywords = str::Split(v[i].kw, L'|');  // views stay valid: `kw` never changes after this
    }
    return v;
  }();
  return prep;
}

}  // namespace

std::span<const Entry> Catalog() { return kCatalog; }

void Search(const fuzzy::Query& q, std::vector<Match>& out, size_t maxResults, float minScore) {
  if (q.lower.empty() || maxResults == 0) return;
  const auto& prep = PreparedCatalog();
  const size_t base = out.size();
  for (size_t i = 0; i < prep.size(); ++i) {
    const Entry& e = kCatalog[i];
    const Prepared& p = prep[i];
    float s = fuzzy::ScoreLowered(q, e.ru, p.ruLower);
    if (s < 1.f) s = std::max(s, fuzzy::ScoreLowered(q, e.en, p.enLower));
    for (size_t k = 0; k < p.keywords.size() && s < 0.9f; ++k) {
      float ks = fuzzy::ScoreLowered(q, p.keywords[k], p.keywords[k]);
      if (ks >= 0.5f) s = std::max(s, ks * 0.9f);  // keyword subsequence matches are too noisy to count
    }
    if (s >= minScore) out.push_back({&e, s});
  }
  auto first = out.begin() + std::ptrdiff_t(base);
  // Stable order for equal scores: catalog order (more common pages come first).
  std::stable_sort(first, out.end(), [](const Match& a, const Match& b) { return a.score > b.score; });
  if (out.size() - base > maxResults) out.resize(base + maxResults);
}

}  // namespace cs::settings
