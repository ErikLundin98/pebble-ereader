// PebbleKit JS bridge for the ereader.
//
// localStorage:
//   ereader.pages    JSON [{ text, chapter, heading? }, ...]
//   ereader.chapters JSON [{ title, firstPage }, ...]
//   ereader.title    book title
//   ereader.author   book author
//   ereader.font     last chosen font id
//   ereader.cursor   last-read page index

var CMD_READY = 1;
var CMD_REQUEST_PAGE = 2;
var CMD_PAGE_DATA = 3;
var CMD_NO_BOOK = 4;
var CMD_OPEN_CONFIG = 5;
var CMD_REQUEST_TOC = 6;
var CMD_TOC_DATA = 7;

var CONFIG_URL = 'https://eriklundin98.github.io/pebble-ereader/config/config.html';

function lsGet(k, def) { var v = localStorage.getItem(k); return v === null ? def : v; }
function lsJSON(k) {
  var v = localStorage.getItem(k);
  if (!v) return null;
  try { return JSON.parse(v); } catch (e) { return null; }
}

function getPages()    { return lsJSON('ereader.pages'); }
function getChapters() { return lsJSON('ereader.chapters') || []; }
function getTitle()    { return lsGet('ereader.title', ''); }
function getFont()     { return lsGet('ereader.font', 'G18'); }
function getCursor() {
  var v = parseInt(lsGet('ereader.cursor', '0'), 10);
  return isNaN(v) ? 0 : v;
}
function setCursor(i) { localStorage.setItem('ereader.cursor', String(i)); }

function saveBook(data) {
  localStorage.setItem('ereader.pages', JSON.stringify(data.pages));
  localStorage.setItem('ereader.chapters', JSON.stringify(data.chapters || []));
  localStorage.setItem('ereader.title', data.title || 'Untitled');
  localStorage.setItem('ereader.author', data.author || '');
  localStorage.setItem('ereader.font', data.font || 'G18');
  var s = parseInt(data.startPage, 10);
  if (isNaN(s) || s < 0) s = 0;
  if (s >= data.pages.length) s = data.pages.length - 1;
  setCursor(s);
}

function configUrl() {
  var info = (Pebble.getActiveWatchInfo && Pebble.getActiveWatchInfo()) || {};
  var platform = info.platform || 'basalt';
  return CONFIG_URL + '?platform=' + encodeURIComponent(platform);
}

function sendNoBook() { Pebble.sendAppMessage({ CMD: CMD_NO_BOOK }); }

function sendPage(index) {
  var pages = getPages();
  if (!pages || !pages.length) { sendNoBook(); return; }
  if (index < 0) index = 0;
  if (index >= pages.length) index = pages.length - 1;
  setCursor(index);

  var page = pages[index];
  var chapters = getChapters();
  var chapter = chapters[page.chapter] || null;
  var progress = Math.round(((index + 1) / pages.length) * 100);

  var msg = {
    CMD: CMD_PAGE_DATA,
    PAGE_INDEX: index,
    TOTAL_PAGES: pages.length,
    TEXT: page.text || '',
    FONT: getFont(),
    CHAPTER_INDEX: page.chapter || 0,
    CHAPTER_TOTAL: chapters.length,
    CHAPTER_TITLE: chapter ? chapter.title : '',
    BOOK_TITLE: getTitle(),
    PROGRESS: progress,
  };
  if (page.heading) msg.HEADING = page.heading;

  Pebble.sendAppMessage(msg, function () {}, function (e) {
    console.log('sendAppMessage(page) failed: ' + JSON.stringify(e));
  });
}

function sendToc() {
  var chapters = getChapters();
  if (!chapters.length) { sendNoBook(); return; }

  // AppMessage payload caps around ~3 KB on iOS; trim if needed.
  // We send titles joined with \n and pages joined with commas.
  var titles = [];
  var pages = [];
  var byteLen = 0;
  for (var i = 0; i < chapters.length; i++) {
    var t = (chapters[i].title || ('Chapter ' + (i + 1))).replace(/\s+/g, ' ').trim();
    // Truncate over-long titles to keep things tidy on the watch.
    if (t.length > 48) t = t.slice(0, 47) + '\u2026';
    byteLen += t.length + 8;
    if (byteLen > 1400) break;
    titles.push(t);
    pages.push(chapters[i].firstPage);
  }

  Pebble.sendAppMessage({
    CMD: CMD_TOC_DATA,
    TOC: titles.join('\n'),
    TOC_PAGES: pages.join(','),
  }, function () {}, function (e) {
    console.log('sendAppMessage(toc) failed: ' + JSON.stringify(e));
  });
}

Pebble.addEventListener('ready', function () {
  console.log('ereader pkjs ready');
});

Pebble.addEventListener('appmessage', function (e) {
  var payload = e.payload || {};
  var cmd = payload.CMD;
  if (cmd === CMD_READY) {
    var pages = getPages();
    if (!pages) {
      sendNoBook();
      Pebble.openURL(configUrl());
      return;
    }
    sendPage(getCursor());
  } else if (cmd === CMD_REQUEST_PAGE) {
    sendPage(payload.PAGE_INDEX || 0);
  } else if (cmd === CMD_OPEN_CONFIG) {
    Pebble.openURL(configUrl());
  } else if (cmd === CMD_REQUEST_TOC) {
    sendToc();
  }
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(configUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e.response) return;
  var data;
  try { data = JSON.parse(decodeURIComponent(e.response)); } catch (err) {
    console.log('config parse error: ' + err);
    return;
  }
  if (data && data.pages && data.pages.length) {
    saveBook(data);
    sendPage(getCursor());
  }
});
