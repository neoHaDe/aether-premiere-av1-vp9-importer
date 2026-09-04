// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 neoHaDe

'use strict';

// Строки панели на двух языках.
//
// Ключ — не сокращение от английской фразы, а имя места: `settings.head`,
// `cache.previewNote`. Так текст можно переписать, не трогая разметку,
// и наоборот. Английский лежит первым, потому что он же по умолчанию
// для всего мира, кроме русской Windows.
//
// ⚠ Ни одна строка здесь не содержит подстановок. Числа собираются в main.js
// сцеплением, как и в C++ (см. tools/localization.h): строка формата, которую
// выбирают на лету, — тот самый способ уронить программу, на котором проект
// уже обжёгся.

const STRINGS = {
    en: {
        'app.version':        'version ',
        'app.versionUnknown': 'version unknown',
        'app.searching':      'looking for the plug-in...',
        'app.notFound':       'plug-in not found',
        'nav.label':          'Aether sections',
        'nav.settings':       'Settings',
        'nav.cache':          'Cache',
        'nav.diagnose':       'Diagnostics',
        'lang.label':         'Language',

        'settings.head':      'How to decode AV1 and VP9 video',
        'settings.enabled':   'Aether is on',
        'settings.enabledNote':
            'Turn it off and, after Adobe restarts, files go to other importers and FFmpeg is not loaded.',
        'settings.auto':      'Automatic (recommended)',
        'settings.autoNote':
            'Processor on machines with 8 threads or more, graphics card otherwise.',
        'settings.software':  'Processor (dav1d)',
        'settings.softwareNote':
            'Faster by measurement: the graphics card has to copy every frame into ordinary memory, the processor writes there at once.',
        'settings.hardware':  'Graphics card (NVIDIA / Intel / AMD)',
        'settings.hardwareNote':
            'Takes load off the processor. Pick it if yours is weak or busy with something else.',
        'settings.hint':      'Changes take effect after Premiere Pro restarts.',
        'settings.save':      'Save',
        'settings.saved':     'Saved. Restart Premiere Pro.',
        'settings.saveFailed':'Could not read the settings: ',

        'cache.head':         'Aether cache',
        'cache.memory':       'Frame cache in memory',
        'cache.off':          'Off',
        'cache.memoryNote':
            'The limit covers only the frames Aether caches. Premiere’s own memory use can be higher.',
        'cache.preview':      'Keep reduced previews on disk',
        'cache.previewNote':
            'BGRA8 Draft/Low up to 2 MiB only. Full-size playback and export do not use this cache.',
        'cache.limit':        'Disk cache limit',
        'cache.usageCounting':'Used: counting...',
        'cache.used':         'Used: ',
        'cache.files':        ', files: ',
        'cache.save':         'Save',
        'cache.clear':        'Clear preview cache',
        'cache.cleared':      'Preview cache cleared.',

        'diag.head':          'Diagnostics',
        'diag.note':
            'Checks the system, the Adobe applications and video decoding. If some file will not open, drop it here and it gets its own check.',
        'diag.drop':          'Drop the file that will not open here',
        'diag.browse':        'Choose a file',
        'diag.forget':        'remove',
        'diag.run':           'Run the check',
        'diag.copy':          'Copy the report',
        'diag.running':       'Checking...',
        'diag.checking':      'Checking: ',
        'diag.failed':        'There are failures - marked red. Attach the report to an issue.',
        'diag.ok':            'All good. That does not promise Premiere will open the file - it goes its own way.',
        'diag.noReport':      'The check did not run: ',
        'diag.empty':         'empty answer',
        'diag.copied':        'Report copied.',
        'err.clipboard':      'clipboard API unavailable',
        'err.write':          'Could not write: ',
        'err.clear':          'Could not clear the cache: ',
        'err.copy':           'Could not copy the report: ',
        'report.fail':        'FAIL',
        'state.prefix':       'Status: ',
        'state.pass':         'pass',
        'state.fail':         'fail',
        'state.warn':         'warning',
        'state.skip':         'skipped',
        'state.info':         'information',
        'diag.reportTitle':   'Aether diagnostic report',
        'diag.reportVersion': 'Version: ',
        'diag.reportTail':
            'The plug-in core was checked on this machine. That does not prove\nPremiere will open the file: it reaches the plug-in its own way.\n',
    },

    ru: {
        'app.version':        'версия ',
        'app.versionUnknown': 'версия неизвестна',
        'app.searching':      'ищем плагин...',
        'app.notFound':       'плагин не найден',
        'nav.label':          'Разделы Aether',
        'nav.settings':       'Настройки',
        'nav.cache':          'Кэш',
        'nav.diagnose':       'Диагностика',
        'lang.label':         'Язык',

        'settings.head':      'Как распаковывать видео AV1 и VP9',
        'settings.enabled':   'Aether включён',
        'settings.enabledNote':
            'Если выключить, после перезапуска Adobe файлы будут переданы другим импортёрам, а FFmpeg не загрузится.',
        'settings.auto':      'Автоматически (рекомендуется)',
        'settings.autoNote':
            'Процессором на машинах от 8 потоков, иначе видеокартой.',
        'settings.software':  'Процессором (dav1d)',
        'settings.softwareNote':
            'По замерам быстрее: видеокарте приходится копировать каждый кадр в обычную память, а процессор сразу пишет туда.',
        'settings.hardware':  'Видеокартой (NVIDIA / Intel / AMD)',
        'settings.hardwareNote':
            'Разгружает процессор. Выбирайте, если он слабый или занят другим.',
        'settings.hint':      'Изменения вступят в силу после перезапуска Premiere Pro.',
        'settings.save':      'Сохранить',
        'settings.saved':     'Сохранено. Перезапустите Premiere Pro.',
        'settings.saveFailed':'Не удалось прочитать настройки: ',

        'cache.head':         'Кэш Aether',
        'cache.memory':       'Кэш кадров в памяти',
        'cache.off':          'Выключен',
        'cache.memoryNote':
            'Лимит относится только к кадрам, которые кэширует Aether. Общая память Premiere может быть выше.',
        'cache.preview':      'Хранить уменьшенные превью на диске',
        'cache.previewNote':
            'Только BGRA8 Draft/Low до 2 MiB. Полноразмерное воспроизведение и экспорт этот кэш не используют.',
        'cache.limit':        'Лимит дискового кэша',
        'cache.usageCounting':'Занято: считаем...',
        'cache.used':         'Занято: ',
        'cache.files':        ', файлов: ',
        'cache.save':         'Сохранить',
        'cache.clear':        'Очистить кэш превью',
        'cache.cleared':      'Кэш превью очищен.',

        'diag.head':          'Диагностика',
        'diag.note':
            'Проверяет систему, приложения Adobe и распаковку видео. Если какой-то файл не открывается — бросьте его сюда, и он будет проверен отдельно.',
        'diag.drop':          'Перетащите сюда файл, который не открывается',
        'diag.browse':        'Выбрать файл',
        'diag.forget':        'убрать',
        'diag.run':           'Запустить проверку',
        'diag.copy':          'Скопировать отчёт',
        'diag.running':       'Проверяем...',
        'diag.checking':      'Проверяем: ',
        'diag.failed':        'Есть сбои — отмечены красным. Приложите отчёт к issue.',
        'diag.ok':            'Всё в порядке. Это не обещает, что Premiere откроет файл, — он ходит своим путём.',
        'diag.noReport':      'Проверка не отработала: ',
        'diag.empty':         'пустой ответ',
        'diag.copied':        'Отчёт скопирован.',
        'err.clipboard':      'clipboard API недоступен',
        'err.write':          'Не удалось записать: ',
        'err.clear':          'Не удалось очистить кэш: ',
        'err.copy':           'Не удалось скопировать отчёт: ',
        'report.fail':        'СБОЙ',
        'state.prefix':       'Статус: ',
        'state.pass':         'успех',
        'state.fail':         'сбой',
        'state.warn':         'предупреждение',
        'state.skip':         'пропущено',
        'state.info':         'информация',
        'diag.reportTitle':   'Отчёт диагностики Aether',
        'diag.reportVersion': 'Версия: ',
        'diag.reportTail':
            'Проверено ядро плагина на этой машине. Это не доказывает, что\nPremiere откроет файл: он ходит к плагину своим путём.\n',
    },
};

