// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 neoHaDe
//
// Панель Aether внутри Premiere.
//
// Логики тут нарочно мало. Всё, что можно решить один раз и в одном месте,
// решено в C++: и настройки, и проверки читает та же программа, которой
// пользуется окно Aether.exe. Панель показывает её ответ и записывает выбор —
// не более. Дублировать проверки на JavaScript значило бы завести вторую
// правду, которая рано или поздно разойдётся с первой.

'use strict';

const fs   = require('fs');
const path = require('path');
const { execFile } = require('child_process');

function system32(name) {
    const root = process.env.SystemRoot || 'C:\\Windows';
    return path.join(root, 'System32', name);
}

function powershellExe() {
    const root = process.env.SystemRoot || 'C:\\Windows';
    return path.join(root, 'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe');
}

// ---------------------------------------------------------------------------
// Где живёт плагин
// ---------------------------------------------------------------------------

// Папку плагинов приложения Adobe сами записывают в реестр — оттуда её берёт
// и установщик, и диагностика. Спрашиваем тем же способом, чтобы три места
// не разошлись во мнениях.
const kExecTimeoutMs = 60000;

function execText(command, args) {
    return new Promise((resolve, reject) => {
        execFile(command, args,
            { encoding: 'utf8', windowsHide: true, timeout: kExecTimeoutMs },
            (error, stdout) => error ? reject(error) : resolve(stdout));
    });
}

async function mediaCoreFromRegistry() {
    const keys = [
        'HKLM\\SOFTWARE\\Adobe\\Premiere Pro\\CurrentVersion',
        'HKLM\\SOFTWARE\\Adobe\\After Effects\\CurrentVersion',
    ];
    for (const key of keys) {
        try {
            const out = await execText(
                system32('reg.exe'), ['query', key, '/v', 'CommonPluginInstallPath']);
            const m = out.match(/CommonPluginInstallPath\s+REG_\w+\s+(.+)/);
            if (m) return m[1].trim();
        } catch (e) { /* ключа нет — пробуем следующий */ }
    }
    return null;
}

async function pluginFolder() {
    const candidates = [];
    const reg = await mediaCoreFromRegistry();
    if (reg) candidates.push(path.join(reg, 'Aether'));

    const pf = process.env['ProgramFiles'] || 'C:\\Program Files';
    candidates.push(path.join(pf, 'Adobe', 'Common', 'Plug-ins', '7.0', 'MediaCore', 'Aether'));

    for (const dir of candidates) {
        if (fs.existsSync(path.join(dir, 'AetherDiagnose.exe'))) return dir;
    }
    return null;
}

let PLUGIN_DIR = null;

// ---------------------------------------------------------------------------
// Цвета из самого Premiere
// ---------------------------------------------------------------------------

// У Premiere четыре ступени яркости интерфейса, и панель, покрашенная в одну
// из них, при любой другой выглядит заплаткой. Спрашиваем, какая стоит сейчас.
function applyHostTheme() {
    let skin;
    try {
        skin = JSON.parse(window.__adobe_cep__.getHostEnvironment()).appSkinInfo;
    } catch (e) {
        return;   // не спросилось — остаёмся при своих, они тёмные
    }

    const rgb = (c) => `rgb(${Math.round(c.red)},${Math.round(c.green)},${Math.round(c.blue)})`;
    const bg  = skin.panelBackgroundColor.color;

    // Светлая тема Premiere или тёмная — решаем по самой подложке, а не по
    // названию: названий у ступеней нет, а число есть.
    const light = (bg.red + bg.green + bg.blue) / 3 > 127;

    const css = document.documentElement.style;
    css.setProperty('--bg', rgb(bg));
    css.setProperty('--panel', light ? 'rgba(0,0,0,.04)' : 'rgba(255,255,255,.04)');
    css.setProperty('--line',  light ? 'rgba(0,0,0,.18)' : 'rgba(255,255,255,.14)');
    css.setProperty('--text',  light ? '#1a1a1a' : '#f0f0f0');
    css.setProperty('--note',  light ? '#5b5b5b' : '#a6a6a6');
    css.setProperty('--accent', light ? '#0067c0' : '#4cc2ff');
    css.setProperty('--onaccent', light ? '#ffffff' : '#001a2b');
    css.setProperty('--good', light ? '#107c10' : '#6ccb5f');
    css.setProperty('--bad',  light ? '#c42b1c' : '#ff99a4');
    css.setProperty('--warn', light ? '#9d5d00' : '#fce100');
}

// ---------------------------------------------------------------------------
// Настройки
// ---------------------------------------------------------------------------

