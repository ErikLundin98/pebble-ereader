#include <pebble.h>

#define PAGE_TEXT_MAX 1536
#define HEADING_MAX 96
#define CHAPTER_TITLE_MAX 64
#define BOOK_TITLE_MAX 64
#define STATUS_MAX 64

#define TOC_BUFFER 1600
#define TOC_MAX_CHAPTERS 80

#define PAGE_STEP 10

#define PERSIST_KEY_PAGE 1
#define PERSIST_KEY_TOTAL 2

#define MSG_CMD MESSAGE_KEY_CMD
#define MSG_PAGE_INDEX MESSAGE_KEY_PAGE_INDEX
#define MSG_TOTAL_PAGES MESSAGE_KEY_TOTAL_PAGES
#define MSG_TEXT MESSAGE_KEY_TEXT
#define MSG_FONT MESSAGE_KEY_FONT
#define MSG_HEADING MESSAGE_KEY_HEADING
#define MSG_CHAPTER_INDEX MESSAGE_KEY_CHAPTER_INDEX
#define MSG_CHAPTER_TOTAL MESSAGE_KEY_CHAPTER_TOTAL
#define MSG_CHAPTER_TITLE MESSAGE_KEY_CHAPTER_TITLE
#define MSG_BOOK_TITLE MESSAGE_KEY_BOOK_TITLE
#define MSG_PROGRESS MESSAGE_KEY_PROGRESS
#define MSG_TOC MESSAGE_KEY_TOC
#define MSG_TOC_PAGES MESSAGE_KEY_TOC_PAGES

enum {
  CMD_READY = 1,
  CMD_REQUEST_PAGE = 2,
  CMD_PAGE_DATA = 3,
  CMD_NO_BOOK = 4,
  CMD_OPEN_CONFIG = 5,
  CMD_REQUEST_TOC = 6,
  CMD_TOC_DATA = 7,
};

// ---- Reader window ----
static Window *s_window;
static TextLayer *s_status_layer;
static TextLayer *s_heading_layer;
static ScrollLayer *s_scroll_layer;
static TextLayer *s_text_layer;

static char s_page_text[PAGE_TEXT_MAX];
static char s_heading_text[HEADING_MAX];
static char s_chapter_title[CHAPTER_TITLE_MAX];
static char s_book_title[BOOK_TITLE_MAX];
static char s_status_text[STATUS_MAX];

static int32_t s_page_index = 0;
static int32_t s_total_pages = 0;
static int32_t s_chapter_index = 0;
static int32_t s_chapter_total = 0;
static int32_t s_progress_pct = 0;
static int32_t s_last_chapter_index = -1;
static bool s_loading = false;
static bool s_has_heading = false;
static const char *s_body_font_key = FONT_KEY_GOTHIC_18;

// ---- TOC window ----
static Window *s_toc_window;
static MenuLayer *s_toc_menu;
static char s_toc_buffer[TOC_BUFFER];
static const char *s_toc_titles[TOC_MAX_CHAPTERS];
static int32_t s_toc_pages[TOC_MAX_CHAPTERS];
static int s_toc_count = 0;
static bool s_toc_loading = false;

// ===========================================================================
// Helpers
// ===========================================================================

static const char *resolve_body_font(const char *id) {
  if (!id || !id[0]) return FONT_KEY_GOTHIC_18;
  if (strcmp(id, "G14") == 0) return FONT_KEY_GOTHIC_14;
  if (strcmp(id, "G18") == 0) return FONT_KEY_GOTHIC_18;
  if (strcmp(id, "G24") == 0) return FONT_KEY_GOTHIC_24_BOLD;
  if (strcmp(id, "G28") == 0) return FONT_KEY_GOTHIC_28;
  return FONT_KEY_GOTHIC_18;
}

