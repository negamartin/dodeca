// From Arduino core Tone.cpp, modified and specialized to produce the alarm
// beep pattern.

#include "common.h"

static volatile uint8_t* tone_PINx_reg;
static volatile uint8_t tone_pin_bitmask;

void dodecaToneSetup() {
    digitalWrite(TONE_PIN, LOW);
    pinMode(TONE_PIN, OUTPUT);
    // Warning: constant output HIGH could burn the buzzer! (there's no DC
    // blocking capacitor)

    // Precompute this for quick toggling (so ISR is short)
    uint8_t port = digitalPinToPort(TONE_PIN);
    tone_PINx_reg = portInputRegister(port);
    tone_pin_bitmask = digitalPinToBitMask(TONE_PIN);
}

volatile bool tone_playing = false;
static volatile unsigned long toggle_count;
static volatile bool make_sound;
static volatile Tone* first_tone;
static volatile Tone* next_tone;

static void dodecaTone(unsigned int frequency, unsigned long duration) {
    TCCR2A = 0;
    TCCR2B = 0;
    bitWrite(TCCR2A, WGM21, 1);
    bitWrite(TCCR2B, CS20, 1);

    make_sound = frequency != 0;
    if (frequency == 0) {
        frequency = 256;
        digitalWrite(TONE_PIN, LOW);
    }

    uint8_t prescalarbits;
    uint32_t ocr = 0;
    // 8 bit timer, scan through prescalars to find the best fit
    ocr = F_CPU / frequency / 2 - 1;
    prescalarbits = 0b001;  // ck/1: same for both timers
    if (ocr > 255) {
        ocr = F_CPU / frequency / 2 / 8 - 1;
        prescalarbits = 0b010;  // ck/8: same for both timers

        if (ocr > 255) {
            ocr = F_CPU / frequency / 2 / 32 - 1;
            prescalarbits = 0b011;
        }

        if (ocr > 255) {
            ocr = F_CPU / frequency / 2 / 64 - 1;
            prescalarbits = 0b100;

            if (ocr > 255) {
                ocr = F_CPU / frequency / 2 / 128 - 1;
                prescalarbits = 0b101;
            }

            if (ocr > 255) {
                ocr = F_CPU / frequency / 2 / 256 - 1;
                prescalarbits = 0b110;
                if (ocr > 255) {
                    // can't do any better than /1024
                    ocr = F_CPU / frequency / 2 / 1024 - 1;
                    prescalarbits = 0b111;
                }
            }
        }
    }
    TCCR2B = (TCCR2B & 0b11111000) | prescalarbits;

    // Calculate the toggle count
    toggle_count = 2 * frequency * duration / 1000;

    // Set the OCR for the given timer,
    // set the toggle count,
    // then turn on the interrupts
    OCR2A = ocr;
    bitWrite(TIMSK2, OCIE2A, 1);
}

void dodecaToneStop() {
    bitWrite(TIMSK2, OCIE2A, 0);  // disable interrupt
    TCCR2A = (1 << WGM20);
    TCCR2B = (TCCR2B & 0b11111000) | (1 << CS22);
    OCR2A = 0;

    digitalWrite(TONE_PIN, LOW);
    tone_playing = false;
}

void dodecaTonePlay(Tone* sequence) {
    noInterrupts();
    first_tone = sequence;
    long raw_tone = pgm_read_dword(sequence++);
    Tone& tone = reinterpret_cast<Tone&>(raw_tone);
    next_tone = sequence;
    dodecaTone(tone.freq, tone.dur);
    tone_playing = true;
    interrupts();
}

ISR(TIMER2_COMPA_vect) {
    if (toggle_count > 0) {
        if (make_sound) {
            *tone_PINx_reg = tone_pin_bitmask;  // toggle the pin
        }
        toggle_count--;
        return;
    }

    // Finished a single tone
    long raw_tone = pgm_read_dword(next_tone++);
    Tone& tone = reinterpret_cast<Tone&>(raw_tone);
    if (tone.dur == 0) {
        if (tone.freq == 0) {
            // Finish playing
            dodecaToneStop();
            return;
        } else {
            // Loop
            next_tone = first_tone;
            raw_tone = pgm_read_dword(next_tone++);
        }
    }
    dodecaTone(tone.freq, tone.dur);
}
