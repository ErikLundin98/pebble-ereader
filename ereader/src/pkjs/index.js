// PebbleKit JS side of the ereader.
//
// Storage:
//   localStorage["ereader.pages"]  JSON array of page strings
//   localStorage["ereader.title"]  book title
//   localStorage["ereader.cursor"] last-known page index (synced with watch)
//
// Protocol with watch (mirror src/c/ereader.c):
//   CMD 1 READY         watch -> phone, phone replies with current page (or NO_BOOK)
//   CMD 2 REQUEST_PAGE  watch -> phone with desired PAGE_INDEX
//   CMD 3 PAGE_DATA     phone -> watch
//   CMD 4 NO_BOOK       phone -> watch

var CMD_READY = 1;
var CMD_REQUEST_PAGE = 2;
var CMD_PAGE_DATA = 3;
var CMD_NO_BOOK = 4;

// Public URL hosting config.html (e.g. GitHub Pages). For local dev, host via
// any static server and use that URL here.
var CONFIG_URL = 'https://example.com/pebble-ereader/config.html';

function getPages() {
  var raw = localStorage.getItem('ereader.pages');
  if (!raw) return null;
  try { return JSON.parse(raw); } catch (e) { return null; }
}

function setPages(pages, title) {
  localStorage.setItem('ereader.pages', JSON.stringify(pages));
  localStorage.setItem('ereader.title', title || 'Untitled');
  localStorage.setItem('ereader.cursor', '0');
}

function getCursor() {
  var v = parseInt(localStorage.getItem('ereader.cursor') || '0', 10);
  return isNaN(v) ? 0 : v;
}

function setCursor(i) {
  localStorage.setItem('ereader.cursor', String(i));
}

function sendNoBook() {
  Pebble.sendAppMessage({ CMD: CMD_NO_BOOK });
}

function sendPage(index) {
  var pages = getPages();
  if (!pages || pages.length === 0) { sendNoBook(); return; }
  if (index < 0) index = 0;
  if (index >= pages.length) index = pages.length - 1;
  setCursor(index);
  Pebble.sendAppMessage({
    CMD: CMD_PAGE_DATA,
    PAGE_INDEX: index,
    TOTAL_PAGES: pages.length,
    TEXT: pages[index],
  }, function () {}, function (e) {
    console.log('sendAppMessage failed: ' + JSON.stringify(e));
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
    if (!pages) { sendNoBook(); return; }
    sendPage(getCursor());
  } else if (cmd === CMD_REQUEST_PAGE) {
    sendPage(payload.PAGE_INDEX || 0);
  }
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(CONFIG_URL);
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e.response) return;
  var data;
  try { data = JSON.parse(decodeURIComponent(e.response)); } catch (err) {
    console.log('config parse error: ' + err);
    return;
  }
  if (data && data.pages && data.pages.length) {
    setPages(data.pages, data.title);
    sendPage(0);
  }
});