static const char *resolve_heading_font(const char *id) {
  if (!id || !id[0]) return FONT_KEY_GOTHIC_24_BOLD;
  if (strcmp(id, "G14") == 0) return FONT_KEY_GOTHIC_18_BOLD;
  if (strcmp(id, "G18") == 0) return FONT_KEY_GOTHIC_24_BOLD;
  if (strcmp(id, "G24") == 0) return FONT_KEY_GOTHIC_28_BOLD;
  if (strcmp(id, "G28") == 0) return FONT_KEY_GOTHIC_28_BOLD;
  return FONT_KEY_GOTHIC_24_BOLD;
}

static int heading_height_for(const char *id) {
  if (id && strcmp(id, "G14") == 0) return 24;
  if (id && strcmp(id, "G18") == 0) return 32;
  if (id && strcmp(id, "G24") == 0) return 36;
  if (id && strcmp(id, "G28") == 0) return 36;
  return 32;
}

static void copy_str(char *dst, size_t cap, const char *src) {
  if (!src) { dst[0] = 0; return; }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
}

static void send_simple(int32_t cmd, int32_t arg) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_int32(iter, MSG_CMD, cmd);
  dict_write_int32(iter, MSG_PAGE_INDEX, arg);
  dict_write_end(iter);
  app_message_outbox_send();
}

static void update_status(void) {
  if (s_total_pages <= 0) {
    if (s_book_title[0]) {
      snprintf(s_status_text, STATUS_MAX, "%s", s_book_title);
    } else {
      snprintf(s_status_text, STATUS_MAX, "Connecting...");
    }
  } else if (s_chapter_total > 0 && s_chapter_title[0]) {
    snprintf(s_status_text, STATUS_MAX, "Ch %ld/%ld - %ld%%",
             (long)(s_chapter_index + 1), (long)s_chapter_total, (long)s_progress_pct);
  } else {
    snprintf(s_status_text, STATUS_MAX, "%ld / %ld - %ld%%",
             (long)(s_page_index + 1), (long)s_total_pages, (long)s_progress_pct);
  }
  text_layer_set_text(s_status_layer, s_status_text);
}

static void relayout_reader(void) {
  Layer *root = window_get_root_layer(s_window);
  GRect b = layer_get_bounds(root);
  const int status_h = 16;

  int top_y = 0;
  int top_h = 0;
  if (s_has_heading) {
    top_h = heading_height_for(NULL); // body font drives this; pick generously
    layer_set_frame(text_layer_get_layer(s_heading_layer),
                    GRect(4, top_y, b.size.w - 8, top_h));
    layer_set_hidden(text_layer_get_layer(s_heading_layer), false);
    text_layer_set_text(s_heading_layer, s_heading_text);
  } else {
    layer_set_hidden(text_layer_get_layer(s_heading_layer), true);
  }

  int scroll_y = top_y + top_h;
  int scroll_h = b.size.h - status_h - scroll_y;
  layer_set_frame(scroll_layer_get_layer(s_scroll_layer),
                  GRect(0, scroll_y, b.size.w, scroll_h));

  text_layer_set_font(s_text_layer, fonts_get_system_font(s_body_font_key));
  text_layer_set_text(s_text_layer, s_page_text);

  Layer *tl = text_layer_get_layer(s_text_layer);
  GRect tf = layer_get_frame(tl);
  tf.size.w = b.size.w - 8;
  tf.size.h = 2000;
  layer_set_frame(tl, tf);
  GSize content = text_layer_get_content_size(s_text_layer);
  tf.size.h = content.h + 8;
  layer_set_frame(tl, tf);
  scroll_layer_set_content_size(s_scroll_layer, GSize(tf.size.w, tf.size.h));
  scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);
}

static void render_page(void) {
  relayout_reader();
  update_status();
  if (s_total_pages > 0 && s_last_chapter_index != -1 &&
      s_chapter_index != s_last_chapter_index) {
    vibes_short_pulse();
  }
  s_last_chapter_index = s_chapter_index;
}

// ===========================================================================
// AppMessage
// ===========================================================================

