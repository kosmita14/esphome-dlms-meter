#pragma once
#include <cstdio>
#define ESP_LOGI(tag, ...) do { std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#define ESP_LOGW(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define ESP_LOGCONFIG(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) do {} while (0)
