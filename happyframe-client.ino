#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <LittleFS.h>
#include <Adafruit_RA8875.h>

// define a credentials.h defining SSID and optionally WPA_KEY
#include "credentials.h"


// Library only supports hardware SPI at this time
// Connect SCLK to UNO Digital #13 (Hardware SPI clock)
// Connect MISO to UNO Digital #12 (Hardware SPI MISO)
// Connect MOSI to UNO Digital #11 (Hardware SPI MOSI)
#define RA8875_INT 4
#define RA8875_CS 16
#define RA8875_RESET 5

// The number of pixels to buffer for loading
#define BUFFPIXEL 20

Adafruit_RA8875 tft = Adafruit_RA8875(RA8875_CS, RA8875_RESET);

void setup() {
    Serial.begin(115200);
    #ifdef WPA_KEY
        WiFi.begin(SSID, WPA_KEY);
    #else
        WiFi.begin(SSID);
    #endif
    while (WiFi.status() != WL_CONNECTED) {
        delay(200);
    }
    Serial.println("Wifi connected");

    while (!tft.begin(RA8875_800x480)) {
        delay(500);
    }
    tft.displayOn(true);
    tft.GPIOX(true);
    tft.PWM1config(true, RA8875_PWM_CLK_DIV1024);
    tft.PWM1out(255);
    Serial.println("TFT initialized");

    LittleFS.begin();
    if (!LittleFS.exists("/image.bmp")) {
        Serial.println("Don't have image, have to go fetch");
        request_and_save_file("https://people.math.sc.edu/Burkardt/data/bmp/dots.bmp");
    }
    display_bmp(LittleFS.open("/image.bmp", "r"));
}

void loop() {
    //tft.fillScreen(RA8875_WHITE);
    //delay(500);
    //tft.fillScreen(RA8875_RED);
    //delay(500);
}

void request_and_save_file(String url) {
    HTTPClient http;
    WiFiClient wifi;
    http.begin(wifi, url);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK && http.header("Content-Type").indexOf("image/bmp") >= 0) {
        File image = LittleFS.open("/image.bmp", "w");
        int len = http.getSize();
        uint8_t buff[128] = { 0 };
        WiFiClient* stream = http.getStreamPtr();
        while (http.connected() && (len > 0 || len == -1)) {
            size_t size = stream->available();
            if (size) {
                // Read up to 128 bytes
                int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                image.write(buff, c);
                if (len > 0) {
                    len -= c;
                }
            }
            delay(1);
        }
    }
    http.end();
}

// Adapted from https://github.com/adafruit/Adafruit_RA8875/blob/master/examples/ra8875_bitmap_fast/ra8875_bitmap_fast.ino
void display_bmp(File image) {
    Serial.println("Displaying image");
    int bmpWidth, bmpHeight;
    uint8_t bmpDepth; // Bit depth (must be 24 currently)
    uint32_t bmpImageOffset;
    uint32_t rowSize;
    uint8_t fsbuffer[3 * BUFFPIXEL]; // (R+G+B per pixel)
    uint16_t lcdbuffer[BUFFPIXEL]; // (16-bit per pixel for display)
    uint8_t buffidx = sizeof(fsbuffer);
    boolean goodBmp = false;
    boolean flip = true;
    int w, h, row, col, xpos, ypos;
    uint8_t r, g, b;
    uint32_t pos = 0;
    uint8_t lcdidx = 0;
    boolean first = true;

    if (read16(image) == 0x4D42) { // BMP header magic number
        (void) read32(image); // read and throw out file size
        (void) read32(image); // read and throw out creator metadata
        bmpImageOffset = read32(image); // Save index of the start of image data
        (void) read32(image); // Throw out start of DIB header
        bmpWidth = read32(image);
        bmpHeight = read32(image);
        if (bmpWidth == 800 && bmpHeight == 480) {
            Serial.println("Image width and height are correct");
            if (read16(image) == 1) { // Must be only 1 plane
                bmpDepth = read16(image);
                Serial.print("Bit depth: ");
                Serial.println(bmpDepth);
                if ((bmpDepth == 24) && (read32(image) == 0)) { // 0 = uncompressed
                    Serial.println("Image has proper bit depth and is uncompressed");
                    goodBmp = true;

                    // BMP rows are padded (if needed) to 4-byte boundary
                    rowSize = (bmpWidth * 3 + 3) & ~3;

                    // If bmpHeight is negative, image is in top-down order as opposed to canonical bottom-up
                    if (bmpHeight < 0) {
                        bmpHeight = -bmpHeight;
                        flip = false;
                    }
                    w = tft.width();
                    h = tft.height();
                    ypos = 0;
                    for (row = 0; row < h; ++row) {
                        // For each scanline seek to start of scanline
                        //Serial.print("X, Y: ");
                        //Serial.print(xpos);
                        //Serial.print(", ");
                        //Serial.println(ypos);
                        //Serial.print("Lcdindex: ");
                        //Serial.println(lcdidx);
                        if (flip) {
                            pos = bmpImageOffset + (bmpHeight - 1 - row) * rowSize;
                        } else {
                            pos = bmpImageOffset + row * rowSize;
                        }

                        if (image.position() != pos) {
                            image.seek(pos);
                            buffidx = sizeof(fsbuffer);
                        }
                        xpos = 0;
                        for (col = 0; col < w; ++col) {
                            if (buffidx >= sizeof(fsbuffer)) {
                                if (lcdidx > 0) {
                                    tft.drawPixels(lcdbuffer, lcdidx, xpos, ypos);
                                    xpos += lcdidx;
                                    lcdidx = 0;
                                    first = false;
                                }

                                image.read(fsbuffer, sizeof(fsbuffer));
                                buffidx = 0;
                            }

                            // Convert pixel from BMP to TFT format
                            b = fsbuffer[buffidx++];
                            g = fsbuffer[buffidx++];
                            r = fsbuffer[buffidx++];
                            lcdbuffer[lcdidx++] = color565(r, g, b);
                            if (lcdidx >= sizeof(lcdbuffer) || (xpos + lcdidx) >= w) {
                                tft.drawPixels(lcdbuffer, lcdidx, xpos, ypos);
                                lcdidx = 0;
                                xpos += lcdidx;
                            }
                        } // end pixel
                        ++ypos;
                    } // end scanline

                    // Write any remaining data to LCD
                    if (lcdidx > 0) {
                        tft.drawPixels(lcdbuffer, lcdidx, xpos, ypos);
                        xpos += lcdidx;
                    }
                } // End goodBmp
            }
        }
    }

    image.close();
    if (!goodBmp) Serial.println("Bad file! Go fix your file you fool");
}


// These read 16- and 32-bit types from the file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t read16(File f) {
    uint16_t result;
    ((uint8_t *)&result)[0] = f.read(); // LSB
    ((uint8_t *)&result)[1] = f.read(); // MSB
    return result;
}

uint32_t read32(File f) {
    uint32_t result;
    ((uint8_t *)&result)[0] = f.read(); // LSB
    ((uint8_t *)&result)[1] = f.read();
    ((uint8_t *)&result)[2] = f.read();
    ((uint8_t *)&result)[3] = f.read(); // MSB
    return result;
}

uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}