static void request_page(int32_t page_index) {
  if (s_loading) return;
  if (s_total_pages > 0) {
    if (page_index < 0) page_index = 0;
    if (page_index >= s_total_pages) page_index = s_total_pages - 1;
    if (page_index == s_page_index) return;
  }
  s_loading = true;
  text_layer_set_text(s_status_layer, "Loading...");
  send_simple(CMD_REQUEST_PAGE, page_index);
}

static void jump_chapter(int delta) {
  // We don't know chapter boundaries on the watch unless TOC was loaded.
  // If we have TOC data, jump to s_toc_pages[chapter+delta]; otherwise nudge
  // pages by a heuristic (one chapter ~ total_pages / chapter_total).
  if (s_toc_count > 0 && s_chapter_total > 0) {
    int32_t target = s_chapter_index + delta;
    if (target < 0) target = 0;
    if (target >= s_toc_count) target = s_toc_count - 1;
    request_page(s_toc_pages[target]);
    return;
  }
  if (s_chapter_total > 0 && s_total_pages > 0) {
    int32_t per = s_total_pages / s_chapter_total;
    if (per < 1) per = 1;
    request_page(s_page_index + delta * per);
    return;
  }
  request_page(s_page_index + delta * PAGE_STEP);
}

static void handle_page_data(DictionaryIterator *iter) {
  Tuple *t;

  t = dict_find(iter, MSG_PAGE_INDEX);   if (t) s_page_index = t->value->int32;
  t = dict_find(iter, MSG_TOTAL_PAGES);  if (t) s_total_pages = t->value->int32;
  t = dict_find(iter, MSG_CHAPTER_INDEX); if (t) s_chapter_index = t->value->int32;
  t = dict_find(iter, MSG_CHAPTER_TOTAL); if (t) s_chapter_total = t->value->int32;
  t = dict_find(iter, MSG_PROGRESS);     if (t) s_progress_pct = t->value->int32;

  t = dict_find(iter, MSG_CHAPTER_TITLE);
  copy_str(s_chapter_title, CHAPTER_TITLE_MAX, t ? t->value->cstring : "");
  t = dict_find(iter, MSG_BOOK_TITLE);
  if (t) copy_str(s_book_title, BOOK_TITLE_MAX, t->value->cstring);

  t = dict_find(iter, MSG_FONT);
  if (t) s_body_font_key = resolve_body_font(t->value->cstring);

  t = dict_find(iter, MSG_HEADING);
  if (t && t->length > 1) {
    s_has_heading = true;
    copy_str(s_heading_text, HEADING_MAX, t->value->cstring);
    text_layer_set_font(s_heading_layer,
                        fonts_get_system_font(
                            resolve_heading_font(t ? t->value->cstring : NULL)));
  } else {
    s_has_heading = false;
    s_heading_text[0] = 0;
  }

  t = dict_find(iter, MSG_TEXT);
  copy_str(s_page_text, PAGE_TEXT_MAX, t ? t->value->cstring : "");
  APP_LOG(APP_LOG_LEVEL_INFO,
          "PAGE_DATA idx=%ld total=%ld text_len=%u heading=%d",
          (long)s_page_index, (long)s_total_pages,
          (unsigned)strlen(s_page_text), (int)s_has_heading);

  persist_write_int(PERSIST_KEY_PAGE, s_page_index);
  persist_write_int(PERSIST_KEY_TOTAL, s_total_pages);

  s_loading = false;
  render_page();
}

