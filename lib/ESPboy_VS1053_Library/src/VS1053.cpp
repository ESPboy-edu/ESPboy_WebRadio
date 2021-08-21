//Modified for www.ESPboy.com project
//by RomanS 12.08.2021
//to use MCP23017 GPIO extension pins for CS, DCS, DREQ



/**
 * This is a driver library for VS1053 MP3 Codec Breakout
 * (Ogg Vorbis / MP3 / AAC / WMA / FLAC / MIDI Audio Codec Chip).
 * Adapted for Espressif ESP8266 and ESP32 boards.
 *
 * version 1.0.1
 *
 * Licensed under GNU GPLv3 <http://gplv3.fsf.org/>
 * Copyright © 2018
 *
 * @authors baldram, edzelf, MagicCube, maniacbug
 *
 * Development log:
 *  - 2011: initial VS1053 Arduino library
 *          originally written by J. Coliz (github: @maniacbug),
 *  - 2016: refactored and integrated into Esp-radio sketch
 *          by Ed Smallenburg (github: @edzelf)
 *  - 2017: refactored to use as PlatformIO library
 *          by Marcin Szalomski (github: @baldram | twitter: @baldram)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License or later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "VS1053.h"

#define LOG Serial.printf

VS1053::VS1053(Adafruit_MCP23017 *mcp_pointer, uint8_t _cs_lcd, uint8_t _cs_pin_mcp, uint8_t _dcoms_pin_mcp, uint8_t _dreq_pin_mcp){
    mcp = mcp_pointer;
    cs_lcd = _cs_lcd;
    cs_pin_mcp = _cs_pin_mcp;
    dcoms_pin_mcp = _dcoms_pin_mcp;
    dreq_pin_mcp = _dreq_pin_mcp;
}


uint16_t VS1053::read_register(uint8_t _reg) const {
  uint16_t result;
    control_mode_on();
    SPI.write(3);    // Read operation
    SPI.write(_reg); // Register to write (0..0xF)
    // Note: transfer16 does not seem to work
    result = (SPI.transfer(0xFF) << 8) | // Read 16 bits data
             (SPI.transfer(0xFF));
    await_data_request(); // Wait for DREQ to be HIGH again
    control_mode_off();
    return result;
}

void VS1053::writeRegister(uint8_t _reg, uint16_t _value) const {
    control_mode_on();
    SPI.write(2);        // Write operation
    SPI.write(_reg);     // Register to write (0..0xF)
    SPI.write16(_value); // Send 16 bits data
    await_data_request();
    control_mode_off();
}


void VS1053::sdi_send_buffer(uint8_t *data, uint32_t len) {
    uint32_t chunk_length;
    data_mode_on();
    while (len){
        await_data_request();
        chunk_length = len;
        if (len > vs1053_chunk_size) {
            chunk_length = vs1053_chunk_size;
        }
        len -= chunk_length;
        SPI.writeBytes(data, chunk_length);
        data += chunk_length;
    }
    data_mode_off();
}


void VS1053::sdi_send_fillers(uint32_t len) {
    uint32_t chunk_length; // Length of chunk 32 byte or shorter
    data_mode_on();
    while (len){
        await_data_request();
        chunk_length = len;
        if (len > vs1053_chunk_size) {
            chunk_length = vs1053_chunk_size;
        }
        len -= chunk_length;
        while (chunk_length--) {
            SPI.write(endFillByte);
        }
    }
    data_mode_off();
}


void VS1053::wram_write(uint16_t address, uint16_t data) {
    writeRegister(SCI_WRAMADDR, address);
    writeRegister(SCI_WRAM, data);
}


uint16_t VS1053::wram_read(uint16_t address) {
    writeRegister(SCI_WRAMADDR, address); // Start reading from WRAM
    return read_register(SCI_WRAM);        // Read back result
}



void VS1053::begin() {
    mcp->pinMode(dreq_pin_mcp, INPUT); // DREQ is an input
    mcp->pinMode(cs_pin_mcp, OUTPUT);  // The SCI and SDI signals
    mcp->pinMode(dcoms_pin_mcp, OUTPUT);

    mcp->digitalWrite(dcoms_pin_mcp, HIGH); // Start HIGH for SCI en SDI
    mcp->digitalWrite(cs_pin_mcp, HIGH);

    softReset();

    VS1053_SPI = SPISettings(200000, MSBFIRST, SPI_MODE0);
    writeRegister(SCI_AUDATA, 44000);
    writeRegister(SCI_CLOCKF, 6 << 12);

    VS1053_SPI = SPISettings(8000000, MSBFIRST, SPI_MODE0);
    writeRegister(SCI_MODE, _BV(SM_SDINEW) | _BV(SM_LINE1));

    await_data_request();
    endFillByte = wram_read(0x1E06) & 0xFF;
}


void VS1053::setVolume(uint8_t vol) {
    // Set volume.  Both left and right. Input value is 0..100.  100 is the loudest.
    uint8_t valueL, valueR;
    curvol = vol;
    valueL = vol;
    valueR = vol;
    if (curbalance < 0) {
        valueR = max(0, vol + curbalance);
    } else if (curbalance > 0) {
        valueL = max(0, vol - curbalance);
    }
    valueL = map(valueL, 0, 100, 0xFE, 0x00); // 0..100% to left channel
    valueR = map(valueR, 0, 100, 0xFE, 0x00); // 0..100% to right channel
    writeRegister(SCI_VOL, (valueL << 8) | valueR); // Volume left and right
}



void VS1053::setBalance(int8_t balance) {
    if (balance > 100) {
        curbalance = 100;
    } else if (balance < -100) {
        curbalance = -100;
    } else {
        curbalance = balance;
    }
}



void VS1053::setTone(uint8_t *rtone) { // Set bass/treble (4 nibbles)
    uint16_t value = 0; // Value to send to SCI_BASS
    int i;              // Loop control

    for (i = 0; i < 4; i++) {
        value = (value << 4) | rtone[i]; // Shift next nibble in
    }
    writeRegister(SCI_BASS, value); // Volume left and right
}



uint8_t VS1053::getVolume() { // Get the currenet volume setting.
    return curvol;
}



int8_t VS1053::getBalance() { // Get the currenet balance setting.
    return curbalance;
}



void VS1053::startSong() {
    sdi_send_fillers(10);
}



void VS1053::playChunk(uint8_t *data, uint32_t len) {
    sdi_send_buffer(data, len);
}



void VS1053::stopSong() {
    uint16_t modereg; // Read from mode register
    int i;            // Loop control

    sdi_send_fillers(2052);
    delay(10);
    writeRegister(SCI_MODE, _BV(SM_SDINEW) | _BV(SM_CANCEL));
    for (i = 0; i < 200; i++) {
        sdi_send_fillers(32);
        modereg = read_register(SCI_MODE); // Read status
        if ((modereg & _BV(SM_CANCEL)) == 0) {
            sdi_send_fillers(2052);
            LOG("Song stopped correctly after %d msec\n", i * 10);
            return;
        }
        delay(10);
    }
    printDetails("Song stopped incorrectly!");
}


void VS1053::softReset() {
    LOG("Performing soft-reset\n");
    writeRegister(SCI_MODE, _BV(SM_SDINEW) | _BV(SM_RESET));
    delay(10);
    await_data_request();
}



void VS1053::printDetails(const char *header) {
    uint16_t regbuf[16];
    uint8_t i;

    LOG("%s", header);
    LOG("REG   Contents\n");
    LOG("---   -----\n");
    for (i = 0; i <= SCI_num_registers; i++) {
        regbuf[i] = read_register(i);
    }
    for (i = 0; i <= SCI_num_registers; i++) {
        delay(5);
        LOG("%3X - %5X\n", i, regbuf[i]);
    }
}


// Read more here: http://www.bajdi.com/lcsoft-vs1053-mp3-module/#comment-33773
void VS1053::switchToMp3Mode() {
    wram_write(0xC017, 3); // GPIO DDR = 3
    wram_write(0xC019, 0); // GPIO ODATA = 0
    delay(100);
    LOG("Switched to mp3 mode\n");
    softReset();
}


// @return true if the chip is wired up correctly
bool VS1053::isChipConnected() {
    uint16_t status = read_register(SCI_STATUS);
    return !(status == 0 || status == 0xFFFF);
}


 // @see VS1053b Datasheet (1.31) / 9.6.5 SCI_DECODE_TIME (RW)
 // @return current decoded time in full seconds
uint16_t VS1053::getDecodedTime() {
    return read_register(SCI_DECODE_TIME);
}


// Clears decoded time (sets SCI_DECODE_TIME register to 0x00)
void VS1053::clearDecodedTime() {
    writeRegister(SCI_DECODE_TIME, 0x00);
    writeRegister(SCI_DECODE_TIME, 0x00);
}


// Fine tune the data rate
void VS1053::adjustRate(long ppm2) {
    writeRegister(SCI_WRAMADDR, 0x1e07);
    writeRegister(SCI_WRAM, ppm2);
    writeRegister(SCI_WRAM, ppm2 >> 16);
    // oldClock4KHz = 0 forces  adjustment calculation when rate checked.
    writeRegister(SCI_WRAMADDR, 0x5b1c);
    writeRegister(SCI_WRAM, 0);
    // Write to AUDATA or CLOCKF checks rate and recalculates adjustment.
    writeRegister(SCI_AUDATA, read_register(SCI_AUDATA));
}


// Load a patch or plugin
void VS1053::loadUserCode(const unsigned short* plugin) {
    int i = 0;
    while (i<sizeof(plugin)/sizeof(plugin[0])) {
        unsigned short addr, n, val;
        addr = plugin[i++];
        n = plugin[i++];
        if (n & 0x8000U) { /* RLE run, replicate n samples */
            n &= 0x7FFF;
            val = plugin[i++];
            while (n--) {
                writeRegister(addr, val);
            }
        } else {           /* Copy run, copy n samples */
            while (n--) {
                val = plugin[i++];
                writeRegister(addr, val);
            }
        }
    }
}


//Load the latest generic firmware patch
void VS1053::loadDefaultVs1053Patches() {
   loadUserCode(PATCHES);
};
