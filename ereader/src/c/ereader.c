#include <pebble.h>

#define PAGE_TEXT_MAX 1024
#define PERSIST_KEY_PAGE 1
#define PERSIST_KEY_TOTAL 2

static Window *s_window;
static ScrollLayer *s_scroll_layer;
static TextLayer *s_text_layer;
static TextLayer *s_status_layer;

static char s_page_text[PAGE_TEXT_MAX];
static int32_t s_page_index = 0;
static int32_t s_total_pages = 0;
static bool s_loading = false;

enum {
  MSG_CMD = 0,
  MSG_PAGE_INDEX = 1,
  MSG_TOTAL_PAGES = 2,
  MSG_TEXT = 3,
};

enum {
  CMD_READY = 1,
  CMD_REQUEST_PAGE = 2,
  CMD_PAGE_DATA = 3,
  CMD_NO_BOOK = 4,
  CMD_OPEN_CONFIG = 5,
};

static void update_status(const char *msg) {
  text_layer_set_text(s_status_layer, msg);
}

static void render_page(void) {
  text_layer_set_text(s_text_layer, s_page_text);
  GSize content = text_layer_get_content_size(s_text_layer);
  Layer *tl = text_layer_get_layer(s_text_layer);
  GRect frame = layer_get_frame(tl);
  frame.size.h = content.h + 8;
  layer_set_frame(tl, frame);
  scroll_layer_set_content_size(s_scroll_layer, GSize(frame.size.w, frame.size.h));
  scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);

  static char status[32];
  if (s_total_pages > 0) {
    snprintf(status, sizeof(status), "%ld / %ld", (long)(s_page_index + 1), (long)s_total_pages);
  } else {
    snprintf(status, sizeof(status), "Page %ld", (long)(s_page_index + 1));
  }
  update_status(status);
}

static void send_request(int32_t cmd, int32_t page_index) {
  DictionaryIterator *iter;
  AppMessageResult r = app_message_outbox_begin(&iter);
  if (r != APP_MSG_OK) return;
  dict_write_int32(iter, MSG_CMD, cmd);
  dict_write_int32(iter, MSG_PAGE_INDEX, page_index);
  dict_write_end(iter);
  app_message_outbox_send();
}

static void request_page(int32_t page_index) {
  if (s_loading) return;
  if (s_total_pages > 0 && (page_index < 0 || page_index >= s_total_pages)) return;
  s_loading = true;
  update_status("Loading...");
  send_request(CMD_REQUEST_PAGE, page_index);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *cmd_t = dict_find(iter, MSG_CMD);
  if (!cmd_t) return;
  int32_t cmd = cmd_t->value->int32;

  if (cmd == CMD_NO_BOOK) {
    s_loading = false;
    strncpy(s_page_text, "No book loaded.\n\nOpen the Pebble app settings to upload an EPUB.", PAGE_TEXT_MAX - 1);
    s_page_text[PAGE_TEXT_MAX - 1] = 0;
    s_total_pages = 0;
    render_page();
    update_status("No book");
    return;
  }

  if (cmd == CMD_PAGE_DATA) {
    Tuple *idx = dict_find(iter, MSG_PAGE_INDEX);
    Tuple *total = dict_find(iter, MSG_TOTAL_PAGES);
    Tuple *text = dict_find(iter, MSG_TEXT);
    if (idx) s_page_index = idx->value->int32;
    if (total) s_total_pages = total->value->int32;
    if (text) {
      strncpy(s_page_text, text->value->cstring, PAGE_TEXT_MAX - 1);
      s_page_text[PAGE_TEXT_MAX - 1] = 0;
    } else {
      s_page_text[0] = 0;
    }
    persist_write_int(PERSIST_KEY_PAGE, s_page_index);
    persist_write_int(PERSIST_KEY_TOTAL, s_total_pages);
    s_loading = false;
    render_page();
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  s_loading = false;
  update_status("Drop");
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  s_loading = false;
  update_status("Send fail");
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  request_page(s_page_index - 1);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  request_page(s_page_index + 1);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  request_page(s_page_index);
}

static void select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  update_status("Open settings on phone");
  send_request(CMD_OPEN_CONFIG, 0);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 600, select_long_click_handler, NULL);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  const int status_h = 16;
  s_status_layer = text_layer_create(GRect(0, bounds.size.h - status_h, bounds.size.w, status_h));
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_text(s_status_layer, "Connecting...");
  layer_add_child(root, text_layer_get_layer(s_status_layer));

  GRect scroll_bounds = GRect(0, 0, bounds.size.w, bounds.size.h - status_h);
  s_scroll_layer = scroll_layer_create(scroll_bounds);
  scroll_layer_set_click_config_onto_window(s_scroll_layer, window);

  s_text_layer = text_layer_create(GRect(4, 0, scroll_bounds.size.w - 8, 2000));
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text(s_text_layer, "");
  text_layer_set_overflow_mode(s_text_layer, GTextOverflowModeWordWrap);

  scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_text_layer));
  layer_add_child(root, scroll_layer_get_layer(s_scroll_layer));

  window_set_click_config_provider(window, click_config_provider);
}

static void window_unload(Window *window) {
  text_layer_destroy(s_text_layer);
  text_layer_destroy(s_status_layer);
  scroll_layer_destroy(s_scroll_layer);
}

static void init(void) {
  s_page_index = persist_exists(PERSIST_KEY_PAGE) ? persist_read_int(PERSIST_KEY_PAGE) : 0;
  s_total_pages = persist_exists(PERSIST_KEY_TOTAL) ? persist_read_int(PERSIST_KEY_TOTAL) : 0;

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_open(2048, 256);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  send_request(CMD_READY, s_page_index);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