function diagnoseExe() {
    if (!PLUGIN_DIR) throw new Error(I18N.t('app.notFound'));
    return path.join(PLUGIN_DIR, 'AetherDiagnose.exe');
}

async function diagnoseJson(args) {
    return JSON.parse(await execText(diagnoseExe(), args));
}

async function readSettings() {
    return diagnoseJson(['--settings-json']);
}

async function writeSettings(settings) {
    return diagnoseJson([
        '--set',
        'lang=' + (settings.lang || 'auto'),
        'enabled=' + (settings.enabled ? 'on' : 'off'),
        'decode=' + settings.decode,
        'cache_memory_mb=' + settings.cache_memory_mb,
        'preview_cache=' + (settings.preview_cache ? 'on' : 'off'),
        'preview_cache_mb=' + settings.preview_cache_mb,
    ]);
}

async function refreshCacheUsage() {
    const usage = await diagnoseJson(['--cache-json']);
    document.getElementById('cache-usage').textContent =
        I18N.t('cache.used') + (usage.bytes / 1024 / 1024).toFixed(1) +
        ' MiB' + I18N.t('cache.files') + usage.files;
    return usage;
}

// ---------------------------------------------------------------------------
// Диагностика
// ---------------------------------------------------------------------------

let lastReport = null;
let userFile   = '';

function runDiagnostics() {
    const results = document.getElementById('results');
    const status  = document.getElementById('status');
    const run     = document.getElementById('run');
    const copy    = document.getElementById('copy');

    if (!PLUGIN_DIR) {
        results.innerHTML = '';
        status.textContent = I18N.t('app.notFound');
        return;
    }

    run.disabled  = true;
    copy.disabled = true;
    status.textContent = I18N.t('diag.running');

    const exe  = path.join(PLUGIN_DIR, 'AetherDiagnose.exe');
    // Язык передаём явно: движок иначе прочитал бы его из файла настроек,
    // а в панели язык мог быть только что переключен и ещё не сохранён.
    const lang = I18N.current();
    const args = userFile ? ['--json', '--lang', lang, userFile]
                          : ['--json', '--lang', lang];

    // Буфер побольше умолчания: отчёт с длинными путями в него не влезал бы,
    // а обрезанный JSON разобрать нельзя вовсе.
    execFile(exe, args, { encoding: 'utf8', maxBuffer: 4 * 1024 * 1024,
                          windowsHide: true, timeout: kExecTimeoutMs },
        (err, stdout) => {
            run.disabled = false;

            // Ненулевой код возврата — это «есть сбои», а не «не запустилось».
            // Отличаем по тому, разобрался ли ответ.
            let report = null;
            try { report = JSON.parse(stdout); } catch (e) { /* ниже */ }

            if (!report) {
                results.innerHTML = '';
                status.textContent = I18N.t('diag.noReport') +
                    (err ? err.message : I18N.t('diag.empty'));
                return;
            }

            lastReport = report;
            render(report);
            copy.disabled = false;
            status.textContent = report.failed
                ? I18N.t('diag.failed')
                : I18N.t('diag.ok');
        });
}

function render(report) {
    const results = document.getElementById('results');
    results.innerHTML = '';

    for (const section of report.sections) {
        const head = document.createElement('div');
        head.className = 'group';
        head.textContent = section.title;
        results.appendChild(head);

        for (const check of section.checks) {
            const row = document.createElement('div');
            row.className = 'row';
            row.setAttribute('role', 'listitem');

            const mark = document.createElement('span');
            mark.className = 'mark ' + check.state;
            mark.textContent = { pass: '\u2713', fail: '\u2715',
                                 warn: '!', skip: '\u2014' }[check.state] || '';
            mark.setAttribute('aria-hidden', 'true');

            const body = document.createElement('div');
            const accessibleState = document.createElement('span');
            accessibleState.className = 'sr-only';
            accessibleState.textContent = I18N.t('state.prefix') + (
                I18N.t('state.' + check.state) || check.state) + '. ';
            body.appendChild(accessibleState);

            const what = document.createElement('div');
            what.className = 'what';
            what.textContent = check.name;
            body.appendChild(what);

            if (check.detail) {
                const detail = document.createElement('div');
                detail.className = 'detail';
                detail.textContent = check.detail;
                body.appendChild(detail);
            }

            row.appendChild(mark);
            row.appendChild(body);
            results.appendChild(row);
        }
    }
}