static void handle_toc_data(DictionaryIterator *iter) {
  Tuple *titles = dict_find(iter, MSG_TOC);
  Tuple *pages = dict_find(iter, MSG_TOC_PAGES);
  s_toc_count = 0;
  if (!titles) return;
  copy_str(s_toc_buffer, TOC_BUFFER, titles->value->cstring);

  // Split s_toc_buffer on '\n' into pointers; in-place.
  char *p = s_toc_buffer;
  s_toc_titles[s_toc_count++] = p;
  while (*p && s_toc_count < TOC_MAX_CHAPTERS) {
    if (*p == '\n') {
      *p = 0;
      s_toc_titles[s_toc_count++] = p + 1;
    }
    p++;
  }
  // Drop trailing empty entry produced by terminal '\n'.
  if (s_toc_count > 0 && s_toc_titles[s_toc_count - 1][0] == 0) s_toc_count--;

  // Parse pages "1,15,42,..."
  int idx = 0;
  if (pages) {
    const char *q = pages->value->cstring;
    int32_t acc = 0;
    bool any = false;
    while (*q && idx < s_toc_count) {
      if (*q >= '0' && *q <= '9') { acc = acc * 10 + (*q - '0'); any = true; }
      else if (*q == ',') {
        if (any) { s_toc_pages[idx++] = acc; acc = 0; any = false; }
      }
      q++;
    }
    if (any && idx < s_toc_count) s_toc_pages[idx++] = acc;
  }
  // Fill any missing with -1 sentinel.
  while (idx < s_toc_count) { s_toc_pages[idx++] = -1; }

  s_toc_loading = false;
  if (s_toc_menu) {
    menu_layer_reload_data(s_toc_menu);
  }
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *cmd_t = dict_find(iter, MSG_CMD);
  if (!cmd_t) return;
  int32_t cmd = cmd_t->value->int32;

  if (cmd == CMD_NO_BOOK) {
    s_loading = false;
    s_total_pages = 0;
    s_chapter_total = 0;
    copy_str(s_page_text, PAGE_TEXT_MAX,
             "No book loaded.\n\nOpen the Pebble app and upload an EPUB.");
    s_has_heading = false;
    s_chapter_title[0] = 0;
    render_page();
  } else if (cmd == CMD_PAGE_DATA) {
    handle_page_data(iter);
  } else if (cmd == CMD_TOC_DATA) {
    handle_toc_data(iter);
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  s_loading = false;
  text_layer_set_text(s_status_layer, "Drop");
}

static void outbox_failed_handler(DictionaryIterator *iter,
                                  AppMessageResult reason, void *context) {
  s_loading = false;
  text_layer_set_text(s_status_layer, "Send fail");
}

// ===========================================================================
// TOC window
// ===========================================================================

static uint16_t toc_get_num_sections(MenuLayer *m, void *ctx) { return 1; }

static uint16_t toc_get_num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  if (s_toc_loading) return 1;
  if (s_toc_count == 0) return 1;
  return s_toc_count;
}

static void toc_draw_row(GContext *gctx, const Layer *cell_layer,
                         MenuIndex *idx, void *ctx) {
  if (s_toc_loading) {
    menu_cell_basic_draw(gctx, cell_layer, "Loading...", NULL, NULL);
    return;
  }
  if (s_toc_count == 0) {
    menu_cell_basic_draw(gctx, cell_layer, "No chapters", NULL, NULL);
    return;
  }
  char sub[16];
  snprintf(sub, sizeof(sub), "Page %ld", (long)(s_toc_pages[idx->row] + 1));
  menu_cell_basic_draw(gctx, cell_layer, s_toc_titles[idx->row], sub, NULL);
}

static void toc_select(MenuLayer *m, MenuIndex *idx, void *ctx) {
  if (s_toc_loading || s_toc_count == 0) return;
  int32_t target = s_toc_pages[idx->row];
  if (target < 0) return;
  window_stack_pop(true);
  request_page(target);
}

static void toc_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  s_toc_menu = menu_layer_create(b);
  menu_layer_set_callbacks(s_toc_menu, NULL, (MenuLayerCallbacks){
    .get_num_sections = toc_get_num_sections,
    .get_num_rows = toc_get_num_rows,
    .draw_row = toc_draw_row,
    .select_click = toc_select,
  });
  menu_layer_set_click_config_onto_window(s_toc_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_toc_menu));
}

