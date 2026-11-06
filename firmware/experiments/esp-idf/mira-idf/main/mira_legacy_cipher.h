#pragma once
// Read-only compatibility with the encrypted strings saved by Mira's
// Arduino beta. No data is written to the old SPIFFS files.
#include <stdbool.h>
#include <stddef.h>
bool mira_legacy_decrypt(const char *stored, char *plaintext, size_t capacity);