let CURRENT = 'en';

// «auto» решаем по языку самого Premiere, а не по языку Windows.
//
// И это не придирка: панель живёт ВНУТРИ Premiere, человек смотрит на неё
// в окружении его меню. Русский Premiere на английской Windows должен дать
// русскую панель, иначе половина окна на одном языке, половина на другом.
// Окно Aether.exe живёт отдельно и спрашивает Windows — там это верно.
function detectLanguage() {
    try {
        const locale = JSON.parse(window.__adobe_cep__.getHostEnvironment()).appUILocale;
        if (locale && locale.toLowerCase().indexOf('ru') === 0) return 'ru';
        return 'en';
    } catch (e) {
        return 'en';
    }
}

function resolveLanguage(setting) {
    if (setting === 'ru' || setting === 'en') return setting;
    return detectLanguage();
}

function t(key) {
    const table = STRINGS[CURRENT] || STRINGS.en;
    return (key in table) ? table[key] : (STRINGS.en[key] || key);
}

// Проставить все надписи разом. Ходим по data-i18n, а не по списку id:
// добавить надпись в разметку и забыть добавить её сюда — самая обычная
// ошибка, а так забыть нечего.
function applyLanguage(lang) {
    CURRENT = (lang === 'ru' || lang === 'en') ? lang : 'en';
    document.documentElement.lang = CURRENT;

    for (const el of document.querySelectorAll('[data-i18n]')) {
        el.textContent = t(el.getAttribute('data-i18n'));
    }
    for (const el of document.querySelectorAll('[data-i18n-label]')) {
        el.setAttribute('aria-label', t(el.getAttribute('data-i18n-label')));
    }
}

// Глобальная переменная, а не module.exports: файл подключён обычным
// <script src>, и в панели CEP с включённым Node объект module принадлежит
// не нам — присвоение ему перетёрло бы чужое.
window.I18N = { t, applyLanguage, resolveLanguage, current: () => CURRENT };
