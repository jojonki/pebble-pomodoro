#include <pebble.h>

#include "pomodoro.h"

static void sec_tick_handler(struct tm *time_now, TimeUnits changed) {
  main_window_sec_update(time_now->tm_hour, time_now->tm_min, time_now->tm_sec);
}

static void min_tick_handler(struct tm *time_now, TimeUnits changed) {
  main_window_min_update(time_now->tm_hour, time_now->tm_min);
}

static void init() {
  main_window_push();

  tick_timer_service_subscribe(MINUTE_UNIT, min_tick_handler);
  tick_timer_service_subscribe(SECOND_UNIT, sec_tick_handler);
}

static void deinit() {
  tick_timer_service_unsubscribe();
}

int main() {
  init();
  app_event_loop();
  deinit();
}