static void toc_window_unload(Window *window) {
  if (s_toc_menu) { menu_layer_destroy(s_toc_menu); s_toc_menu = NULL; }
}

static void open_toc(void) {
  if (s_total_pages <= 0) return;
  s_toc_loading = true;
  if (!s_toc_window) {
    s_toc_window = window_create();
    window_set_window_handlers(s_toc_window, (WindowHandlers){
      .load = toc_window_load,
      .unload = toc_window_unload,
    });
  }
  window_stack_push(s_toc_window, true);
  send_simple(CMD_REQUEST_TOC, 0);
}

// ===========================================================================
// Reader window: clicks
// ===========================================================================

static void up_multi(ClickRecognizerRef r, void *c) {
  uint8_t n = click_number_of_clicks_counted(r);
  request_page(s_page_index - ((n >= 2) ? PAGE_STEP : 1));
}
static void down_multi(ClickRecognizerRef r, void *c) {
  uint8_t n = click_number_of_clicks_counted(r);
  request_page(s_page_index + ((n >= 2) ? PAGE_STEP : 1));
}
static void up_long(ClickRecognizerRef r, void *c) { jump_chapter(-1); }
static void down_long(ClickRecognizerRef r, void *c) { jump_chapter(1); }
static void select_short(ClickRecognizerRef r, void *c) { open_toc(); }
static void select_long(ClickRecognizerRef r, void *c) {
  send_simple(CMD_OPEN_CONFIG, 0);
  text_layer_set_text(s_status_layer, "Open settings on phone");
}

static void click_config_provider(void *context) {
  window_multi_click_subscribe(BUTTON_ID_UP, 1, 2, 250, true, up_multi);
  window_multi_click_subscribe(BUTTON_ID_DOWN, 1, 2, 250, true, down_multi);
  window_long_click_subscribe(BUTTON_ID_UP, 600, up_long, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 600, down_long, NULL);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_short);
  window_long_click_subscribe(BUTTON_ID_SELECT, 600, select_long, NULL);
}

// ===========================================================================
// Reader window: lifecycle
// ===========================================================================

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  const int status_h = 16;

  s_status_layer = text_layer_create(
      GRect(0, b.size.h - status_h, b.size.w, status_h));
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_text(s_status_layer, "Connecting...");
  layer_add_child(root, text_layer_get_layer(s_status_layer));

  s_heading_layer = text_layer_create(GRect(4, 0, b.size.w - 8, 32));
  text_layer_set_font(s_heading_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_overflow_mode(s_heading_layer, GTextOverflowModeWordWrap);
  layer_set_hidden(text_layer_get_layer(s_heading_layer), true);
  layer_add_child(root, text_layer_get_layer(s_heading_layer));

  s_scroll_layer = scroll_layer_create(
      GRect(0, 0, b.size.w, b.size.h - status_h));

  s_text_layer = text_layer_create(GRect(4, 0, b.size.w - 8, 2000));
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_overflow_mode(s_text_layer, GTextOverflowModeWordWrap);
  scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_text_layer));
  layer_add_child(root, scroll_layer_get_layer(s_scroll_layer));

  window_set_click_config_provider(window, click_config_provider);
}

static void window_unload(Window *window) {
  text_layer_destroy(s_text_layer);
  text_layer_destroy(s_heading_layer);
  text_layer_destroy(s_status_layer);
  scroll_layer_destroy(s_scroll_layer);
}

// ===========================================================================
// Init / main
// ===========================================================================

static void init(void) {
  s_page_index = persist_exists(PERSIST_KEY_PAGE) ? persist_read_int(PERSIST_KEY_PAGE) : 0;
  s_total_pages = persist_exists(PERSIST_KEY_TOTAL) ? persist_read_int(PERSIST_KEY_TOTAL) : 0;

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_open(3072, 256);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  send_simple(CMD_READY, s_page_index);
}

static void deinit(void) {
  if (s_toc_window) window_destroy(s_toc_window);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