// Текст для issue собирается здесь, но чистить его уже не нужно: имена
// пользователя и машины движок убрал ещё до того, как отдал нам JSON.
function reportText(report) {
    const mark = { pass: 'OK  ', fail: I18N.t('report.fail'),
                   warn: '!   ', skip: '-   ', info: '    ' };
    let out = I18N.t('diag.reportTitle') + '\n' +
              I18N.t('diag.reportVersion') + report.version + '\n\n';
    for (const section of report.sections) {
        out += section.title + '\n';
        for (const c of section.checks) {
            out += '  ' + (mark[c.state] || '') + '  ' + c.name +
                   (c.detail ? ': ' + c.detail : '') + '\n';
        }
        out += '\n';
    }
    out += I18N.t('diag.reportTail');
    return out;
}

// ---------------------------------------------------------------------------
// Связывание
// ---------------------------------------------------------------------------

function setFile(file) {
    userFile = file || '';
    const text   = document.getElementById('drop-text');
    const drop   = document.getElementById('drop');
    const forget = document.getElementById('forget');

    if (userFile) {
        text.textContent = path.basename(userFile);
        text.title = userFile;
        drop.classList.add('has');
        forget.hidden = false;
    } else {
        text.textContent = I18N.t('diag.drop');
        text.title = '';
        drop.classList.remove('has');
        forget.hidden = true;
    }
}

async function copyText(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
        try {
            await navigator.clipboard.writeText(text);
            return;
        } catch (e) {
            // Старый CEP часто не считает панель secure context — fallback ниже.
        }
    }

    const area = document.createElement('textarea');
    area.value = text;
    area.setAttribute('aria-hidden', 'true');
    area.style.position = 'fixed';
    area.style.opacity = '0';
    document.body.appendChild(area);
    area.select();
    const copied = document.execCommand('copy');
    document.body.removeChild(area);
    if (!copied) throw new Error(I18N.t('err.clipboard'));
}

