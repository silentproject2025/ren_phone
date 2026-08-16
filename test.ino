  loadWifiCreds();
  loadNoteFromSD();
  connectWifi();

  // Buffer canvas persisten di PSRAM
  canvasBuf = (lv_color_t*)heap_caps_malloc(CANVAS_BUF_BYTES, MALLOC_CAP_SPIRAM);
  if (canvasBuf) {
    for (uint32_t i = 0; i < (uint32_t)CANVAS_W*CANVAS_H; i++) canvasBuf[i] = T().bg;
    loadCanvasFromSD();
  }

  // Init LVGL
  lv_init();
  lvBuf1 = (lv_color_t*)heap_caps_malloc(320 * LV_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  lv_disp_draw_buf_init(&draw_buf, lvBuf1, NULL, 320 * LV_BUF_LINES);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 320;
  disp_drv.ver_res = 240;
  disp_drv.flush_cb = lvglFlushCb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = lvglTouchReadCb;
  lv_indev_drv_register(&indev_drv);

  buildStyles();
  buildHomeScreen();
  buildClockScreen();
  buildCalcScreen();
  buildSensorScreen();
  buildSettingsScreen();
  buildNotepadScreen();
  buildCanvasScreen();
  buildStatusBar();
  lv_scr_load(scrHome);

  lv_timer_create(statusTimerCb, 1000, nullptr);
  lv_timer_create(sensorTimerCb, 2000, nullptr);
}

// =============================================
// LOOP
// =============================================
unsigned long lastTick = 0;
void loop() {
  unsigned long now = millis();
  lv_tick_inc(now - lastTick);
  lastTick = now;
  lv_timer_handler();
  delay(5);
}
