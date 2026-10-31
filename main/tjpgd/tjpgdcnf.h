/*
 * TJpgDec configuration for this project (replaces the file of the same name
 * that ships with TJpgDec, which reads the settings from esp_jpeg's Kconfig).
 *
 * TJpgDec R0.03, (C)ChaN 2021, free software - see the header of tjpgd.c.
 */

#pragma once

/* Size of the stream input buffer.  The JPEG sits in RAM, so this is only a
 * copy buffer; 512 bytes keep the number of calls small without wasting RAM. */
#define JD_SZBUF        512

/* Output pixel format: 0 = RGB888, 1 = RGB565, 2 = grayscale.
 * RGB565 matches what the ILI9341 and the canvas use. */
#define JD_FORMAT       1

/* Output descaling (1/2, 1/4, 1/8) - not used, keeps the code small */
#define JD_USE_SCALE    0

/* Table based saturation arithmetic: ~1 KB more code, a bit faster */
#define JD_TBLCLIP      1

/* 0 = basic, 1 = + 32-bit barrel shifter (right for the ESP32),
 * 2 = + huffman lookup tables (needs 6 << 10 bytes more RAM) */
#define JD_FASTDECODE   1