document.addEventListener('DOMContentLoaded', async () => {
    applyHostTheme();

    // Вкладки
    const pages = { settings: 'page-settings', cache: 'page-cache', diagnose: 'page-diagnose' };
    const tabNames = Object.keys(pages);
    const activateTab = (name, moveFocus) => {
        for (const other of tabNames) {
            const selected = other === name;
            const tab = document.getElementById('tab-' + other);
            tab.classList.toggle('active', selected);
            tab.setAttribute('aria-selected', selected ? 'true' : 'false');
            tab.tabIndex = selected ? 0 : -1;
            document.getElementById(pages[other]).hidden = !selected;
        }
        if (moveFocus) document.getElementById('tab-' + name).focus();
    };
    for (const name of tabNames) {
        const tab = document.getElementById('tab-' + name);
        tab.addEventListener('click', () => activateTab(name, false));
        tab.addEventListener('keydown', (event) => {
            const current = tabNames.indexOf(name);
            let next = current;
            if (event.key === 'ArrowRight') next = (current + 1) % tabNames.length;
            else if (event.key === 'ArrowLeft') next = (current - 1 + tabNames.length) % tabNames.length;
            else if (event.key === 'Home') next = 0;
            else if (event.key === 'End') next = tabNames.length - 1;
            else return;
            event.preventDefault();
            activateTab(tabNames[next], true);
        });
    }

    const saveSettings = async () => {
        const chosen = document.querySelector('input[name="decode"]:checked').value;
        const saved  = document.getElementById('saved');
        const cacheStatus = document.getElementById('cache-status');
        try {
            await writeSettings({
                enabled: document.getElementById('enabled').checked,
                decode: chosen,
                cache_memory_mb: Number(document.getElementById('memory-cache').value),
                preview_cache: document.getElementById('preview-cache').checked,
                preview_cache_mb: Number(document.getElementById('preview-limit').value),
            });
            saved.textContent = I18N.t('settings.saved');
            cacheStatus.textContent = saved.textContent;
        } catch (e) {
            saved.textContent = I18N.t('err.write') + e.message;
            cacheStatus.textContent = saved.textContent;
        }
    };
    document.getElementById('save').addEventListener('click', saveSettings);
    document.getElementById('save-cache').addEventListener('click', saveSettings);

    const previewToggle = document.getElementById('preview-cache');
    const syncPreviewControls = () => {
        document.getElementById('preview-limit').disabled = !previewToggle.checked;
    };
    previewToggle.addEventListener('change', syncPreviewControls);

    document.getElementById('clear-cache').addEventListener('click', async () => {
        const status = document.getElementById('cache-status');
        try {
            const usage = await diagnoseJson(['--clear-preview-cache']);
            document.getElementById('cache-usage').textContent =
                I18N.t('cache.used') + (usage.bytes / 1024 / 1024).toFixed(1) +
                ' MiB' + I18N.t('cache.files') + usage.files;
            status.textContent =
                I18N.t('cache.cleared');
        } catch (e) {
            status.textContent = I18N.t('err.clear') + e.message;
        }
    });

    // Файл человека
    const drop = document.getElementById('drop');
    drop.addEventListener('dragover', (e) => {
        e.preventDefault();
        drop.classList.add('over');
    });
    drop.addEventListener('dragleave', () => drop.classList.remove('over'));
    drop.addEventListener('drop', (e) => {
        e.preventDefault();
        drop.classList.remove('over');
        if (e.dataTransfer.files.length) setFile(e.dataTransfer.files[0].path);
    });
    const picker = document.getElementById('file-picker');
    document.getElementById('browse').addEventListener('click', () => picker.click());
    picker.addEventListener('change', () => {
        if (picker.files.length) {
            const selected = picker.files[0];
            setFile(selected.path || selected.name);
        }
    });
    document.getElementById('forget').addEventListener('click', () => setFile(''));

    document.getElementById('run').addEventListener('click', runDiagnostics);

    document.getElementById('copy').addEventListener('click', async () => {
        if (!lastReport) return;
        const status = document.getElementById('status');
        try {
            await copyText(reportText(lastReport));
            status.textContent = I18N.t('diag.copied');
        } catch (e) {
            status.textContent = I18N.t('err.copy') + e.message;
        }
    });

    // Реестр и PowerShell читаются асинхронно: открытие панели не должно
    // замораживаться из-за медленного диска, реестра или запуска процесса.
    const version = document.getElementById('version');
    const run = document.getElementById('run');
    run.disabled = true;
    version.textContent = I18N.t('app.searching');
    PLUGIN_DIR = await pluginFolder();
    run.disabled = false;

    if (!PLUGIN_DIR) {
        version.textContent = I18N.t('app.notFound');
        return;
    }

    try {
        const settings = await readSettings();

        // Язык применяем ПЕРВЫМ делом: до этого на экране английский по
        // умолчанию, и человек с русской настройкой увидел бы вспышку
        // чужого языка на каждом открытии панели.
        const langSelect = document.getElementById('lang');
        langSelect.value = settings.lang || 'auto';
        I18N.applyLanguage(I18N.resolveLanguage(settings.lang));

        langSelect.addEventListener('change', async () => {
            I18N.applyLanguage(I18N.resolveLanguage(langSelect.value));
            try {
                const current = await readSettings();
                current.lang = langSelect.value;
                await writeSettings(current);
            } catch (e) {
                document.getElementById('saved').textContent =
                    I18N.t('err.write') + e.message;
            }
        });

        document.getElementById('enabled').checked = settings.enabled;
        const radio = document.querySelector(
            `input[name="decode"][value="${settings.decode}"]`);
        if (radio) radio.checked = true;
        document.getElementById('memory-cache').value =
            String(settings.cache_memory_mb);
        document.getElementById('preview-cache').checked = settings.preview_cache;
        document.getElementById('preview-limit').value =
            String(settings.preview_cache_mb);
        syncPreviewControls();
        await refreshCacheUsage();
    } catch (e) {
        document.getElementById('saved').textContent =
            I18N.t('settings.saveFailed') + e.message;
    }

    // Версия — из ресурса самого .prm.
    //
    // ⚠ Раньше путь передавался лишним аргументом в расчёте на $args[0], и это
    // не работало НИКОГДА: $args заполняется только при -File, а с -Command всё
    // после него склеивается в команду. Скрипт получал пустой путь, Get-Item
    // падал, и панель всегда писала «версия неизвестна» — ровно это и было
    // видно на живой панели.
    //
    // Путь вставляем в одинарные кавычки, удвоив внутренние: одинарная строка
    // в PowerShell ничего не раскрывает, а удвоение — её единственный способ
    // экранирования. Кавычка в имени папки скрипт не изменит.
    try {
        const prm = path.join(PLUGIN_DIR, 'Aether.prm').replace(/'/g, "''");
        const script =
            "(Get-Item -LiteralPath '" + prm + "').VersionInfo.ProductVersion";
        const out = (await execText(powershellExe(), [
            '-NoProfile', '-Command', script,
        ])).trim();
        version.textContent = out ? I18N.t('app.version') + out
                                  : I18N.t('app.versionUnknown');
    } catch (e) {
        version.textContent = I18N.t('app.versionUnknown');
    }
});
